# PC-C (registered at 894437c6): the code PR's OWN output with its first cell's first piece scaled by (1 + 1e-5) about its centroid,
# graded by verify.py's compare() against the reference. Prints the compare() result as JSON.
#   geo-venv/Scripts/python -I parity/pc-control.py <code-pr pieces.json>
import json, sys
from shapely import affinity
from shapely.geometry import Polygon
here = "parity"
vsrc = open(f"{here}/verify.py", encoding="utf-8").read().split("mode = sys.argv")[0]; V = {"__name__": "v"}; exec(compile(vsrc, "verify-head", "exec"), V)
ref = json.load(open(V["REF"], encoding="utf-8"))
cand = json.load(open(sys.argv[1], encoding="utf-8"))
k = sorted(cand)[0]; p = cand[k][0]; g = Polygon(p["outer"], p["holes"]); s = affinity.scale(g, 1 + 1e-5, 1 + 1e-5, origin=g.centroid)
cand[k][0] = {"src": p["src"], "outer": list(map(list, s.exterior.coords)), "holes": [list(map(list, h.coords)) for h in s.interiors]}
r = V["compare"](ref, cand); r["altered"] = k
print(json.dumps(r))
