"""LAEA reference pairs from pyproj, for proxy/test/wx-opera.test.ts (docs/wx-opuk.md).

The projection is OPERA's, as read from a frame's own GeoKeys (the fixture's expected.json); the points are a lattice
over the opuk grid and its source window -- grid nodes, not places. Fractions are rounded (metres to 4 dp, degrees to
10 dp) so no number carries 16 digits.

Usage: python -I tools/wx/laea_pairs.py <expected.json> <out.json>
"""
import json
import sys

from pyproj import Transformer

exp = json.load(open(sys.argv[1]))
pr = exp["projection"]
proj = (f"+proj=laea +lat_0={pr['lat_0']} +lon_0={pr['lon_0']} +x_0={pr['x_0']} +y_0={pr['y_0']} "
        f"+ellps=WGS84 +units=m +no_defs")
fwd = Transformer.from_crs("EPSG:4326", proj, always_xy=True)
inv = Transformer.from_crs(proj, "EPSG:4326", always_xy=True)

forward = []
for lat in range(48, 65):
    for lon in range(-12, 4):
        for dl in (0.0, 0.5):
            x, y = fwd.transform(lon + dl, lat + dl)
            forward.append([lon + dl, lat + dl, round(x, 4), round(y, 4)])

# inverse: metre lattice points across the window's extent (the raster's own corner and pixel size come from the
# frame; here a 50 km lattice spanning the forward lattice's x/y range is enough)
xs = [p[2] for p in forward]
ys = [p[3] for p in forward]
inverse = []
x = min(xs)
while x <= max(xs):
    y = min(ys)
    while y <= max(ys):
        lon, lat = inv.transform(x, y)
        inverse.append([round(x, 4), round(y, 4), round(lon, 10), round(lat, 10)])
        y += 50_000
    x += 50_000

json.dump({"proj": proj, "pyproj": __import__("pyproj").__version__, "forward": forward, "inverse": inverse},
          open(sys.argv[2], "w"), indent=0)
print(json.dumps({"forward": len(forward), "inverse": len(inverse)}))
