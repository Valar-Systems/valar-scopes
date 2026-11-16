"""P5 1c's plants (docs/speed-p5p6.md section 1c, red sets frozen in amendment 4m before any run): each plants ONE break
into a copy of the file it names and requires test_p5_clear to turn red in exactly the rows named.

    python -I sabotage_p5_clear.py [<repo-root> <c++ compiler>]   exit 0 every plant as predicted, 1 otherwise, 2 rig

With no arguments the root is this file's own repo and the compiler is $CXX, as run.sh's passes call every driver.
A planted DirtyClear.h goes ahead of include/ on the include path; a planted test is compiled in place of the real one.
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

HEADER = "include/DirtyClear.h"
TEST = "test/host/test_p5_clear.cpp"

PLANTS = [
    # The registered one: a restore that leaves the indicator's band out.
    ("P5-INDSKIP: the band left out", [(HEADER,
      "        for (int y = ind.y0; y <= ind.y1; ++y) {   // the indicator's band, every frame\n",
      "        for (int y = ind.y0; y <= ind.y0 - 1; ++y) {   // P5-INDSKIP\n")], {"SEQ2", "IND-STOP"}),
    # The registered one: one dirty set shared by both buffers (the caller's wiring, so it is planted in SEQ2's).
    ("P5-ONEBUF: one set for both buffers", [
        (TEST, "dirtyclear::Restore(newBuf[b].p.data(), layer.p.data(), sets[b], gen, kBand)",
         "dirtyclear::Restore(newBuf[b].p.data(), layer.p.data(), sets[0], gen, kBand)"),
        (TEST, "        Tracked tn{pn, &sets[b]}, to{po, nullptr};   // SEQ2's twins: this buffer's own set\n",
         "        Tracked tn{pn, &sets[0]}, to{po, nullptr};   // P5-ONEBUF\n")], {"SEQ2"}),
    # A rebuilt layer not forcing a full restore.
    ("P5-LAYERGEN: the layer's generation ignored", [(HEADER, "    if (s.full || s.gen != layerGen) {\n",
                                                      "    if (s.full) {   // P5-LAYERGEN\n")], {"SEQ2"}),
    # A mark one column short: the triangle's right edge left out.
    ("P5-MARKSHORT: a triangle's mark one column short", [(HEADER,
      "    s.Box(Min3(x0, x1, x2), Min3(y0, y1, y2), Max3(x0, x1, x2), Max3(y0, y1, y2));\n",
      "    s.Box(Min3(x0, x1, x2), Min3(y0, y1, y2), Max3(x0, x1, x2) - 1, Max3(y0, y1, y2));   // P5-MARKSHORT\n")],
     {"MARK-COVER", "SEQ2"}),
]

ROW = re.compile(r"^\s*FAIL\s+(MARK-COVER|SEQ2|IND-STOP|CMP control)\s", re.M)


def build(tree, exe):
    test = os.path.join(tree, "test.cpp") if os.path.exists(os.path.join(tree, "test.cpp")) else \
        os.path.join(ROOT, *TEST.split("/"))
    return subprocess.run([CXX, *FLAGS, "-I" + tree, "-I" + os.path.join(ROOT, "include"),
                           "-I" + os.path.join(ROOT, "test", "host"), test, "-o", exe], capture_output=True, text=True)


def main():
    tmp = tempfile.mkdtemp(prefix="p5c-sab-")
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
        bad = 0
        for i, (label, edits, want) in enumerate(PLANTS):
            tree = os.path.join(tmp, f"p{i}")
            os.makedirs(tree)
            planted = {}
            for rel, old, new in edits:
                text = planted.get(rel) or open(os.path.join(ROOT, *rel.split("/")), encoding="utf-8", newline="").read()
                if text.count(old) != 1:
                    print(f"  RIG {label}: the anchor occurs {text.count(old)} times in {rel}"); return 2
                after = text.replace(old, new)
                if after == text:
                    print(f"  RIG {label}: the plant did not apply"); return 2
                planted[rel] = after
            for rel, text in planted.items():
                name = "DirtyClear.h" if rel == HEADER else "test.cpp"
                open(os.path.join(tree, name), "w", encoding="utf-8", newline="").write(text)
            pexe = os.path.join(tmp, f"t{i + 1}.exe")
            c = build(tree, pexe)
            if c.returncode != 0:
                print(f"  RIG {label}: the planted test did not compile\n{c.stderr[-900:]}"); return 2
            r = subprocess.run([pexe], capture_output=True, text=True)
            got = set(ROW.findall(r.stdout))
            ok = r.returncode == 1 and got == want
            print(f"  {'ok  ' if ok else 'FAIL'} {label}: red {sorted(got)} (want {sorted(want)}), exit {r.returncode}")
            bad += 0 if ok else 1
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    print("P5 CLEAR PLANTS AS PREDICTED" if bad == 0 else f"P5 CLEAR PLANTS: {bad} NOT AS PREDICTED")
    return 0 if bad == 0 else 1


def anchor_entries():
    """Every plant's anchor in the file it plants, for run.sh's --anchors pass (no plant runs)."""
    out = []
    for label, edits, _ in PLANTS:
        for rel, old, _ in edits:
            out.append((label, rel, sc.read(os.path.join(ROOT, *rel.split("/"))), old))
    return out


# --control: this driver's unplanted run alone, its suite against its own count. OK_COUNT is the one place the count
# lives (lint_driver_counts.py): main()'s unplanted run reads it too.
OK_COUNT = 4
CONTROL_TABLE = {"test_p5_clear": ("ok", OK_COUNT)}


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
