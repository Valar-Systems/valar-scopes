"""Dump an ODIM HDF5 file's tree and attributes, with every coordinate-bearing value redacted.

Usage: python -I dump_h5.py <file.h5>
Redacted: any attribute whose name contains lat/lon/LL_/UL_/UR_/LR_, and every number inside projdef.
"""
import re
import sys

import h5py
import numpy as np

COORD = re.compile(r"(lat|lon|^LL_|^UL_|^UR_|^LR_)", re.I)


def show(name, obj):
    kind = "dataset" if isinstance(obj, h5py.Dataset) else "group"
    extra = f" shape={obj.shape} dtype={obj.dtype}" if kind == "dataset" else ""
    print(f"[{kind}] /{name}{extra}")
    for k, v in obj.attrs.items():
        if isinstance(v, bytes):
            v = v.decode("utf-8", "replace")
        if isinstance(v, np.ndarray) and v.dtype.kind == "S":
            v = [x.decode("utf-8", "replace") for x in v.ravel()]
        if COORD.search(k):
            v = "<redacted>"
        elif k == "projdef":
            v = re.sub(r"[-+]?\d+(\.\d+)?", "#", str(v))
        s = str(v)
        if len(s) > 400:
            s = s[:400] + f"... ({len(s)} chars)"
        print(f"    @{k} = {s}")


with h5py.File(sys.argv[1], "r") as f:
    show("", f)
    f.visititems(show)
