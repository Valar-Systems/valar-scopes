"""A3: crop placement, checked with an INDEPENDENT crop decoder (written here from the format, not the TS code).

    python -I tools/wx/a3_check.py <frame.grib2> <a3-target.json> <cropDir>

For each of N/E/S/W: decode the crop (header + RLE), place every crop cell on the source grid using ONLY the
crop's header (NW cell centre + cell size), and compare with eccodes' reference class for that grid cell.
  - the strongest-echo cell: crop class == reference class (prediction: 4 of 4);
  - every cell of the crop: crop class == reference class (prediction: 100 %);
  - the sabotaged twin (origin moved by one cell): >= 5 % of echo cells mismatch in each of the 4 crops.
Prints counts and percentages only, never coordinates.
"""
import json
import struct
import sys

import eccodes
import numpy as np


def ref_classes(v):
    c = np.zeros(v.shape, dtype=np.uint8)
    e = v >= 15
    c[e] = np.minimum(13, 1 + np.floor((v[e] - 15) / 5)).astype(np.uint8)
    c[v == -999] = 14
    return c


def decode(path):
    b = open(path, "rb").read()
    assert b[:4] == b"WXC1", "not a WXC1 crop"
    coarsen = b[5]
    lat_nw, lon_nw = struct.unpack_from("<ii", b, 12)
    cell_lat, cell_lon = struct.unpack_from("<II", b, 20)
    rows, cols = struct.unpack_from("<HH", b, 28)
    codes = np.empty(rows * cols, dtype=np.uint8)
    p = 0
    for byte in b[32:]:
        run = (byte & 15) + 1
        codes[p:p + run] = byte >> 4
        p += run
    assert p == rows * cols, (p, rows * cols)
    return coarsen, lat_nw / 1e6, lon_nw / 1e6, cell_lat / 1e6, cell_lon / 1e6, codes.reshape(rows, cols)


def main(grib, target, cdir):
    with open(grib, "rb") as f:
        h = eccodes.codes_grib_new_from_file(f)
        la1 = eccodes.codes_get(h, "latitudeOfFirstGridPointInDegrees")
        lo1 = eccodes.codes_get(h, "longitudeOfFirstGridPointInDegrees") - 360.0
        v = eccodes.codes_get_values(h).reshape(3500, 7000)
    ref = ref_classes(v)
    t = json.load(open(target))
    tr, tc = t["row"], t["col"]
    bad = 0
    for d in "NESW":
        for tag in ("", "-sab"):
            k, lat_nw, lon_nw, dlat, dlon, cc = decode(f"{cdir}/a3-{d}{tag}.wxc")
            assert k == 1, f"A3 crops are expected uncoarsened, got {k}"
            rows, cols = cc.shape
            # place each crop cell on the grid from the header alone
            gr = np.rint((la1 - (lat_nw - np.arange(rows) * dlat)) / 0.01).astype(int)
            gc = np.rint(((lon_nw + np.arange(cols) * dlon) - lo1) / 0.01).astype(int)
            inside = (gr[:, None] >= 0) & (gr[:, None] < 3500) & (gc[None, :] >= 0) & (gc[None, :] < 7000)
            rr = np.clip(gr, 0, 3499)[:, None].repeat(cols, 1); ccx = np.clip(gc, 0, 6999)[None, :].repeat(rows, 0)
            expect = np.where(inside, ref[rr, ccx], 14)
            same = (expect == cc)
            echo = (expect >= 1) & (expect <= 13)
            mism_echo = 100.0 * (~same[echo]).mean() if echo.any() else 0.0
            # the strongest-echo cell within this crop
            ii = np.where(gr == tr)[0]; jj = np.where(gc == tc)[0]
            at = (int(cc[ii[0], jj[0]]), int(ref[tr, tc])) if len(ii) and len(jj) else None
            if tag == "":
                ok_cell = at is not None and at[0] == at[1]
                ok_all = same.all()
                print(f"  {d}: strongest-echo cell crop={at[0] if at else '-'} ref={at[1] if at else '-'} "
                      f"{'MATCH' if ok_cell else 'MISMATCH'} | whole crop {100.0 * same.mean():.4f} % same "
                      f"({rows}x{cols}, {int(echo.sum())} echo cells)")
                bad += (not ok_cell) + (not ok_all)
            else:
                print(f"     sabotaged twin: echo cells mismatching {mism_echo:.2f} % "
                      f"({'>= 5 %, caught' if mism_echo >= 5 else 'UNDER 5 %, NOT caught'}); "
                      f"strongest cell crop={at[0] if at else '-'} ref={at[1] if at else '-'}")
                bad += mism_echo < 5
    print("A3 " + ("as predicted" if bad == 0 else f"OFF-PREDICTION ({bad})"))
    return 0 if bad == 0 else 1


if __name__ == "__main__":
    sys.exit(main(*sys.argv[1:4]))
