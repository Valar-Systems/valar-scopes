# PC-C' (registered at 2891a937): the code PR's pieces with the first cell's first piece scaled by (1 + 1e-5) about its centroid, WRITTEN to
# a file for verify2.py to grade. Prints the altered cell and confirms exactly one piece changed.
#   geo-venv/Scripts/python -I parity/pc-alter.py <pieces.json> <altered.json>
import json, sys
from shapely import affinity
from shapely.geometry import Polygon
cand = json.load(open(sys.argv[1], encoding="utf-8")); orig = json.dumps(cand)
k = sorted(cand)[0]; p = cand[k][0]; g = Polygon(p["outer"], p["holes"]); s = affinity.scale(g, 1 + 1e-5, 1 + 1e-5, origin=g.centroid)
cand[k][0] = {"src": p["src"], "outer": list(map(list, s.exterior.coords)), "holes": [list(map(list, h.coords)) for h in s.interiors]}
changed = sum(1 for c in cand for i, q in enumerate(cand[c]) if json.dumps(q) != json.dumps(json.loads(orig)[c][i]))
json.dump(cand, open(sys.argv[2], "w", encoding="utf-8"))
print(json.dumps({"altered": k, "pieces_changed": changed}))
