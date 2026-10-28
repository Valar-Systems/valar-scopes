"""Grade M1 (docs/uk-data.md A2.4) from m1_sizes.py's JSON: the frozen rows, the controls, and the sabotages.

Usage: python -I m1_grade.py <main.json> [<eps0.json> <nodrop.json> <nochain.json>]
Prints one line per row: the reading, then PASS / MISS; a row that cannot be read is BLIND (which counts as FAIL).
"""
import json
import statistics
import sys

main = json.load(open(sys.argv[1]))
sab = [json.load(open(p)) for p in sys.argv[2:5]] if len(sys.argv) >= 5 else None
runs = [r for r in main["runs"] if r["ni_in"]]
busy = ["LHR", "MAN", "BHX"]
others = ["NCL", "EDI", "GLA", "ABZ", "INV", "CWL", "NQY", "BFS", "LDY"]


def get(c, r, v, src=runs):
    m = [x for x in src if x["centre"] == c and x["r_km"] == r and x["variant"] == v and x["ni_in"]]
    return m[0] if len(m) == 1 else None


def row(name, readings, ok, pred):
    if any(x is None for x in readings.values()):
        print(f"{name}: BLIND -- missing {[k for k, v in readings.items() if v is None]}")
        return
    print(f"{name}: {readings} -> {'as predicted' if ok else 'MISS'} (predicted {pred})")


ctl = main["controls"]
print("controls:", ctl)
print("controls: every package validates kOk:", all(r["validate"] == "kOk" for r in main["runs"]),
      f"({sum(r['validate'] == 'kOk' for r in main['runs'])}/{len(main['runs'])})")

a = {f"{c}/{v}": (get(c, 161, v) or {}).get("outcome") for c in busy for v in ("i", "ii")}
row("M1-a", a, all(o == "X" for o in a.values()), "X")
b = {f"{c}/iv": (get(c, 161, "iv") or {}).get("outcome") for c in busy}
row("M1-b", b, all(o in ("C", "D") for o in b.values()), "C or D")
c_ = {f"{c}/iii": (get(c, 161, "iii") or {}).get("outcome") for c in busy}
row("M1-c", c_, all(o in ("D", "X") for o in c_.values()), "D or X")
d = {f"{c}/iv": (get(c, 161, "iv") or {}).get("outcome") for c in others}
row("M1-d", d, all(o in ("F", "C", "D") for o in d.values()), "F, C or D; never X")
e = {f"{c}/iv": (get(c, 222, "iv") or {}).get("outcome") for c in busy + others}
row("M1-e", e, all(o in ("D", "X") for o in e.values()), "D or X")
diffs = [dd for x in runs if x["variant"] == "i" and x["r_km"] in (161, 222) for dd in x["q16_diffs_m"]]
if diffs:
    share = sum(1 for dd in diffs if dd > 50) / len(diffs)
    med = statistics.median(diffs)
    row("M1-f", {"hills": len(diffs), "share_over_50m": round(share, 4), "median_m": med, "max_m": max(diffs)},
        share <= 0.20 and med <= 30, "share <= 0.20 and median <= 30 m")
else:
    print("M1-f: BLIND -- no hills")

print("\nM1-g (printed): centre r variant ni outcome first->final class2_kept doubled dropped collapsed sections")
for x in main["runs"]:
    print(f"  {x['centre']} {x['r_km']} {x['variant']} ni={int(x['ni_in'])} {x['outcome']} {x['bytes_first']}->{x['bytes_final']} "
          f"c2kept={x['class2_km_kept_share']} dbl={int(x['eps_doubled'])} drop={x['dropped']} coll={x['collapsed']} "
          f"sec={x['section_bytes']} n={x['counts']}")
print("pop_stats:", main["pop_stats"])
print("terrain tiles:", main["terrain_tiles"], "sources:", main["terrain_sources"])

if sab:
    e0, nd, nc = sab
    def rb(src, c, v):
        g = get(c, 161, v, [r for r in src["runs"] if r["ni_in"]])
        return None if g is None else int(g["section_bytes"].get("3", 0))
    rise = {c: (rb(main, c, "i"), rb(e0, c, "i")) for c in busy + others}
    print("\nsabotage eps0 (road bytes at 161 km, variant i, normal -> eps 0):", rise,
          "RED (rose everywhere)" if all(b is not None and a is not None and b > a for a, b in rise.values()) else "NOT RED")
    ds = [(x["centre"], x["r_km"], x["variant"]) for x in runs if x["outcome"] == "D" and x["r_km"] == 161]
    after = {f"{c}/{v}": (get(c, r, v, [y for y in nd["runs"] if y["ni_in"]]) or {}).get("outcome") for c, r, v in ds}
    print("sabotage nodrop (every 161 km D):", after, "RED (all X)" if after and all(o == "X" for o in after.values()) else "NOT RED")
    same = {c: (get(c, 161, "iv", [y for y in nc["runs"] if y["ni_in"]]) or {}).get("bytes_first") ==
               (get(c, 161, "ii") or {}).get("bytes_first") for c in busy + others}
    print("sabotage nochain ((iv) == (ii) bytes at 161 km):", same, "RED (identical everywhere)" if all(same.values()) else "NOT RED")
