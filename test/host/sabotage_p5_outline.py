"""P5 W's plants (REG-W section 2, as amendment 2 corrected it before any plant ran): each plants ONE break into a copy
of include/OutlineW.h and requires test_p5_outline to turn red in exactly its graded rows.

    python -I sabotage_p5_outline.py [<repo-root> <c++ compiler>]   exit 0 every plant as predicted, 1 otherwise, 2 rig

With no arguments the root is this file's own repo and the compiler is $CXX, as run.sh's passes call every driver. The
planted copy goes ahead of include/ on the path; the real header is never edited. W-CALLS is printed for each plant as
information and is not graded (amendment 2: whether a correctness break changes the total call count cannot be
predicted from the break). The clean run grades all four rows.
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

HEADER = "include/OutlineW.h"
TEST = "test/host/test_p5_outline.cpp"
GRADED = {"W-EQ", "W-FIT", "CMP control"}

PAINT = "            c.drawFastHLine(m.x0 + s, m.y0 + r, len, color);\n"
PLANTS = [
    ("W-DROP: the (+1, +1) copy skipped",
     "            if (ox == 0 && oy == 0) continue;\n            copy(m, x + ox, y + oy);\n",
     "            if (ox == 0 && oy == 0) continue;\n            if (ox == 1 && oy == 1) continue;   // W-DROP\n"
     "            copy(m, x + ox, y + oy);\n", {"W-EQ"}),
    ("W-ROWOFF: the runs painted one row below the mask's origin", PAINT,
     "            c.drawFastHLine(m.x0 + s, m.y0 + r + 1, len, color);   // W-ROWOFF\n", {"W-EQ"}),
    ("W-RUNSPLIT: each run painted one pixel short", PAINT,
     "            c.drawFastHLine(m.x0 + s, m.y0 + r, len - 1, color);   // W-RUNSPLIT\n", {"W-EQ"}),
    ("W-ORDER: the marker drawn before the runs", "    Paint(m, c, black);\n    marker();\n",
     "    marker();   // W-ORDER\n    Paint(m, c, black);\n", {"W-EQ"}),
    ("W-SMALLMASK: a 16-row mask", "constexpr int kRows = 32;", "constexpr int kRows = 16;   /* W-SMALLMASK */",
     {"W-FIT", "W-EQ"}),
]

ROW = re.compile(r"^\s*FAIL\s+(W-EQ|W-FIT|W-CALLS|CMP control)\s", re.M)


def src():
    return open(os.path.join(ROOT, *HEADER.split("/")), encoding="utf-8", newline="").read()


def build(inc, exe):
    return subprocess.run([CXX, *FLAGS, "-I" + inc, "-I" + os.path.join(ROOT, "include"),
                           os.path.join(ROOT, *TEST.split("/")), "-o", exe], capture_output=True, text=True)


def stage(inc, text):
    os.makedirs(inc)
    open(os.path.join(inc, "OutlineW.h"), "w", encoding="utf-8", newline="").write(text)


def main():
    tmp = tempfile.mkdtemp(prefix="p5w-sab-")
    try:
        clean = os.path.join(tmp, "clean")
        stage(clean, src())
        exe = os.path.join(tmp, "t0.exe")
        c = build(clean, exe)
        if c.returncode != 0:
            print(f"  RIG: the test did not compile\n{c.stderr[-900:]}"); return 2
        r0 = subprocess.run([exe], capture_output=True, text=True)
        if r0.returncode != 0 or len(re.findall(r"^\s*ok\b", r0.stdout, re.M)) != OK_COUNT:
            print(f"  RIG: the unplanted test did not pass with {OK_COUNT} ok lines\n{r0.stdout[-900:]}"); return 2
        text = src()
        bad = 0
        for i, (label, old, new, want) in enumerate(PLANTS):
            if text.count(old) != 1:
                print(f"  RIG {label}: the anchor occurs {text.count(old)} times in {HEADER}"); return 2
            planted = text.replace(old, new)
            if planted == text:
                print(f"  RIG {label}: the plant did not apply"); return 2
            inc = os.path.join(tmp, f"p{i}")
            stage(inc, planted)
            pexe = os.path.join(tmp, f"t{i + 1}.exe")
            c = build(inc, pexe)
            if c.returncode != 0:
                print(f"  RIG {label}: the planted test did not compile\n{c.stderr[-900:]}"); return 2
            r = subprocess.run([pexe], capture_output=True, text=True)
            red = set(ROW.findall(r.stdout))
            got = red & GRADED
            ok = r.returncode == 1 and got == want
            calls = "red" if "W-CALLS" in red else "ok"
            print(f"  {'ok  ' if ok else 'FAIL'} {label}: red {sorted(got)} (want {sorted(want)}), exit {r.returncode};"
                  f" W-CALLS {calls} (information)")
            bad += 0 if ok else 1
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    print("P5 OUTLINE PLANTS AS PREDICTED" if bad == 0 else f"P5 OUTLINE PLANTS: {bad} NOT AS PREDICTED")
    return 0 if bad == 0 else 1


def anchor_entries():
    """Every plant's anchor in the file it plants, for run.sh's --anchors pass (no plant runs)."""
    text = sc.read(os.path.join(ROOT, *HEADER.split("/")))
    return [(label, HEADER, text, old) for label, old, _, _ in PLANTS]


# --control: this driver's unplanted run alone, its suite against its own count. OK_COUNT is the one place the count
# lives (lint_driver_counts.py): main()'s unplanted run reads it too.
OK_COUNT = 4
CONTROL_TABLE = {"test_p5_outline": ("ok", OK_COUNT)}


def control():
    with tempfile.TemporaryDirectory() as td:
        inc = os.path.join(td, "inc")
        stage(inc, src())
        exe = os.path.join(td, "t.exe")
        build(inc, exe)
        subprocess.run([exe], capture_output=True, text=True)


if __name__ == "__main__":
    if sc.wants_control():
        sys.exit(sc.control_mode(os.path.basename(__file__), control, CONTROL_TABLE))
    if sc.wants_anchors():
        sys.exit(sc.anchors_mode(os.path.basename(__file__), anchor_entries()))
    sys.exit(main())
