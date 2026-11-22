"""P5 1b's plants (REG-1b-main section 4; docs/speed-p5p6.md section 1b, amendment 4k): each plants ONE break into a copy
of include/WxSweepLut.h or include/MapSweep.h and requires test_p5_blend to turn red in exactly the rows named, frozen
in the registration before any run.

    python -I sabotage_p5_blend.py [<repo-root> <c++ compiler>]   exit 0 every plant as predicted, 1 otherwise, 2 rig

With no arguments the root is this file's own repo and the compiler is $CXX, as run.sh's passes call every driver.
BOTH headers are copied into the plant's include directory, one planted and one clean, ahead of include/ on the path:
MapSweep.h includes "WxSweepLut.h" by quotes, which searches MapSweep.h's own directory first, so a planted copy of
either header alone would be compiled beside the real other one. include/WxSweep.h (the reference) is never touched.
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
MAP = "include/MapSweep.h"
HEADERS = (LUT, MAP)
TEST = "test/host/test_p5_blend.cpp"
FIXTURES = os.path.join(ROOT, "test", "fixtures", "wx")
W5C = {"W5c@A", "W5c@B", "W5c-PCT"}
W5D = {"W5d-CS3", "W5d-CS4@A", "W5d-CS4@B", "W5d-CS5", "W5d-CS6", "W5d@A", "W5d@B", "W5d-PCT"}

PLANTS = [
    # 1b's: one rounding step changed. Build's level rounded instead of truncated; it feeds v AND lvl, so both blends.
    # Mark 10 alone crosses a multiple of 4 (23.70: 23 truncated, 24 rounded), and both Green565 and Add565 read lvl >> 2.
    ("P5-ROUND: the table's level rounded", LUT, "        const uint8_t lvl = wxsweep::SliceLevel(m);\n",
     "        const float g = 1.0f - m / (float)wxsweep::kSegs;\n"
     "        const uint8_t lvl = (uint8_t)(g * g * wxsweep::kPeak + 0.5f);   // P5-ROUND\n",
     W5C | W5D),
    # 1b's: the weather blend's guard off, a code past the table masked into it instead of computed.
    ("P5-CODEGUARD: codes masked into the table", LUT, "            const uint8_t c = code(x, y);\n",
     "            const uint8_t c = (uint8_t)(code(x, y) & 15);   // P5-CODEGUARD\n", W5C),
    # 1b's: a table never rebuilt, the first frame's percent kept when the look changes. Every other row builds its own.
    ("P5-STALE: the table never rebuilt", LUT, "    if (t.pct != pct) Build(t, pct);\n",
     "    if (t.pct < 0) Build(t, pct);   // P5-STALE\n", {"W5c-PCT", "W5d-PCT"}),
    # New: the map's stored level alone rounded; v is untouched, so no weather row moves.
    ("P5-ROUND-MAP: the map's stored level rounded", LUT, "        t.lvl[m] = lvl;\n",
     "        const float gm = 1.0f - m / (float)wxsweep::kSegs;\n"
     "        t.lvl[m] = (uint8_t)(gm * gm * wxsweep::kPeak + 0.5f);   // P5-ROUND-MAP\n", W5D),
    # New: the map blend's guard off. Codes 18-29 and 34-39 mask onto drawn 2-13; only W5c's scene holds codes past 15.
    ("P5-CODEGUARD-MAP: the map's codes masked into the table", MAP, "            const uint8_t cm = code(x, y);",
     "            const uint8_t cm = (uint8_t)(code(x, y) & 15);   /* P5-CODEGUARD-MAP */",
     {"W5d@A", "W5d@B", "W5d-PCT"}),
]

ROW = re.compile(r"^\s*FAIL\s+(W5c(?:@[AB]|-PCT)|W5d(?:@[AB]|-PCT|-CS3|-CS4@[AB]|-CS5|-CS6)|CMP control)\s", re.M)


def src_of(rel):
    return open(os.path.join(ROOT, *rel.split("/")), encoding="utf-8", newline="").read()


def stage(inc, planted_rel=None, planted_text=None):
    """Both headers into `inc`: the planted one as given, the other as it is in the tree."""
    os.makedirs(inc)
    for rel in HEADERS:
        text = planted_text if rel == planted_rel else src_of(rel)
        open(os.path.join(inc, os.path.basename(rel)), "w", encoding="utf-8", newline="").write(text)


def build(inc, exe):
    return subprocess.run([CXX, *FLAGS, "-I" + inc, "-I" + os.path.join(ROOT, "include"),
                           "-I" + os.path.join(ROOT, "src", "weatherscope"),
                           os.path.join(ROOT, *TEST.split("/")), "-o", exe], capture_output=True, text=True)


def main():
    tmp = tempfile.mkdtemp(prefix="p5b-sab-")
    try:
        clean = os.path.join(tmp, "clean")
        stage(clean)
        exe = os.path.join(tmp, "t0.exe")
        c = build(clean, exe)
        if c.returncode != 0:
            print(f"  RIG: the test did not compile\n{c.stderr[-900:]}"); return 2
        r0 = subprocess.run([exe, FIXTURES], capture_output=True, text=True)
        if r0.returncode != 0 or len(re.findall(r"^\s*ok\b", r0.stdout, re.M)) != OK_COUNT:
            print(f"  RIG: the unplanted test did not pass with {OK_COUNT} ok lines\n{r0.stdout[-900:]}"); return 2
        bad = 0
        for i, (label, rel, old, new, want) in enumerate(PLANTS):
            src = src_of(rel)
            if src.count(old) != 1:
                print(f"  RIG {label}: the anchor occurs {src.count(old)} times in {rel}"); return 2
            planted = src.replace(old, new)
            if planted == src:
                print(f"  RIG {label}: the plant did not apply"); return 2
            inc = os.path.join(tmp, f"p{i}")
            stage(inc, rel, planted)
            pexe = os.path.join(tmp, f"t{i + 1}.exe")
            c = build(inc, pexe)
            if c.returncode != 0:
                print(f"  RIG {label}: the planted test did not compile\n{c.stderr[-900:]}"); return 2
            r = subprocess.run([pexe, FIXTURES], capture_output=True, text=True)
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
    return [(label, rel, sc.read(os.path.join(ROOT, *rel.split("/"))), old) for label, rel, old, _, _ in PLANTS]


# --control: this driver's unplanted run alone, its suite against its own count. OK_COUNT is the one place the count
# lives (lint_driver_counts.py): main()'s unplanted run reads it too.
OK_COUNT = 12
CONTROL_TABLE = {"test_p5_blend": ("ok", OK_COUNT)}


def control():
    with tempfile.TemporaryDirectory() as td:
        inc = os.path.join(td, "inc")
        stage(inc)
        exe = os.path.join(td, "t.exe")
        build(inc, exe)
        subprocess.run([exe, FIXTURES], capture_output=True, text=True)


if __name__ == "__main__":
    if sc.wants_control():
        sys.exit(sc.control_mode(os.path.basename(__file__), control, CONTROL_TABLE))
    if sc.wants_anchors():
        sys.exit(sc.anchors_mode(os.path.basename(__file__), anchor_entries()))
    sys.exit(main())
