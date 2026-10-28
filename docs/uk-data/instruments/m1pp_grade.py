"""Grade M1'' (docs/uk-data.md A3.6). Usage: python -I m1pp_grade.py <m1pp.json> <m1pp-sab.json> <m1p-sizes.json>"""
import json
import sys

m, sab, m1p = (json.load(open(p)) for p in sys.argv[1:4])
C = ["LHR", "MAN", "BHX"]


def get(src, c, v="iii", n=256):
    r = [x for x in src["runs"] if x["centre"] == c and x["r_km"] == 161 and x["variant"] == v and x.get("N") == n and not x["ni_in"]]
    return r[0] if len(r) == 1 else None


def row(name, readings, ok, pred):
    if any(v is None for v in readings.values()):
        print(f"{name}: BLIND -- missing {[k for k, v in readings.items() if v is None]}")
    else:
        print(f"{name}: {readings} -> {'as predicted' if ok else 'MISS'} (predicted {pred})")


print("controls:", json.dumps({k: m["controls"][k] for k in ("drop_order_synthetic", "synth_check")}))
print("control order property on every package:", all(r.get("order_property_ok") for r in m["runs"]), f"({len(m['runs'])})")
print("every package kOk:", all(r["validate"] == "kOk" for r in m["runs"]))
a = {c: (get(m, c) or {}).get("outcome") for c in C}
prev = {c: (get(m1p, c) or {}).get("outcome") for c in C}
row("M1''-a", {**a, "M1' was": prev}, all(a[c] == "D" and a[c] == prev[c] for c in C), "D, D, D, unchanged")
b = {c: (get(m, c) or {}).get("nonprimary_km_kept") for c in C}
row("M1''-b", b, all(v == 0 for v in b.values()), "0 km at all three")
share = {}
for c in C:
    g = get(m, c)
    share[c] = None if g is None or not g["primary_km_in_square"] else round(g["primary_km_kept"] / g["primary_km_in_square"], 4)
row("M1''-c", share, all(v is not None and v >= 0.60 for v in share.values()), ">= 0.60 at each")
for c in C:
    g = get(m, c)
    print(f"  M1''-d {c}: primary kept {g['primary_km_kept']} of {g['primary_km_in_square']} km; non-primary kept "
          f"{g['nonprimary_km_kept']} of {g['nonprimary_km_in_square']} km; mixed chains {g['mixed_chains']} ({g['mixed_km']} km); "
          f"class-2 kept {g['class2_km_kept']}; bytes {g['bytes_first']}->{g['bytes_final']}; drops {g['dropped']}")
eq = {c: ((get(sab, c) or {}).get("bytes_final"), (get(m1p, c) or {}).get("bytes_final"),
          (get(sab, c) or {}).get("class2_km_kept"), (get(m1p, c) or {}).get("class2_km_kept")) for c in C}
red = all(x[0] == x[1] and x[2] == x[3] and None not in x for x in eq.values())
print("sabotage, drop key off (bytes final, M1' bytes final, class-2 km kept, M1' km kept):", eq,
      "RED (reproduces M1' exactly)" if red else "NOT RED")
