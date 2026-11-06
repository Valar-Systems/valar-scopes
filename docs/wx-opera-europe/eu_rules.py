"""Radar beyond the UK, plan sizing (read-only, offline): the proposed products as an ORDERED rule list.

A served point (in the union of the 40 countries' in-domain Natural Earth parts, or in today's UK land box) is
answered by the FIRST rule whose rectangle holds it. A product's grid is the box of the served land its rule takes,
grown by 2.02 deg, at 0.01 deg (as OPUK_GRID). Checks: the rules take every served point (nothing left over), and
each product's estimated build memory stays within 1.5 x opuk's (estimate: fill Int32 per cell + cellOf Int32 and
value/quality Float32 per composite window pixel + the WXG1 frame; opuk's real peak was 54 MB in K11 against this
estimate's 35 MB, so the cap is on the estimate, not on bytes).

Usage: python -I eu_rules.py <graded.json> <ne.geojson> <out.json>
"""
import hashlib
import json
import math
import sys

import numpy as np
from pyproj import Transformer
from shapely.geometry import box, shape
from shapely.ops import unary_union

GRADED_SHA = "0b0d43ac4363093b840655aaa763a57b78946b856f13bc1c60156b6374c1a6bd"
NE_SHA = "239eec57ac17f100a11e2536cffc56752c318b50ae765b0918ff7aab4ce8f255"
TINY = ["San Marino", "Monaco", "Gibraltar", "Guernsey"]
TILE, DEG, MARGIN = 512, 0.01, 2.02
UK_BOX = box(-8.7, 49.8, 1.8, 60.9)                 # OPUK_LAND, kept as served (no UK point loses opuk)
# (product, lonW, latS, lonE, latN), first match wins
RULES = [
    ("isles",     -15.0,  49.1,   1.8,  61.5),
    ("iceland",   -25.0,  62.0,  -7.0,  72.0),
    ("iberia",    -10.0,  35.1,   4.5,  43.8),
    ("malta",      14.0,  35.7,  15.0,  36.2),
    ("west",       -6.0,  41.3,  10.5,  53.7),
    ("central",    10.5,  47.0,  24.2,  55.1),
    ("southeast",  10.5,  41.8,  30.5,  48.6),
    ("baltic",     16.0,  53.5,  32.0,  63.0),
    ("scandisw",    4.0,  53.5,  16.0,  63.0),
    ("scandin",     4.0,  63.0,  32.0,  71.3),
]

gp, nep, outp = sys.argv[1:4]
assert hashlib.sha256(open(gp, "rb").read()).hexdigest() == GRADED_SHA
assert hashlib.sha256(open(nep, "rb").read()).hexdigest() == NE_SHA
g = json.load(open(gp, encoding="utf-8"))
ne = json.load(open(nep, encoding="utf-8"))
served = sorted([r["name"] for r in g["ranked"] if r["covered_median"] >= 90] + TINY)
P, ras = g["raster"]["proj"], g["raster"]
fwd = Transformer.from_crs("EPSG:4326", f"+proj=laea +lat_0={P['lat0']} +lon_0={P['lon0']} +x_0={P['x0']} "
                           f"+y_0={P['y0']} +a={P['a']} +rf={P['invF']} +units=m +no_defs", always_xy=True)
W, H = ras["width"], ras["height"]
feat = {f["properties"]["ADMIN"]: f for f in ne["features"]}
parts = []
for n in served:
    geo = shape(feat[n]["geometry"])
    for pt in (geo.geoms if geo.geom_type == "MultiPolygon" else [geo]):
        c = np.asarray(pt.exterior.coords)
        x, y = fwd.transform(c[:, 0], c[:, 1])
        if ((x >= 0) & (x <= W * ras["xs"]) & (y <= 0) & (y >= -H * ras["ys"])).any():
            parts.append(pt)
region = unary_union(parts + [UK_BOX])


def window(la1, lo1, ni, nj):
    north, south = la1 + DEG / 2, la1 - (nj - 1) * DEG - DEG / 2
    west, east = lo1 - DEG / 2, lo1 + (ni - 1) * DEG + DEG / 2
    n = 400
    lons = np.r_[np.linspace(west, east, n + 1), np.linspace(west, east, n + 1), np.full(n + 1, west), np.full(n + 1, east)]
    lats = np.r_[np.full(n + 1, north), np.full(n + 1, south), np.linspace(south, north, n + 1), np.linspace(south, north, n + 1)]
    x, y = fwd.transform(lons, lats)
    c = np.clip(np.floor(x / ras["xs"]), 0, W - 1)
    r = np.clip(np.floor(-y / ras["ys"]), 0, H - 1)
    return int((r.max() - r.min() + 1) * (c.max() - c.min() + 1))


def est_mem(ni, nj, win):
    tx, ty = math.ceil(ni / TILE), math.ceil(nj / TILE)
    return 4 * ni * nj + 12 * win + 64 + tx * ty * TILE * TILE // 2


opuk_mem = est_mem(1455, 1515, window(62.92, -10.72, 1455, 1515))
taken, prods = None, []
for name, lonW, latS, lonE, latN in RULES:
    rect = box(lonW, latS, lonE, latN)
    mine = region.intersection(rect) if taken is None else region.intersection(rect).difference(taken)
    taken = rect if taken is None else taken.union(rect)
    assert not mine.is_empty, name
    bW, bS, bE, bN = mine.bounds
    la1, lo1 = round(bN + MARGIN, 2), round(bW - MARGIN, 2)
    nj = int(round((la1 - round(bS - MARGIN, 2)) / DEG)) + 1
    ni = int(round((round(bE + MARGIN, 2) - lo1) / DEG)) + 1
    tx, ty = math.ceil(ni / TILE), math.ceil(nj / TILE)
    win = window(la1, lo1, ni, nj)
    m = est_mem(ni, nj, win)
    prods.append({"product": name, "rule": [lonW, latS, lonE, latN], "land_bbox": [round(v, 2) for v in mine.bounds],
                  "la1": la1, "lo1": lo1, "ni": ni, "nj": nj, "cells": ni * nj, "tiles": tx * ty,
                  "frame_bytes": 64 + tx * ty * TILE * TILE // 2, "window": win,
                  "mem_x_opuk": round(m / opuk_mem, 2), "cells_x_opuk": round(ni * nj / (1455 * 1515), 2)})
left = region.difference(taken)
tot = {k: sum(p[k] for p in prods) for k in ("cells", "tiles", "frame_bytes", "window")}
out = {"rules": RULES, "products": prods, "totals": tot, "cells_x_opuk": round(tot["cells"] / (1455 * 1515), 2),
       "window_x_opuk": round(tot["window"] / 2224675, 2), "left_over_area_deg2": left.area,
       "max_mem_x_opuk": max(p["mem_x_opuk"] for p in prods)}
json.dump(out, open(outp, "w"), indent=1)
print(json.dumps({k: out[k] for k in ("totals", "cells_x_opuk", "window_x_opuk", "left_over_area_deg2", "max_mem_x_opuk")}))
for p in prods:
    print(f"  {p['product']:<10} land {p['land_bbox']}  grid {p['ni']}x{p['nj']} = {p['cells'] / 1e6:.2f} M "
          f"(x{p['cells_x_opuk']}), {p['tiles']} tiles, {p['frame_bytes'] / 1e6:.2f} MB, window x{p['window'] / 2224675:.2f}, "
          f"mem est x{p['mem_x_opuk']}")
