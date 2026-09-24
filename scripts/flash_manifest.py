#!/usr/bin/env python3
"""flash_manifest.py -- describe a factory image as regions a flasher can write
WITHOUT touching the customer's settings.

WHY. firmware.factory.bin spans 0x0..end-of-app, so it CONTAINS the NVS partition
as 0xFF gap fill. Writing it at 0x0 erases Wi-Fi, location, the logbook and the
device key -- right for a factory board, wrong for a customer's. valar-flasher's
default mode writes the regions below one by one and never the NVS span, so an
update over USB keeps everything a customer configured.

Every number here is READ FROM THE BUILT ARTIFACT, never assumed:
  * partition offsets come from the built partitions.bin (binary), not the CSV;
  * the bootloader offset comes from the chip;
  * boot_app0 is written at the otadata offset the TABLE names. The platform's
    uploader writes it at a hardcoded 0xe000, which on partitions-s3-16mb-bignvs
    is INSIDE the 84 KB NVS -- copying the uploader's layout would make a
    "preserve NVS" flash overwrite the customer's settings.

And then PROVEN against the artifact: 0xFF-fill a buffer the size of the factory
image, lay every region at its offset, and require the result to equal the factory
image byte for byte, with no region touching a preserved span. If the manifest
does not reproduce the image exactly, nothing is written.

    flash_manifest.py --build .pio/build/<env> --slug s3-128 --env <env> \
        --boot-app0 <path> --out <dir> [--fw-version N] [--commit SHA]
    flash_manifest.py --selftest

Writes into --out:
    firmware-<slug>.factory.bin   the whole image (factory reset / provisioning)
    bootloader-<slug>.bin, partitions-<slug>.bin, boot_app0-<slug>.bin
    firmware-<slug>.bin           the app region (the same bytes as the OTA image)
    flash-manifest-<slug>.json
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import struct
import sys

# Second-stage bootloader offset by chip (ESP-IDF CONFIG_BOOTLOADER_OFFSET_IN_FLASH).
BOOTLOADER_OFFSET = {"esp32": 0x1000, "esp32s2": 0x1000, "esp32s3": 0x0, "esp32c2": 0x0,
                     "esp32c3": 0x0, "esp32c6": 0x0, "esp32h2": 0x0}
# esp_image_header_t.chip_id (byte 12, little-endian u16) -> esptool's chip name.
CHIP_ID = {0: "esp32", 2: "esp32s2", 9: "esp32s3", 5: "esp32c3", 12: "esp32c2", 13: "esp32c6", 16: "esp32h2"}
PARTITION_TABLE_OFFSET = 0x8000
T_APP, T_DATA = 0x00, 0x01
ST_FACTORY, ST_OTA0, ST_OTADATA, ST_NVS = 0x00, 0x10, 0x00, 0x02


class Refused(Exception):
    pass


def parse_partitions(blob: bytes) -> list:
    """[(label, type, subtype, offset, size)] from a binary ESP partition table."""
    out = []
    for i in range(0, len(blob) - 31, 32):
        e = blob[i:i + 32]
        if e[:2] == b"\xaa\x50":
            typ, sub, off, size = struct.unpack_from("<BBII", e, 2)
            label = e[12:28].split(b"\0")[0].decode("ascii", "replace")
            out.append((label, typ, sub, off, size))
        elif e[:2] in (b"\xeb\xeb", b"\xff\xff"):
            if e[:2] == b"\xff\xff":
                break
        else:
            raise Refused(f"partition table entry {i // 32} has no magic -- not a partition table")
    if not out:
        raise Refused("partition table holds no entries")
    return out


def chip_of(bootloader: bytes) -> str:
    """The chip this image was built for, read from the bootloader's own header
    rather than typed on a command line that can disagree with it."""
    if len(bootloader) < 16 or bootloader[0] != 0xE9:
        raise Refused("bootloader has no ESP image header")
    cid = int.from_bytes(bootloader[12:14], "little")
    if cid not in CHIP_ID:
        raise Refused(f"unknown chip id {cid} in the bootloader header")
    return CHIP_ID[cid]


def plan(factory: bytes, bootloader: bytes, partitions: bytes, app: bytes, boot_app0: bytes, chip: str):
    """(regions, preserve) -- each region (name, offset, bytes); preserve (name, offset, size).
    Raises Refused unless the regions reproduce `factory` exactly."""
    if chip not in BOOTLOADER_OFFSET:
        raise Refused(f"unknown chip {chip!r}")
    parts = parse_partitions(partitions)
    nvs = [p for p in parts if p[1] == T_DATA and p[2] == ST_NVS]
    otadata = [p for p in parts if p[1] == T_DATA and p[2] == ST_OTADATA]
    boot_app = ([p for p in parts if p[1] == T_APP and p[2] == ST_FACTORY]
                or [p for p in parts if p[1] == T_APP and p[2] == ST_OTA0])
    if len(nvs) != 1:
        raise Refused(f"expected exactly one nvs partition, found {len(nvs)}")
    if not boot_app:
        raise Refused("no factory or ota_0 app partition")
    regions = [("bootloader", BOOTLOADER_OFFSET[chip], bootloader),
               ("partitions", PARTITION_TABLE_OFFSET, partitions)]
    if otadata:
        if len(boot_app0) > otadata[0][4]:
            raise Refused("boot_app0 is larger than the otadata partition")
        regions.append(("boot_app0", otadata[0][3], boot_app0))
    regions.append(("app", boot_app[0][3], app))
    regions.sort(key=lambda r: r[1])
    preserve = [(nvs[0][0], nvs[0][3], nvs[0][4])]

    # PROOF 1: no two regions overlap, and none touches a preserved span.
    for (n1, o1, b1), (n2, o2, _) in zip(regions, regions[1:]):
        if o1 + len(b1) > o2:
            raise Refused(f"region {n1} overlaps {n2}")
    for name, off, b in regions:
        for pn, po, ps in preserve:
            if off < po + ps and po < off + len(b):
                raise Refused(f"region {name} at {off:#x} overlaps preserved {pn} {po:#x}+{ps:#x}")
    # PROOF 2: the regions, laid on 0xFF, ARE the factory image.
    end = max(off + len(b) for _, off, b in regions)
    if end != len(factory):
        raise Refused(f"regions end at {end:#x} but the factory image is {len(factory):#x} bytes")
    rebuilt = bytearray(b"\xff" * len(factory))
    for _, off, b in regions:
        rebuilt[off:off + len(b)] = b
    if bytes(rebuilt) != factory:
        first = next(i for i in range(len(factory)) if rebuilt[i] != factory[i])
        raise Refused(f"regions do not reproduce the factory image (first difference at {first:#x})")
    return regions, preserve


def sha256(b: bytes) -> str:
    return hashlib.sha256(b).hexdigest()


def write(args) -> int:
    rd = lambda p: open(p, "rb").read()
    b = args.build
    factory = rd(os.path.join(b, "firmware.factory.bin"))
    app = rd(os.path.join(b, "firmware.bin"))
    bootloader = rd(os.path.join(b, "bootloader.bin"))
    try:
        chip = chip_of(bootloader)
        if args.chip != "auto" and args.chip != chip:
            raise Refused(f"--chip {args.chip} but the bootloader was built for {chip}")
        regions, preserve = plan(factory, bootloader, rd(os.path.join(b, "partitions.bin")),
                                 app, rd(args.boot_app0), chip)
    except Refused as e:
        print(f"REFUSED: {e}")
        return 1
    os.makedirs(args.out, exist_ok=True)
    s = args.slug
    names = {"bootloader": f"bootloader-{s}.bin", "partitions": f"partitions-{s}.bin",
             "boot_app0": f"boot_app0-{s}.bin", "app": f"firmware-{s}.bin"}
    for name, _, data in regions:
        with open(os.path.join(args.out, names[name]), "wb") as f:
            f.write(data)
    shutil.copyfile(os.path.join(b, "firmware.factory.bin"), os.path.join(args.out, f"firmware-{s}.factory.bin"))
    manifest = {
        "v": 1, "slug": s, "env": args.env, "chip": chip,
        "fw_version": args.fw_version, "commit": args.commit,
        "factory": {"file": f"firmware-{s}.factory.bin", "offset": "0x0",
                    "size": len(factory), "sha256": sha256(factory)},
        "regions": [{"name": n, "offset": hex(o), "file": names[n], "size": len(d), "sha256": sha256(d)}
                    for n, o, d in regions],
        "preserve": [{"name": n, "offset": hex(o), "size": hex(z),
                      "why": "customer settings, Wi-Fi, logbook and device key -- flash mode never writes here"}
                     for n, o, z in preserve],
        "scan": {"file": f"factory-scan-{s}.json"},
    }
    path = os.path.join(args.out, f"flash-manifest-{s}.json")
    with open(path, "w", encoding="utf-8") as f:
        json.dump(manifest, f, indent=1)
        f.write("\n")
    for n, o, d in regions:
        print(f"  {n:10} {o:#08x}  {len(d):8} B  {names[n]}")
    for n, o, z in preserve:
        print(f"  {n:10} {o:#08x}  {z:8} B  PRESERVE")
    print(f"PROVEN: {len(regions)} regions on 0xFF reproduce firmware-{s}.factory.bin exactly ({len(factory)} B)")
    print(f"wrote {path}")
    return 0


def selftest() -> int:
    """Each proof must be able to refuse."""
    def table(entries):
        blob = b"".join(b"\xaa\x50" + struct.pack("<BBII", t, st, o, z) + l.encode().ljust(16, b"\0") + b"\0" * 4
                        for l, t, st, o, z in entries)
        return blob.ljust(0xC00, b"\xff")
    good = [("nvs", T_DATA, ST_NVS, 0x9000, 0x15000), ("otadata", T_DATA, ST_OTADATA, 0x1E000, 0x2000),
            ("app0", T_APP, ST_OTA0, 0x20000, 0x100000)]
    bl, ba0, app = b"\xe9" + b"B" * 99, b"\x00" * 12 + b"\xff" * (0x2000 - 12), b"\xe9" + b"A" * 199

    def image(pt, extra=None):
        f = bytearray(b"\xff" * (0x20000 + len(app)))
        f[0:len(bl)] = bl
        f[0x8000:0x8000 + len(pt)] = pt
        f[0x1E000:0x1E000 + len(ba0)] = ba0
        f[0x20000:] = app
        if extra is not None:
            f[extra] = 0x42
        return bytes(f)

    pt = table(good)
    cases = [
        ("a correct image is accepted", image(pt), pt, None),
        ("a stray byte inside NVS is refused", image(pt, 0xA000), pt, "reproduce"),
        ("a region inside NVS is refused (boot_app0 at 0xe000)",
         image(table([good[0], ("otadata", T_DATA, ST_OTADATA, 0xE000, 0x2000), good[2]])),
         table([good[0], ("otadata", T_DATA, ST_OTADATA, 0xE000, 0x2000), good[2]]), "overlaps preserved"),
        ("a table with no nvs is refused", image(table(good[1:])), table(good[1:]), "exactly one nvs"),
    ]
    bad = 0
    for name, img, table_blob, want in cases:
        try:
            plan(img, bl, table_blob, app, ba0, "esp32s3")
            got = None
        except Refused as e:
            got = str(e)
        ok = (want is None and got is None) or (want is not None and got is not None and want in got)
        bad += not ok
        print(f"  {'PASS' if ok else 'FAIL'}  {name}: {got or 'accepted'}")
    print("SELFTEST " + ("PASSED" if not bad else f"FAILED ({bad})"))
    return 1 if bad else 0


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--build")
    ap.add_argument("--slug")
    ap.add_argument("--env")
    ap.add_argument("--chip", default="auto", help="auto = read it from the bootloader header")
    ap.add_argument("--boot-app0")
    ap.add_argument("--out")
    ap.add_argument("--fw-version", type=int)
    ap.add_argument("--commit")
    ap.add_argument("--selftest", action="store_true")
    a = ap.parse_args(argv)
    if a.selftest:
        return selftest()
    if not all([a.build, a.slug, a.env, a.boot_app0, a.out]):
        ap.print_usage()
        return 2
    return write(a)


if __name__ == "__main__":
    sys.exit(main())
