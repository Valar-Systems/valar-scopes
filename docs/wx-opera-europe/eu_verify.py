"""Verify the plan's unmeasured claims (read-only). Usage: python -I eu_verify.py <graded.json> <ne.geojson> <eu_rules.json>"""
import json, sys
from shapely.geometry import box, shape, Point
from shapely.ops import unary_union
g = json.load(open(sys.argv[1], encoding="utf-8")); ne = json.load(open(sys.argv[2], encoding="utf-8"))
er = json.load(open(sys.argv[3], encoding="utf-8"))
feat = {f["properties"]["ADMIN"]: f for f in ne["features"]}
excl = [r["name"] for r in g["ranked"] if r["covered_median"] < 90] + ["Morocco", "Russia", "Turkey", "Tunisia", "Algeria", "Greenland"]
ukbox = box(-8.7, 49.8, 1.8, 60.9)
hits = [n for n in feat if shape(feat[n]["geometry"]).intersects(ukbox)]
print("countries meeting the UK box:", sorted(hits))
for n in hits:
    print("  ", n, [round(v, 2) for v in shape(feat[n]["geometry"]).intersection(ukbox).bounds])
print("excluded meeting the UK box:", [n for n in excl if n in hits])
grids = [(p["product"], p["la1"], p["lo1"], p["ni"], p["nj"]) for p in er["products"]]
def in_grid(lat, lon):
    return [nm for nm, la1, lo1, ni, nj in grids if la1 - (nj - 1) * 0.01 <= lat <= la1 and lo1 <= lon <= lo1 + (ni - 1) * 0.01]
for n in excl:
    pr = feat[n]["properties"]; lat, lon = round(pr["LABEL_Y"], 2), round(pr["LABEL_X"], 2)
    print(f"label {n:<16} inside own polygon: {shape(feat[n]['geometry']).contains(Point(pr['LABEL_X'], pr['LABEL_Y']))}; in product grids: {in_grid(lat, lon)}")
s = sum((p["cells"] / (1455 * 1515) + p["window"] / 2224675) / 2 for p in er["products"])
print("model sum (unrounded):", round(s, 4), "-> ms a set", round(s * 1612.5, 1))
for p in er["products"]:
    print("  ", p["product"], round((p["cells"] / (1455 * 1515) + p["window"] / 2224675) / 2 * 1612.5, 1))
