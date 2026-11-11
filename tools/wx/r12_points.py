"""R1 and R2 on production (docs/wx-opera-r12-prod.md): the points to ask for, and R2 recomputed from the table.

Decodes proxy/src/wx/region.generated.ts in Python, a second implementation of region.ts's decoder. It counts
opuk's lattice points outside today's UK land box (R2: 48,721 as frozen) by area, and writes a TSV of request points
(label, lat, lon, expected product) to <out.tsv>. **It never prints a coordinate:** only labels, counts and areas.

  python -I r12_points.py <region.generated.ts> <region-points.json> <out.tsv>
"""
import base64
import json
import math
import re
import struct
import sys

src, fixture, out = sys.argv[1], sys.argv[2], sys.argv[3]
text = open(src, encoding="utf-8").read()
header = json.loads(re.search(r"export const REGION_HEADER = (\{.*?\n\});", text, re.S).group(1))


def b64_of(name):
    body = re.search(r"export const %s: string = \[(.*?)\]" % name, text, re.S).group(1)
    return base64.b64decode("".join(re.findall(r'"([A-Za-z0-9+/=]*)"', body)))


ri, ru = b64_of("REGION_ROW_INDEX"), b64_of("REGION_RUNS")
rows, nruns = header["rows"], header["runs"]
if len(ri) != (rows + 1) * 4 or len(ru) != nruns * 6:
    raise SystemExit("RIG: the table's sizes disagree with its header")
row_index = struct.unpack("<%dI" % (rows + 1), ri)
runs = struct.unpack("<%dH" % (nruns * 3), ru)
ids = [r["id"] for r in header["rules"]]
k = ids.index("opuk")
box = header["ukBox"]
bS, bN, bW, bE = (round(box[x] * 100) for x in ("latS", "latN", "lonW", "lonE"))


def area(r, c):
    """r, c in hundredths. The three areas the record names (docs/wx-opera-code-stage.md §7.2)."""
    if c < bW:
        return "western Ireland"
    if r < bS and -300 <= c <= -200:
        return "the Channel Islands"
    if r < bS and -200 < c <= bE:
        return "France's coast"
    return "other"


switched = {}   # area -> list of (r, c), in the table's scan order (north to south, west to east)
total_points = 0
for i in range(rows):
    r = header["row0"] - i
    for j in range(row_index[i], row_index[i + 1]):
        a, b, rule = runs[j * 3], runs[j * 3 + 1], runs[j * 3 + 2]
        total_points += b - a + 1
        if rule != k:
            continue
        for cc in range(a + header["col0"], b + header["col0"] + 1):
            if not (bS <= r <= bN and bW <= cc <= bE):
                switched.setdefault(area(r, cc), []).append((r, cc))

n = sum(len(v) for v in switched.values())
print(json.dumps({"table_points": total_points, "header_points": header["points"], "R2": n,
                  "by_area": {a: len(v) for a, v in sorted(switched.items())}}))

def round2(v):
    """The device's Round2, as wx-region.test.ts's deviceQuery: half away from zero (JS Math.round on |v|)."""
    m = math.floor(abs(v) * 100 + 0.5) / 100
    return -m if v < 0 else m


lines = []
for e in json.load(open(fixture, encoding="utf-8"))["d1"]:
    lines.append((e["name"], round2(e["lat"]), round2(e["lon"]), "opuk" if e["product"] == "opuk" else "cref"))
for a in ("western Ireland", "the Channel Islands", "France's coast"):
    pts = switched.get(a, [])
    if not pts:
        continue
    for tag, (r, c) in (("first", pts[0]), ("middle", pts[len(pts) // 2]), ("last", pts[-1])):
        lines.append(("R2 %s, %s" % (a, tag), r / 100, c / 100, "opuk"))
with open(out, "w", encoding="utf-8", newline="\n") as f:
    for label, lat, lon, want in lines:
        f.write("%s\t%.2f\t%.2f\t%s\n" % (label, lat, lon, want))
print(json.dumps({"points_written": len(lines)}))
