"""K1's fixtures (docs/wx-opuk.md): two real OPERA CIRRUS DBZH frames, as the opuk COG reader will read them.

Selects the wettest and the driest frame among those the R1 collection held at 2026-10-29 03:00Z that are still in the
cache (K1, restated), by echo cells (>= 15 dBZ) in the window. For each it writes, under <out>:
  <stamp>.ranges.json  the byte ranges the reader requests: the first 8,192 B, then one range per window row of tiles
  <stamp>.ranges.bin   those ranges' bytes, concatenated in that order
  <stamp>.expected.json  the window, its counts, the sha256 of its value band (float32 LE, NaN as 0x7FC00000) computed
                         from the frame's own HDF5, and the projection numbers from the HDF5's projdef
The expected sha is cross-checked against tifffile's decode of the full COG; a mismatch stops the tool.

The window is the opuk WXG1 grid's cell-boundary rectangle, its four edges sampled every 0.01 deg, projected with the
frame's projdef, floored to pixel indices: the same rule src/wx/opera.ts implements.
Usage: python -I tools/wx/opera_fixture.py <collection dir> <scratch dir> <out dir>
"""
import glob
import hashlib
import json
import math
import os
import re
import sys
import urllib.request
import zlib

import h5py
import numpy as np
import tifffile
from pyproj import Transformer

BASE = "https://s3.waw3-1.cloudferro.com/openradar-24h"
UA = {"User-Agent": "BlipscopeWxIngest/1 (+https://github.com/Valar-Systems/valar-scopes)"}
# The opuk WXG1 grid (docs/wx-opuk.md; products.ts): NW cell centre, 0.01 deg cells, 1,455 cols x 1,515 rows.
LA1, LO1, D, NI, NJ = 62.92, -10.72, 0.01, 1455, 1515
CUTOFF, FIRST = "20261029T0300", "20261028T0000"
NAN_BITS = np.uint32(0x7FC00000)

coll, scratch, out = sys.argv[1], sys.argv[2], sys.argv[3]
os.makedirs(out, exist_ok=True)
os.makedirs(scratch, exist_ok=True)


def get(url, rng=None):
    h = dict(UA)
    if rng:
        h["Range"] = f"bytes={rng[0]}-{rng[1]}"
    with urllib.request.urlopen(urllib.request.Request(url, headers=h), timeout=120) as r:
        return r.read()


def held_echo(stamp):
    meta = json.load(open(os.path.join(coll, "frames", stamp + ".json")))
    raw = open(os.path.join(coll, "frames", stamp + ".tiles"), "rb").read()
    n, pos = 0, 0
    tw, tl = meta["tile"]
    r0, r1, c0, c1 = meta["window"]
    tx = meta["tiles_x"]
    for i, bc in zip(meta["idx"], meta["bytecounts"]):
        a = np.frombuffer(zlib.decompress(raw[pos:pos + bc]), dtype="<f4").reshape(tl, tw, 2)[:, :, 0]
        pos += bc
        ty, txi = divmod(i, tx)
        rs, re_ = max(r0, ty * tl) - ty * tl, min(r1, ty * tl + tl - 1) - ty * tl
        cs, ce = max(c0, txi * tw) - txi * tw, min(c1, txi * tw + tw - 1) - txi * tw
        sub = a[rs:re_ + 1, cs:ce + 1]
        n += int(np.count_nonzero(sub >= 15.0))   # NaN and -9999000 compare False
    return n


stamps = sorted(os.path.basename(p)[:-5] for p in glob.glob(os.path.join(coll, "frames", "*.json")))
cand = [s for s in stamps if FIRST <= s <= CUTOFF]
counts = {s: held_echo(s) for s in cand}
wet = max(cand, key=lambda s: (counts[s], [-ord(ch) for ch in s]))   # most echo; ties -> earliest stamp
dry = min(cand, key=lambda s: (counts[s], s))                         # least echo; ties -> earliest stamp
print(json.dumps({"candidates": len(cand), "wet": [wet, counts[wet]], "dry": [dry, counts[dry]]}))


def window(tr, ulx, uly, xs, ys):
    north, south = LA1 + D / 2, LA1 - (NJ - 1) * D - D / 2
    west, east = LO1 - D / 2, LO1 + (NI - 1) * D + D / 2
    nlat, nlon = round((north - south) / D), round((east - west) / D)
    pts = [(west + (east - west) * i / nlon, north) for i in range(nlon + 1)]
    pts += [(west + (east - west) * i / nlon, south) for i in range(nlon + 1)]
    pts += [(west, south + (north - south) * j / nlat) for j in range(nlat + 1)]
    pts += [(east, south + (north - south) * j / nlat) for j in range(nlat + 1)]
    lon, lat = np.array([p[0] for p in pts]), np.array([p[1] for p in pts])
    x, y = tr.transform(lon, lat)
    cols, rows = (x - ulx) / xs, (uly - y) / ys
    return int(math.floor(rows.min())), int(math.floor(rows.max())), int(math.floor(cols.min())), int(math.floor(cols.max()))


def canonical(win):
    v = np.asarray(win, dtype="<f4").copy()
    bits = v.view("<u4")
    bits[np.isnan(v)] = NAN_BITS
    return hashlib.sha256(bits.tobytes()).hexdigest()


for role, stamp in (("wet", wet), ("dry", dry)):
    day = f"{stamp[:4]}/{stamp[4:6]}/{stamp[6:8]}"
    tif_url = f"{BASE}/{day}/OPERA/COMP/OPERA@{stamp}@0@DBZH.tiff"
    h5_url = f"{BASE}/{day}/OPERA/COMP/OPERA@{stamp}@0@DBZH.h5"
    tif_p, h5_p = os.path.join(scratch, stamp + ".tiff"), os.path.join(scratch, stamp + ".h5")
    for u, p in ((tif_url, tif_p), (h5_url, h5_p)):
        if not os.path.exists(p):
            open(p, "wb").write(get(u))
    with h5py.File(h5_p, "r") as f:
        w = f["where"].attrs
        projdef = w["projdef"].decode() if isinstance(w["projdef"], bytes) else str(w["projdef"])
        par = {k: float(v) for k, v in re.findall(r"\+(lat_0|lon_0|x_0|y_0)=([-\d.]+)", projdef)}
        if "+proj=laea" not in projdef or "WGS84" not in projdef:
            raise SystemExit(f"unexpected projdef in {stamp}")
        tr = Transformer.from_crs("EPSG:4326", projdef, always_xy=True)
        ulx, uly = tr.transform(float(w["UL_lon"]), float(w["UL_lat"]))
        xs, ys = float(w["xscale"]), float(w["yscale"])
        r0, r1, c0, c1 = window(tr, ulx, uly, xs, ys)
        h = f["dataset1/data1/data"][r0:r1 + 1, c0:c1 + 1].astype("<f4")
    hv = h.copy()
    hv[h == np.float32(-8888000.0)] = np.nan               # undetect, as the GeoTIFF writes it
    sha_h5 = canonical(hv)
    t = tifffile.TiffFile(tif_p).pages[0]
    a = t.asarray()[:, :, 0]
    sha_tif = canonical(a[r0:r1 + 1, c0:c1 + 1])
    if sha_h5 != sha_tif:
        raise SystemExit(f"{stamp}: the HDF5 and tifffile disagree on the window ({sha_h5[:12]} / {sha_tif[:12]})")
    offs, bcs = t.tags["TileOffsets"].value, t.tags["TileByteCounts"].value
    tw, tl, wpx = t.tilewidth, t.tilelength, t.shape[1]
    txn = math.ceil(wpx / tw)
    ranges = [(0, 8191)]
    for ty in range(r0 // tl, r1 // tl + 1):
        row = [ty * txn + c for c in range(c0 // tw, c1 // tw + 1)]
        ranges.append((int(offs[row[0]]), int(offs[row[-1]] + bcs[row[-1]] - 1)))
    raw = open(tif_p, "rb").read()
    blob = b"".join(raw[a_:b_ + 1] for a_, b_ in ranges)
    open(os.path.join(out, stamp + ".ranges.bin"), "wb").write(blob)
    json.dump({"url": tif_url, "size": len(raw), "ranges": ranges}, open(os.path.join(out, stamp + ".ranges.json"), "w"),
              indent=1)
    exp = {"role": role, "stamp": stamp, "window": [r0, r1, c0, c1], "sha256_value_band": sha_h5,
           "counts": {"nodata": int(np.count_nonzero(h == np.float32(-9999000.0))),
                      "undetect": int(np.count_nonzero(h == np.float32(-8888000.0))),
                      "echo_ge_15": int(np.count_nonzero(h >= 15.0))},
           "projection": {"lat_0": par["lat_0"], "lon_0": par["lon_0"], "x_0": par["x_0"], "y_0": par["y_0"]},
           "cross_check": "sha equal: HDF5 and tifffile (full COG)",
           "h5_sha256": hashlib.sha256(open(h5_p, "rb").read()).hexdigest(),
           "tif_sha256": hashlib.sha256(raw).hexdigest()}
    json.dump(exp, open(os.path.join(out, stamp + ".expected.json"), "w"), indent=1)
    print(json.dumps({k: exp[k] for k in ("role", "stamp", "counts")} | {"bytes": len(blob), "ranges": len(ranges)}))
