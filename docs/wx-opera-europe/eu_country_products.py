"""Which ordered-rule products (eu_rules.py) take each served country's in-domain land, by area share.
Usage: python -I eu_country_products.py <graded.json> <ne.geojson> <eu_rules.json>"""
import json, sys
import numpy as np
from pyproj import Transformer
from shapely.geometry import box, shape
from shapely.ops import unary_union
g = json.load(open(sys.argv[1], encoding="utf-8")); ne = json.load(open(sys.argv[2], encoding="utf-8"))
rules = json.load(open(sys.argv[3], encoding="utf-8"))["rules"]
served = sorted([r["name"] for r in g["ranked"] if r["covered_median"] >= 90] + ["San Marino", "Monaco", "Gibraltar", "Guernsey"])
P, ras = g["raster"]["proj"], g["raster"]
fwd = Transformer.from_crs("EPSG:4326", f"+proj=laea +lat_0={P['lat0']} +lon_0={P['lon0']} +x_0={P['x0']} +y_0={P['y0']} +a={P['a']} +rf={P['invF']} +units=m +no_defs", always_xy=True)
feat = {f["properties"]["ADMIN"]: f for f in ne["features"]}
taken, owner = None, []
for name, a, b, c, d in rules:
    r = box(a, b, c, d); mine = r if taken is None else r.difference(taken); taken = r if taken is None else taken.union(r); owner.append((name, mine))
for n in served:
    geo = shape(feat[n]["geometry"]); keep = []
    for pt in (geo.geoms if geo.geom_type == "MultiPolygon" else [geo]):
        cc = np.asarray(pt.exterior.coords); x, y = fwd.transform(cc[:, 0], cc[:, 1])
        if ((x >= 0) & (x <= ras["width"] * ras["xs"]) & (y <= 0) & (y >= -ras["height"] * ras["ys"])).any(): keep.append(pt)
    land = unary_union(keep); tot = land.area
    share = [(name, land.intersection(m).area / tot) for name, m in owner if land.intersects(m)]
    share = [(nm, s) for nm, s in share if s > 0]
    lx, ly = feat[n]["properties"].get("LABEL_X"), feat[n]["properties"].get("LABEL_Y")
    from shapely.geometry import Point
    lab_in = land.contains(Point(lx, ly)) if lx is not None else None
    lab_owner = next((nm for nm, m in owner if m.contains(Point(lx, ly))), None) if lx is not None else None
    print(f"{n:<24} " + ", ".join(f"{nm} {100*s:.1f}%" for nm, s in sorted(share, key=lambda t: -t[1])) + f" | label point inside: {lab_in}, its product: {lab_owner}")
