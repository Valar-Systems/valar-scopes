"""K1 diagnosis (docs/wx-opuk.md), read-only: a frame's GeoTIFF corner convention against its HDF5's. Prints differences in metres and the
convention keys only -- never a corner's coordinates."""
import json
import re
import sys

import h5py
import tifffile
from pyproj import Transformer

h5_p, tif_p = sys.argv[1], sys.argv[2]
t = tifffile.TiffFile(tif_p)
pg = t.pages[0]
tie = pg.tags["ModelTiepointTag"].value
scale = pg.tags["ModelPixelScaleTag"].value
gk = pg.tags["GeoKeyDirectoryTag"].value
keys = {gk[4 + 4 * i]: gk[4 + 4 * i + 3] for i in range(gk[3]) if gk[4 + 4 * i + 1] == 0}
raster_type = {1: "PixelIsArea", 2: "PixelIsPoint"}.get(keys.get(1025), f"absent/{keys.get(1025)}")
tif_ulx, tif_uly = tie[3], tie[4]

with h5py.File(h5_p, "r") as f:
    w = f["where"].attrs
    projdef = w["projdef"].decode() if isinstance(w["projdef"], bytes) else str(w["projdef"])
    tr = Transformer.from_crs("EPSG:4326", projdef, always_xy=True)
    corners = {}
    for c in ("UL", "UR", "LL", "LR"):
        corners[c] = tr.transform(float(w[c + "_lon"]), float(w[c + "_lat"]))
    xs, ys = float(w["xscale"]), float(w["yscale"])
    nx, ny = int(w["xsize"]), int(w["ysize"])

ulx, uly = corners["UL"]
out = {
    "geotiff_raster_type_1025": raster_type,
    "geotiff_tiepoint_raster_ij": [tie[0], tie[1]],
    "pixel_scale": [scale[0], scale[1]], "h5_scale": [xs, ys], "size_h5": [nx, ny], "size_tif": [pg.imagewidth, pg.imagelength],
    "h5_UL_minus_tif_tie_m": [round(ulx - tif_ulx, 3), round(uly - tif_uly, 3)],
    # if the HDF5 corners are pixel CORNERS, UR - UL = nx * xs and UL - LL = ny * ys
    "h5_UR_minus_UL_x_over_xs": round((corners["UR"][0] - ulx) / xs, 4),
    "h5_UL_minus_LL_y_over_ys": round((uly - corners["LL"][1]) / ys, 4),
    "h5_LR_minus_UL": [round((corners["LR"][0] - ulx) / xs, 4), round((uly - corners["LR"][1]) / ys, 4)],
}
print(json.dumps(out, indent=1))
