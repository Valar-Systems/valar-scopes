"""Radar beyond the UK, plan sizing (read-only, offline): the served region's geometry from Natural Earth 1:10m
admin-0 v5.1.2 (the survey's file, sha256 pinned) and A3.23's graded.json (sha256 pinned).

Prints:
  - the 40 countries (A3.23 ranked >= 90 % covered, plus the four listed apart at 100 %), with in-domain % and covered %
  - each country's polygon parts that lie outside the OPERA composite's raster (dropped from the served region)
  - the region's lat/lon box (in-domain parts), and per-option grid sizes at opuk's 0.01 deg, 512-cell WXG1 tiles

Usage: python -I eu_region.py <graded.json> <ne_10m_admin_0_countries.geojson> <out.json>
"""
import hashlib
import json
import math
import sys

import numpy as np
from pyproj import Transformer
from shapely.geometry import Polygon, box, shape
from shapely.ops import unary_union

GRADED_SHA = "0b0d43ac4363093b840655aaa763a57b78946b856f13bc1c60156b6374c1a6bd"
NE_SHA = "239eec57ac17f100a11e2536cffc56752c318b50ae765b0918ff7aab4ce8f255"
TINY = ["San Marino", "Monaco", "Gibraltar", "Guernsey"]
TILE, DEG, MARGIN = 512, 0.01, 2.02
UK_BOX = (49.8, 60.9, -8.7, 1.8)            # proxy/src/wx/products.ts OPUK_LAND: latS, latN, lonW, lonE
OPUK_CELLS = 1455 * 1515                    # OPUK_GRID ni x nj


def sha(p):
    return hashlib.sha256(open(p, "rb").read()).hexdigest()


graded_p, ne_p, out_p = sys.argv[1:4]
assert sha(graded_p) == GRADED_SHA, "graded.json is not A3.23's"
assert sha(ne_p) == NE_SHA, "Natural Earth file is not the survey's"
g = json.load(open(graded_p, encoding="utf-8"))
ne = json.load(open(ne_p, encoding="utf-8"))

ranked = {r["name"]: r for r in g["ranked"]}
apart = {r["name"]: r for r in g["apart"]}
served = sorted([n for n, r in ranked.items() if r["covered_median"] >= 90] + TINY)
assert all(apart[n]["covered_median"] == 100 for n in TINY)
excluded_ranked = sorted(n for n, r in ranked.items() if r["covered_median"] < 90)

ras = g["raster"]
P = ras["proj"]
to_laea = Transformer.from_crs("EPSG:4326",
                               f"+proj=laea +lat_0={P['lat0']} +lon_0={P['lon0']} +x_0={P['x0']} +y_0={P['y0']} "
                               f"+a={P['a']} +rf={P['invF']} +units=m +no_defs", always_xy=True)
W_M, H_M = ras["width"] * ras["xs"], ras["height"] * ras["ys"]   # raster: x 0..W_M, y 0..-H_M (corner = false origin)


def in_raster(lon, lat):
    x, y = to_laea.transform(np.asarray(lon), np.asarray(lat))
    return (x >= 0) & (x <= W_M) & (y <= 0) & (y >= -H_M)


def laea_area(poly):
    ext = np.asarray(poly.exterior.coords)
    x, y = to_laea.transform(ext[:, 0], ext[:, 1])
    a = Polygon(np.c_[x, y]).area
    for h in poly.interiors:
        hc = np.asarray(h.coords)
        hx, hy = to_laea.transform(hc[:, 0], hc[:, 1])
        a -= Polygon(np.c_[hx, hy]).area
    return a / 1e6


feat = {f["properties"]["ADMIN"]: f for f in ne["features"]}
assert all(n in feat for n in served + excluded_ranked), [n for n in served + excluded_ranked if n not in feat]

rows, kept_parts, dropped = [], {}, []
for n in served:
    geom = shape(feat[n]["geometry"])
    parts = list(geom.geoms) if geom.geom_type == "MultiPolygon" else [geom]
    keep = []
    for pt in parts:
        c = np.asarray(pt.exterior.coords)
        ins = in_raster(c[:, 0], c[:, 1])
        if ins.all():
            keep.append(pt)
        elif ins.any():
            keep.append(pt)
            dropped.append({"country": n, "part": "straddles the raster edge (kept)", "km2": round(laea_area(pt)),
                            "bbox": [round(v, 2) for v in pt.bounds]})
        else:
            dropped.append({"country": n, "part": "outside the raster (dropped)", "km2": round(laea_area(pt)),
                            "bbox": [round(v, 2) for v in pt.bounds]})
    kept_parts[n] = keep
    r = ranked.get(n) or apart[n]
    rows.append({"country": n, "covered_pct": r["covered_median"], "in_domain_pct": r["in_domain_pct"],
                 "pixels": r["pixels"], "bbox": [round(v, 2) for v in unary_union(keep).bounds]})

region = unary_union([p for v in kept_parts.values() for p in v])
lonW, latS, lonE, latN = region.bounds


def grid_for(latS_, latN_, lonW_, lonE_, margin=MARGIN):
    la1, lo1 = round(latN_ + margin, 2), round(lonW_ - margin, 2)
    nj = int(round((la1 - (latS_ - margin)) / DEG)) + 1
    ni = int(round((lonE_ + margin - lo1) / DEG)) + 1
    tx, ty = math.ceil(ni / TILE), math.ceil(nj / TILE)
    return {"la1": la1, "lo1": lo1, "ni": ni, "nj": nj, "cells": ni * nj, "tiles": tx * ty, "tilesX": tx, "tilesY": ty,
            "frame_bytes": 64 + tx * ty * TILE * TILE // 2}


one = grid_for(latS, latN, lonW, lonE)

# A': the same grid, building only the tiles the crops can read: a tile is needed if it meets the region grown by
# the crop's reach (MARGIN deg in lat and lon; the crop's lon half-extent is clamped at 2.0 deg, ProjectAt).
reach = region.buffer(0)  # (geometry in degrees; reach is tested per tile below with a box grown by MARGIN)
need = 0
for ty in range(one["tilesY"]):
    for tx in range(one["tilesX"]):
        t_latN = one["la1"] - ty * TILE * DEG + DEG / 2
        t_latS = one["la1"] - ((ty + 1) * TILE - 1) * DEG - DEG / 2
        t_lonW = one["lo1"] + tx * TILE * DEG - DEG / 2
        t_lonE = one["lo1"] + ((tx + 1) * TILE - 1) * DEG + DEG / 2
        if region.intersects(box(t_lonW - MARGIN, t_latS - MARGIN, t_lonE + MARGIN, t_latN + MARGIN)):
            need += 1

out = {"served": rows, "n_served": len(served), "excluded_ranked": excluded_ranked, "parts_outside_or_straddling": dropped,
       "region_bbox": {"latS": round(latS, 2), "latN": round(latN, 2), "lonW": round(lonW, 2), "lonE": round(lonE, 2)},
       "uk_box": UK_BOX,
       "option_one_full": one,
       "option_one_sparse_tiles_built": need,
       "opuk_cells": OPUK_CELLS, "opuk_frame_bytes": 64 + 9 * TILE * TILE // 2}
json.dump(out, open(out_p, "w", encoding="utf-8"), indent=1)
print(json.dumps({k: out[k] for k in ("n_served", "region_bbox", "option_one_full", "option_one_sparse_tiles_built",
                                     "opuk_cells", "opuk_frame_bytes")}, indent=1))
for d in dropped:
    print("part:", json.dumps(d))
