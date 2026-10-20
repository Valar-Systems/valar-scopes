"""Keep every weather-spike test location >= 150 km from every coordinate in our docs and tests.

    python tools/wx/forbidden.py <repo-root> <lat> <lon> [<lat> <lon> ...]

Scans docs/, test/, proxy/test/, scripts/ and the root *.md for anything that looks like a lat/lon pair
(two decimal numbers within a short span, with at least 2 decimals, lat in [-90, 90], lon in [-180, 180]).
It deliberately over-collects: a false pair only makes the exclusion stricter. Prints ONLY the verdict per
query point (and the count of coordinates scanned) -- NEVER a scanned coordinate, never a distance to one.
Exit 0 if every query point is clear, 1 if any is within 150 km.
"""
import math
import os
import re
import sys

KM = 150.0
PAIR = re.compile(r"(-?\d{1,2}\.\d{2,})[^0-9\-]{1,40}?(-?\d{1,3}\.\d{2,})")


def haversine(a, b):
    la1, lo1, la2, lo2 = map(math.radians, (a[0], a[1], b[0], b[1]))
    h = math.sin((la2 - la1) / 2) ** 2 + math.cos(la1) * math.cos(la2) * math.sin((lo2 - lo1) / 2) ** 2
    return 2 * 6371.0 * math.asin(math.sqrt(min(1.0, h)))


def scan(root):
    pts = set()
    dirs = ["docs", "test", os.path.join("proxy", "test"), "scripts"]
    files = [os.path.join(root, f) for f in os.listdir(root) if f.endswith(".md")]
    for d in dirs:
        for dp, _, fs in os.walk(os.path.join(root, d)):
            files += [os.path.join(dp, f) for f in fs if f.endswith((".md", ".cpp", ".h", ".ts", ".js", ".mjs", ".py", ".sh", ".json", ".txt"))]
    for p in files:
        try:
            t = open(p, encoding="utf-8", errors="replace").read()
        except OSError:
            continue
        for m in PAIR.finditer(t):
            la, lo = float(m.group(1)), float(m.group(2))
            if -90 <= la <= 90 and -180 <= lo <= 180 and abs(la) > 0.5 and abs(lo) > 0.5:
                pts.add((round(la, 4), round(lo, 4)))
    return pts


def selftest(root):
    """CONTROL: a scanned coordinate must come back TOO CLOSE, and a point in the far Southern Ocean clear."""
    pts = sorted(scan(root))
    if not pts:
        print("SELFTEST BLIND: scanned 0 coordinate pairs -- the scan cannot see anything"); return 2
    near = any(haversine(pts[0], p) < KM for p in pts)          # pts[0] is itself in the set
    far = (-65.0, 40.0)
    far_clear = not any(haversine(far, p) < KM for p in pts)
    print(f"selftest: {len(pts)} pairs scanned; a scanned point reads {'TOO CLOSE' if near else 'clear'} (want TOO CLOSE); "
          f"the far point reads {'clear' if far_clear else 'TOO CLOSE'} (want clear)")
    return 0 if near and far_clear else 1


def main(argv):
    if argv[1:2] == ["--selftest"] and len(argv) == 3:
        return selftest(argv[2])
    if len(argv) < 4 or len(argv) % 2:
        print(__doc__)
        return 2
    pts = scan(argv[1])
    qs = [(float(argv[i]), float(argv[i + 1])) for i in range(2, len(argv), 2)]
    print(f"scanned {len(pts)} coordinate pairs in the repo (not printed)")
    bad = 0
    for i, q in enumerate(qs):
        near = any(haversine(q, p) < KM for p in pts)
        print(f"  query {i}: {'TOO CLOSE (< 150 km) -- do not use' if near else 'clear (>= 150 km from all)'}")
        bad += near
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
