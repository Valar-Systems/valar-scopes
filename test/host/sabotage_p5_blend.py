"""P5 1b's plants (docs/speed-p5p6.md section 1b, red sets frozen in amendment 4k before any run): each plants ONE break
into a copy of include/WxSweepLut.h and requires test_p5_blend to turn red in exactly the rows named.

    python -I sabotage_p5_blend.py [<repo-root> <c++ compiler>]   exit 0 every plant as predicted, 1 otherwise, 2 rig

With no arguments the root is this file's own repo and the compiler is $CXX, as run.sh's passes call every driver.
The planted copy goes ahead of include/ on the include path; include/WxSweep.h (the reference) is never touched.
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

LUT = "include/WxSweepLut.h"
TEST = "test/host/test_p5_blend.cpp"
LOOKS = {"W5c@A", "W5c@B"}

PLANTS = [
    # The registered one: one rounding step changed. The table's level rounded instead of truncated.
    ("P5-ROUND: the table's level rounded", "        const uint8_t lvl = wxsweep::SliceLevel(m);\n",
     "        const float g = 1.0f - m / (float)wxsweep::kSegs;\n"
     "        const uint8_t lvl = (uint8_t)(g * g * wxsweep::kPeak + 0.5f);   // P5-ROUND\n",
     LOOKS | {"W5c-PCT"}),
    # The guard off: a code past the table masked into it instead of computed.
    ("P5-CODEGUARD: codes masked into the table", "            const uint8_t c = code(x, y);\n",
     "            const uint8_t c = (uint8_t)(code(x, y) & 15);   // P5-CODEGUARD\n",
     LOOKS | {"W5c-PCT"}),
    # A table never rebuilt: the first frame's percent kept when the look changes.
    ("P5-STALE: the table never rebuilt", "    if (t.pct != pct) Build(t, pct);\n",
     "    if (t.pct < 0) Build(t, pct);   // P5-STALE\n", {"W5c-PCT"}),
]

ROW = re.compile(r"^\s*FAIL\s+(W5c(?:@[AB]|-PCT)|CMP control)\s", re.M)


def build(inc, exe):
    return subprocess.run([CXX, *FLAGS, "-I" + inc, "-I" + os.path.join(ROOT, "include"),
                           os.path.join(ROOT, *TEST.split("/")), "-o", exe], capture_output=True, text=True)


def main():
    tmp = tempfile.mkdtemp(prefix="p5b-sab-")
    try:
        clean = os.path.join(tmp, "clean")
        os.makedirs(clean)
        exe = os.path.join(tmp, "t0.exe")
        c = build(clean, exe)
        if c.returncode != 0:
            print(f"  RIG: the test did not compile\n{c.stderr[-900:]}"); return 2
        r0 = subprocess.run([exe], capture_output=True, text=True)
        if r0.returncode != 0 or len(re.findall(r"^\s*ok\b", r0.stdout, re.M)) != OK_COUNT:
            print(f"  RIG: the unplanted test did not pass with {OK_COUNT} ok lines\n{r0.stdout[-900:]}"); return 2
        src = open(os.path.join(ROOT, *LUT.split("/")), encoding="utf-8", newline="").read()
        bad = 0
        for i, (label, old, new, want) in enumerate(PLANTS):
            if src.count(old) != 1:
                print(f"  RIG {label}: the anchor occurs {src.count(old)} times in {LUT}"); return 2
            planted = src.replace(old, new)
            if planted == src:
                print(f"  RIG {label}: the plant did not apply"); return 2
            inc = os.path.join(tmp, f"p{i}")
            os.makedirs(inc)
            open(os.path.join(inc, "WxSweepLut.h"), "w", encoding="utf-8", newline="").write(planted)
            pexe = os.path.join(tmp, f"t{i + 1}.exe")
            c = build(inc, pexe)
            if c.returncode != 0:
                print(f"  RIG {label}: the planted test did not compile\n{c.stderr[-900:]}"); return 2
            r = subprocess.run([pexe], capture_output=True, text=True)
            got = set(ROW.findall(r.stdout))
            ok = r.returncode == 1 and got == want
            print(f"  {'ok  ' if ok else 'FAIL'} {label}: red {sorted(got)} (want {sorted(want)}), exit {r.returncode}")
            bad += 0 if ok else 1
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    print("P5 BLEND PLANTS AS PREDICTED" if bad == 0 else f"P5 BLEND PLANTS: {bad} NOT AS PREDICTED")
    return 0 if bad == 0 else 1


def anchor_entries():
    """Every plant's anchor in the file it plants, for run.sh's --anchors pass (no plant runs)."""
    text = sc.read(os.path.join(ROOT, *LUT.split("/")))
    return [(label, LUT, text, old) for label, old, _, _ in PLANTS]


# --control: this driver's unplanted run alone, its suite against its own count. OK_COUNT is the one place the count
# lives (lint_driver_counts.py): main()'s unplanted run reads it too.
OK_COUNT = 4
CONTROL_TABLE = {"test_p5_blend": ("ok", OK_COUNT)}


def control():
    with tempfile.TemporaryDirectory() as td:
        exe = os.path.join(td, "t.exe")
        build(td, exe)
        subprocess.run([exe], capture_output=True, text=True)


if __name__ == "__main__":
    if sc.wants_control():
        sys.exit(sc.control_mode(os.path.basename(__file__), control, CONTROL_TABLE))
    if sc.wants_anchors():
        sys.exit(sc.anchors_mode(os.path.basename(__file__), anchor_entries()))
    sys.exit(main())
