"""P5 1a's plants (docs/speed-p5p6.md section 1a, red sets restated by amendment 4e before any run): each plants ONE
break into a copy of the file it names and requires test_p5_outline to turn red in exactly the rows named, no fewer
and no more.

    python -I sabotage_p5_outline.py [<repo-root> <c++ compiler>]   exit 0 every plant as predicted, 1 otherwise, 2 rig

With no arguments the root is this file's own repo and the compiler is $CXX, as run.sh's passes call every driver.
"""
import os
import re
import shutil
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sabotage_common as sc   # the --anchors and --control passes

if not (sc.wants_anchors() or sc.wants_control()) and sys.argv[1:]:
    ROOT, CXX = sys.argv[1:]
else:
    ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    CXX = os.environ.get("CXX") or os.path.expanduser("~/.platformio/packages/toolchain-gccmingw32/bin/g++.exe")
FLAGS = ["-std=c++11", "-Wall", "-Wextra", "-Werror", "-O1", "-static-libgcc", "-static-libstdc++"]

HEADER = "include/OutlineMask.h"
TEST = "test/host/test_p5_outline.cpp"
PORT = "test/host/LgfxPort.h"
AM = "src/AircraftManager.cpp"
COPIED = [HEADER, TEST, PORT, AM]

EDGE = {"H-EDGE@40", "H-EDGE@60"}
OVERLAP = {"H-OVERLAP@40", "H-OVERLAP@60"}
CULL = {"H-CULL@40", "H-CULL@60"}

PLANTS = [
    # The reviewer's: every edge in one pass before every marker (the harness's NEW loop split in two).
    ("P5-ONEPASS: all edges in one pass, then all markers", TEST,
     "        if (Culled(f, k)) continue;\n        NewEdge(c, s, f, k, st);\n        Colour(c, f, k);\n    }\n",
     "        if (Culled(f, k)) continue;\n        NewEdge(c, s, f, k, st);\n    }\n"
     "    for (const Contact& k : f.k) {\n        if (Culled(f, k)) continue;\n        Colour(c, f, k);\n    }\n",
     OVERLAP),
    # The mask's writes clipped one pixel short of LovyanGFX's 0..239 on every side.
    ("P5-NOCLIP: the edge clipped to 1..238", HEADER,
     "            c.drawFastHLine(bx + j, by + r, len, black);\n",
     "            { int sx = bx + j, sw = len; const int sy = by + r; if (sx < 1) { sw += sx - 1; sx = 1; }\n"
     "              if (sx + sw > 239) sw = 239 - sx;\n"
     "              if (sy >= 1 && sy <= 238 && sw > 0) c.drawFastHLine(sx, sy, sw, black); }\n",
     EDGE),
    # A 4-neighbour dilation: no diagonals. No non-empty pixel set has the same 4- and 8-neighbour dilation, so every
    # row that draws a marker through the mask reads red (amendment 4e: H-CULL draws its just-inside contacts).
    ("P5-DILATE4: a 4-neighbour dilation", HEADER,
     "        const uint32_t d = v | (v << 1) | (v >> 1);",
     "        const uint32_t d = v | (m[r + 1] << 1) | (m[r + 1] >> 1);",
     OVERLAP | EDGE | CULL),
    # The outline drawn for a contact the zoom culls: a call ahead of the radar loop's cull.
    ("P5-CULLSKIP: the edge drawn before the cull", AM,
     "        if (ZoomCulled(x, y)) continue;\n",
     "        if (WxOutlineNow()) DrawOutlineMask(backbuffer, x, y, tracked);   // P5-CULLSKIP\n"
     "        if (ZoomCulled(x, y)) continue;\n",
     {"H-CULL-ORDER"}),
    # The guard off: the mask even where an offset's integer vertices are not the centre's moved (amendment 4e).
    ("P5-NOGUARD: no shift check", HEADER,
     "        exact = Same(buildAt(x + kOff[k][0], y + kOff[k][1]), m0, kOff[k][0], kOff[k][1]);",
     "        exact = exact || Same(buildAt(x + kOff[k][0], y + kOff[k][1]), m0, kOff[k][0], kOff[k][1]);",
     EDGE),
]

ROW = re.compile(r"^\s*FAIL\s+(H-[A-Z-]+(?:@\d+)?|CMP control)\s", re.M)


def build(tree, exe):
    return subprocess.run([CXX, *FLAGS, "-I" + os.path.join(tree, "include"), os.path.join(tree, *TEST.split("/")),
                           "-o", exe], capture_output=True, text=True)


def copy_tree(dst):
    for rel in COPIED:
        os.makedirs(os.path.dirname(os.path.join(dst, *rel.split("/"))), exist_ok=True)
        shutil.copyfile(os.path.join(ROOT, *rel.split("/")), os.path.join(dst, *rel.split("/")))


def main():
    tmp = tempfile.mkdtemp(prefix="p5o-sab-")
    try:
        clean = os.path.join(tmp, "clean")
        copy_tree(clean)
        exe = os.path.join(tmp, "t0.exe")
        c = build(clean, exe)
        if c.returncode != 0:
            print(f"  RIG: the test did not compile\n{c.stderr[-600:]}"); return 2
        r0 = subprocess.run([exe, clean], capture_output=True, text=True)
        if r0.returncode != 0 or len(re.findall(r"^\s*ok\b", r0.stdout, re.M)) != OK_COUNT:
            print(f"  RIG: the unplanted test did not pass with {OK_COUNT} ok lines\n{r0.stdout[-900:]}"); return 2
        bad = 0
        for i, (label, rel, old, new, want) in enumerate(PLANTS):
            tree = os.path.join(tmp, f"p{i}")
            copy_tree(tree)
            path = os.path.join(tree, *rel.split("/"))
            src = open(path, encoding="utf-8", newline="").read()
            if src.count(old) != 1:
                print(f"  RIG {label}: the anchor occurs {src.count(old)} times in {rel}"); return 2
            planted = src.replace(old, new)
            open(path, "w", encoding="utf-8", newline="").write(planted)
            if open(path, encoding="utf-8", newline="").read() == src:
                print(f"  RIG {label}: the plant did not apply"); return 2
            pexe = os.path.join(tmp, f"t{i + 1}.exe")
            c = build(tree, pexe)
            if c.returncode != 0:
                print(f"  RIG {label}: the planted test did not compile\n{c.stderr[-600:]}"); return 2
            r = subprocess.run([pexe, tree], capture_output=True, text=True)
            got = set(ROW.findall(r.stdout))
            ok = r.returncode == 1 and got == want
            print(f"  {'ok  ' if ok else 'FAIL'} {label}: red {sorted(got)} (want {sorted(want)}), exit {r.returncode}")
            bad += 0 if ok else 1
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    print("P5 OUTLINE PLANTS AS PREDICTED" if bad == 0 else f"P5 OUTLINE PLANTS: {bad} NOT AS PREDICTED")
    return 0 if bad == 0 else 1


def anchor_entries():
    """Every plant's anchor in the file it plants, for run.sh's --anchors pass (no plant runs)."""
    return [(label, rel, sc.read(os.path.join(ROOT, *rel.split("/"))), old) for label, rel, old, _, _ in PLANTS]


# --control: this driver's unplanted run alone, its suite against its own count. OK_COUNT is the one place the count
# lives (lint_driver_counts.py): main()'s unplanted run reads it too.
OK_COUNT = 8
CONTROL_TABLE = {"test_p5_outline": ("ok", OK_COUNT)}


def control():
    with tempfile.TemporaryDirectory() as td:
        exe = os.path.join(td, "t.exe")
        subprocess.run([CXX, *FLAGS, "-I" + os.path.join(ROOT, "include"), os.path.join(ROOT, *TEST.split("/")),
                        "-o", exe], capture_output=True, text=True)
        subprocess.run([exe, ROOT], capture_output=True, text=True)


if __name__ == "__main__":
    if sc.wants_control():
        sys.exit(sc.control_mode(os.path.basename(__file__), control, CONTROL_TABLE))
    if sc.wants_anchors():
        sys.exit(sc.anchors_mode(os.path.basename(__file__), anchor_entries()))
    sys.exit(main())
