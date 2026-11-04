"""R1's C, clutter (docs/uk-data.md A1.3, with the details pinned in A3.12), on the collection's native 1 km window.

    python -I r1_c.py selftest                      synthetic frames with known answers; the control and the sabotage
    python -I r1_c.py read <collection_dir> <out.json>

An echo is a cell >= 15 dBZ. Isolated: its 11 x 11 neighbourhood (clipped at the window's edge) holds <= 3 echoes,
itself included. Persistent: isolated in >= 0.10 x N of the week's N held frames. The mask at t: quality < t is not an
echo, and the masked run recomputes isolation. Prints counts only, never a position.
"""
import glob
import json
import os
import re
import sys
import zlib
from datetime import datetime, timezone

import numpy as np

W0 = datetime(2026, 10, 28, tzinfo=timezone.utc).timestamp()
W1 = datetime(2026, 11, 4, 16, tzinfo=timezone.utc).timestamp()
THRESH = [None, 0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8, 0.9]   # None = unmasked
NODATA = -9999000.0
R = 5   # 11 x 11


def decode(coll, stamp):
    """The stored window's value and quality bands, as decode_check.py lays the tiles out."""
    meta = json.load(open(os.path.join(coll, "frames", stamp + ".json")))
    raw = open(os.path.join(coll, "frames", stamp + ".tiles"), "rb").read()
    if meta["compression"] != [8] or meta["samples"] != [2]:
        raise SystemExit(f"{stamp}: unexpected layout")
    tw, tl = meta["tile"]
    tx = meta["tiles_x"]
    r0, r1, c0, c1 = meta["window"]
    val = np.full((r1 - r0 + 1, c1 - c0 + 1), np.nan, dtype="<f4")
    qi = np.zeros_like(val)
    pos = 0
    for i, n in zip(meta["idx"], meta["bytecounts"]):
        a = np.frombuffer(zlib.decompress(raw[pos:pos + n]), dtype="<f4").reshape(tl, tw, 2)
        pos += n
        ty, txi = divmod(i, tx)
        rs, re_ = max(r0, ty * tl), min(r1, ty * tl + tl - 1)
        cs, ce = max(c0, txi * tw), min(c1, txi * tw + tw - 1)
        if rs > re_ or cs > ce:
            continue
        val[rs - r0:re_ - r0 + 1, cs - c0:ce - c0 + 1] = a[rs - ty * tl:re_ - ty * tl + 1, cs - txi * tw:ce - txi * tw + 1, 0]
        qi[rs - r0:re_ - r0 + 1, cs - c0:ce - c0 + 1] = a[rs - ty * tl:re_ - ty * tl + 1, cs - txi * tw:ce - txi * tw + 1, 1]
    if pos != len(raw):
        raise SystemExit(f"{stamp}: tile bytes {pos} != file {len(raw)}")
    return val, qi, tuple(meta["window"])


def boxcount(e):
    """Echoes in each cell's 11 x 11 neighbourhood, clipped at the edge (zero padding counts nothing)."""
    p = np.pad(e.astype(np.int32), R + 1)
    s = p.cumsum(0).cumsum(1)
    h, w = e.shape
    k = 2 * R + 1
    return s[k:k + h, k:k + w] - s[0:h, k:k + w] - s[k:k + h, 0:w] + s[0:h, 0:w]


def isolated(e):
    return e & (boxcount(e) <= 3)


class Acc:
    def __init__(self, shape):
        self.n = 0
        self.iso = {t: np.zeros(shape, np.uint16) for t in THRESH}
        self.c3num = {t: 0 for t in THRESH}
        self.c3den = 0
        self.ge65 = 0
        self.ge65m = 0
        self.plant = {q: {t: np.zeros(shape, np.uint16) for t in (None, 0.5)} for q in (1.0, 0.2)}

    def add(self, v, q, plant_cell=None):
        self.n += 1
        echo = v >= 15                                   # NaN and NODATA are not echoes
        iso_u = None
        for t in THRESH:
            e = echo if t is None else echo & (q >= t)
            iso = isolated(e)
            self.iso[t] += iso
            if t is None:
                iso_u = iso
        nonis = echo & ~iso_u
        self.c3den += int(nonis.sum())
        for t in THRESH[1:]:
            self.c3num[t] += int((nonis & (q < t)).sum())
        self.ge65 += int((v >= 65).sum())
        self.ge65m += int(((v >= 65) & (q >= 0.5)).sum())
        if plant_cell is not None:
            r, c = plant_cell
            for pq in (1.0, 0.2):
                vv, qq = v.copy(), q.copy()
                vv[r, c], qq[r, c] = 40.0, pq
                ee = vv >= 15
                self.plant[pq][None] += isolated(ee)
                self.plant[pq][0.5] += isolated(ee & (qq >= 0.5))

    def persistent(self, arr):
        return int((arr >= 0.10 * self.n).sum())

    def rows(self, mask_on=True):
        c1 = self.persistent(self.iso[None])
        c2 = self.persistent(self.iso[0.5] if mask_on else self.iso[None])
        out = {"N": self.n, "C1": c1, "C2": c2, "C2_share_of_C1": (c2 / c1) if c1 else None,
               "C3": self.c3num[0.5] / self.c3den if self.c3den else None,
               "C4": {"ge65_unmasked": self.ge65, "ge65_masked": self.ge65m,
                      "by_threshold": {str(t): {"C2": self.persistent(self.iso[t]),
                                                "C3": self.c3num[t] / self.c3den if self.c3den else None} for t in THRESH[1:]}}}
        out["control"] = {str(pq): {"C1": self.persistent(self.plant[pq][None]), "C2": self.persistent(self.plant[pq][0.5])}
                          for pq in (1.0, 0.2)}
        return out


def grade(r):
    c1, c2, c3 = r["C1"], r["C2"], r["C3"]
    ctrl = r["control"]
    control_ok = (ctrl["1.0"]["C1"] == c1 + 1 and ctrl["1.0"]["C2"] == c2 + 1
                  and ctrl["0.2"]["C1"] == c1 + 1 and ctrl["0.2"]["C2"] == c2)
    return {"control": control_ok, "C1": c1 >= 1, "C2": (c1 > 0 and c2 <= 0.25 * c1), "C3": c3 is not None and c3 <= 0.05}


def control_cell(frames):
    """The first cell, row-major, covered in every frame and with no echo in its 11 x 11 neighbourhood in any frame."""
    any_echo = cover = None
    for v, _ in frames:
        e = v >= 15
        c = v != NODATA
        any_echo = e if any_echo is None else (any_echo | e)
        cover = c if cover is None else (cover & c)
    ok = cover & (boxcount(any_echo) == 0)
    idx = np.flatnonzero(ok)
    return None if idx.size == 0 else tuple(int(x) for x in np.unravel_index(idx[0], ok.shape))


# ---- the selftest -------------------------------------------------------------------------------------------------------

def synth_frames(n=40):
    """Known answers, by construction, on a 120 x 160 window:
      - clutter speck A at (20, 20): isolated echo, QI 0.2, in every frame         -> C1 counts it; the mask removes it
      - real speck B at (20, 60): isolated echo, QI 0.9, in every frame            -> C1 and C2 count it
      - rare speck D at (20, 100): isolated, QI 0.2, in 3 of 40 frames (< 10 %)    -> never persistent
      - a rain block 10 x 10 at rows 70-79, cols 30-39, 30 dBZ, QI 0.9, except its first row at QI 0.3, in every frame
        -> non-isolated; C3 = 10 / 100 at t = 0.5
      - the window's last 10 rows are no coverage (NODATA)."""
    out = []
    for i in range(n):
        v = np.full((120, 160), np.nan, np.float32)
        q = np.zeros_like(v)
        v[110:, :] = NODATA
        v[20, 20], q[20, 20] = 40, 0.2
        v[20, 60], q[20, 60] = 40, 0.9
        if i < 3:
            v[20, 100], q[20, 100] = 40, 0.2
        v[70:80, 30:40], q[70:80, 30:40] = 30, 0.9
        q[70, 30:40] = 0.3
        out.append((v, q))
    return out


def selftest():
    fails = []
    def check(ok, what):
        print(("ok    " if ok else "FAIL  ") + what)
        if not ok:
            fails.append(what)

    fr = synth_frames()
    cell = control_cell(fr)
    check(cell is not None, f"the control's cell is found ({'found' if cell else 'none'})")
    acc = Acc(fr[0][0].shape)
    for v, q in fr:
        acc.add(v, q, cell)
    r = acc.rows()
    check((r["C1"], r["C2"]) == (2, 1), f"synthetic: C1 {r['C1']} (2: A and B), C2 {r['C2']} (1: B)")
    check(abs(r["C3"] - 0.10) < 1e-12, f"synthetic: C3 {r['C3']:.4f} (0.10: one row of ten at QI 0.3)")
    g = grade(r)
    check(g["control"], f"control: QI 1.0 gives C1 {r['control']['1.0']['C1']}, C2 {r['control']['1.0']['C2']}; "
                        f"QI 0.2 gives C1 {r['control']['0.2']['C1']}, C2 {r['control']['0.2']['C2']} (3, 2; 3, 1)")
    rs = acc.rows(mask_on=False)
    check(rs["C2"] == rs["C1"] and not grade(rs)["C2"], f"SABOTAGE mask off in C2's run: C2 {rs['C2']} = C1 {rs['C1']} (red)")
    # the decoder round-trips a known window through the collection's tile layout
    import tempfile
    with tempfile.TemporaryDirectory() as td:
        os.makedirs(os.path.join(td, "frames"))
        tw = tl = 8
        tx, win = 3, [5, 20, 3, 17]
        full = np.random.default_rng(1).normal(20, 10, (24, 24, 2)).astype("<f4")
        idx, bcs, blob = [], [], b""
        for ty in range(win[0] // tl, win[1] // tl + 1):
            for t in range(win[2] // tw, win[3] // tw + 1):
                z = zlib.compress(full[ty * tl:(ty + 1) * tl, t * tw:(t + 1) * tw, :].tobytes())
                idx.append(ty * tx + t); bcs.append(len(z)); blob += z
        json.dump({"compression": [8], "samples": [2], "tile": [tw, tl], "tiles_x": tx, "window": win, "idx": idx,
                   "bytecounts": bcs}, open(os.path.join(td, "frames", "x.json"), "w"))
        open(os.path.join(td, "frames", "x.tiles"), "wb").write(blob)
        v, q, _ = decode(td, "x")
        check(np.array_equal(v, full[5:21, 3:18, 0]) and np.array_equal(q, full[5:21, 3:18, 1]),
              "the decoder round-trips a synthetic window through the tile layout")
    print("SELFTEST " + ("PASSED" if not fails else f"FAILED ({len(fails)})"))
    return 0 if not fails else 1


# ---- read the week ------------------------------------------------------------------------------------------------------

def week_stamps(coll):
    out = []
    for f in sorted(glob.glob(os.path.join(coll, "frames", "*.json"))):
        s = os.path.basename(f)[:-5]
        m = re.fullmatch(r"(\d{8})T(\d{4})", s)
        if not m or not os.path.exists(f[:-5] + ".tiles"):
            continue
        t = datetime(int(s[:4]), int(s[4:6]), int(s[6:8]), int(s[9:11]), int(s[11:13]), tzinfo=timezone.utc).timestamp()
        if W0 <= t <= W1:
            out.append(s)
    return out


def read(coll):
    stamps = week_stamps(coll)
    windows = set()
    def frames():
        for s in stamps:
            v, q, w = decode(coll, s)
            windows.add(w)
            yield v, q
    cell = control_cell(frames())
    if len(windows) != 1:
        return {"STOP": f"{len(windows)} different windows among the frames"}
    if cell is None:
        return {"BLIND": "no cell qualifies for the control"}
    acc = None
    for v, q in frames():
        acc = acc or Acc(v.shape)
        acc.add(v, q, cell)
    r = acc.rows()
    r["grade"] = grade(r)
    r["sabotage_mask_off"] = {"C2": acc.rows(mask_on=False)["C2"], "red": not grade(acc.rows(mask_on=False))["C2"]}
    r["frames"] = len(stamps)
    return r


if __name__ == "__main__":
    if sys.argv[1] == "selftest":
        sys.exit(selftest())
    r = read(sys.argv[2])
    json.dump(r, open(sys.argv[3], "w"), indent=1)
    print(json.dumps({k: r[k] for k in r if k != "C4"}, indent=1))
