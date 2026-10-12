#!/usr/bin/env python3
"""d6-flash-image.py -- write a PRE-BUILT app image to a Missileer bench board's
app0, and NOTHING else.

WHY THIS EXISTS (D6, 2026-10-11). The D6 bench sitting (docs/bench-runbook-device-
game.md) flashes six images back to back. `scripts/bench-flash.ps1` rebuilds each
one from source, and two builds of one commit are not byte-identical -- so the
`# elf_sha256` a probe prints at boot can only be tied back to a commit if the image
that booted is the one a manifest describes. This writes exactly that image.

WHAT IT WRITES: the app region only, at the app0 (ota_0) offset READ FROM THE
IMAGE'S OWN BUILT partitions.bin -- never assumed. Not the bootloader, not the
partition table, not otadata, and never NVS: Wi-Fi, enrollment and device identity
survive. Station 4's NTP test joins Wi-Fi from those stored credentials.

WHAT IT CHECKS FIRST, because an app-only write can be silently INEFFECTIVE, and a
failed flash and an ineffective one produce the same observation:

  1. the board, by NAME, resolved from its USB serial by scripts/board-port.py --
     never a COM number. The Blipscope soak board is refused with no override, and
     so is any board that is not a Missileer board (D6 is Missileer work; CLAUDE.md
     "Bench boards"). A missileer-* image is refused on a non-Missileer board.
  2. the image against its MANIFEST.json (sha256 + size of firmware.bin and
     partitions.bin), and its header (an ESP32-S3 app with an app descriptor).
  3. the BOARD's partition table (read back, 0xC00 bytes at 0x8000) must equal the
     image's. If it differs, app0 is somewhere else on that board and an app-only
     write would land in the wrong place: REFUSED -- that board needs a full flash,
     which is Daniel's call (it can move NVS).
  4. the BOARD's otadata (read back) must select app0. A board that once took an
     OTA boots app1, and an app0 write would be ignored on reboot: REFUSED --
     `scripts/bench-flash.ps1` (pio upload) rewrites otadata to select app0 and
     leaves NVS alone.
  5. after the write, esptool's on-chip hash check must have PRINTED "Hash of data
     verified." -- esptool can skip that check silently (see fake_esptool.py), so a
     zero exit alone is not proof.

Neither read touches NVS (0x9000..0x1E000): the table ends at 0x8C00 and otadata
starts at 0x1E000.

USAGE
    python scripts/d6-flash-image.py --board missileer \
        --image C:\\Github\\missileer-overnight\\d6\\images\\station1-gesture
    python scripts/d6-flash-image.py ... --dry-run     # prints every esptool command;
                                                       # opens no port, resolves none
    python scripts/d6-flash-image.py --selftest        # no board: a fake esptool

  --label   names the log: bench-logs/flash-<label>-<stamp>.txt (default
            d6-<station>), the same name shape bench-flash.ps1 uses.
  --esptool path to esptool (default: PlatformIO's, ~/.platformio/penv, else PATH).

EXIT  0 written + verified   1 write failed / not verified   2 usage
      3 image or manifest invalid   4 board refused   5 board not reachable
      6 refused: the board's table or otadata makes an app-only write unsafe
"""
from __future__ import annotations

import argparse
import ast
import datetime
import hashlib
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
RESOLVER = os.path.join(HERE, "board-port.py")

PT_OFFSET, PT_SIZE = 0x8000, 0xC00
T_APP, T_DATA = 0x00, 0x01
ST_OTA0, ST_OTADATA, ST_NVS = 0x10, 0x00, 0x02
CHIP_ID_S3 = 9
APP_DESC_MAGIC = 0xABCD5432

EXIT_OK, EXIT_WRITE, EXIT_USAGE, EXIT_IMAGE, EXIT_BOARD, EXIT_UNREACHABLE, EXIT_STATE = 0, 1, 2, 3, 4, 5, 6


class Refused(Exception):
    def __init__(self, msg, code):
        super().__init__(msg)
        self.code = code


# ---- the board table, read as a literal ---------------------------------------
def load_boards(path=RESOLVER):
    """BOARDS from board-port.py, parsed rather than imported: importing it imports
    pyserial, and a dry run must work (and touch nothing) where pyserial is absent."""
    with open(path, encoding="utf-8") as f:
        tree = ast.parse(f.read())
    for node in tree.body:
        if isinstance(node, ast.Assign) and any(getattr(t, "id", None) == "BOARDS" for t in node.targets):
            return ast.literal_eval(node.value)
    raise Refused(f"no BOARDS table in {path}", EXIT_USAGE)


def is_missileer_board(name):
    return re.fullmatch(r"missileer\d*", name) is not None


def check_board(name, env, boards):
    """Raise Refused unless this image may go on this board."""
    if name not in boards:
        raise Refused(f"unknown board {name!r}; board-port.py knows: {', '.join(boards)}", EXIT_BOARD)
    if name == "blipscope" or "SOAK" in boards[name][1].upper():
        raise Refused(f"{name} is the Blipscope SOAK board -- never flashed for Missileer work, and this "
                      f"tool has no override (CLAUDE.md, Bench boards).", EXIT_BOARD)
    if env.startswith("missileer-") and not is_missileer_board(name):
        raise Refused(f"{env} is Missileer firmware and only goes on a Missileer board, not {name}.", EXIT_BOARD)
    if not is_missileer_board(name):
        raise Refused(f"{name} is not a Missileer board; D6 images go only on a Missileer bench board.", EXIT_BOARD)


# ---- the image and its manifest --------------------------------------------------
def parse_partitions(blob):
    """[(label, type, subtype, offset, size)] from a binary partition table."""
    out = []
    for i in range(0, len(blob) - 31, 32):
        e = blob[i:i + 32]
        if e[:2] == b"\xaa\x50":
            typ, sub, off, size = struct.unpack_from("<BBII", e, 2)
            out.append((e[12:28].split(b"\0")[0].decode("ascii", "replace"), typ, sub, off, size))
        elif e[:2] in (b"\xeb\xeb", b"\xff\xff"):
            break
        else:
            return []
    return out


def sha256(b):
    return hashlib.sha256(b).hexdigest()


def load_image(d):
    """The image dir, verified against its manifest. Raises Refused."""
    paths = {k: os.path.join(d, k) for k in ("MANIFEST.json", "firmware.bin", "partitions.bin")}
    for k, p in paths.items():
        if not os.path.isfile(p):
            raise Refused(f"{p} is missing -- not a D6 image directory", EXIT_IMAGE)
    with open(paths["MANIFEST.json"], encoding="utf-8") as f:
        m = json.load(f)
    fw = open(paths["firmware.bin"], "rb").read()
    pt = open(paths["partitions.bin"], "rb").read()
    if len(fw) != m["firmware"]["size"] or sha256(fw) != m["firmware"]["sha256"]:
        raise Refused(f"firmware.bin does not match its MANIFEST.json (sha256 {sha256(fw)[:16]} vs "
                      f"{m['firmware']['sha256'][:16]}) -- the image changed after it was described", EXIT_IMAGE)
    if sha256(pt) != m["partitions"]["sha256"]:
        raise Refused("partitions.bin does not match its MANIFEST.json", EXIT_IMAGE)
    if len(fw) < 64 or fw[0] != 0xE9 or int.from_bytes(fw[12:14], "little") != CHIP_ID_S3:
        raise Refused("firmware.bin is not an ESP32-S3 app image", EXIT_IMAGE)
    if struct.unpack_from("<I", fw, 32)[0] != APP_DESC_MAGIC:
        raise Refused("firmware.bin carries no app descriptor -- not an app image", EXIT_IMAGE)
    parts = parse_partitions(pt)
    app0 = [p for p in parts if p[1] == T_APP and p[2] == ST_OTA0]
    nvs = [p for p in parts if p[1] == T_DATA and p[2] == ST_NVS]
    otadata = [p for p in parts if p[1] == T_DATA and p[2] == ST_OTADATA]
    if len(app0) != 1 or len(nvs) != 1 or len(otadata) != 1:
        raise Refused("the image's partition table lacks exactly one app0, nvs and otadata", EXIT_IMAGE)
    off = app0[0][3]
    if hex(off) != m["app_offset"]:
        raise Refused(f"app0 is {off:#x} in partitions.bin but {m['app_offset']} in the manifest", EXIT_IMAGE)
    if len(fw) > app0[0][4]:
        raise Refused("firmware.bin is larger than the app0 slot", EXIT_IMAGE)
    # Paranoia that costs nothing: the write must not overlap anything preserved.
    for name, po, ps in ((nvs[0][0], nvs[0][3], nvs[0][4]), (otadata[0][0], otadata[0][3], otadata[0][4]),
                         ("partition table", PT_OFFSET, PT_SIZE)):
        if off < po + ps and po < off + len(fw):
            raise Refused(f"the app write {off:#x}+{len(fw):#x} would overlap {name}", EXIT_IMAGE)
    return {"dir": d, "manifest": m, "fw_path": paths["firmware.bin"], "fw": fw, "pt": pt, "parts": parts,
            "app_off": off, "otadata": otadata[0], "nvs": nvs[0],
            "n_ota": len([p for p in parts if p[1] == T_APP and 0x10 <= p[2] < 0x20])}


# ---- what the board says ---------------------------------------------------------
def selected_slot(blob, n_ota=2):
    """(slot, why) -- which ota_N the bootloader will boot, from an otadata read-back.
    Two 32-byte esp_ota_select_entry_t at 0x0 and 0x1000: ota_seq, label[20], state,
    crc. An entry is valid when ota_seq != 0xFFFFFFFF and crc == crc32_le(~0, ota_seq);
    the highest valid seq wins and boots ota_((seq - 1) % n). None valid -> ota_0."""
    best = None
    for base in (0x0, 0x1000):
        if len(blob) < base + 32:
            continue
        seq, = struct.unpack_from("<I", blob, base)
        crc, = struct.unpack_from("<I", blob, base + 28)
        if seq == 0xFFFFFFFF or zlib.crc32(blob[base:base + 4], 0xFFFFFFFF) != crc:
            continue
        best = seq if best is None or seq > best else best
    if best is None:
        return 0, "otadata holds no valid entry -> the bootloader boots ota_0 (app0)"
    return (best - 1) % n_ota, f"otadata's highest valid ota_seq is {best} -> boots ota_{(best - 1) % n_ota}"


# ---- esptool ---------------------------------------------------------------------
def find_esptool(explicit=None):
    if explicit:
        return [explicit] if not explicit.endswith(".py") else [sys.executable, explicit]
    pio = os.path.join(os.path.expanduser("~"), ".platformio", "penv",
                       "Scripts" if os.name == "nt" else "bin", "esptool.exe" if os.name == "nt" else "esptool")
    if os.path.isfile(pio):
        return [pio]
    for n in ("esptool", "esptool.py"):
        p = shutil.which(n)
        if p:
            return [p]
    return None


def esptool_major(cmd):
    """Major version, from `esptool version` -- local, opens no port."""
    try:
        out = subprocess.run(cmd + ["version"], capture_output=True, text=True, timeout=60).stdout
    except (OSError, subprocess.SubprocessError):
        return None
    m = re.search(r"v?(\d+)\.\d+", out)
    return int(m.group(1)) if m else None


def spell(major, word):
    """esptool 5 spells commands with hyphens; 4 with underscores."""
    return word if major >= 5 else word.replace("-", "_")


def commands(esptool, major, port, img, pt_out, ota_out, baud):
    s = lambda w: spell(major, w)
    base = esptool + ["--chip", "esp32s3", "--port", port, "--baud", str(baud)]
    od = img["otadata"]
    return {
        "read-table": base + ["--before", s("default-reset"), "--after", s("no-reset"),
                              s("read-flash"), hex(PT_OFFSET), hex(PT_SIZE), pt_out],
        "read-otadata": base + ["--before", s("default-reset"), "--after", s("no-reset"),
                                s("read-flash"), hex(od[3]), hex(od[4]), ota_out],
        "write-app": base + ["--before", s("default-reset"), "--after", s("hard-reset"),
                             s("write-flash"), hex(img["app_off"]), img["fw_path"]],
    }


class Log:
    def __init__(self, path):
        self.path = path
        self.f = open(path, "a", encoding="utf-8") if path else None

    def say(self, t=""):
        print(f"[d6-flash] {t}" if t else "", flush=True)
        if self.f:
            self.f.write(f"[d6-flash] {t}\n" if t else "\n")
            self.f.flush()

    def run(self, cmd):
        """Run BARE (never filtered), echo + log every line, return (rc, text)."""
        self.say("$ " + " ".join(f'"{c}"' if " " in c else c for c in cmd))
        p = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                             encoding="utf-8", errors="replace")
        lines = []
        for line in p.stdout:
            lines.append(line)
            print(line, end="", flush=True)
            if self.f:
                self.f.write(line)
        rc = p.wait()
        if self.f:
            self.f.flush()
        self.say(f"exit={rc}")
        return rc, "".join(lines)


def resolve_ports(name):
    """(app_port, download_port) via board-port.py. The second call 1200-baud-touches a
    TinyUSB board into ROM download mode -- that is the only port-opening this does."""
    def call(extra):
        r = subprocess.run([sys.executable, RESOLVER, name] + extra, capture_output=True, text=True)
        return r.stdout.strip() if r.returncode == 0 and r.stdout.strip() else None
    app = call([])
    if app is None:
        return None, None
    return app, call(["--upload"])


def describe(img, name, log):
    m = img["manifest"]
    log.say(f"image    {img['dir']}")
    log.say(f"station  {m['station']}   env {m['env']}   extra flags: {m.get('extra_build_flags') or '(none)'}")
    log.say(f"commit   {m['commit']}")
    log.say(f"app      {len(img['fw'])} B  sha256 {m['firmware']['sha256']}")
    log.say(f"         -> app0 at {img['app_off']:#x}; NVS {img['nvs'][3]:#x}+{img['nvs'][4]:#x} is not written")
    log.say(f"expect   '# elf_sha256   {m['provenance_elf_sha256']}' in the boot provenance (stations 1-2)")
    log.say(f"board    {name}")


# ---- the two modes ---------------------------------------------------------------
def dry_run(img, name, esptool, major, baud):
    log = Log(None)
    describe(img, name, log)
    if esptool is None:
        esptool, major = ["<esptool: not found -- pass --esptool>"], 5
    elif major is None:
        log.say("WARNING: could not read the esptool version; showing esptool 5 spellings")
        major = 5
    port = f"<PORT = output of: python scripts/board-port.py {name} --upload>"
    cmds = commands(esptool, major, port, img, "<tmp>/board-partitions.bin", "<tmp>/board-otadata.bin", baud)
    log.say("DRY RUN -- nothing below is executed; no port is resolved or opened.")
    log.say(f"0. python scripts/board-port.py {name}            (where the board is now)")
    log.say(f"   python scripts/board-port.py {name} --upload   (its download port; a TinyUSB board is")
    log.say("                                                    1200-baud-touched into ROM download mode)")
    for i, (k, c) in enumerate(cmds.items(), 1):
        log.say(f"{i}. [{k}] " + " ".join(c))
    log.say("   1 -> the board's table must equal the image's partitions.bin, else REFUSED (exit 6)")
    log.say("   2 -> otadata must select app0, else REFUSED (exit 6)")
    log.say("   3 -> exit 0 AND 'Hash of data verified.' in esptool's output, else FAILED (exit 1)")
    return EXIT_OK


def live(img, name, esptool, major, baud, log, resolve=resolve_ports):
    describe(img, name, log)
    app, port = resolve(name)
    if app is None:
        log.say(f"REFUSED: {name} is not connected (no port carries its USB serial).")
        return EXIT_UNREACHABLE
    log.say(f"{name} is on {app} (resolved from its USB serial)")
    if port is None:
        log.say(f"REFUSED: could not put {name} into download mode. Is a monitor still holding {app}?")
        log.say("  Close it (Ctrl+C in pio device monitor) and run this again.")
        return EXIT_UNREACHABLE
    if port != app:
        log.say(f"download port: {port} (the app was on {app})")
    tmp = tempfile.mkdtemp(prefix="d6-flash-")
    try:
        pt_out, ota_out = os.path.join(tmp, "board-partitions.bin"), os.path.join(tmp, "board-otadata.bin")
        cmds = commands(esptool, major, port, img, pt_out, ota_out, baud)

        rc, _ = log.run(cmds["read-table"])
        if rc != 0 or not os.path.isfile(pt_out):
            log.say("FAILED: could not read the board's partition table. Nothing was written.")
            return EXIT_UNREACHABLE
        board_parts = parse_partitions(open(pt_out, "rb").read())
        if board_parts != img["parts"]:
            log.say("REFUSED: the board's partition table differs from the image's -- app0 is not where")
            log.say("  this image expects it, so an app-only write would land in the wrong place.")
            for p in board_parts or [("(no readable table)", 0, 0, 0, 0)]:
                log.say(f"  board: {p[0]:10} type {p[1]} sub {p[2]:#x} {p[3]:#09x} {p[4]:#09x}")
            for p in img["parts"]:
                log.say(f"  image: {p[0]:10} type {p[1]} sub {p[2]:#x} {p[3]:#09x} {p[4]:#09x}")
            log.say("  This board needs a FULL flash (bench-flash.ps1), which can move NVS: ask Daniel.")
            return EXIT_STATE
        log.say("board partition table == image partition table")

        rc, _ = log.run(cmds["read-otadata"])
        if rc != 0 or not os.path.isfile(ota_out):
            log.say("FAILED: could not read the board's otadata. Nothing was written.")
            return EXIT_UNREACHABLE
        slot, why = selected_slot(open(ota_out, "rb").read(), img["n_ota"])
        log.say(why)
        if slot != 0:
            log.say(f"REFUSED: this board boots ota_{slot}, so a write to app0 would be IGNORED on reboot")
            log.say("  and the old app would come back looking like the new one. Use")
            log.say(f"  scripts/bench-flash.ps1 -PioEnv {img['manifest']['env']} -Board {name} -Label "
                    f"d6-{img['manifest']['station']} -NoCapture")
            log.say("  instead: pio upload rewrites otadata to select app0 and leaves NVS alone.")
            return EXIT_STATE

        rc, out = log.run(cmds["write-app"])
        if rc != 0:
            log.say(f"WRITE FAILED, exit={rc}. The board may hold a partial app0; re-run this to retry.")
            return EXIT_WRITE
        if "Hash of data verified" not in out:
            log.say("NOT VERIFIED: esptool exited 0 but never printed 'Hash of data verified.' --")
            log.say("  its on-chip check can be skipped silently, and an unverified write is not a write.")
            return EXIT_WRITE
        log.say(f"WRITTEN + VERIFIED: {img['manifest']['station']} -> {name} app0 {img['app_off']:#x}")
        log.say("The board has been reset into the new image. Open the capture next (docs/bench-d6-checklist.md).")
        return EXIT_OK
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--board")
    ap.add_argument("--image")
    ap.add_argument("--label")
    ap.add_argument("--esptool")
    ap.add_argument("--baud", type=int, default=460800)
    ap.add_argument("--dry-run", "--whatif", action="store_true")
    ap.add_argument("--selftest", action="store_true")
    a = ap.parse_args(argv)
    if a.selftest:
        return selftest()
    if not a.board or not a.image:
        ap.print_usage()
        return EXIT_USAGE
    try:
        boards = load_boards()
        img = load_image(a.image)
        check_board(a.board, img["manifest"]["env"], boards)
    except Refused as e:
        print(f"[d6-flash] REFUSED: {e}")
        return e.code
    esptool = find_esptool(a.esptool)
    major = esptool_major(esptool) if esptool else None
    if a.dry_run:
        return dry_run(img, a.board, esptool, major, a.baud)
    if esptool is None or major is None:
        print("[d6-flash] REFUSED: esptool not found, or its version unreadable -- pass --esptool <path>")
        return EXIT_USAGE
    label = a.label or f"d6-{img['manifest']['station']}"
    os.makedirs(os.path.join(REPO, "bench-logs"), exist_ok=True)
    path = os.path.join(REPO, "bench-logs", f"flash-{label}-{datetime.datetime.now():%Y-%m-%d-%H%M}.txt")
    log = Log(path)
    log.say(f"log: {path}")
    log.say(f"esptool: {' '.join(esptool)} (major {major})")
    rc = live(img, a.board, esptool, major, a.baud, log)
    log.say(f"exit {rc}")
    return rc


# ---- selftest: no board, no PlatformIO; the repo's fake esptool -----------------
def selftest():
    bad = 0

    def check(name, ok, got=""):
        nonlocal bad
        bad += not ok
        print(f"  {'PASS' if ok else 'FAIL'}  {name}{(': ' + got) if got and not ok else ''}")

    def table(rows):
        return b"".join(b"\xaa\x50" + struct.pack("<BBII", t, s, o, z) + l.encode().ljust(16, b"\0") + b"\0" * 4
                        for l, t, s, o, z in rows).ljust(PT_SIZE, b"\xff")

    def ota_entry(seq):
        e = struct.pack("<I", seq) + b"\xff" * 20 + struct.pack("<I", 0xFFFFFFFF)
        return e + struct.pack("<I", zlib.crc32(struct.pack("<I", seq), 0xFFFFFFFF))

    rows = [("nvs", T_DATA, ST_NVS, 0x9000, 0x15000), ("otadata", T_DATA, ST_OTADATA, 0x1E000, 0x2000),
            ("app0", T_APP, 0x10, 0x20000, 0x10000), ("app1", T_APP, 0x11, 0x30000, 0x10000)]
    boards = load_boards()

    # 1. board policy
    for name, env, want in [("blipscope", "probe-s3-128-gesture", "SOAK"), ("nosuch", "probe-s3-128-gesture", "unknown"),
                            ("missileer", "probe-s3-128-gesture", None), ("missileer9", "gametest-s3-128-arm", None),
                            ("missileer-photo", "probe-s3-128-gesture", "not a Missileer board")]:
        b = dict(boards)
        # missileer9: a FAKE second bench board (dummy serial) for the accepted path.
        # missileer-photo: the photo unit (#394), which D6 must never flash.
        b.setdefault("missileer9", ("000000000009", "flash freely (Missileer work)"))
        b.setdefault("missileer-photo", ("90706931E794", "Missileer envs only; flash on Daniel's go (product photos)"))
        try:
            check_board(name, env, b)
            got = None
        except Refused as e:
            got = str(e)
        check(f"board {name!r} -> {'refused' if want else 'accepted'}",
              (want is None and got is None) or (want and got and want in got), got or "accepted")
    other = dict(boards, otherboard=("000000000000", "flash freely"))
    try:
        check_board("otherboard", "missileer-s3-128", other)
        got = None
    except Refused as e:
        got = str(e)
    check("a missileer-* image on a non-Missileer board is refused", bool(got and "Missileer firmware" in got), got)

    # 2. otadata
    blank = b"\xff" * 0x2000
    boot_app0 = ota_entry(1).ljust(0x1000, b"\xff") + b"\xff" * 0x1000
    seq2 = ota_entry(1).ljust(0x1000, b"\xff") + ota_entry(2).ljust(0x1000, b"\xff")
    badcrc = (struct.pack("<I", 2) + b"\xff" * 28).ljust(0x1000, b"\xff") + b"\xff" * 0x1000
    for nm, blob, want in [("blank", blank, 0), ("boot_app0 (seq 1)", boot_app0, 0), ("after one OTA (seq 2)", seq2, 1),
                           ("seq 2 with a bad crc is ignored", badcrc, 0)]:
        got = selected_slot(blob)[0]
        check(f"otadata {nm} -> ota_{want}", got == want, f"ota_{got}")
    check("the real boot_app0.bin convention: crc32(seq=1) == 0x4743989a",
          zlib.crc32(struct.pack("<I", 1), 0xFFFFFFFF) == 0x4743989A)

    # 3. image + manifest
    tmp = tempfile.mkdtemp(prefix="d6-selftest-")
    try:
        imgdir = os.path.join(tmp, "img")
        os.makedirs(imgdir)
        fw = bytearray(b"\xe9" + b"\x00" * 11 + struct.pack("<H", CHIP_ID_S3) + b"\x00" * 18)
        fw += struct.pack("<I", APP_DESC_MAGIC) + bytes(range(256)) * 15
        fw = bytes(fw)
        pt = table(rows)
        open(os.path.join(imgdir, "firmware.bin"), "wb").write(fw)
        open(os.path.join(imgdir, "partitions.bin"), "wb").write(pt)
        man = {"station": "selftest", "env": "probe-s3-128-gesture", "commit": "0" * 40, "extra_build_flags": "",
               "firmware": {"size": len(fw), "sha256": sha256(fw)}, "partitions": {"sha256": sha256(pt)},
               "app_offset": "0x20000", "provenance_elf_sha256": "0123456789abcdef"}
        json.dump(man, open(os.path.join(imgdir, "MANIFEST.json"), "w"))
        try:
            img = load_image(imgdir)
            check("a well-formed image dir loads", img["app_off"] == 0x20000)
        except Refused as e:
            check("a well-formed image dir loads", False, str(e))
            return 1
        tampered = os.path.join(tmp, "tampered")
        shutil.copytree(imgdir, tampered)
        open(os.path.join(tampered, "firmware.bin"), "ab").write(b"\x00")
        try:
            load_image(tampered)
            check("an image that no longer matches its manifest is refused", False, "accepted")
        except Refused as e:
            check("an image that no longer matches its manifest is refused", "does not match" in str(e), str(e))

        # 4. live path against the fake esptool. Its flash is 0x40000; app0 here is 0x20000.
        fake = [sys.executable, os.path.join(HERE, "fake_esptool.py")]

        def board(otadata, board_pt=pt):
            d = tempfile.mkdtemp(dir=tmp)
            flash = bytearray(b"\xff" * 0x40000)
            flash[PT_OFFSET:PT_OFFSET + len(board_pt)] = board_pt
            flash[0x9000:0x1E000] = (b"NVS!" * (0x15000 // 4))        # "settings" that must survive
            flash[0x1E000:0x20000] = otadata
            flash[0x20000:0x21000] = b"OLD!" * 0x400
            open(os.path.join(d, "flash.bin"), "wb").write(bytes(flash))
            return d

        def go(d, extra_env=None, resolve=lambda n: ("FAKEAPP", "FAKEDL")):
            saved = dict(os.environ)
            os.environ["FAKE_ESPTOOL_DIR"] = d
            for k in ("FAKE_FAIL", "FAKE_NO_HASH", "FAKE_DROP_WRITES", "FAKE_BOOT_WRITES_NVS"):
                os.environ.pop(k, None)
            os.environ.update(extra_env or {})
            try:
                import io
                import contextlib
                with contextlib.redirect_stdout(io.StringIO()):
                    rc = live(img, "missileer", fake, 5, 460800, Log(os.path.join(d, "log.txt")), resolve=resolve)
            finally:
                os.environ.clear()
                os.environ.update(saved)
            flash = open(os.path.join(d, "flash.bin"), "rb").read()
            calls = [json.loads(l)["sub"] for l in open(os.path.join(d, "calls.jsonl"))] \
                if os.path.exists(os.path.join(d, "calls.jsonl")) else []
            return rc, flash, calls

        d = board(boot_app0)
        rc, flash, calls = go(d)
        check("a good board: exit 0", rc == EXIT_OK, f"exit {rc}")
        check("  the app landed at app0", flash[0x20000:0x20000 + len(fw)] == fw)
        check("  NVS is byte-for-byte untouched", flash[0x9000:0x1E000] == b"NVS!" * (0x15000 // 4))
        check("  otadata and the table are untouched", flash[0x1E000:0x20000] == boot_app0 and
              flash[PT_OFFSET:PT_OFFSET + PT_SIZE] == pt)
        check("  esptool calls: read table, read otadata, write app", calls == ["read-flash", "read-flash", "write-flash"],
              str(calls))
        rc, flash, calls = go(board(seq2))
        check("a board booting app1 is REFUSED and nothing is written",
              rc == EXIT_STATE and "write-flash" not in calls and flash[0x20000:0x20004] == b"OLD!", f"exit {rc} {calls}")
        other_pt = table([rows[0], rows[1], ("app0", T_APP, 0x10, 0x10000, 0x10000), rows[3]])
        rc, flash, calls = go(board(boot_app0, other_pt))
        check("a board whose table differs is REFUSED and nothing is written",
              rc == EXIT_STATE and "write-flash" not in calls, f"exit {rc} {calls}")
        rc, _, _ = go(board(boot_app0), {"FAKE_NO_HASH": "1"})
        check("exit 0 WITHOUT 'Hash of data verified.' is a FAILURE", rc == EXIT_WRITE, f"exit {rc}")
        rc, _, _ = go(board(boot_app0), {"FAKE_DROP_WRITES": "1"})
        check("a write the board did not take is a FAILURE", rc == EXIT_WRITE, f"exit {rc}")
        rc, _, _ = go(board(boot_app0), {"FAKE_FAIL": "write-flash"})
        check("an esptool failure is a FAILURE", rc == EXIT_WRITE, f"exit {rc}")
        rc, _, calls = go(board(boot_app0), resolve=lambda n: (None, None))
        check("a board that is not connected: exit 5, esptool never called", rc == EXIT_UNREACHABLE and not calls)
        rc, _, calls = go(board(boot_app0), resolve=lambda n: ("FAKEAPP", None))
        check("a board that will not enter download mode: exit 5, esptool never called",
              rc == EXIT_UNREACHABLE and not calls)

        # 5. dry run resolves nothing and runs nothing on a port
        import contextlib
        import io
        real_run, real_popen = subprocess.run, subprocess.Popen

        def boom(*a, **k):
            raise AssertionError("dry run executed a command: %r" % (a[0] if a else k))
        subprocess.run, subprocess.Popen = boom, boom
        try:
            buf = io.StringIO()
            with contextlib.redirect_stdout(buf):
                rc = dry_run(img, "missileer", ["esptool"], 5, 460800)
            out = buf.getvalue()
            check("dry run: exit 0, executes nothing", rc == EXIT_OK)
            check("dry run prints the app-only write at 0x20000",
                  "write-flash 0x20000" in out and "--after hard-reset" in out, out[-400:])
            check("dry run prints the port as a board-port.py placeholder",
                  "board-port.py missileer --upload" in out)
        except AssertionError as e:
            check("dry run: executes nothing", False, str(e))
        finally:
            subprocess.run, subprocess.Popen = real_run, real_popen
        check("esptool 4 spellings", spell(4, "write-flash") == "write_flash" and spell(4, "hard-reset") == "hard_reset")
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    print("SELFTEST " + ("PASSED" if not bad else f"FAILED ({bad})"))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
