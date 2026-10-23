"""WD5o's sabotages (docs/wx-device.md, WD5 addendum 3): each plants ONE break into a copy of src/AircraftManager.cpp
and requires test_wx_outline to turn red in exactly the checks named, no fewer and no more.

    python -I sabotage_wx_outline.py <repo-root> <c++ compiler>     exit 0 every sabotage as predicted, 1 otherwise, 2 rig
"""
import os
import re
import shutil
import subprocess
import sys
import tempfile

ROOT, CXX = sys.argv[1], sys.argv[2]
FLAGS = ["-std=c++11", "-Wall", "-Wextra", "-Werror", "-O1", "-static-libgcc", "-static-libstdc++"]

DOT_OUTLINE = "else backbuffer.fillCircle(x + ox, y + oy, 3, 0u);"
DOT = "else backbuffer.fillCircle(x, y, 3, markerColor);"
PLANTS = [
    # The construction addendum 3 replaced: one black disc 1 px larger, centred under the dot.
    ("the old disc 1 px larger", [(DOT_OUTLINE, "else {}"), (DOT, "else { backbuffer.fillCircle(x, y, 4, 0u); backbuffer.fillCircle(x, y, 3, markerColor); }")],
     {"WD5o"}),
    # The right construction, drawn after the dot, so the black covers it.
    ("the outline drawn after the dot", [(DOT, "else {}"), (DOT_OUTLINE, "else { if (ox == -1 && oy == -1) backbuffer.fillCircle(x, y, 3, markerColor); backbuffer.fillCircle(x + ox, y + oy, 3, 0u); }")],
     {"WD5o order"}),
]


def red(output):
    return {m.group(1).strip() for m in re.finditer(r"^\s*FAIL\s+(WD5o(?: order| control)?)\s", output, re.M)}


def main():
    path = os.path.join(ROOT, "src", "AircraftManager.cpp")
    src = open(path, encoding="utf-8", newline="").read()
    test = os.path.join(ROOT, "test", "host", "test_wx_outline.cpp")
    tmp = tempfile.mkdtemp(prefix="wxo-sab-")
    try:
        exe = os.path.join(tmp, "t.exe")
        c = subprocess.run([CXX, *FLAGS, test, "-o", exe], capture_output=True, text=True)
        if c.returncode != 0:
            print(f"  RIG: the test did not compile\n{c.stderr[-600:]}"); return 2
        os.makedirs(os.path.join(tmp, "src"))
        bad = 0
        for label, edits, want in PLANTS:
            planted = src
            for old, new in edits:
                if planted.count(old) != 1:
                    print(f"  RIG {label}: the anchor occurs {planted.count(old)} times: {old}"); return 2
                planted = planted.replace(old, new)
            open(os.path.join(tmp, "src", "AircraftManager.cpp"), "w", encoding="utf-8", newline="").write(planted)
            assert open(os.path.join(tmp, "src", "AircraftManager.cpp"), encoding="utf-8", newline="").read() != src, "the plant did not apply"
            r = subprocess.run([exe, tmp], capture_output=True, text=True)
            got = red(r.stdout)
            ok = r.returncode == 1 and got == want
            print(f"  {'ok  ' if ok else 'FAIL'} {label}: red {sorted(got)} (want {sorted(want)}), exit {r.returncode}")
            bad += 0 if ok else 1
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    print("WX OUTLINE SABOTAGES AS PREDICTED" if bad == 0 else f"WX OUTLINE SABOTAGES: {bad} NOT AS PREDICTED")
    return 0 if bad == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
