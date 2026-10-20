"""A1: compare the TypeScript decoder's output against eccodes (the independent decoder) for one frame.

    python -I tools/wx/compare_a1.py <file.grib2 (gunzipped)> <tsPrefix> [--missing=<v>]

Independent class rule, written here from the ruling (not imported from the TS code):
    -999 -> 14 (no coverage); <missing> -> 15 only if the product has one; < 15 -> 0; else 1 + floor((v-15)/5), max 13.
Compares: per-cell class agreement over the classes the product uses; echo cells' values (|delta| dBZ);
the lat/lon of the TS decoder's 1,000 sampled cells against eccodes' own latitudes/longitudes arrays.
Exit 0 = every bar met (100 % class agreement, echo max |delta| <= 0.5 dBZ, lat/lon max error <= 0.001 deg).
"""
import json
import sys

import eccodes
import numpy as np


def classes(v, missing=None):
    c = np.empty(v.shape, dtype=np.uint8)
    echo = v >= 15
    c[~echo] = 0
    c[echo] = np.minimum(13, 1 + np.floor((v[echo] - 15) / 5)).astype(np.uint8)
    c[v == -999] = 14
    if missing is not None:
        c[v == missing] = 15
    return c


def main(argv):
    grib, ts = argv[1], argv[2]
    mflag = [a for a in argv[3:] if a.startswith("--missing=")]
    missing = float(mflag[0].split("=", 1)[1]) if mflag else None
    with open(grib, "rb") as f:
        h = eccodes.codes_grib_new_from_file(f)
        ref = eccodes.codes_get_values(h).astype(np.float64)
        lats = eccodes.codes_get_array(h, "latitudes")
        lons = eccodes.codes_get_array(h, "longitudes")
        eccodes.codes_release(h)
    tsv = np.fromfile(ts + ".f32", dtype="<f4").astype(np.float64)
    tsc = np.fromfile(ts + ".codes", dtype=np.uint8)
    meta = json.load(open(ts + ".json"))
    assert tsv.size == ref.size == tsc.size, (tsv.size, ref.size, tsc.size)
    rc = classes(ref, missing)
    agree = (rc == tsc)
    used = sorted(set(np.unique(rc).tolist()))
    print(f"frame {meta['grid']['validTimeUtc']}  cells {ref.size}  classes used by the product: {used}"
          f"{'  [TS output is SABOTAGED: rows shifted]' if meta.get('sabotageShift') else ''}")
    print(f"  class agreement: {100.0 * agree.mean():.4f} %  ({int((~agree).sum())} cells differ)")
    for name, sel in [("no echo / <15 (0)", rc == 0), ("echo (1-13)", (rc >= 1) & (rc <= 13)), ("no coverage (14)", rc == 14), ("missing (15)", rc == 15)]:
        n = int(sel.sum())
        if n:
            print(f"    {name:20} reference {n:9d}   agree {100.0 * agree[sel].mean():8.4f} %")
    echo = (ref >= 15) | (tsv >= 15)
    d = np.abs(ref[echo] - tsv[echo])
    print(f"  echo cells {int(echo.sum())}: max |delta| {d.max() if d.size else 0:.4f} dBZ; beyond 0.5 dBZ: {int((d > 0.5).sum())} ({100.0 * (d > 0.5).mean() if d.size else 0:.3f} %)")
    s = meta["samples"]
    idx = np.array([x["idx"] for x in s])
    lat_err = np.abs(np.array([x["lat"] for x in s]) - lats[idx])
    lon_ref = np.where(lons[idx] > 180, lons[idx] - 360, lons[idx])
    lon_err = np.abs(np.array([x["lon"] for x in s]) - lon_ref)
    print(f"  lat/lon of {len(s)} sampled cells: max error lat {lat_err.max():.2e} deg, lon {lon_err.max():.2e} deg")
    ok = agree.all() and (d.max() if d.size else 0) <= 0.5 and lat_err.max() <= 0.001 and lon_err.max() <= 0.001
    print("  BARS " + ("MET" if ok else "NOT MET"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
