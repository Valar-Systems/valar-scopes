"""Data beside M1's verdict, not a re-grade: the bytes the ONS parse defect removed from each package's PLACES section.

The defect: m1_sizes.py found the counts column with startswith("Count"), which "Country code" matches first, so every
England and Wales place got population 0 (varint 1 byte). Here the column is found exactly ("Counts"), and for each
centre and radius the missing bytes are sum(len(varint(count)) - 1) over the E&W places inside the disc.
Usage: python -I m1_popbound.py <m1src> <m1work> <repo> <airports.csv> <m1-sizes.json>
"""
import csv
import glob
import json
import math
import os
import pickle
import sys

import openpyxl
from shapely import STRtree
from shapely.geometry import Point, shape as geo_shape

SRC, WORK, REPO, APTS, SIZES = sys.argv[1:6]
sys.path.insert(0, os.path.join(REPO, "tools", "weatherscope", "basemap"))
import bmpk  # noqa: E402

wb = openpyxl.load_workbook(os.path.join(SRC, "ons", "ons-bua-census2021.xlsx"), read_only=True)
ons = {}
for sheet in ("1c", "1d"):
    rows = list(wb[sheet].iter_rows(values_only=True))
    hi = next(i for i, r in enumerate(rows) if r and "BUA code" in [str(c).strip() for c in r if c is not None])
    hdr = [str(c).strip() if c is not None else "" for c in rows[hi]]
    ci, cc = hdr.index("BUA code"), hdr.index("Counts")
    for r in rows[hi + 1:]:
        if r and r[ci] and isinstance(r[cc], (int, float)):
            ons[str(r[ci]).strip()] = int(r[cc])
polys, pops = [], []
for gp in sorted(glob.glob(os.path.join(SRC, "ons-bua", "*.geojson"))):
    for ft in json.load(open(gp, encoding="utf-8"))["features"]:
        polys.append(geo_shape(ft["geometry"]))
        pops.append(ons.get(ft["properties"]["BUA22CD"], 0))
tree = STRtree(polys)
geo = pickle.load(open(os.path.join(WORK, "geo_gb.pkl"), "rb"))
extra = {}
for f in geo["places"]:
    if f["pop"]:
        continue   # Scotland (NRS) was read correctly
    pt = Point(f["x"], f["y"])
    hit = [i for i in tree.query(pt) if polys[i].covers(pt)]
    if hit and pops[min(hit)]:
        extra[f["id"]] = len(bmpk.varint(pops[min(hit)])) - 1
apts = {}
with open(APTS, encoding="utf-8") as fh:
    for row in csv.DictReader(fh):
        if row.get("iata_code") and row["type"] in ("large_airport", "medium_airport", "small_airport"):
            apts.setdefault(row["iata_code"], (float(row["latitude_deg"]), float(row["longitude_deg"])))
sizes = json.load(open(SIZES))
out = {"ons_counts": len(ons), "places_gaining_a_count": len(extra), "per_package": []}
for r in sizes["runs"]:
    lat, lon = apts[r["centre"]]
    lat0, lon0 = math.floor(lat * 100 + 0.5) / 100, math.floor(lon * 100 + 0.5) / 100
    c0 = max(math.cos(lat0 * math.pi / 180), 0.01)
    add = 0
    for f in geo["places"]:
        if f["id"] in extra and math.hypot((f["lon"] - lon0) * 111.0 * c0, (lat0 - f["lat"]) * 111.0) <= r["r_km"]:
            add += extra[f["id"]]
    out["per_package"].append({"centre": r["centre"], "r_km": r["r_km"], "variant": r["variant"], "ni_in": r["ni_in"],
                               "outcome": r["outcome"], "bytes_first": r["bytes_first"], "missing_bytes": add,
                               "first_corrected_bound": r["bytes_first"] + add})
print(json.dumps({k: v for k, v in out.items() if k != "per_package"}))
for p in out["per_package"]:
    flip = ""
    if p["outcome"] == "F" and p["first_corrected_bound"] > 20 * 1024:
        flip = "  <- F may become C"
    if p["outcome"] in ("F", "C") and p["first_corrected_bound"] > 32 * 1024:
        flip = "  <- may cross the cap"
    if p["r_km"] in (161, 222) and p["ni_in"]:
        print(f"{p['centre']} {p['r_km']} {p['variant']} {p['outcome']} first {p['bytes_first']} +{p['missing_bytes']}{flip}")
