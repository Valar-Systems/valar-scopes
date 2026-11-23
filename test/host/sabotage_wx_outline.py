"""WD5o's sabotages (docs/wx-device.md, WD5 addendum 3): each plants ONE break into a copy of src/AircraftManager.cpp
or, since P5 W (REG-W amendment 3), of include/OutlineW.h, where the 8 offsets and the order now live, and
requires test_wx_outline to turn red in exactly the checks named, no fewer and no more.

    python -I sabotage_wx_outline.py [<repo-root> <c++ compiler>]   exit 0 every sabotage as predicted, 1 otherwise, 2 rig

With no arguments the root is this file's own repo and the compiler is $CXX, as in sabotage_wx_policy.py (run.sh's
full pass runs every driver that way; docs/wx-device.md, M2). An explicit pair is still honoured.
"""
import os
import re
import shutil
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sabotage_common as sc   # the --anchors pass (main's #506; docs/wx-device.md, "CI's anchors pass")

# run.sh's --anchors, --control and full passes give no arguments: the root is then this file's own repo.
if not (sc.wants_anchors() or sc.wants_control()) and sys.argv[1:]:
    ROOT, CXX = sys.argv[1:]
else:
    ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    CXX = os.environ.get("CXX") or os.path.expanduser("~/.platformio/packages/toolchain-gccmingw32/bin/g++.exe")
FLAGS = ["-std=c++11", "-Wall", "-Wextra", "-Werror", "-O1", "-static-libgcc", "-static-libstdc++"]

AMCPP = "src/AircraftManager.cpp"
OUTW = "include/OutlineW.h"
FILES = (AMCPP, OUTW)
DOT_OUTLINE = "else m.fillCircle(cx, cy, 3, 0u);"   # the copy, in the lambda W rasterises (REG-W)
DOT = "else backbuffer.fillCircle(x, y, 3, markerColor);"
PLANTS = [
    # The construction addendum 3 replaced: one black disc 1 px larger, centred under the dot.
    ("the old disc 1 px larger", [(AMCPP, DOT_OUTLINE, "else {}"),
                                  (AMCPP, DOT, "else { backbuffer.fillCircle(x, y, 4, 0u); backbuffer.fillCircle(x, y, 3, markerColor); }")],
     {"WD5o"}),
    # The right construction, drawn after the dot, so the black covers it: since W, the order is Draw's.
    ("the outline drawn after the dot", [(OUTW, "    Paint(m, c, black);\n    marker();\n",
                                         "    marker();\n    Paint(m, c, black);\n")],
     {"WD5o order"}),
]


def red(output):
    return {m.group(1).strip() for m in re.finditer(r"^\s*FAIL\s+(WD5o(?: order| control)?)\s", output, re.M)}


def main():
    srcs = {rel: open(os.path.join(ROOT, *rel.split("/")), encoding="utf-8", newline="").read() for rel in FILES}
    test = os.path.join(ROOT, "test", "host", "test_wx_outline.cpp")
    tmp = tempfile.mkdtemp(prefix="wxo-sab-")
    try:
        exe = os.path.join(tmp, "t.exe")
        c = subprocess.run([CXX, *FLAGS, test, "-o", exe], capture_output=True, text=True)
        if c.returncode != 0:
            print(f"  RIG: the test did not compile\n{c.stderr[-600:]}"); return 2
        r0 = subprocess.run([exe, ROOT], capture_output=True, text=True)
        if r0.returncode != 0 or len(re.findall(r"^\s*ok\b", r0.stdout, re.M)) != OK_COUNT:
            print(f"  RIG: the unplanted test did not pass with {OK_COUNT} ok lines\n{r0.stdout[-600:]}"); return 2
        os.makedirs(os.path.join(tmp, "src"))
        os.makedirs(os.path.join(tmp, "include"))
        bad = 0
        for label, edits, want in PLANTS:
            planted = dict(srcs)
            for rel, old, new in edits:
                if planted[rel].count(old) != 1:
                    print(f"  RIG {label}: the anchor occurs {planted[rel].count(old)} times in {rel}: {old}"); return 2
                planted[rel] = planted[rel].replace(old, new)
            for rel in FILES:
                open(os.path.join(tmp, *rel.split("/")), "w", encoding="utf-8", newline="").write(planted[rel])
            assert planted != srcs, "the plant did not apply"
            r = subprocess.run([exe, tmp], capture_output=True, text=True)
            got = red(r.stdout)
            ok = r.returncode == 1 and got == want
            print(f"  {'ok  ' if ok else 'FAIL'} {label}: red {sorted(got)} (want {sorted(want)}), exit {r.returncode}")
            bad += 0 if ok else 1
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    print("WX OUTLINE SABOTAGES AS PREDICTED" if bad == 0 else f"WX OUTLINE SABOTAGES: {bad} NOT AS PREDICTED")
    return 0 if bad == 0 else 1


def anchor_entries():
    """Every plant's anchor in the file it plants, for run.sh's --anchors pass (no plant runs)."""
    return [(label, rel, sc.read(os.path.join(ROOT, *rel.split("/"))), old)
            for label, edits, _ in PLANTS for rel, old, _ in edits]


# --control (#510; docs/wx-device.md, "#510's --control"): this driver's unplanted run alone, its suite against
# its own count, nothing planted. OK_COUNT is the one place the count lives: main()'s unplanted run reads it too.
OK_COUNT = 3
CONTROL_TABLE = {"test_wx_outline": ("ok", OK_COUNT)}


def control():
    with tempfile.TemporaryDirectory() as td:
        exe = os.path.join(td, "t.exe")
        subprocess.run([CXX, *FLAGS, os.path.join(ROOT, "test", "host", "test_wx_outline.cpp"), "-o", exe],
                       capture_output=True, text=True)
        subprocess.run([exe, ROOT], capture_output=True, text=True)


if __name__ == "__main__":
    if sc.wants_control():
        sys.exit(sc.control_mode(os.path.basename(__file__), control, CONTROL_TABLE))
    if sc.wants_anchors():
        sys.exit(sc.anchors_mode(os.path.basename(__file__), anchor_entries()))
    sys.exit(main())
