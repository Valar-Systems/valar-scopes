"""How much of OPERA's DBZH COG a UK ingest must read. The box is the UK's land extent plus the radar's
2.02 deg maximum view, held only inside this script; only counts and bytes are printed.
Usage: python -I cog_window.py <DBZH.tiff> <DBZH.h5>"""
import sys, math
import numpy as np, tifffile, h5py
from pyproj import Transformer
LAT0, LAT1, LON0, LON1, M = 49.8, 60.9, -8.7, 1.8, 2.02
la0, la1, lo0, lo1 = LAT0 - M, LAT1 + M, LON0 - M, LON1 + M
with h5py.File(sys.argv[2]) as f:
    w = f["where"].attrs
    proj = w["projdef"].decode() if isinstance(w["projdef"], bytes) else str(w["projdef"])
    tr = Transformer.from_crs("EPSG:4326", proj, always_xy=True)
    ulx, uly = tr.transform(float(w["UL_lon"]), float(w["UL_lat"]))
    xs, ys = float(w["xscale"]), float(w["yscale"])
rows, cols = [], []
for i in range(201):
    for (lon, lat) in ((lo0 + (lo1 - lo0) * i / 200, la0), (lo0 + (lo1 - lo0) * i / 200, la1),
                       (lo0, la0 + (la1 - la0) * i / 200), (lo1, la0 + (la1 - la0) * i / 200)):
        x, y = tr.transform(lon, lat)
        cols.append((x - ulx) / xs); rows.append((uly - y) / ys)
r0, r1, c0, c1 = math.floor(min(rows)), math.floor(max(rows)), math.floor(min(cols)), math.floor(max(cols))
t = tifffile.TiffFile(sys.argv[1]); p = t.pages[0]
tw, tl = p.tilewidth, p.tilelength
tx = math.ceil(p.shape[1] / tw)
need = [(ty, txx) for ty in range(r0 // tl, r1 // tl + 1) for txx in range(c0 // tw, c1 // tw + 1)]
bc = p.tags["TileByteCounts"].value
nb = sum(bc[ty * tx + txx] for ty, txx in need)
print("window px:", r1 - r0 + 1, "x", c1 - c0 + 1, "of", p.shape[0], "x", p.shape[1])
print("COG tiles needed:", len(need), "of", len(bc), "| compressed bytes:", nb, "of", sum(bc), f"({nb / sum(bc):.1%})")
print("WXG1 grid at 0.01 deg:", round((la1 - la0) / 0.01) + 1, "rows x", round((lo1 - lo0) / 0.01) + 1, "cols ->",
      math.ceil((round((la1 - la0) / 0.01) + 1) / 512) * math.ceil((round((lo1 - lo0) / 0.01) + 1) / 512), "tiles of 512")
