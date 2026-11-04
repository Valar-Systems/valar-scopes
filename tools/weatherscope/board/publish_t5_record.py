"""Builds the COMMITTED record of a t5 board run from its raw (local) run directory, under Q62 (A12.67): no time is
committed. The raw trace and lines.json stay local; only their sha256 goes in. captures.json loses every time field.
serial.txt, the boot reads and the grade go in with the boot's Wi-Fi fields redacted (the LAN address and the BSSID's
tail), then a scan for addresses, network names, ISO times and coordinates. The graded B1 and B3 snapshots go in as
PNGs (the B1 images in both themes go to Daniel, A12.43).
EVERY file is prepared and scanned in memory first; nothing is written unless all of them pass.

    python -I publish_t5_record.py <raw run dir> <record dir> [<boot.txt> ...] [--grade <grade.txt>]
"""
import hashlib, json, os, re, struct, sys, zlib

N = 240
TIME_KEYS = {"start", "end", "utc"}
SCAN = [("a LAN address", re.compile(r"\b(192\.168|10\.\d+|172\.(1[6-9]|2\d|3[01]))\.\d+\.\d+\b")),
        ("a BSSID", re.compile(r"\b([0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2}\b")),
        ("a network name", re.compile(r"(?i)\bb?ssid\s*[=:]\s*(?!<redacted>)\S")),
        ("an ISO time", re.compile(r"\d{4}-\d{2}-\d{2}T\d{2}:\d{2}")),
        ("a coordinate-like number", re.compile(r"(?<![\w.])-?\d{1,3}\.\d{4,}(?![\w.])"))]


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
    if n_ip or n_bssid:
        print(f"{name}: redacted {n_ip} LAN address(es) and {n_bssid} BSSID(s)")
    return text


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
    sys.exit(main(sys.argv))
