"""WD4's sabotages (docs/wx-device.md, WD4 and its addenda): each plants ONE break into a copy of include/WxS2.h and
requires the host test to turn red in exactly the checks named, no fewer and no more.

    python -I sabotage_wx_s2.py <repo-root> <c++ compiler>       exit 0 every sabotage as predicted, 1 otherwise, 2 rig

The copy lives in a temporary tree mirroring the repo's (include/WxS2.h beside src/weatherscope/WxPalette.h), because
WxS2.h includes the palette by a relative path: a copy without it would compile against nothing, or against the real
header, and prove nothing.
"""
import os
import re
import shutil
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sabotage_common as sc   # the --anchors pass (main's #506; docs/wx-device.md, "CI's anchors pass")

# run.sh's --anchors pass gives no arguments: the root is then this file's own repo.
if sc.wants_anchors():
    ROOT, CXX = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))), None
else:
    ROOT, CXX = sys.argv[1], sys.argv[2]
FLAGS = ["-std=c++11", "-Wall", "-Wextra", "-Werror", "-O1", "-static-libgcc", "-static-libstdc++"]

PLANTS = [
    ("B at A's factor", "constexpr int kPercentB = 25;", "constexpr int kPercentB = 40;",
     {"WD4b B"}),
    ("code 13 as the table's black", "    if (code >= 1 && code <= 13) return wxpalette::Dim(wxpalette::Rain(code), pct);",
     "    if (code == 13) return wxpalette::kDry;\n    if (code >= 1 && code <= 13) return wxpalette::Dim(wxpalette::Rain(code), pct);",
     {"WD4b A", "WD4b B"}),
    ("14 drawn as 0", "if (code == 14) return NoCoverageLit(x, y) ? Pack(wxpalette::kNoCoverage) : 0;",
     "if (code == 14) return 0;", {"WD4c A", "WD4c B"}),
    # The undimmed table's green and yellow ARE plane colours, so the value check and WD4d both go red.
    ("the dim dropped", "if (code >= 1 && code <= 13) return wxpalette::Dim(wxpalette::Rain(code), pct);",
     "if (code >= 1 && code <= 13) { (void)pct; return wxpalette::Rain(code); }",
     {"WD4b A", "WD4b B", "WD4d A", "WD4d B"}),
    # WD7. S-t1: light rain at the look's plain percent (no x 55/40). S-t2: light rain from the table's old blue.
    # Both are dimmer than code 2, so WD4e stays green and only the value check goes red.
    ("S-t1 code 1 at the plain percent", "return code == 1 ? (pct * kCode1Num + kCode1Den / 2) / kCode1Den : pct;",
     "(void)code; return pct;", {"WD4b A", "WD4b B"}),
    ("S-t2 code 1 from the old blue", "    if (code >= 1 && code <= 13) return wxpalette::Dim(wxpalette::Rain(code), pct);",
     "    if (code == 1) return wxpalette::Dim(wxpalette::kRainTable[0], pct);\n"
     "    if (code >= 1 && code <= 13) return wxpalette::Dim(wxpalette::Rain(code), pct);",
     {"WD4b A", "WD4b B"}),
]


def red(output):
    look, out = None, set()
    for line in output.splitlines():
        m = re.match(r"== look ([AB]) ", line)
        if m:
            look = m.group(1)
        m = re.match(r"\s*FAIL\s+(WD4[a-f])", line)
        if m:
            out.add(f"{m.group(1)} {look}")
    return out


def main():
    src = open(os.path.join(ROOT, "include", "WxS2.h"), encoding="utf-8").read()
    pal = os.path.join(ROOT, "src", "weatherscope", "WxPalette.h")
    test = os.path.join(ROOT, "test", "host", "test_wx_s2.cpp")
    bad = 0
    for label, old, new, want in PLANTS:
        if src.count(old) != 1:
            print(f"  RIG {label}: the anchor occurs {src.count(old)} times"); return 2
        tmp = tempfile.mkdtemp(prefix="wxs2-sab-")
        try:
            os.makedirs(os.path.join(tmp, "include"))
            os.makedirs(os.path.join(tmp, "src", "weatherscope"))
            shutil.copyfile(pal, os.path.join(tmp, "src", "weatherscope", "WxPalette.h"))
            planted = src.replace(old, new)
            open(os.path.join(tmp, "include", "WxS2.h"), "w", encoding="utf-8").write(planted)
            assert open(os.path.join(tmp, "include", "WxS2.h"), encoding="utf-8").read() != src, "the plant did not apply"
            exe = os.path.join(tmp, "t.exe")
            c = subprocess.run([CXX, *FLAGS, "-I" + os.path.join(tmp, "include"), test, "-o", exe], capture_output=True, text=True)
            if c.returncode != 0:
                print(f"  RIG {label}: did not compile\n{c.stderr[-600:]}"); return 2
            r = subprocess.run([exe, ROOT], capture_output=True, text=True)
            got = red(r.stdout)
            ok = r.returncode == 1 and got == want
            print(f"  {'ok  ' if ok else 'FAIL'} {label}: red {sorted(got)} (want {sorted(want)}), exit {r.returncode}")
            bad += 0 if ok else 1
        finally:
            shutil.rmtree(tmp, ignore_errors=True)
    print("WX S2 SABOTAGES AS PREDICTED" if bad == 0 else f"WX S2 SABOTAGES: {bad} NOT AS PREDICTED")
    return 0 if bad == 0 else 1


def anchor_entries():
    """Every plant's anchor in the file it plants, for run.sh's --anchors pass (no plant runs)."""
    src = sc.read(os.path.join(ROOT, "include", "WxS2.h"))
    return [(label, "include/WxS2.h", src, old) for label, old, _, _ in PLANTS]


if __name__ == "__main__":
    if sc.wants_anchors():
        sys.exit(sc.anchors_mode(os.path.basename(__file__), anchor_entries()))
    sys.exit(main())
