"""P6's plants (docs/speed-p5p6.md section 2, restated by amendment 4g before any run): each plants ONE break into a
copy of the file it names and requires test_p6_labels to turn red in exactly the rows named, no fewer and no more.

    python -I sabotage_p6_labels.py [<repo-root> <c++ compiler>]   exit 0 every plant as predicted, 1 otherwise, 2 rig

With no arguments the root is this file's own repo and the compiler is $CXX, as run.sh's passes call every driver.
The build is run.sh's: src/AircraftInfoFields.{h,cpp} staged beside test/host/p6_fake/TrackedAircraft.h as
models/TrackedAircraft.h, the P6 String shim first on the include path; a planted LabelCache.h goes ahead of include/.
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

CACHE = "include/LabelCache.h"
FIELDS = "src/AircraftInfoFields.cpp"
TEST = "test/host/test_p6_labels.cpp"

SEQ = {"P6-SEQ@20", "P6-SEQ@40", "P6-SEQ@60"}
SEQH = {"P6-SEQH@20", "P6-SEQH@40", "P6-SEQH@60"}   # amendment 4i: the inputs held across frames
HIT = "    if (e.valid && e.enabled == mask && e.key == k) {\n"
HIT_COUNTED = HIT + "        if (st) ++st->hits;\n"

PLANTS = [
    # The cache never invalidated: a valid entry is a hit whatever changed.
    ("P6-NEVER: never invalidated", [(CACHE, HIT, "    if (e.valid) {\n")], SEQ | SEQH),
    # A stale width kept across a text change: each line measured once, the first time.
    ("P6-WIDTH: a stale width", [(CACHE, "        e.width[j] = (int16_t)f.Width(text);\n",
                                  "        if (e.width[j] == 0) e.width[j] = (int16_t)f.Width(text);\n")], SEQ | SEQH),
    # The placement cached with the strings: the first frame's (x, y) reused while the inputs hold.
    ("P6-POS: the placement cached", [
        (CACHE, "    char text[kLines][kTextBytes] = {};\n};\n",
         "    char text[kLines][kTextBytes] = {};\n    int16_t px = 0, py = 0;\n    bool placed = false;\n};\n"),
        (CACHE, "    const int lineHeight = f.LineHeight();\n    int placed = 0;\n    for (int k = 0; k < e.n; ++k) {\n",
         "    if (!e.placed) { Entry& m = const_cast<Entry&>(e); m.px = (int16_t)x; m.py = (int16_t)y; m.placed = true; }\n"
         "    x = e.px;\n    y = e.py;\n"
         "    const int lineHeight = f.LineHeight();\n    int placed = 0;\n    for (int k = 0; k < e.n; ++k) {\n")],
     SEQ | SEQH),
    # An input dropped from the key, beside the formatters: the squawk. SEQ moves a float every frame, so its key never
    # holds and it cannot see this (4h's finding); SEQH changes one input at a time, so it can (4i).
    ("P6-KEYDROP: the squawk out of LabelKey", [(FIELDS, "    k.Str(t.state.squawk.c_str());\n", "")],
     {"P6-COVER"} | SEQH),
    # No sharing: every Refresh refills, so the draw never finds the box's entry (correct, and slow). SEQH's control
    # requires keys held across frames, so it reads red too: with no hits, SEQH has exercised nothing it exists for.
    ("P6-NOSHARE: every lookup refills", [(CACHE, HIT, "    if (false) {\n")], {"P6-SHARE"} | SEQH),
    # The orchestrator's (4i): a hit returns the PREVIOUS fill's widths. SEQ's draw hits within each frame, so SEQ reads
    # it too.
    ("P6-HITWIDTH: a hit returns the previous fill's widths", [
        (CACHE, "    int16_t width[kLines] = {};\n", "    int16_t width[kLines] = {};\n    int16_t prev[kLines] = {};\n"),
        (CACHE, "        e.width[j] = (int16_t)f.Width(text);\n",
         "        e.prev[j] = e.width[j];\n        e.width[j] = (int16_t)f.Width(text);\n"),
        (CACHE, HIT_COUNTED, HIT_COUNTED + "        for (int j = 0; j < e.n; ++j) e.width[j] = e.prev[j];\n")],
     SEQ | SEQH),
    # Only a key held across frames can show this one: from the second CONSECUTIVE hit on, the widths drift. SEQ never
    # hits twice in a row (each frame's box refills), so it must stay green.
    ("P6-HELDHIT: widths drift from the second consecutive hit", [
        (CACHE, "    int16_t width[kLines] = {};\n", "    int16_t width[kLines] = {};\n    uint8_t run = 0;\n"),
        (CACHE, "    e.valid = true;\n", "    e.valid = true;\n    e.run = 0;\n"),
        (CACHE, HIT_COUNTED,
         HIT_COUNTED + "        if (++e.run >= 2) for (int j = 0; j < e.n; ++j) e.width[j] = (int16_t)(e.width[j] + 1);\n")],
     SEQH),
]

ROW = re.compile(r"^\s*FAIL\s+(P6-[A-Z]+(?:@\d+)?|CMP control)\s", re.M)


def stage(tree, planted=None):
    """run.sh's staging, in `tree`; `planted` maps a repo path to replacement text for it."""
    planted = planted or {}
    src, inc = os.path.join(tree, "src"), os.path.join(tree, "inc")
    os.makedirs(os.path.join(src, "models"), exist_ok=True)
    os.makedirs(inc, exist_ok=True)
    for name in ("AircraftInfoFields.cpp", "AircraftInfoFields.h"):
        rel = "src/" + name
        dst = os.path.join(src, name)
        if rel in planted:
            open(dst, "w", encoding="utf-8", newline="").write(planted[rel])
        else:
            shutil.copyfile(os.path.join(ROOT, "src", name), dst)
    shutil.copyfile(os.path.join(ROOT, "test", "host", "p6_fake", "TrackedAircraft.h"),
                    os.path.join(src, "models", "TrackedAircraft.h"))
    if CACHE in planted:
        open(os.path.join(inc, "LabelCache.h"), "w", encoding="utf-8", newline="").write(planted[CACHE])
    return src, inc


def build(tree, exe, planted=None):
    src, inc = stage(tree, planted)
    return subprocess.run([CXX, *FLAGS, "-I" + os.path.join(ROOT, "test", "host", "p6_shim"), "-I" + inc,
                           "-I" + os.path.join(ROOT, "include"), "-I" + src, os.path.join(ROOT, *TEST.split("/")),
                           os.path.join(src, "AircraftInfoFields.cpp"), "-o", exe], capture_output=True, text=True)


def main():
    tmp = tempfile.mkdtemp(prefix="p6l-sab-")
    try:
        exe = os.path.join(tmp, "t0.exe")
        c = build(os.path.join(tmp, "clean"), exe)
        if c.returncode != 0:
            print(f"  RIG: the test did not compile\n{c.stderr[-900:]}"); return 2
        r0 = subprocess.run([exe], capture_output=True, text=True)
        if r0.returncode != 0 or len(re.findall(r"^\s*ok\b", r0.stdout, re.M)) != OK_COUNT:
            print(f"  RIG: the unplanted test did not pass with {OK_COUNT} ok lines\n{r0.stdout[-900:]}"); return 2
        bad = 0
        for i, (label, edits, want) in enumerate(PLANTS):
            planted = {}
            for rel, old, new in edits:
                text = planted.get(rel) or open(os.path.join(ROOT, *rel.split("/")), encoding="utf-8", newline="").read()
                if text.count(old) != 1:
                    print(f"  RIG {label}: the anchor occurs {text.count(old)} times in {rel}"); return 2
                after = text.replace(old, new)
                if after == text:
                    print(f"  RIG {label}: the plant did not apply"); return 2
                planted[rel] = after
            pexe = os.path.join(tmp, f"t{i + 1}.exe")
            c = build(os.path.join(tmp, f"p{i}"), pexe, planted)
            if c.returncode != 0:
                print(f"  RIG {label}: the planted test did not compile\n{c.stderr[-900:]}"); return 2
            r = subprocess.run([pexe], capture_output=True, text=True)
            got = set(ROW.findall(r.stdout))
            ok = r.returncode == 1 and got == want
            print(f"  {'ok  ' if ok else 'FAIL'} {label}: red {sorted(got)} (want {sorted(want)}), exit {r.returncode}")
            bad += 0 if ok else 1
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    print("P6 LABEL PLANTS AS PREDICTED" if bad == 0 else f"P6 LABEL PLANTS: {bad} NOT AS PREDICTED")
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
OK_COUNT = 9
CONTROL_TABLE = {"test_p6_labels": ("ok", OK_COUNT)}


def control():
    with tempfile.TemporaryDirectory() as td:
        exe = os.path.join(td, "t.exe")
        build(os.path.join(td, "clean"), exe)
        subprocess.run([exe], capture_output=True, text=True)


if __name__ == "__main__":
    if sc.wants_control():
        sys.exit(sc.control_mode(os.path.basename(__file__), control, CONTROL_TABLE))
    if sc.wants_anchors():
        sys.exit(sc.anchors_mode(os.path.basename(__file__), anchor_entries()))
    sys.exit(main())
