"""What check_wifi.py cannot read, read (docs/weatherscope-stage3.md A12.113-A12.116a; Q80, Q81, Q82).

scripts/check_wifi.py scans every tracked file it can decode, and EXCUSES an undecodable file whose extension is in its
BINARY_EXT -- images, firmware heads, packages, fonts, archives. Their bytes are never read by it. This reads them:

  - every printable ASCII run, and every UTF-16LE run, of 6 bytes or more, under check_wifi.py's OWN patterns (a network
    name in an association or ssid context, a BSSID, a private LAN address), plus a user-directory path (C:/Users/<x>,
    /Users/<x>, /home/<x>; GitHub's runner prefix /home/runner/ names no one and is allowed);
  - for jpg, jpeg, png and webp: EXIF GPS (a GPS IFD holding a latitude or longitude tag) and XMP location tags;
  - `wifi-store`: ESP-IDF's Wi-Fi NVS namespace or the firmware's own Wi-Fi namespaces, in a file that is NOT an ESP app
    image -- the tell of a committed settings or flash dump, whose network name has no context and whose BSSID is 6 raw
    bytes. Every app image carries those names in its code, so an app image (E9, and esp_app_desc_t's magic at 0x20) is
    exempt; a bootloader or a full flash dump starts E9 too, without that magic, and is not;
  - CONTAINERS (zip, 3mf, gz, tgz, tar, xz): their members are inflated with the standard library and judged by their OWN
    extension under the same rules, reported as <container>!<member>. Inflation is capped per file; a member that is
    itself a container is not opened; a container that cannot be opened (7z, encrypted, corrupt, past the cap, nested)
    fails as `container` until it is reviewed;
  - fonts and PDFs are not read: each is settled by its exact hash in the allowlist (a PDF after a recorded text read),
    so a new or changed one fails until it is reviewed.

THE POPULATION AND THE PATTERNS ARE check_wifi.py's, IMPORTED: what it excuses (check_wifi.excused, the one definition)
is exactly what this reads.

    python3 scripts/check_binary_strings.py [--root DIR] [--allow FILE]   exit 0 clean; 1 a finding, a stale or idle
                                                                           allowance; 2 rig or BLIND
    python3 scripts/check_binary_strings.py --selftest [--root DIR]       25 rows, each plant in a copy of a real tracked
                                                                           file; each must see exactly its file and kind
    python3 scripts/check_binary_strings.py --sabotage                    each sabotage, planted into a copy of this
                                                                           script, must turn exactly its rows red

The allowlist (scripts/binary-allow.txt) pins kind, path and the SHA-256 of the whole file. A changed file is a stale
entry; an entry with no finding of its kind is idle (a font or pdf entry: whose file is no longer a font or pdf); both
are refused. It prints verdicts, kinds and masked paths -- never a matched value, never a coordinate.
"""
import collections
import gzip
import hashlib
import io
import lzma
import os
import re
import shutil
import struct
import subprocess
import sys
import tarfile
import tempfile
import zipfile
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
ALLOW = os.path.join(HERE, "binary-allow.txt")
sys.path.insert(0, HERE)
import check_wifi as cw  # noqa: E402  the population and the patterns: imported, never restated

ASCII = re.compile(rb"[\x20-\x7e]{6,}")
UTF16 = re.compile(rb"(?:[\x20-\x7e]\x00){6,}")
USERPATH = re.compile(r"(?:[A-Za-z]:[\\/](?:Users|home)[\\/]|/Users/|/home/(?!runner/))[^/\\\s]+", re.I)
XMP_LOC = (b"GPSLatitude", b"GPSLongitude", b"GPSPosition", b"GPSAltitude", b"photoshop:City", b"photoshop:State",
           b"photoshop:Country", b"Iptc4xmpCore:Location", b"LocationShown", b"LocationCreated", b"GpsLatitude",
           b"GpsLongitude")
# ESP-IDF's Wi-Fi NVS namespace (libnet80211 also names sta.ssid, sta.pswd, sta.bssid in it) and the firmware's own
# (include/WiFiManagerHelpers.h: wifi-fast holds the last good BSSID as 6 raw bytes; wifi-join its join state).
WIFI_STORE = (b"nvs." + b"net80211", b"wifi-" + b"fast", b"wifi-" + b"join")
APP_MAGIC = bytes([0x32, 0x54, 0xCD, 0xAB])     # esp_app_desc_t's magic, at file offset 0x20 of an app image
FONT_EXT = {"woff", "woff2", "ttf", "otf"}
PDF_EXT = {"pdf"}
CONTAINER_EXT = {"zip", "3mf", "gz", "tgz", "tar", "xz", "7z"}
PHOTO = {"jpg", "jpeg", "png", "webp"}
CAP = 64 * 1024 * 1024                          # inflated bytes per container (Q82 c1)
KINDS = ("name", "bssid", "lan", "path", "exif-gps", "xmp-location", "wifi-store", "font", "pdf", "container")
HASH_SETTLED = {"font": FONT_EXT, "pdf": PDF_EXT}


def ext_of(name):
    base = name.replace("\\", "/").rsplit("/", 1)[-1]
    return base.rsplit(".", 1)[-1].lower() if "." in base else ""


# ------------------------------------------------------------------------------------------------- photo metadata

def tiff_gps(t):
    """True when a TIFF/EXIF block's IFD0 points at a GPS IFD holding a latitude or longitude tag (1-4)."""
    if len(t) < 8 or t[:2] not in (b"II", b"MM"):
        return False
    e = "<" if t[:2] == b"II" else ">"
    try:
        off = struct.unpack(e + "I", t[4:8])[0]
        for i in range(struct.unpack(e + "H", t[off:off + 2])[0]):
            tag, _, _, val = struct.unpack(e + "HHII", t[off + 2 + 12 * i:off + 14 + 12 * i])
            if tag == 0x8825:
                tags = {struct.unpack(e + "H", t[val + 2 + 12 * j:val + 4 + 12 * j])[0]
                        for j in range(struct.unpack(e + "H", t[val:val + 2])[0])}
                return bool(tags & {1, 2, 3, 4})
    except (struct.error, IndexError):
        return False
    return False


def exif_blocks(data, ext):
    """The EXIF (TIFF) blocks of a JPEG's APP1 segments, a PNG's eXIf chunks or a WebP's EXIF chunk."""
    out = []
    try:
        if ext in ("jpg", "jpeg") and data[:2] == b"\xff\xd8":
            i = 2
            while i + 4 <= len(data) and data[i] == 0xFF and data[i + 1] not in (0xD9, 0xDA):
                ln = struct.unpack(">H", data[i + 2:i + 4])[0]
                seg = data[i + 4:i + 2 + ln]
                if data[i + 1] == 0xE1 and seg[:6] == b"Exif\x00\x00":
                    out.append(seg[6:])
                i += 2 + ln
        elif ext == "png" and data[:8] == b"\x89PNG\r\n\x1a\n":
            i = 8
            while i + 8 <= len(data):
                ln = struct.unpack(">I", data[i:i + 4])[0]
                if data[i + 4:i + 8] == b"eXIf":
                    out.append(data[i + 8:i + 8 + ln])
                i += 12 + ln
        elif ext == "webp" and data[:4] == b"RIFF":
            i = 12
            while i + 8 <= len(data):
                ln = struct.unpack("<I", data[i + 4:i + 8])[0]
                if data[i:i + 4] == b"EXIF":
                    blk = data[i + 8:i + 8 + ln]
                    out.append(blk[6:] if blk[:6] == b"Exif\x00\x00" else blk)
                i += 8 + ln + (ln & 1)
    except struct.error:
        pass
    return out


# ------------------------------------------------------------------------------------------------- containers

class Unopenable(Exception):
    """A container this cannot read in full: it fails as `container` until reviewed."""


def is_app_image(raw):
    return raw[:1] == b"\xe9" and raw[0x20:0x24] == APP_MAGIC


def is_container(data):
    """By magic: zip, gzip, xz, tar (Q82 c2)."""
    return (data[:4] == b"PK\x03\x04" or data[:2] == b"\x1f\x8b" or data[:6] == b"\xfd7zXZ\x00"
            or data[257:262] == b"ustar")


def _capped(stream, budget):
    out = bytearray()
    while True:
        chunk = stream.read(1 << 20)
        if not chunk:
            return bytes(out)
        out += chunk
        if len(out) > budget:
            raise Unopenable("past the inflation cap")


def _tar_members(blob, cap, used):
    out = []
    with tarfile.open(fileobj=io.BytesIO(blob), mode="r:") as t:
        for m in t.getmembers():
            if m.isfile():
                data = _capped(t.extractfile(m), cap - used)
                used += len(data)
                out.append((m.name, data))
    return out


def members(ext, raw, cap):
    """[(member name, inflated bytes)] of a container, or Unopenable. Inflation is capped across the whole file."""
    used, out = 0, []
    try:
        if ext in ("zip", "3mf"):
            with zipfile.ZipFile(io.BytesIO(raw)) as z:
                for info in z.infolist():
                    if info.is_dir():
                        continue
                    if info.flag_bits & 0x1:
                        raise Unopenable("an encrypted member")
                    with z.open(info) as f:
                        data = _capped(f, cap - used)
                    used += len(data)
                    out.append((info.filename, data))
        elif ext in ("gz", "tgz"):
            data = _capped(gzip.GzipFile(fileobj=io.BytesIO(raw)), cap)
            out = _tar_members(data, cap, len(data)) if data[257:262] == b"ustar" else [(ext_strip(ext), data)]
        elif ext == "tar":
            out = _tar_members(raw, cap, 0)
        elif ext == "xz":
            data = _capped(lzma.LZMAFile(io.BytesIO(raw)), cap)
            out = _tar_members(data, cap, len(data)) if data[257:262] == b"ustar" else [(ext_strip(ext), data)]
        else:
            raise Unopenable("%s cannot be opened with the standard library" % ext)
    except (zipfile.BadZipFile, zipfile.LargeZipFile, tarfile.TarError, OSError, EOFError, lzma.LZMAError,
            zlib.error, NotImplementedError, RuntimeError) as e:
        raise Unopenable(type(e).__name__)
    for name, data in out:
        if is_container(data):
            raise Unopenable("a member that is itself a container")
    return out


def ext_strip(ext):
    return "member-of-." + ext


# ------------------------------------------------------------------------------------------------- the scan

def kinds_of_bytes(ext, raw):
    """The kinds of finding in one file's (or one member's) bytes, judged by its own extension."""
    found = set()
    runs = [m.group().decode("ascii") for m in ASCII.finditer(raw)]
    runs += [m.group().decode("utf-16-le") for m in UTF16.finditer(raw)]
    for r in runs:
        found.update(cw.line_findings(r)[0])
        if USERPATH.search(r):
            found.add("path")
    if ext in PHOTO:
        if any(tiff_gps(b) for b in exif_blocks(raw, ext)):
            found.add("exif-gps")
        if b"x:xmpmeta" in raw and any(t in raw for t in XMP_LOC):
            found.add("xmp-location")
    if not is_app_image(raw) and any(n in raw for n in WIFI_STORE):
        found.add("wifi-store")
    return found


def kinds_in(ext, raw, cap):
    """([(where, kind)], members read): where is "" for the file itself, "!<member>" inside a container."""
    if ext in FONT_EXT:
        return [("", "font")], 0
    if ext in PDF_EXT:
        return [("", "pdf")], 0
    if ext in CONTAINER_EXT:
        try:
            ms = members(ext, raw, cap)
        except Unopenable:
            return [("", "container")], 0
        return sorted(("!" + name, k) for name, data in ms for k in kinds_of_bytes(ext_of(name), data)), len(ms)
    return sorted(("", k) for k in kinds_of_bytes(ext, raw)), 0


def excused(root, files):
    """[(path, ext, bytes)] for every file check_wifi.py excuses -- check_wifi.excused(), the one definition."""
    out = []
    for rel in files:
        rel = rel.replace(os.sep, "/")
        try:
            raw = open(os.path.join(root, rel), "rb").read()
        except OSError:
            continue
        if not cw.excused(rel, raw):
            continue
        out.append((rel, ext_of(rel), raw))
    return out


def read_allow(path):
    """[(kind, path, sha256, reason)]; None if unreadable or malformed."""
    try:
        text = open(path, encoding="utf-8").read()
    except OSError:
        return None
    out = []
    for raw in text.splitlines():
        if not raw.strip() or raw.startswith("#"):
            continue
        parts = raw.split("\t")
        if (len(parts) != 4 or parts[0] not in KINDS or not re.fullmatch(r"[0-9a-f]{64}", parts[2])
                or not parts[3].strip()):
            return None
        out.append(tuple(parts))
    return out


def judge(root, files, allow, cap=CAP):
    """Everything the verdict needs. Findings are (path, kind); no value leaves this function."""
    pop = excused(root, files)
    found, counts = {}, collections.Counter()
    for rel, ext, raw in pop:
        ks, n = kinds_in(ext, raw, cap)
        found[rel] = (ext, ks, hashlib.sha256(raw).hexdigest())
        if ext in FONT_EXT:
            counts["fonts"] += 1
        elif ext in PDF_EXT:
            counts["pdfs"] += 1
        elif ext in CONTAINER_EXT and n:
            counts["containers"] += 1
            counts["members"] += n
    used, stale, idle = set(), [], []
    for kind, rel, sha, _ in allow:
        if rel not in found or found[rel][2] != sha:
            stale.append((rel, kind))
        elif kind in HASH_SETTLED:
            # A hash-settled allowance is idle only if its file is no longer that class (A12.116a).
            if found[rel][0] in HASH_SETTLED[kind]:
                used.add((rel, kind))
            else:
                idle.append((rel, kind))
        elif ("", kind) not in found[rel][1]:
            idle.append((rel, kind))
        else:
            used.add((rel, kind))
    findings = sorted((rel + where, k) for rel, (_, ks, _) in found.items() for where, k in ks
                      if not (where == "" and (rel, k) in used))
    return {"excused": len(pop), "by_ext": collections.Counter(e for _, e, _ in pop), "counts": counts,
            "findings": findings, "allowed": sorted(used), "stale": stale, "idle": idle}


def report(v):
    c = v["counts"]
    print("binary strings: %d excused by check_wifi.py (%s); %d font(s) and %d pdf settled by hash, %d container(s) read "
          "(%d members), %d scanned; findings %d, allowed %d, stale %d, idle %d"
          % (v["excused"], ", ".join("%s %d" % kv for kv in sorted(v["by_ext"].items())), c["fonts"], c["pdfs"],
             c["containers"], c["members"], v["excused"] - c["fonts"] - c["pdfs"] - c["containers"],
             len(v["findings"]), sum(1 for _, k in v["allowed"] if k not in HASH_SETTLED), len(v["stale"]),
             len(v["idle"])))
    for rel, kind in v["findings"]:
        print("  FINDING %s: %s" % (kind, cw.masked(rel)))
    for rel, kind in v["stale"]:
        print("  STALE allowance %s: %s (the file is gone or its bytes changed)" % (kind, cw.masked(rel)))
    for rel, kind in v["idle"]:
        print("  IDLE allowance %s: %s (it allows nothing)" % (kind, cw.masked(rel)))
    bad = v["findings"] or v["stale"] or v["idle"]
    print("binary strings: " + ("FAILED" if bad else "OK"))
    return 1 if bad else 0


# ------------------------------------------------------------------------------------------------- selftest

def _tiff_gps():
    """A minimal little-endian TIFF whose IFD0 points at a GPS IFD holding a latitude (tag 2)."""
    t = bytearray(b"II*\x00" + struct.pack("<I", 8))
    t += struct.pack("<H", 1) + struct.pack("<HHII", 0x8825, 4, 1, 26) + struct.pack("<I", 0)   # IFD0 at 8
    t += struct.pack("<H", 1) + struct.pack("<HHII", 2, 5, 3, 44) + struct.pack("<I", 0)        # GPS IFD at 26
    t += struct.pack("<6I", 1, 1, 2, 1, 3, 1)                                                     # 3 rationals at 44
    return bytes(t)


def _exif_gps_segment():
    body = b"Exif\x00\x00" + _tiff_gps()
    return b"\xff\xe1" + struct.pack(">H", len(body) + 2) + body


def _png_with_gps(png):
    """The real PNG's signature and IHDR, an eXIf chunk holding a GPS IFD, and IEND."""
    tiff = _tiff_gps()
    return png[:33] + struct.pack(">I", len(tiff)) + b"eXIf" + tiff + b"\x00" * 4 + b"\x00\x00\x00\x00IEND\xaeB`\x82"


def _zip_with(raw, name, data, method):
    buf = io.BytesIO(raw)
    with zipfile.ZipFile(buf, "a") as z:
        z.writestr(name, data, compress_type=method)
    return buf.getvalue()


def selftest(root):
    files = cw.tracked(root)
    pop = excused(root, files) if files else []
    pick = {}
    for rel, ext, raw in pop:
        key = "app" if ext == "bin" and is_app_image(raw) else ext
        if key in pick or key not in ("png", "jpg", "bin", "woff2", "3mf", "pdf", "app"):
            continue
        ks, _ = kinds_in(ext, raw, CAP)
        if all(k in ("font", "pdf") for _, k in ks):
            pick[key] = (rel, raw)
    if len(pick) != 7:
        print("BLIND: cannot find a clean tracked png, jpg, bin, app-image bin, woff2, 3mf and pdf (found %s)"
              % sorted(pick))
        return 2
    (png, pngr), (jpg, jpgr), (bn, bnr) = pick["png"], pick["jpg"], pick["bin"]
    (font, fontr), (tmf, tmfr), (pdf, pdfr), (app, appr) = pick["woff2"], pick["3mf"], pick["pdf"], pick["app"]
    base_allow = [("font", font, hashlib.sha256(fontr).hexdigest(), "selftest"),
                  ("pdf", pdf, hashlib.sha256(pdfr).hexdigest(), "selftest")]
    rels = sorted(r for r, _ in pick.values())
    originals = dict(pick.values())

    # Built in pieces so this file does not itself match check_wifi.py's patterns (as that file's own are written).
    name = b"ss" + b'id: "PlantNet7"'
    strings = {"name": name, "bssid": b"bss" + b"id " + b":".join([b"12", b"34", b"56", b"78", b"9a", b"bc"]),
               "lan": b"host " + b".".join([b"192", b"168", b"7", b"23"]) + b" up", "path": b"C:/" + b"Users/someone/x"}
    nvs = b"\x00" + b"nvs." + b"net80211" + b"\x00" + b"sta." + b"ssid" + b"\x00"   # not WIFI_STORE: S-Q82d empties it
    no_desc = appr[:0x20] + b"\x00" * 4 + appr[0x24:]

    # (number, name, planted {rel: bytes}, extra files, allowlist, cap, want) -- want: [(path, kind)], or
    # ("stale+", [(path, kind)]) for a stale allowance plus those findings, or "stale" / "idle" alone.
    rows = [(1, "control: the unplanted copies", {}, [], base_allow, CAP, [])]
    n = 2
    for kind, s in strings.items():
        for rel, raw in ((png, pngr), (bn, bnr)):
            rows.append((n, "%s in a real %s" % (kind, ext_of(rel)), {rel: raw + b"\x00" + s + b"\x00"}, [],
                         base_allow, CAP, [(rel, kind)]))
            n += 1
    rows += [
        (10, "an EXIF GPS block in a real jpg", {jpg: jpgr[:2] + _exif_gps_segment() + jpgr[2:]}, [], base_allow, CAP,
         [(jpg, "exif-gps")]),
        (11, "an XMP location tag in a real png",
         {png: pngr + b'<x:xmpmeta xmlns:x="adobe:ns:meta/"><rdf:Description exif:GPSLatitude="1,0N"/></x:xmpmeta>'},
         [], base_allow, CAP, [(png, "xmp-location")]),
        (12, "a font with no allowance fails", {}, [], base_allow[1:], CAP, [(font, "font")]),
        (13, "a stale allowance", {}, [], base_allow + [("exif-gps", jpg, "0" * 64, "selftest")], CAP, "stale"),
        (14, "an idle allowance", {}, [], base_allow + [("exif-gps", jpg, hashlib.sha256(jpgr).hexdigest(), "s")], CAP,
         "idle"),
        (15, "(a) a BOM-led UTF-16 .bin with a name", {"plant/bom.bin": b"\xff\xfe" + name.decode().encode("utf-16-le")},
         ["plant/bom.bin"], base_allow, CAP, [("plant/bom.bin", "name")]),
        (16, "(b) an allowlisted font with one byte changed", {font: fontr[:-1] + bytes([fontr[-1] ^ 1])}, [],
         base_allow, CAP, ("stale+", [(font, "font")])),
        (17, "(c) a deflated .3mf member with a name and a path",
         {tmf: _zip_with(tmfr, "plant/notes.txt", name + b"\n" + strings["path"], zipfile.ZIP_DEFLATED)}, [],
         base_allow, CAP, [(tmf + "!plant/notes.txt", "name"), (tmf + "!plant/notes.txt", "path")]),
        (18, "(c) a changed PDF fails",
         {pdf: pdfr + b"\n9 0 obj<</Filter/FlateDecode>>stream\n" + zlib.compress(name) + b"\nendstream endobj\n"}, [],
         base_allow, CAP, ("stale+", [(pdf, "pdf")])),
        (19, "(c) an unopenable .3mf", {tmf: tmfr[:len(tmfr) // 2]}, [], base_allow, CAP, [(tmf, "container")]),
        (20, "(c1) a .3mf past a small cap", {}, [], base_allow, 1024, [(tmf, "container")]),
        (21, "(c2) a zip stored inside a .3mf", {tmf: _zip_with(tmfr, "plant/inner.zip",
                                                                _zip_with(b"", "x.txt", b"x" * 8, zipfile.ZIP_STORED),
                                                                zipfile.ZIP_STORED)}, [], base_allow, CAP,
         [(tmf, "container")]),
        (22, "(c3) a .png member with EXIF GPS in a .3mf",
         {tmf: _zip_with(tmfr, "plant/thumb.png", _png_with_gps(pngr), zipfile.ZIP_DEFLATED)}, [], base_allow, CAP,
         [(tmf + "!plant/thumb.png", "exif-gps")]),
        (23, "(d) the Wi-Fi NVS names in a real .bin", {bn: bnr + nvs}, [], base_allow, CAP, [(bn, "wifi-store")]),
        (24, "(d) control: the names in an app image", {app: appr + nvs}, [], base_allow, CAP, []),
        (25, "(d2) the names after E9 with no app descriptor", {app: no_desc + nvs}, [], base_allow, CAP,
         [(app, "wifi-store")]),
    ]

    ok = True
    for num, label, planted, extra, allow, cap, want in rows:
        for rel, raw in planted.items():
            assert raw != originals.get(rel), "row %d: the plant did not change %s" % (num, rel)   # it planted
        td = tempfile.mkdtemp(prefix="binstrings-")
        try:
            for rel, raw in list(originals.items()) + list(planted.items()):
                p = os.path.join(td, *rel.split("/"))
                os.makedirs(os.path.dirname(p), exist_ok=True)
                open(p, "wb").write(raw)
            v = judge(td, rels + extra, allow, cap)
        finally:
            shutil.rmtree(td, ignore_errors=True)
        if want == "stale":
            good = len(v["stale"]) == 1 and not v["findings"] and not v["idle"]
        elif want == "idle":
            good = len(v["idle"]) == 1 and not v["findings"] and not v["stale"]
        elif isinstance(want, tuple):
            good = len(v["stale"]) == 1 and v["findings"] == sorted(want[1]) and not v["idle"]
        else:
            good = v["findings"] == sorted(want) and not v["stale"] and not v["idle"]
        ok &= good
        print("  %s %2d %-46s findings %s stale %d idle %d" % ("ok  " if good else "FAIL", num, label,
                                                             [k for _, k in v["findings"]] or "-", len(v["stale"]),
                                                             len(v["idle"])))
    print("selftest " + ("PASSED" if ok else "FAILED") + ": %d rows, planted into copies of %s" % (len(rows), ", ".join(rels)))
    return 0 if ok else 1


# ------------------------------------------------------------------------------------------------- sabotage

# Each anchor occurs exactly once in this file: the constants are split so that this table does not hold a second copy.
SABOTAGES = {
    "S-Q82a": ("        if not cw.exc" + "used(rel, raw):\n            continue\n",
               "        try:  # SABOTAGE S-Q82a: excused decided by check_wifi.decode() again\n"
               "            cw.decode(raw)\n            continue\n        except UnicodeDecodeError:\n            pass\n"
               "        if not cw.is_binary_ext(rel):\n            continue\n", {15}),
    "S-Q82c": ("    if ext in PDF_" + "EXT:\n        return [(\"\", \"pdf\")], 0\n    if ext in CONTAINER_" + "EXT:\n",
               "    if False:  # SABOTAGE S-Q82c: no PDF hash rule, no container reading\n", {17, 18, 19, 20, 21, 22}),
    "S-Q82d": ("WIFI_STORE = (b\"nvs.\" + b\"net80211\"", "WIFI_STORE = () and (b\"nvs.\" + b\"net80211\"", {23, 25}),
    "S-Q82e": ("    return raw[:1] == b\"\\xe9\" and raw[0x20:0x24] == APP_" + "MAGIC\n",
               "    return raw[:1] == b\"\\xe9\"  # SABOTAGE S-Q82e: E9 alone exempts\n", {25}),
}
ROW = re.compile(r"^\s*(ok|FAIL)\s+(\d+)\s")


def _selftest_of(src, root):
    with tempfile.TemporaryDirectory() as td:
        os.makedirs(os.path.join(td, "scripts"))
        open(os.path.join(td, "scripts", "check_binary_strings.py"), "w", encoding="utf-8", newline="\n").write(src)
        shutil.copy(os.path.join(HERE, "check_wifi.py"), os.path.join(td, "scripts", "check_wifi.py"))
        r = subprocess.run([sys.executable, "-I", os.path.join(td, "scripts", "check_binary_strings.py"), "--selftest",
                            "--root", root], capture_output=True, text=True)
    red = {int(m.group(2)) for m in (ROW.match(l) for l in r.stdout.splitlines()) if m and m.group(1) == "FAIL"}
    rows = {int(m.group(2)) for m in (ROW.match(l) for l in r.stdout.splitlines()) if m}
    return r.returncode, red, rows


def sabotage(root):
    src = open(os.path.abspath(__file__), encoding="utf-8").read().replace("\r\n", "\n")
    ok = True
    rc, red, rows = _selftest_of(src, root)
    good = rc == 0 and not red and len(rows) == 25
    print("  %s control: the unplanted copy's selftest: exit %d, %d rows, red %s" % ("ok  " if good else "FAIL", rc,
                                                                                  len(rows), sorted(red) or "-"))
    ok &= good
    for name, (anchor, repl, want) in SABOTAGES.items():
        n = src.count(anchor)
        if n != 1:
            print("  FAIL %s: its anchor occurs %d times, need exactly 1 -- refused, not planted" % (name, n))
            ok = False
            continue
        planted = src.replace(anchor, repl)
        assert planted != src
        rc, red, rows = _selftest_of(planted, root)
        good = rc == 1 and red == want and len(rows) == 25
        ok &= good
        print("  %s %s: exit %d, red %s (want %s)" % ("ok  " if good else "FAIL", name, rc, sorted(red), sorted(want)))
    print("SABOTAGE " + ("PASSED" if ok else "FAILED"))
    return 0 if ok else 1


# ------------------------------------------------------------------------------------------------- main

def main(argv):
    root, allow_path = ROOT, ALLOW
    if "--root" in argv:
        root = argv[argv.index("--root") + 1]
    if "--allow" in argv:
        allow_path = argv[argv.index("--allow") + 1]
    if "--selftest" in argv:
        return selftest(root)
    if "--sabotage" in argv:
        return sabotage(root)
    files = cw.tracked(root)
    if not files:
        print("BLIND: git ls-files returned nothing under %s" % root)
        return 2
    allow = read_allow(allow_path)
    if allow is None:
        print("BLIND: cannot read %s, or it holds a malformed entry" % allow_path)
        return 2
    v = judge(root, files, allow)
    if v["excused"] == 0:
        print("BLIND: check_wifi.py excuses nothing here; there is nothing to read")
        return 2
    return report(v)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
