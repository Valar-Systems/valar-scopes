"""M1′ (docs/uk-data.md A3.3): Open Rivers re-extracted WITH its node ids, for chaining by name (A3.1).

The filter and the name rule are M1's extract's, unchanged: form inlandRiver or tidalRiver, named; the English name
where there are two (name2, else name1). Decoded by the DBF's own language-driver byte. Source order as RD2.
Usage: python -I m1p_extract_rivers.py <m1src> <m1work>     Writes <m1work>/rivers_nodes.pkl and prints its counts.
"""
import glob
import json
import os
import pickle
import sys

import shapefile

src, work = sys.argv[1], sys.argv[2]
p = sorted(glob.glob(os.path.join(src, "os-rivers", "**", "WatercourseLink.shp"), recursive=True))
assert len(p) == 1, p
p = p[0]
if open(p[:-4] + ".dbf", "rb").read(32)[29] != 0x03:
    raise SystemExit("unexpected DBF language driver")
r = shapefile.Reader(p, encoding="cp1252")
names = [f[0] for f in r.fields[1:]]
rel = os.path.relpath(p, src).replace("\\", "/")
out, seen, no_nodes = [], 0, 0
for ri, sr in enumerate(r.iterShapeRecords()):
    seen += 1
    a = dict(zip(names, sr.record))
    n1, n2 = (a["name1"] or "").strip(), (a["name2"] or "").strip()
    if a["form"] not in ("inlandRiver", "tidalRiver") or not (n1 or n2):
        continue
    if not (a["startNode"] and a["endNode"]):
        no_nodes += 1
    pts = sr.shape.points
    parts = list(sr.shape.parts) + [len(pts)]
    for pi in range(len(parts) - 1):
        out.append({"key": (rel, ri, pi), "name": n2 or n1, "a": a["startNode"] or None, "b": a["endNode"] or None,
                    "pts": [tuple(x) for x in pts[parts[pi]:parts[pi + 1]]]})
pickle.dump(out, open(os.path.join(work, "rivers_nodes.pkl"), "wb"), protocol=4)
print(json.dumps({"links_read": seen, "kept_polylines": len(out), "kept_without_nodes": no_nodes}))
