"""Do the 10 products' windows leave the raster? windowFor REFUSES if they do; eu_rules.py's window() clipped silently.
Usage: python -I eu_window_edge.py <graded.json> <eu_rules.json>"""
import json, math, sys
import numpy as np
from pyproj import Transformer
g = json.load(open(sys.argv[1], encoding="utf-8")); er = json.load(open(sys.argv[2], encoding="utf-8"))
P, ras = g["raster"]["proj"], g["raster"]
fwd = Transformer.from_crs("EPSG:4326", f"+proj=laea +lat_0={P['lat0']} +lon_0={P['lon0']} +x_0={P['x0']} +y_0={P['y0']} +a={P['a']} +rf={P['invF']} +units=m +no_defs", always_xy=True)
W, H, DEG = ras["width"], ras["height"], 0.01
def win(la1, lo1, ni, nj):
    north, south = la1 + DEG / 2, la1 - (nj - 1) * DEG - DEG / 2
    west, east = lo1 - DEG / 2, lo1 + (ni - 1) * DEG + DEG / 2
    nlat, nlon = nj, ni
    lons = np.r_[np.linspace(west, east, nlon + 1), np.linspace(west, east, nlon + 1), np.full(nlat + 1, west), np.full(nlat + 1, east)]
    lats = np.r_[np.full(nlon + 1, north), np.full(nlon + 1, south), np.linspace(south, north, nlat + 1), np.linspace(south, north, nlat + 1)]
    x, y = fwd.transform(lons, lats)
    c = x / ras["xs"]; r = -y / ras["ys"]
    return math.floor(r.min()), math.floor(r.max()), math.floor(c.min()), math.floor(c.max())
print("opuk today", win(62.92, -10.72, 1455, 1515), "raster rows 0..%d cols 0..%d" % (H - 1, W - 1))
for p in er["products"]:
    r0, r1, c0, c1 = win(p["la1"], p["lo1"], p["ni"], p["nj"])
    out = []
    if r0 < 0: out.append(f"north by {-r0} px")
    if r1 >= H: out.append(f"south by {r1 - H + 1} px")
    if c0 < 0: out.append(f"west by {-c0} px")
    if c1 >= W: out.append(f"east by {c1 - W + 1} px")
    print(f"{p['product']:<10} window rows {r0}..{r1} cols {c0}..{c1} -> {'LEAVES: ' + ', '.join(out) if out else 'inside'}")
