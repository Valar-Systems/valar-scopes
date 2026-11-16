# SCRATCH (PARITY step 2, registered at 8c77f0c81665 and 1d8bbde33377): grades the full TS ingest's output (pieces + stats) against the dry
# run. PB-0 the extent (sea-extent-cells.json); PB-1 the source counts; PB-2 the selected lakes by .shp record index, the reference's
# selection recomputed by the dry run's OWN code (its sections 1-2, executed); PA-1..PA-5 by verify.py's compare(), with the reference's
# names mapped latin-1 -> UTF-8 first (the dry run read the UTF-8 DBF as latin-1). Every file is read as UTF-8.
#   geo-venv/Scripts/python -I parity/verify2.py <pieces.json> <stats.json> [--no-name-map]     (C-NAME is --no-name-map)
import json, sys, io, contextlib
here = "parity"
vsrc = open(f"{here}/verify.py", encoding="utf-8").read().split("mode = sys.argv")[0]; V = {"__name__": "v"}; exec(compile(vsrc, "verify-head", "exec"), V)
cand = json.load(open(sys.argv[1], encoding="utf-8")); st = json.load(open(sys.argv[2], encoding="utf-8")); namemap = "--no-name-map" not in sys.argv
res = {}
# PB-0
ref_ext = {tuple(c) for c in json.load(open("sea-extent-cells.json", encoding="utf-8"))}; cand_ext = {tuple(int(x) for x in k.split("/")) for k in st["extent"]}
res["PB-0"] = {"ok": ref_ext == cand_ext, "ref": len(ref_ext), "cand": len(cand_ext), "only_ref": len(ref_ext - cand_ext), "only_cand": len(cand_ext - ref_ext)}
# PB-1
want = {"ocean_outers": 2, "ocean_holes": 6820, "lakes_read": 1355, "lakes_selected": 1168, "lakes_inside_us": 149, "lakes_small": 38}
res["PB-1"] = {"ok": all(st[k] == v for k, v in want.items()), **{k: st[k] for k in want}}
# PB-2: the dry run's own selection, by index in its lakes_all list (its shp() keeps polygon records in file order)
head = open("sea-dryrun.py", encoding="utf-8").read().split("# ---- 3. the cut")[0]; G = {"__name__": "ref"}
with contextlib.redirect_stdout(io.StringIO()): exec(compile(head, "sea-dryrun-head", "exec"), G)
from shapely.ops import unary_union
sel = []
for i, (a, rings) in enumerate(G["lakes_all"]):
    g = unary_union(G["polygonize"](rings))
    if g.is_empty or G["km2"](g) < 1.5 or G["us"].contains(g): continue
    sel.append(i)
res["PB-2"] = {"ok": len(G["lakes_all"]) == 1355 and sorted(sel) == sorted(st["selected_indices"]), "ref_selected": len(sel), "cand_selected": len(st["selected_indices"]),
               "only_ref": len(set(sel) - set(st["selected_indices"])), "only_cand": len(set(st["selected_indices"]) - set(sel))}
# PA-1..5
ref = json.load(open("sea-pieces.json", encoding="utf-8"))
def fix(s):
    try: return s.encode("latin-1").decode("utf-8")
    except (UnicodeEncodeError, UnicodeDecodeError): return s
if namemap: ref = {k: [{**p, "src": fix(p["src"])} for p in v] for k, v in ref.items()}
c = V["compare"](ref, cand)
pa1 = c["totals_cand"] == {"cells_with_sea": 1435, "pieces": 1914, "holes_kept": 497} and st["holes_dropped"] == 135
res["PA"] = {"PA-1": pa1, "fails": c["fails"], "pairs": c["pairs"], "first": c["first"], "recorded": c["recorded"], "name_map": namemap}
ok = res["PB-0"]["ok"] and res["PB-1"]["ok"] and res["PB-2"]["ok"] and pa1 and all(n == 0 for n in c["fails"].values())
print(json.dumps(res)); sys.exit(0 if ok else 1)
