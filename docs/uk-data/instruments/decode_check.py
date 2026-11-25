"""The collection's daily decode control (docs/uk-data.md A1.3): a held frame's raw tiles, decoded, against its .h5.

Usage: python -I decode_check.py <out_dir> <stamp>      e.g. 20261028T0755
Prints the window's cell count and how many cells differ (undetect is NaN in the TIFF, -8888000 in the HDF5).
"""
import json
import os
import sys
import zlib

import h5py
import numpy as np

out, stamp = sys.argv[1], sys.argv[2]
meta = json.load(open(os.path.join(out, "frames", stamp + ".json")))
raw = open(os.path.join(out, "frames", stamp + ".tiles"), "rb").read()
if meta["compression"] != [8] or meta["samples"] != [2]:
    raise SystemExit(f"unexpected compression {meta['compression']} or samples {meta['samples']}")
tw, tl = meta["tile"]
tx = meta["tiles_x"]
r0, r1, c0, c1 = meta["window"]
tiles, pos = {}, 0
for i, n in zip(meta["idx"], meta["bytecounts"]):
    a = np.frombuffer(zlib.decompress(raw[pos:pos + n]), dtype="<f4").reshape(tl, tw, 2)
    tiles[i] = a
    pos += n
if pos != len(raw):
    raise SystemExit(f"tile bytes {pos} != file {len(raw)}")
win = np.empty((r1 - r0 + 1, c1 - c0 + 1), dtype="<f4")
for r in range(r0, r1 + 1):
    ty = r // tl
    for txi in range(c0 // tw, c1 // tw + 1):
        t = tiles[ty * tx + txi]
        cs, ce = max(c0, txi * tw), min(c1, txi * tw + tw - 1)
        win[r - r0, cs - c0: ce - c0 + 1] = t[r - ty * tl, cs - txi * tw: ce - txi * tw + 1, 0]
with h5py.File(os.path.join(out, "h5", stamp + ".h5"), "r") as f:
    h = f["dataset1/data1/data"][r0:r1 + 1, c0:c1 + 1].astype("<f4")
und = h == -8888000.0
same = np.where(und, np.isnan(win), win == h)
print(json.dumps({"stamp": stamp, "cells": int(h.size), "differ": int((~same).sum()),
                  "undetect": int(und.sum()), "nodata": int((h == -9999000.0).sum()), "echo": int((h > -100).sum())}))
