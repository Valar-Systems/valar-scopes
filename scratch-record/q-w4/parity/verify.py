# SCRATCH (PARITY, registered at 381791c4de84 and 25411c25f5cd): the verifier, in the geo-venv (shapely 2.1.2, GEOS 3.13.1).
# Grades a candidate's pieces against a reference by PA-1..PA-5, and records vertex agreement (Hausdorff, vertex counts) without grading it.
#   PA-1 totals (cells with sea, pieces, holes kept; holes dropped and lakes selected where the caller supplies them)
#   PA-2 per cell: the same number of pieces; each candidate piece matched 1:1 to a reference piece of the SAME source by least
#        symmetric-difference area; every piece matched
#   PA-3 per pair: the same number of rings (outer + holes)
#   PA-4 per pair: |A_cand - A_ref| <= max(1e-6 * A_ref, 1e-12 deg2)
#   PA-5 per pair: area(cand symmetric-difference ref) <= max(1e-6 * A_ref, 1e-12 deg2)
# Modes:
#   real <cand.json> [<stats.json>]     the candidate against sea-pieces.json (8ad5d92fa7c7)
#   self                                C-SELF: the reference against itself
#   area                                C-AREA: the reference against a copy with ONE piece scaled by (1 + 1e-5) about its centroid
#   pd                                  the PD-1..PD-12 suite: GEOS clip_by_rect as truth against rectclip-run.mts on the same input
# Every JSON file is opened as UTF-8 (the clip's output is UTF-8; Windows Python's default would read it as cp1252 and garble names).
# Prints counts and the first failures only. Exit 0 = every graded row holds; 1 = a graded row fails; 2 = the instrument could not run.
import json, math, os, subprocess, sys
import shapely
from shapely.geometry import Polygon
from shapely import affinity
REF = "sea-pieces.json"; TOL, FLOOR = 1e-6, 1e-12

def poly(p):
    g = Polygon(p["outer"], p["holes"])
    return g if g.is_valid else shapely.make_valid(g)

def compare(ref, cand):
    keys = sorted(set(ref) | set(cand)); f = {"PA-2": [], "PA-3": [], "PA-4": [], "PA-5": []}; rec = {"hausdorff_max": 0.0, "vcount_diff_pairs": 0}
    pairs = 0
    for k in keys:
        R, C = ref.get(k, []), cand.get(k, [])
        if len(R) != len(C): f["PA-2"].append((k, f"pieces {len(C)} vs ref {len(R)}"))
        Rg, Cg = [poly(p) for p in R], [poly(p) for p in C]
        cost = sorted(((shapely.symmetric_difference(Cg[i], Rg[j]).area, i, j) for i in range(len(C)) for j in range(len(R)) if C[i]["src"] == R[j]["src"]))
        ui, uj, match = set(), set(), []
        for c, i, j in cost:
            if i in ui or j in uj: continue
            ui.add(i); uj.add(j); match.append((i, j, c))
        if len(match) != max(len(R), len(C)) and not any(e[0] == k for e in f["PA-2"]): f["PA-2"].append((k, f"{len(match)} matched of {len(R)} ref / {len(C)} cand"))
        for i, j, sd in match:
            pairs += 1; a_r, a_c = Rg[j].area, Cg[i].area; tol = max(TOL * a_r, FLOOR)
            if 1 + len(C[i]["holes"]) != 1 + len(R[j]["holes"]): f["PA-3"].append((k, f"rings {1 + len(C[i]['holes'])} vs {1 + len(R[j]['holes'])}"))
            if abs(a_c - a_r) > tol: f["PA-4"].append((k, f"area {a_c:.12g} vs {a_r:.12g}"))
            if sd > tol: f["PA-5"].append((k, f"symdiff {sd:.3g} > {tol:.3g}"))
            rec["hausdorff_max"] = max(rec["hausdorff_max"], shapely.hausdorff_distance(Cg[i].boundary, Rg[j].boundary))
            nv = lambda p: len(p["outer"]) + sum(len(h) for h in p["holes"])
            rec["vcount_diff_pairs"] += nv(C[i]) != nv(R[j])
    totals = lambda D: {"cells_with_sea": len(D), "pieces": sum(len(v) for v in D.values()), "holes_kept": sum(len(p["holes"]) for v in D.values() for p in v)}
    return {"totals_cand": totals(cand), "totals_ref": totals(ref), "pairs": pairs, "fails": {k: len(v) for k, v in f.items()}, "first": {k: v[:3] for k, v in f.items() if v}, "recorded": rec}

def done(res, extra_ok=True):
    print(json.dumps(res)); sys.exit(0 if (all(n == 0 for n in res["fails"].values()) and extra_ok) else 1)

mode = sys.argv[1] if len(sys.argv) > 1 else ""
if mode in ("real", "self", "area"):
    ref = json.load(open(REF, encoding="utf-8"))
    if mode == "self": done(compare(ref, ref))
    if mode == "area":
        cand = json.loads(json.dumps(ref)); k = sorted(cand)[0]; p = cand[k][0]; g = Polygon(p["outer"], p["holes"]); c = g.centroid
        s = affinity.scale(g, 1 + 1e-5, 1 + 1e-5, origin=c)
        cand[k][0] = {"src": p["src"], "outer": list(map(list, s.exterior.coords)), "holes": [list(map(list, h.coords)) for h in s.interiors]}
        r = compare(ref, cand); r["altered"] = k; done(r)
    cand = json.load(open(sys.argv[2], encoding="utf-8")); r = compare(ref, cand)
    if len(sys.argv) > 3:  # PA-1 also checks the run's own dropped count
        st = json.load(open(sys.argv[3], encoding="utf-8")); r["holes_dropped_cand"] = st.get("holes_dropped")
        pa1 = (r["totals_cand"] == {"cells_with_sea": 1435, "pieces": 1914, "holes_kept": 497} and st.get("holes_dropped") == 135)
        r["PA-1"] = pa1; done(r, pa1)
    done(r)
if mode == "pd":
    B = lambda x0, y0, x1, y1: [[x0, y0], [x1, y0], [x1, y1], [x0, y1], [x0, y0]]
    CASES = {
        "PD-1": ([[0.2, 0.2], [1, 0.5], [0.2, 0.8], [0.2, 0.2]], []), "PD-2": ([[1, 0.5], [1.5, 0.2], [1.5, 0.8], [1, 0.5]], []),
        "PD-3": (B(0.5, 0.3, 1, 0.7), []), "PD-4": (B(1, 0.3, 1.5, 0.7), []),
        "PD-5": ([[0.5, 0.3], [1, 0.3], [1, 0.5], [1.5, 0.5], [1.5, 0.7], [0.5, 0.7], [0.5, 0.3]], []),
        "PD-6": (B(1, 1, 1.5, 1.5), []), "PD-7": (B(0.5, 0.5, 1, 1), []), "PD-8": (B(0, 0, 1, 1), []),
        "PD-9": (B(-1, -1, 2, 2), [B(0.6, 0.4, 1, 0.6)]), "PD-10": (B(-1, -1, 2, 2), [[[0.6, 0.4], [1, 0.5], [0.6, 0.6], [0.6, 0.4]]]),
        "PD-11": (B(-1, -1, 2, 2), [B(0.5, 0.3, 1.5, 0.7)]), "PD-12": (B(-2, -2, 3, 3), [B(-1, -1, 2, 2)]),
    }
    EXPECT = {"PD-1": (1, 1), "PD-2": (0, 0), "PD-3": (1, 1), "PD-4": (0, 0), "PD-5": (1, 1), "PD-6": (0, 0), "PD-7": (1, 1), "PD-8": (1, 1),
              "PD-9": (1, 1), "PD-10": (1, None), "PD-11": (1, 1), "PD-12": (0, 0)}
    allok, rows = True, {}
    here = os.path.dirname(os.path.abspath(__file__)); proxy = os.path.join(here, "..", "wt-main", "proxy")
    for name, (outer, holes) in CASES.items():
        g = Polygon(outer, holes); truth = [q for q in shapely.get_parts(shapely.clip_by_rect(g, 0, 0, 1, 1)) if q.geom_type == "Polygon" and not q.is_empty and q.area > 0]
        ref = {"0/0": [{"src": "t", "outer": list(map(list, q.exterior.coords)), "holes": [list(map(list, h.coords)) for h in q.interiors]} for q in truth]} if truth else {}
        inp, outp = os.path.join(here, f"pd-{name}.in.json"), os.path.join(here, f"pd-{name}.out.json")
        json.dump({"cells": [[0, 0]], "sources": [{"src": "t", "outer": outer, "holes": holes}]}, open(inp, "w"))
        p = subprocess.run(["node", "--import", "tsx", os.path.join(here, "rectclip-run.mts"), inp, outp, "--min-island", "off"], cwd=proxy, capture_output=True, text=True)
        if p.returncode != 0: print(json.dumps({"case": name, "STOP": "rectclip-run failed", "stderr": p.stderr[-300:]})); sys.exit(2)
        cand = json.load(open(outp, encoding="utf-8")); r = compare(ref, cand); ok = all(n == 0 for n in r["fails"].values()); allok &= ok
        geos = (len(truth), (1 + len(list(truth[0].interiors))) if truth else 0); mine = EXPECT[name]
        rows[name] = {"graded_PA2-5": "PASS" if ok else "FAIL", "geos_pieces_rings": geos, "my_reading": mine, "reading_agrees": mine[0] == geos[0] and (mine[1] is None or mine[1] == geos[1]), "fails": r["fails"]}
    for k, v in rows.items(): print(json.dumps({"case": k, **v}))
    print(json.dumps({"PD": "ALL PASS" if allok else "FAIL"})); sys.exit(0 if allok else 1)
print("usage: verify.py real <cand.json> [<stats.json>] | self | area | pd"); sys.exit(2)
