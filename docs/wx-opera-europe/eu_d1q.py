"""D1's premise, quantised: each of the 40 label points, rounded to 2 dp as the device sends it, against the served
region and the ordered rules. Usage: python -I eu_d1q.py <graded.json> <ne.geojson> <eu_rules.json>"""
import json, sys
import numpy as np
from pyproj import Transformer
from shapely.geometry import box, shape, Point
from shapely.ops import unary_union
g = json.load(open(sys.argv[1], encoding="utf-8")); ne = json.load(open(sys.argv[2], encoding="utf-8"))
rules = json.load(open(sys.argv[3], encoding="utf-8"))["rules"]
served = sorted([r["name"] for r in g["ranked"] if r["covered_median"] >= 90] + ["San Marino", "Monaco", "Gibraltar", "Guernsey"])
P, ras = g["raster"]["proj"], g["raster"]
fwd = Transformer.from_crs("EPSG:4326", f"+proj=laea +lat_0={P['lat0']} +lon_0={P['lon0']} +x_0={P['x0']} +y_0={P['y0']} +a={P['a']} +rf={P['invF']} +units=m +no_defs", always_xy=True)
feat = {f["properties"]["ADMIN"]: f for f in ne["features"]}
parts = []
for n in served:
    geo = shape(feat[n]["geometry"])
    for pt in (geo.geoms if geo.geom_type == "MultiPolygon" else [geo]):
        c = np.asarray(pt.exterior.coords); x, y = fwd.transform(c[:, 0], c[:, 1])
        if ((x >= 0) & (x <= ras["width"] * ras["xs"]) & (y <= 0) & (y >= -ras["height"] * ras["ys"])).any(): parts.append(pt)
region = unary_union(parts + [box(-8.7, 49.8, 1.8, 60.9)])
bad = 0
for n in served:
    pr = feat[n]["properties"]; q = Point(round(pr["LABEL_X"], 2), round(pr["LABEL_Y"], 2))
    own = shape(feat[n]["geometry"]).contains(q); inreg = region.contains(q)
    prod = next((nm for nm, a, b, c, d in rules if box(a, b, c, d).contains(q)), None) if inreg else None
    if not (own and inreg): bad += 1
    print(f"{n:<24} quantised inside own country: {own}; inside region: {inreg}; product: {prod}")
print("not inside own country and region:", bad)
