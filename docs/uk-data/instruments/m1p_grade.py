"""Grade M1′ (docs/uk-data.md A3.3): the controls, M1′-a..e, Q12's outcome, and the four sabotages.

Usage: python -I m1p_grade.py <m1p-sizes.json> <m1-sizes.json> [<eps0.json> <nodrop.json> <norchain.json> <nall.json>]
A row whose inputs are missing prints BLIND (which counts as FAIL).
"""
import json
import sys

main = json.load(open(sys.argv[1]))
m1 = json.load(open(sys.argv[2]))
sab = [json.load(open(p)) for p in sys.argv[3:7]] if len(sys.argv) >= 7 else None
busy = ["LHR", "MAN", "BHX"]
others = ["NCL", "EDI", "GLA", "ABZ", "INV", "CWL", "NQY", "BFS", "LDY"]


def get(src, c, r, v, n, ni=False):
    m = [x for x in src["runs"] if x["centre"] == c and x["r_km"] == r and x["variant"] == v and x.get("N") == n
         and x["ni_in"] == ni]
    return m[0] if len(m) == 1 else None


def row(name, readings, ok, pred):
    if any(v is None for v in readings.values()):
        print(f"{name}: BLIND -- missing {[k for k, v in readings.items() if v is None]}")
    else:
        print(f"{name}: {readings} -> {'as predicted' if ok else 'MISS'} (predicted {pred})")


ctl = main["controls"]
print("controls:", json.dumps(ctl))
print("controls: every package kOk:", all(r["validate"] == "kOk" for r in main["runs"]), f"({len(main['runs'])} packages)")
print("rule L applied:", main["pop_stats"]["rule_l"], "| pop_from:", main["pop_stats"]["pop_from"])

for v in ("iv", "iii"):
    xs = {n: [c for c in busy + others if (get(main, c, 161, v, n) or {}).get("outcome") == "X"] for n in (64, 128, 256)}
    print(f"X at 161 km, variant ({v}), by N:", xs)
xs_iv = {n: [c for c in busy + others if (get(main, c, 161, "iv", n) or {}).get("outcome") == "X"] for n in (64, 128, 256)}
ok_ns = [n for n in (64, 128, 256) if not xs_iv[n]]
row("M1'-a", {f"N={n}": len(xs_iv[n]) for n in xs_iv}, all(len(x) == 0 for x in xs_iv.values()), "no centre X for any N")
print("  the rule's N (largest with no centre X at 161 km, (iv)):", max(ok_ns) if ok_ns else "none -> (c) stop")
row("M1'-b", {"BHX/161/iv/256": (get(main, "BHX", 161, "iv", 256) or {}).get("outcome")},
    (get(main, "BHX", 161, "iv", 256) or {}).get("outcome") == "D", "D")
cc = {f"{c}/161/iii/256": (get(main, c, 161, "iii", 256) or {}).get("outcome") for c in busy}
row("M1'-c", cc, all(o == "D" for o in cc.values()), "D, all three")
dd = {f"{c}/222/iv/256": (get(main, c, 222, "iv", 256) or {}).get("outcome") for c in busy}
row("M1'-d", dd, all(o == "D" for o in dd.values()), "D")
ee = {f"{c}/161/iv/256": (get(main, c, 161, "iv", 256) or {}).get("outcome") for c in others}
row("M1'-e", ee, all(o in ("F", "C", "D") for o in ee.values()), "F, C or D; never X")

fit = lambda o: o in ("F", "C")  # noqa: E731
iii_fit = all(fit((get(main, c, 161, "iii", 256) or {}).get("outcome")) for c in busy)
iv_fit = all(fit((get(main, c, 161, "iv", 256) or {}).get("outcome")) for c in busy)
q12 = "(d) every A road" if iii_fit else ("(e) primary A roads" if iv_fit else "(f) neither fits without drops: for a ruling")
print("Q12:", q12)
print("Q12 kept A-road lengths at 161 km, N=256 (km kept / km in the square):")
for c in busy + others:
    a, b = get(main, c, 161, "iii", 256), get(main, c, 161, "iv", 256)
    if a and b:
        print(f"  {c}: (iii) every A road {a['class2_km_kept']}/{a['class2_km_in_square']} [{a['outcome']}]   "
              f"(iv) primary {b['class2_km_kept']}/{b['class2_km_in_square']} [{b['outcome']}]")

print("\nM1'-f (printed): centre r v N ni outcome first->final drops sections_first -> sections_final")
for x in main["runs"]:
    print(f"  {x['centre']} {x['r_km']} {x['variant']} N={x['N']} ni={int(x['ni_in'])} {x['outcome']} "
          f"{x['bytes_first']}->{x['bytes_final']} drop={x['dropped']} {x['section_bytes_first']} -> {x['section_bytes_final']}")

if sab:
    e0, nd, nr, na = sab
    rise = {c: ((get(main, c, 161, "iv", 256) or {}).get("section_bytes_first", {}).get("3"),
                (get(e0, c, 161, "iv", 256) or {}).get("section_bytes_first", {}).get("3")) for c in busy + others}
    red = all(a is not None and b is not None and b > a for a, b in rise.values())
    print("\nsabotage eps0, pre-budget road bytes (normal, eps 0):", rise, "RED" if red else "NOT RED")
    ds = [c for c in busy + others if (get(main, c, 161, "iv", 256) or {}).get("outcome") == "D"]
    after = {c: (get(nd, c, 161, "iv", 256) or {}).get("outcome") for c in ds}
    print("sabotage nodrop, every 161 km (iv) N=256 D:", after, "RED" if after and all(o == "X" for o in after.values()) else "NOT RED")
    m1fc = [c for c in busy + others if any(x["centre"] == c and x["r_km"] == 161 and x["variant"] == "iv" and x["ni_in"]
                                              and x["outcome"] in ("F", "C") for x in m1["runs"])]
    cmp = {}
    for c in m1fc:
        m1r = [x for x in m1["runs"] if x["centre"] == c and x["r_km"] == 161 and x["variant"] == "iv" and x["ni_in"]][0]
        cmp[c] = (m1r["section_bytes"]["2"], (get(nr, c, 161, "iv", 256) or {}).get("section_bytes_first", {}).get("2"),
                  (get(main, c, 161, "iv", 256) or {}).get("section_bytes_first", {}).get("2"))
    red = bool(cmp) and all(a == b and c_ < a for a, b, c_ in cmp.values())
    print("sabotage norchain, river bytes (M1 unchained, M1' chaining off, M1' chained) where M1 was F/C:", cmp,
          "RED" if red else "NOT RED")
    pl = {c: ((get(main, c, 161, "iv", 256) or {}).get("section_bytes_first", {}).get("5"),
              (get(na, c, 161, "iv", None) or {}).get("section_bytes_first", {}).get("5"),
              (get(na, c, 161, "iv", None) or {}).get("counts", {}).get("places")) for c in busy + others}
    red = all(a is not None and b is not None and (b > a if n > 256 else b == a) for a, b, n in pl.values())
    print("sabotage nall, places bytes (N=256, all, count of all):", pl, "RED" if red else "NOT RED")
