"""look_frames.py's sabotages (docs/weatherscope-stage3.md A12.29, frozen there; A12.30's table): each planted in memory,
each proven to have taken, each graded by the EXACT set of rows it turns red. Prints no coordinate.

    python -I tools/weatherscope/screens/sabotage_look_frames.py

  S-LF1'-IN   a code-5 cell inside the 60 mi disc of F5's crop     -> red exactly {LF1' F5}
  S-LF1'-OUT  a code-5 cell outside the disc of the same crop      -> red exactly {} (LF1' reads pixels, not crops)
  S-LF2       light (a)'s label role one step off in red           -> red exactly light (a)'s LF2 F0/F1/F17 and LF3 on
                                                                      all 17 frames: 20 rows, none in dark or (b) (ruled)
  S-LF3       one terrain pixel of F7 set to the code-13 role      -> red exactly {LF3 F7 dark/light-a/light-b}
  S-LF4       F3's indices copied over F4, at LF4 only             -> red exactly {LF4}
The clean run first must be all green. Exits 0 only if every sabotage reds exactly its set.
"""
import importlib.util, math, os, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.dont_write_bytecode = True
_spec = importlib.util.spec_from_file_location("look_frames", os.path.join(HERE, "look_frames.py"))
lf = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(lf)
r = lf.r


def red(rows):
    return {k for k, (g, _) in rows.items() if not g}


def cell_of_pixel(h, view, x, y):
    """render_map's cell for pixel (x, y): the index into the crop's codes."""
    lat = view.home[0] + (r.C - (y + 0.5)) / r.C * view.rLat
    lon = view.home[1] + ((x + 0.5) - r.C) / r.C * view.rLon
    row = int(math.floor((h["latNw"] - lat) / h["cellLat"] + 0.5))
    col = int(math.floor((lon - h["lonNw"]) / h["cellLon"] + 0.5))
    return row * h["cols"] + col


def main():
    ok = True
    rows, idx, pals, expect, fs = lf.run(quiet=True)
    clean = red(rows)
    ok &= not clean
    print(f"  {'ok  ' if not clean else 'FAIL'}  clean: {len(rows)} rows, red {sorted(clean)}")
    f5 = fs[5]
    view = r.View(f5["h"], 60)
    # S-LF1'-IN: the first pixel (row-major from row 60) that F5 draws in a terrain role; its cell becomes 5
    terr = {lf.IDX[f"t{i}"] for i in range(37)}
    px = next((x, y) for y in range(60, 190) for x in range(240) if idx[5][y * 240 + x] in terr)
    cell = cell_of_pixel(f5["h"], view, *px)
    rows_in, idx_in, *_ = lf.run(plant=("lf1", 5, cell), quiet=True)
    took = idx_in[5].count(bytes([lf.IDX["c5"]]))
    # S-LF1'-OUT: the crop's corner cell, outside the disc
    corner = 0
    cx, cy = view.project(f5["h"]["latNw"], f5["h"]["lonNw"])
    outside = (cx - r.C) ** 2 + (cy - r.C) ** 2 > r.C * r.C
    rows_out, idx_out, _, _, fs_out = lf.run(plant=("lf1", 5, corner), quiet=True)
    out_drawn = idx_out[5].count(bytes([lf.IDX["c5"]]))
    out_in_crop = 5 in fs_out[5]["codes"]
    # S-LF2
    lab = pals["light-a"]["label"]
    off = (lab[0] + 1, lab[1], lab[2])
    rows_2, *_ = lf.run(plant=("lf2", "light-a", "label", off), quiet=True)
    # S-LF3: F7's first terrain pixel to the code-13 role
    p3 = next(i for i, v in enumerate(idx[7]) if v in terr)
    rows_3, idx_3, *_ = lf.run(plant=("lf3", 7, p3, "c13"), quiet=True)
    # S-LF4
    rows_4, *_ = lf.run(plant=("lf4", 3, 4), quiet=True)
    cases = [
        ("S-LF1'-IN", red(rows_in), {"LF1' F5"}, took > 0, f"F5 draws {took} code-5 pixel(s)"),
        ("S-LF1'-OUT", red(rows_out), set(), outside and out_in_crop and out_drawn == 0,
         f"the corner cell is outside the disc: {outside}; the crop's codes include 5: {out_in_crop}; F5 draws {out_drawn}"),
        ("S-LF2", red(rows_2),   # restated by A12.34's ruling (A12.32 item 2): every light (a) row that draws the label
         {f"LF2 F{n} light-a" for n in (0, 1, 17)} | {f"LF3 F{n} light-a" for n in list(lf.LOOP_FRAMES) + [18, 19]},
         off != lab, f"light (a)'s label {lab} -> {off}"),
        ("S-LF3", red(rows_3), {"LF3 F7 dark", "LF3 F7 light-a", "LF3 F7 light-b"}, idx_3[7] != idx[7],
         f"one byte of F7: {sum(a != b for a, b in zip(idx_3[7], idx[7]))} differs"),
        ("S-LF4", red(rows_4), {"LF4"}, idx[3] != idx[4], f"F3 and F4 differ before the copy: {idx[3] != idx[4]}"),
    ]
    for name, got, want, planted, proof in cases:
        good = planted and got == want
        ok &= good
        print(f"  {'ok  ' if good else 'FAIL'}  {name}: plant {'took' if planted else 'DID NOT TAKE'} ({proof}); "
              f"red {sorted(got)}, expected {sorted(want)}")
    print("sabotages: " + ("every one reds exactly its set" if ok else "OFF-PREDICTION: stop and report"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
