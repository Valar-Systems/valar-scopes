"""The OTA identity heads' printable strings (docs/weatherscope-stage3.md A12.110, Q78/Q79).

    python -I tools/weatherscope/ota/scan_heads.py [<head.bin> ...]     (no arguments: every test/fixtures/ota/heads/*.head.bin)

WHY IT EXISTS. scripts/check_wifi.py excuses a `.bin` it cannot decode from its text scan, so the heads -- the first
352 bytes of real firmware images -- are never read by it. This reads them: it prints every printable run of 4 or more
bytes in each head, and checks each run for a path, an IPv4 address, a network-name context and a 16-hex device id.

THE ONE PATH ALLOWED is GitHub's runner prefix `/home/runner/`. It names no person and no machine, and it is already
public inside v18's published firmware-s3-128.bin, from which the radar's pre-check head is cut (Q79). It is printed
and explained, never silently skipped; any other path is a finding.

ITS CONTROL runs first: a copy of the first head with `C:/Users/someone/` planted in it, in memory, must be flagged as a
forbidden path. If it is not, the scan cannot see what it is looking for: exit 2, BLIND.
Exit 0 every head clean (allowed paths shown), 1 a finding, 2 blind or nothing to scan.
"""
import glob, os, re, sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
HEADS = os.path.join(ROOT, "test", "fixtures", "ota", "heads")
PRINTABLE = re.compile(rb"[\x20-\x7e]{4,}")
PATH = re.compile(r"(^|[^A-Za-z0-9])([A-Za-z]:[\\/]|/(home|Users|usr|opt|tmp|mnt|root|var|runner)/|\\Users\\|~/)")
ALLOWED_PATH = "/home/runner/"
IPV4 = re.compile(r"(?<![\d.])\d{1,3}\.\d{1,3}\.\d{1,3}\.\d{1,3}(?![\d.])")
NETNAME = re.compile(r"ssid|wifi|wi-fi|associat|connected with|network|psk|passw", re.I)
DEVID = re.compile(r"\b[0-9a-f]{16}\b")


def scan(data):
    """(runs, findings, allowed): every printable run, the runs that are findings by kind, the allowed paths."""
    runs = [m.group().decode("ascii") for m in PRINTABLE.finditer(data)]
    findings, allowed = [], []
    for r in runs:
        if PATH.search(r):
            if r.startswith(ALLOWED_PATH):
                allowed.append(r)
            else:
                findings.append(("path", r))
        for kind, rx in (("ip", IPV4), ("network name", NETNAME), ("device id", DEVID)):
            if rx.search(r):
                findings.append((kind, r))
    return runs, findings, allowed


def main(argv):
    paths = argv[1:] or sorted(glob.glob(os.path.join(HEADS, "*.head.bin")))
    if not paths:
        print("BLIND: no head to scan")
        return 2
    planted = open(paths[0], "rb").read() + b"\0C:/Users/someone/.platformio/x\0"
    _, f, _ = scan(planted)
    seen = any(kind == "path" for kind, _ in f)
    print(f"CONTROL: a planted C:/Users/ path is {'flagged' if seen else 'NOT flagged'} (must be flagged)")
    if not seen:
        print("BLIND: the scan cannot see a forbidden path")
        return 2
    bad = 0
    for p in paths:
        data = open(p, "rb").read()
        runs, findings, allowed = scan(data)
        print(f"{os.path.basename(p)} ({len(data)} bytes): {runs}")
        for a in allowed:
            print(f"    allowed path: {a!r} -- GitHub's runner prefix, public in v18's published image (Q79)")
        for kind, r in findings:
            print(f"    FINDING {kind}: {r!r}")
        print(f"    -> {'clean' if not findings else 'FINDINGS'}" + (f" ({len(allowed)} allowed path)" if allowed else ""))
        bad += bool(findings)
    print(f"heads: {len(paths)}, clean {len(paths) - bad}, with findings {bad}")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
