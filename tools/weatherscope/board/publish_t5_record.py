"""Builds the COMMITTED record of a t5 board run from its raw (local) run directory, under Q62 (A12.67): no time is
committed. The raw trace and lines.json stay local; only their sha256 goes in. captures.json loses every time field.
serial.txt, the boot reads and the grade go in with the boot's Wi-Fi fields redacted (the LAN address, the BSSID's
tail, and the network's name on the association line, A12.83), then a scan for addresses, network names, ISO times and
coordinates. The graded B1 and B3 snapshots go in as PNGs (the B1 images in both themes go to Daniel, A12.43).
EVERY file is prepared and scanned in memory first; nothing is written unless all of them pass.

    python -I publish_t5_record.py <raw run dir> <record dir> [<boot.txt> ...] [--grade <grade.txt>]
    python -I publish_t5_record.py --selftest      (the association rule's control; prints no name)
"""
import hashlib, json, os, re, struct, sys, zlib

N = 240
TIME_KEYS = {"start", "end", "utc"}
SCAN = [("a LAN address", re.compile(r"\b(192\.168|10\.\d+|172\.(1[6-9]|2\d|3[01]))\.\d+\.\d+\b")),
        ("a BSSID", re.compile(r"\b([0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2}\b")),
        ("a network name", re.compile(r"(?i)\bb?ssid\s*[=:]\s*(?!<redacted>)\S")),
        ("an ISO time", re.compile(r"\d{4}-\d{2}-\d{2}T\d{2}:\d{2}")),
        ("a coordinate-like number", re.compile(r"(?<![\w.])-?\d{1,3}\.\d{4,}(?![\w.])")),
        # A12.83: the firmware names the network it joins as `[WiFi] Associated with "<name>"`, a form the ssid rule
        # above cannot see. Any quoted string, single or double, on a line that mentions association is refused unless
        # it is the placeholder.
        ("a network name on an association line",
         re.compile(r"(?im)^[^\n]*associat[^\n]*?([\"'])(?!<redacted>\1)[^\"'\n]*\1"))]
ASSOC_LINE = re.compile(r"(?i)associat")
QUOTED = re.compile(r"([\"'])(?!<redacted>\1)[^\"'\n]*\1")


def png_bytes(rgb565):
    rows = b""
    for y in range(N):
        rows += b"\x00"
        for x in range(N):
            v = rgb565[2 * (y * N + x)] << 8 | rgb565[2 * (y * N + x) + 1]
            r, g, b = (v >> 11) & 31, (v >> 5) & 63, v & 31
            rows += bytes(((r * 527 + 23) >> 6, (g * 259 + 33) >> 6, (b * 527 + 23) >> 6))

    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d))
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", N, N, 8, 2, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(rows, 9)) + chunk(b"IEND", b""))


def redact(name, text):
    text, n_ip = re.subn(r"(\[WiFi\] CONNECTED\s+IP=)\S+", r"\1<redacted>", text)
    text, n_bssid = re.subn(r"(?i)(\bbssid=)\S+", r"\1<redacted>", text)
    n_name = 0
    lines = text.split("\n")
    for i, line in enumerate(lines):
        if ASSOC_LINE.search(line):
            lines[i], n = QUOTED.subn(lambda m: m.group(1) + "<redacted>" + m.group(1), line)
            n_name += n
    text = "\n".join(lines)
    if n_ip or n_bssid or n_name:
        print(f"{name}: redacted {n_ip} LAN address(es), {n_bssid} BSSID(s) and {n_name} network name(s)")
    return text


def selftest():
    """The association rule's control (A12.83): planted FAKE names only, and nothing printed but rule names and verdicts.
    (1) a double-quoted name on an association line is refused by exactly that rule; (2) single-quoted, the same;
    (3) redact() of (1) passes every rule and the fake name is gone; (4) every text file in this publisher's record
    directories passes every rule."""
    here = os.path.dirname(os.path.abspath(__file__))
    fake = "PlantedNet"
    rule = "a network name on an association line"
    ok = True

    def hits(t):
        return [w for w, rx in SCAN if rx.search(t)]

    for label, line in (("(1) double-quoted", f'  6.39s [WiFi] Associated with "{fake}", waiting for IP...\n'),
                        ("(2) single-quoted", f"  6.39s [WiFi] Associated with '{fake}', waiting for IP...\n")):
        good = hits(line) == [rule]
        ok &= good
        print(f"  {'ok  ' if good else 'FAIL'} {label}: refused by {hits(line)} (want [{rule!r}])")
    red = redact("(3)", f'  6.39s [WiFi] Associated with "{fake}", waiting for IP...\n')
    good = hits(red) == [] and fake not in red and '"<redacted>"' in red
    ok &= good
    print(f"  {'ok  ' if good else 'FAIL'} (3) the redacted form: refused by {hits(red)} (want []); the fake name gone: {fake not in red}")
    n, ok4 = 0, True
    for d in ("run-2026-11-02-t5", "run-2026-11-04-swb"):
        for f in sorted(os.listdir(os.path.join(here, d))):
            if f.endswith(".png"):
                continue
            n += 1
            h = hits(open(os.path.join(here, d, f), encoding="utf-8", errors="replace").read())
            ok4 &= not h
            if h:
                print(f"  FAIL (4) {d}/{f}: refused by {h}")
    ok &= ok4
    print(f"  {'ok  ' if ok4 else 'FAIL'} (4) {n} record file(s) checked")
    print("SELFTEST " + ("PASSED" if ok else "FAILED"))
    return 0 if ok else 1


def main(argv):
    grade = argv[argv.index("--grade") + 1] if "--grade" in argv else None
    args = [a for i, a in enumerate(argv[1:], 1) if a != "--grade" and argv[i - 1] != "--grade"]
    raw, rec, boots = args[0], args[1], args[2:]
    texts, blobs = {}, {}
    caps = json.load(open(os.path.join(raw, "captures.json")))
    for c in caps:
        for f in c.get("fetches", []):
            for k in TIME_KEYS:
                f.pop(k, None)
        if c.get("verdict") == "graded" and c.get("step") in ("B1", "B3"):
            blobs[f"{c['step']}-{c['label']}.png"] = png_bytes(open(os.path.join(raw, c["graded"]), "rb").read())
    texts["captures.json"] = json.dumps(caps, indent=1)
    texts["serial.txt"] = redact("serial.txt", open(os.path.join(raw, "serial.txt"), encoding="utf-8", errors="replace").read())
    for name in ("watch.json", "after-close.json"):
        texts[name] = open(os.path.join(raw, name), encoding="utf-8").read()
    for b in boots:
        texts[os.path.basename(b)] = redact(os.path.basename(b), open(b, encoding="utf-8", errors="replace").read())
    if grade:
        texts["grade.txt"] = open(grade, encoding="utf-8", errors="replace").read()
    texts["SHA256SUMS.local"] = "\n".join(
        f"{hashlib.sha256(open(os.path.join(raw, n), 'rb').read()).hexdigest()}  {n} (kept local: it carries times, Q62)"
        for n in ("trace.txt", "lines.json")) + "\n"
    bad = [(name, what) for name, t in texts.items() if name != "SHA256SUMS.local"
           for what, rx in SCAN if rx.search(t)]
    if bad:
        for name, what in bad:
            print(f"REFUSED: {name} holds {what}")
        print("nothing written")
        return 1
    os.makedirs(rec, exist_ok=True)
    for name, t in texts.items():
        open(os.path.join(rec, name), "w", encoding="utf-8", newline="\n").write(t)
    for name, b in blobs.items():
        open(os.path.join(rec, name), "wb").write(b)
    print(f"published to {rec}: {len(texts)} text file(s), {len(blobs)} PNG(s)")
    return 0


if __name__ == "__main__":
    sys.exit(selftest() if sys.argv[1:] == ["--selftest"] else main(sys.argv))
