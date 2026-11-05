"""What check_wifi.py cannot read, read (docs/weatherscope-stage3.md A12.113; Q80, Q81).

scripts/check_wifi.py scans every tracked file it can decode, and EXCUSES an undecodable file whose extension is in its
BINARY_EXT -- images, firmware heads, packages, fonts. Their bytes are never read by it. This reads them:

  - every printable ASCII run, and every UTF-16LE run, of 6 bytes or more, under check_wifi.py's OWN patterns (a network
    name in an association or ssid context, a BSSID, a private LAN address), plus a user-directory path (C:/Users/<x>,
    /Users/<x>, /home/<x>; GitHub's runner prefix /home/runner/ names no one and is allowed);
  - for jpg, jpeg, png and webp: EXIF GPS (a GPS IFD holding a latitude or longitude tag) and XMP location tags;
  - fonts (woff, woff2, ttf, otf) are third-party and out of scope (Q80): not scanned, but each must be allowed by its
    exact hash, so a new or changed font fails until it is reviewed.

THE POPULATION, THE PATTERNS AND THE DECODE ARE check_wifi.py's, IMPORTED, never restated: what it excuses is exactly
what this scans, and the two cannot drift.

    python3 scripts/check_binary_strings.py [--root DIR] [--allow FILE]    exit 0 clean; 1 a finding, or a stale or idle
                                                                            allowance; 2 rig or BLIND
    python3 scripts/check_binary_strings.py --selftest                     each plant, in a copy of a real tracked file,
                                                                            must turn it red naming exactly its file and kind

The allowlist (scripts/binary-allow.txt) pins kind, path and the SHA-256 of the whole file. A changed file is a stale
entry and an entry with no finding of its kind is idle; both are refused. It prints verdicts, kinds and masked paths --
never a matched value, never a coordinate.
"""
import collections
import hashlib
import os
import re
import shutil
import struct
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
ALLOW = os.path.join(HERE, "binary-allow.txt")
sys.path.insert(0, HERE)
import check_wifi as cw  # noqa: E402  the population, the patterns and the decode: imported, never restated

ASCII = re.compile(rb"[\x20-\x7e]{6,}")
UTF16 = re.compile(rb"(?:[\x20-\x7e]\x00){6,}")
USERPATH = re.compile(r"(?:[A-Za-z]:[\\/](?:Users|home)[\\/]|/Users/|/home/(?!runner/))[^/\\\s]+", re.I)
XMP_LOC = (b"GPSLatitude", b"GPSLongitude", b"GPSPosition", b"GPSAltitude", b"photoshop:City", b"photoshop:State",
           b"photoshop:Country", b"Iptc4xmpCore:Location", b"LocationShown", b"LocationCreated", b"GpsLatitude",
           b"GpsLongitude")
FONT = {"woff", "woff2", "ttf", "otf"}
PHOTO = {"jpg", "jpeg", "png", "webp"}
KINDS = ("name", "bssid", "lan", "path", "exif-gps", "xmp-location", "font")


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


# ------------------------------------------------------------------------------------------------- the scan

def excused(root, files):
    """[(path, ext, bytes)] for every file check_wifi.py excuses: undecodable, with an extension in its BINARY_EXT."""
    out = []
    for rel in files:
        rel = rel.replace(os.sep, "/")
        base = rel.rsplit("/", 1)[-1]
        ext = base.rsplit(".", 1)[-1].lower() if "." in base else ""
        if ext not in cw.BINARY_EXT:
            continue
        try:
            raw = open(os.path.join(root, rel), "rb").read()
        except OSError:
            continue
        try:
            cw.decode(raw)
        except UnicodeDecodeError:
            out.append((rel, ext, raw))
    return out


def kinds_in(ext, raw):
    """The kinds of finding in one file (fonts are not scanned: their kind is 'font', settled by the allowlist)."""
    if ext in FONT:
        return {"font"}
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
    return found


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


def judge(root, files, allow):
    """Everything the verdict needs. Findings are (path, kind); no value leaves this function."""
    pop = excused(root, files)
    found = {}
    for rel, ext, raw in pop:
        found[rel] = (kinds_in(ext, raw), hashlib.sha256(raw).hexdigest())
    used, stale, idle = set(), [], []
    for kind, rel, sha, _ in allow:
        if rel not in found or found[rel][1] != sha:
            stale.append((rel, kind))
        elif kind not in found[rel][0]:
            idle.append((rel, kind))
        else:
            used.add((rel, kind))
    findings = sorted((rel, k) for rel, (ks, _) in found.items() for k in ks if (rel, k) not in used)
    by_ext = collections.Counter(ext for _, ext, _ in pop)
    return {"excused": len(pop), "by_ext": by_ext, "fonts": sum(1 for _, e, _ in pop if e in FONT),
            "findings": findings, "allowed": sorted(used), "stale": stale, "idle": idle}


def report(v):
    print("binary strings: %d excused by check_wifi.py (%s); %d font(s) settled by hash, %d scanned; "
          "findings %d, allowed %d, stale %d, idle %d"
          % (v["excused"], ", ".join("%s %d" % kv for kv in sorted(v["by_ext"].items())), v["fonts"],
             v["excused"] - v["fonts"], len(v["findings"]), sum(1 for _, k in v["allowed"] if k != "font"),
             len(v["stale"]), len(v["idle"])))
    for rel, kind in v["findings"]:
        print("  FINDING %s: %s" % (kind, cw.masked(rel)))
    for rel, kind in v["stale"]:
        print("  STALE allowance %s: %s (the file is gone or its bytes changed)" % (kind, cw.masked(rel)))
    for rel, kind in v["idle"]:
        print("  IDLE allowance %s: %s (no finding of that kind: it allows nothing)" % (kind, cw.masked(rel)))
    bad = v["findings"] or v["stale"] or v["idle"]
    print("binary strings: " + ("FAILED" if bad else "OK"))
    return 1 if bad else 0


# ------------------------------------------------------------------------------------------------- selftest

def _exif_gps_segment():
    """A minimal APP1 Exif segment whose IFD0 points at a GPS IFD holding a latitude (tag 2)."""
    t = bytearray(b"II*\x00" + struct.pack("<I", 8))
    t += struct.pack("<H", 1) + struct.pack("<HHII", 0x8825, 4, 1, 26) + struct.pack("<I", 0)   # IFD0 at 8
    t += struct.pack("<H", 1) + struct.pack("<HHII", 2, 5, 3, 44) + struct.pack("<I", 0)        # GPS IFD at 26
    t += struct.pack("<6I", 1, 1, 2, 1, 3, 1)                                                     # 3 rationals at 44
    body = b"Exif\x00\x00" + bytes(t)
    return b"\xff\xe1" + struct.pack(">H", len(body) + 2) + body


def selftest():
    files = cw.tracked(ROOT)
    pop = excused(ROOT, files) if files else []
    pick = {}
    for rel, ext, raw in pop:
        if ext in ("png", "jpg", "bin", "woff2") and ext not in pick and not kinds_in(ext, raw) - {"font"}:
            pick[ext] = (rel, raw)
    if len(pick) != 4:
        print("BLIND: cannot find a clean tracked png, jpg, bin and woff2 to plant into (found %s)" % sorted(pick))
        return 2
    font_rel, font_raw = pick["woff2"]
    font_allow = ("font", font_rel, hashlib.sha256(font_raw).hexdigest(), "selftest")

    def tree(planted):
        """A scratch tree holding the four clean copies, `planted` (rel -> bytes) overriding."""
        td = tempfile.mkdtemp(prefix="binstrings-")
        for rel, raw in list((r, b) for r, b in pick.values()) + list(planted.items()):
            p = os.path.join(td, *rel.split("/"))
            os.makedirs(os.path.dirname(p), exist_ok=True)
            open(p, "wb").write(raw)
        return td

    rels = sorted(r for r, _ in pick.values())
    png, jpg, bn = pick["png"], pick["jpg"], pick["bin"]
    # Built in pieces so this file does not itself match check_wifi.py's patterns (as that file's own are written).
    strings = {"name": b"ss" + b'id: "PlantNet7"',
               "bssid": b"bss" + b"id " + b":".join([b"12", b"34", b"56", b"78", b"9a", b"bc"]),
               "lan": b"host " + b".".join([b"192", b"168", b"7", b"23"]) + b" up",
               "path": b"C:/" + b"Users/someone/x"}
    cases = [("control: the unplanted copies", {}, [font_allow], [])]
    for kind, s in strings.items():
        for rel, raw in (png, bn):
            cases.append(("%s in a real %s" % (kind, rel.rsplit(".", 1)[-1]), {rel: raw + b"\x00" + s + b"\x00"},
                          [font_allow], [(rel, kind)]))
    j = jpg[1]
    cases.append(("an EXIF GPS block in a real jpg", {jpg[0]: j[:2] + _exif_gps_segment() + j[2:]}, [font_allow],
                  [(jpg[0], "exif-gps")]))
    cases.append(("an XMP location tag in a real png",
                  {png[0]: png[1] + b'<x:xmpmeta xmlns:x="adobe:ns:meta/"><rdf:Description exif:GPSLatitude="1,0N"/>'
                                    b"</x:xmpmeta>"}, [font_allow], [(png[0], "xmp-location")]))
    cases.append(("a font whose bytes changed", {font_rel: font_raw[:-1] + bytes([font_raw[-1] ^ 1])}, [],
                  [(font_rel, "font")]))
    stale = ("exif-gps", jpg[0], "0" * 64, "selftest")
    cases.append(("a stale allowance", {}, [font_allow, stale], "stale"))
    idle = ("exif-gps", jpg[0], hashlib.sha256(jpg[1]).hexdigest(), "selftest")
    cases.append(("an idle allowance", {}, [font_allow, idle], "idle"))

    ok = True
    for name, planted, allow, want in cases:
        originals = dict(pick.values())
        for rel, raw in planted.items():
            assert raw != originals.get(rel), "%s: the plant did not change %s" % (name, rel)   # it planted
        td = tree(planted)
        try:
            v = judge(td, rels, allow)
        finally:
            shutil.rmtree(td, ignore_errors=True)
        if want == "stale":
            good = v["stale"] == [(stale[1], stale[0])] and not v["findings"] and not v["idle"]
        elif want == "idle":
            good = v["idle"] == [(idle[1], idle[0])] and not v["findings"] and not v["stale"]
        else:
            good = v["findings"] == sorted(want) and not v["stale"] and not v["idle"]
        ok &= good
        print("  %s %-38s findings %s stale %d idle %d" % ("ok  " if good else "FAIL", name,
                                                           [k for _, k in v["findings"]] or "-", len(v["stale"]),
                                                           len(v["idle"])))
    print("selftest " + ("PASSED" if ok else "FAILED") + " (planted into copies of %s)" % ", ".join(rels))
    return 0 if ok else 1


def main(argv):
    if argv[:1] == ["--selftest"]:
        return selftest()
    root, allow_path = ROOT, ALLOW
    if "--root" in argv:
        root = argv[argv.index("--root") + 1]
    if "--allow" in argv:
        allow_path = argv[argv.index("--allow") + 1]
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
