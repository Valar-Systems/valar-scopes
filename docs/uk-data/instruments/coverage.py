"""M3, M4, O3, C2 and C3 of docs/uk-data.md, from sampled frames. No coordinate is ever printed.

Usage: python -I coverage.py <airports.csv> <mo_dir> <op_dir> <out.json> [--sabotage-c2] [--sabotage-c3]
Airports are looked up by IATA code inside this script; only codes and verdicts leave it.
"""
import csv
import glob
import json
import math
import os
import re
import sys

import h5py
import numpy as np
from pyproj import Transformer

UK = ["LHR", "MAN", "BHX", "NCL", "EDI", "GLA", "ABZ", "INV", "SYY", "KOI", "LSI", "CWL", "NQY", "ISC", "BFS", "BHD", "LDY"]
NEIGHBOUR = ["DUB"]
OPERA_CONTROL = ["FRA"]
DISC_KM = 160.0

args = [a for a in sys.argv[1:] if not a.startswith("--")]
SAB_C2 = "--sabotage-c2" in sys.argv
SAB_C3 = "--sabotage-c3" in sys.argv
apt_csv, mo_dir, op_dir, out_path = args

pos = {}
with open(apt_csv, encoding="utf-8") as f:
    for row in csv.DictReader(f):
        c = row.get("iata_code") or ""
        if c in UK + NEIGHBOUR + OPERA_CONTROL and row["type"] in ("large_airport", "medium_airport", "small_airport"):
            pos[c] = (float(row["longitude_deg"]), float(row["latitude_deg"]))
missing = [c for c in UK + NEIGHBOUR + OPERA_CONTROL if c not in pos]


def s(v):
    return v.decode() if isinstance(v, bytes) else str(v)


def key_time(path):
    b = os.path.basename(path)
    m = re.match(r"(\d{8})T?(\d{4})", b)
    d, hm = m.groups()
    if SAB_C2:
        hm = hm[2:] + hm[:2]
    return d, hm


def open_frame(path):
    f = h5py.File(path, "r")
    w = f["where"].attrs
    proj = s(w["projdef"])
    tr = Transformer.from_crs("EPSG:4326", proj, always_xy=True)
    ulx, uly = tr.transform(float(w["UL_lon"]), float(w["UL_lat"]))
    lrx, lry = tr.transform(float(w["LR_lon"]), float(w["LR_lat"]))
    xs, ys, nx, ny = float(w["xscale"]), float(w["yscale"]), int(w["xsize"]), int(w["ysize"])
    # Residual of the georeference: the LR corner against UL + size x scale (metres), both readings of "corner".
    resid_edge = (lrx - (ulx + nx * xs), lry - (uly - ny * ys))
    resid_centre = (lrx - (ulx + (nx - 1) * xs), lry - (uly - (ny - 1) * ys))
    d1 = f["dataset1/data1"]
    what = d1["what"].attrs
    data = d1["data"][()]
    return {
        "f": f, "tr": tr, "ulx": ulx, "uly": uly, "xs": xs, "ys": ys, "nx": nx, "ny": ny, "data": data,
        "nodata": float(what["nodata"]), "undetect": float(what["undetect"]), "quantity": s(what["quantity"]),
        "proj_kind": re.search(r"\+proj=(\w+)", proj).group(1),
        "resid_edge_m": [round(r, 1) for r in resid_edge], "resid_centre_m": [round(r, 1) for r in resid_centre],
        "datasets": sorted(k for k in f.keys() if k.startswith("dataset")),
        "data_groups": sorted(k for k in f["dataset1"].keys() if k.startswith("data")),
        "file_time": (s(f["what"].attrs["date"]), s(f["what"].attrs["time"])),
        "storage": {"chunks": d1["data"].chunks, "compression": d1["data"].compression,
                    "opts": d1["data"].compression_opts, "shuffle": d1["data"].shuffle, "dtype": str(d1["data"].dtype)},
    }


def covered_mask(fr):
    ref = fr["undetect"] if SAB_C3 else fr["nodata"]
    return ~np.isclose(fr["data"], ref) & ~np.isnan(fr["data"])


def pixel(fr, code):
    x, y = fr["tr"].transform(*pos[code])
    col = math.floor((x - fr["ulx"]) / fr["xs"])
    row = math.floor((fr["uly"] - y) / fr["ys"])
    return row, col, x, y


def disc_share(fr, mask, code):
    row, col, x, y = pixel(fr, code)
    r = int(math.ceil(DISC_KM * 1000 / fr["xs"])) + 1
    r0, r1, c0, c1 = max(0, row - r), min(fr["ny"], row + r + 1), max(0, col - r), min(fr["nx"], col + r + 1)
    rr, cc = np.mgrid[r0:r1, c0:c1]
    px = fr["ulx"] + (cc + 0.5) * fr["xs"]
    py = fr["uly"] - (rr + 0.5) * fr["ys"]
    inside = (px - x) ** 2 + (py - y) ** 2 <= (DISC_KM * 1000) ** 2
    full = math.pi * (DISC_KM * 1000 / fr["xs"]) ** 2          # pixels a full disc would hold, grid or not
    return round(float(mask[r0:r1, c0:c1][inside].sum()) / full, 4)


def grade(paths, codes, control_codes):
    frames = []
    per_code = {c: {"pixel_covered_all": True, "on_grid_all": True, "disc160_min": 1.0} for c in codes + control_codes}
    corners_nodata_all = True
    for p in sorted(paths):
        fr = open_frame(p)
        mask = covered_mask(fr)
        kd, kt = key_time(p)
        fd, ft = fr["file_time"]
        c2 = (kd == fd and kt == ft[:4])
        corners = [mask[0, 0], mask[0, -1], mask[-1, 0], mask[-1, -1]]
        corners_nodata_all &= not any(corners)
        for c in codes + control_codes:
            row, col, _, _ = pixel(fr, c)
            on = 0 <= row < fr["ny"] and 0 <= col < fr["nx"]
            per_code[c]["on_grid_all"] &= on
            per_code[c]["pixel_covered_all"] &= bool(on and mask[row, col])
            per_code[c].setdefault("pixel_by_frame", []).append(None if not on else bool(mask[row, col]))
            per_code[c]["disc160_min"] = min(per_code[c]["disc160_min"], disc_share(fr, mask, c) if on else 0.0)
        vals = fr["data"][mask & ~np.isclose(fr["data"], fr["undetect"])]
        frames.append({
            "file": os.path.basename(p), "C2_key_equals_file_time": c2, "quantity": fr["quantity"],
            "proj": fr["proj_kind"], "grid": [fr["nx"], fr["ny"], fr["xs"], fr["ys"]], "datasets": fr["datasets"],
            "data_groups": fr["data_groups"], "storage": fr["storage"], "nodata": fr["nodata"], "undetect": fr["undetect"],
            "georef_resid_edge_m": fr["resid_edge_m"], "georef_resid_centre_m": fr["resid_centre_m"],
            "corners_covered": [bool(x) for x in corners], "covered_share_of_grid": round(float(mask.mean()), 4),
            "echo_pixels": int(vals.size),
            "echo_values": None if vals.size == 0 else {
                "min": float(vals.min()), "p50": float(np.median(vals)), "max": float(vals.max()),
                "distinct": int(np.unique(vals).size),
                "smallest_step": float(np.diff(np.unique(vals)).min()) if np.unique(vals).size > 1 else None},
        })
        fr["f"].close()
    return {"frames": frames, "centres": per_code, "C3_corners_nodata_in_every_frame": corners_nodata_all}


out = {"missing_codes": missing, "sabotage": {"c2": SAB_C2, "c3": SAB_C3}}
out["met_office"] = grade(glob.glob(os.path.join(mo_dir, "*.h5")), UK + NEIGHBOUR, [])
for q in ("DBZH", "RATE"):
    out[f"opera_{q}"] = grade(glob.glob(os.path.join(op_dir, f"*_{q}.h5")), UK + NEIGHBOUR, OPERA_CONTROL)
json.dump(out, open(out_path, "w"), indent=1)
print(json.dumps(out, indent=1))
