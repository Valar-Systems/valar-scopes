#!/usr/bin/env python3
"""Red first for anchor_compare (board addendum 2, docs/speed-p5p6.md section 7): each plant breaks a copy of one file
and requires `anchor_compare --selftest` to turn red in EXACTLY the rows named (no fewer, no more). The sets below are
the predictions, frozen before the first run.

    sabotage_anchor.py              every plant; exit 0 all as predicted, 1 a plant read otherwise, 2 the rig
    sabotage_anchor.py --control    the real files: every row ok, exit 0
    sabotage_anchor.py --anchors    every plant's needle present exactly once in the real file

CXX (env) is the compiler; the default is g++.
"""
import os
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
CXX = os.environ.get("CXX") or "g++"
FLAGS = ["-std=c++11", "-Wall", "-Wextra", "-Werror", "-O1"]

CORE = "tools/speed-p5p6/anchor_core.h"
MAIN = "tools/speed-p5p6/anchor_compare.cpp"
COPIED = [CORE, MAIN, "test/host/p5_outline_port.h", "test/host/LgfxPort.h", "include/OutlineMask.h"]
ALL = ["A-565", "A-BE", "A-SCENE", "A-SELF", "A-CMP", "A-SHIFT", "A-REGION", "A-REGION-IN"]

PLANTS = [
    # The region ignored: every pixel compared. Only the distance-3 change can tell. ((void) keeps -Werror's
    # unused-parameter quiet: the first run's RIG exit 2, 01:40Z.)
    ("AP-WHOLE: the whole frame compared", CORE,
     "        if (!region[i]) continue;\n        ++r.region;\n",
     "        (void)region;\n        ++r.region;\n",
     {"A-REGION"}),
    # The region one pixel narrower than registered.
    ("AP-DIL1: kDilate 1", CORE,
     "constexpr int kDilate = 2;",
     "constexpr int kDilate = 1;",
     {"A-REGION-IN"}),
    # A comparison that never counts.
    ("AP-BLIND: no pixel ever differs", CORE,
     "        if (host.p[i] != dev.p[i]) ++r.mismatch;\n",
     "        (void)host; (void)dev;\n",
     {"A-CMP", "A-SHIFT", "A-REGION-IN"}),
    # RenderOld used as it stands: the port's own background painted over the grab before the contacts.
    ("AP-REPAINT: Composite repaints Img::Background()", CORE,
     "    std::memcpy(out->p, bg.p, sizeof out->p);\n",
     "    std::memcpy(out->p, bg.p, sizeof out->p);\n    out->Background();\n",
     {"A-SELF", "A-CMP", "A-REGION", "A-REGION-IN"}),
    # Green's low bit dropped.
    ("AP-565: a wrong green mask", CORE,
     "((c888 >> 5) & 0x07E0u)",
     "((c888 >> 5) & 0x07C0u)",
     {"A-565"}),
    # The grab read little-endian.
    ("AP-LE: the body decoded little-endian", CORE,
     "(uint16_t)((body[2 * i] << 8) | body[2 * i + 1])",
     "(uint16_t)((body[2 * i + 1] << 8) | body[2 * i])",
     {"A-BE"}),
]

ROW = re.compile(r"^\s*(ok|FAIL)\s+(A-[A-Z0-9-]+)\s", re.M)


def copy_tree(dst):
    for rel in COPIED:
        os.makedirs(os.path.dirname(os.path.join(dst, *rel.split("/"))), exist_ok=True)
        shutil.copyfile(os.path.join(ROOT, *rel.split("/")), os.path.join(dst, *rel.split("/")))


def run(tree, exe):
    b = subprocess.run([CXX, *FLAGS, "-I" + os.path.join(tree, "include"), "-I" + os.path.join(tree, "test", "host"),
                        os.path.join(tree, *MAIN.split("/")), "-o", exe], capture_output=True, text=True)
    if b.returncode != 0:
        return None, b.stderr.strip()[:600]
    r = subprocess.run([exe, "--selftest"], capture_output=True, text=True)
    rows = ROW.findall(r.stdout)
    return (r.returncode, rows, r.stdout), None


def read(rel):
    with open(os.path.join(ROOT, *rel.split("/")), encoding="utf-8", newline="") as f:
        return f.read()


def anchors():
    bad = 0
    for name, rel, old, _new, _want in PLANTS:
        n = read(rel).count(old)
        if n != 1:
            bad += 1
            print(f"  FAIL  {name}: its needle appears {n} times in {rel}")
    print(f"ANCHORS sabotage_anchor.py: {len(PLANTS)} plant(s), "
          + ("all present exactly once" if not bad else f"{bad} not exactly once"))
    return 1 if bad else 0


def control():
    tmp = tempfile.mkdtemp(prefix="anc-ctl-")
    try:
        copy_tree(tmp)
        out, err = run(tmp, os.path.join(tmp, "a.exe"))
        if out is None:
            print(f"RIG: the real files did not compile: {err}")
            return 2
        code, rows, _ = out
        okn = sum(1 for s, _ in rows if s == "ok")
        ids = [i for _, i in rows]
        good = code == 0 and okn == len(ALL) and ids == ALL
        print(f"  {'ok  ' if good else 'FAIL'}  anchor_compare --selftest: {okn} ok, {len(rows) - okn} FAIL, "
              f"exit {code}, rows {'as listed' if ids == ALL else ids}")
        print("CONTROL sabotage_anchor.py: " + ("all rows ok" if good else "NOT as predicted"))
        return 0 if good else 1
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def main():
    if anchors() != 0:
        return 2
    tmp = tempfile.mkdtemp(prefix="anc-sab-")
    bad = 0
    try:
        for name, rel, old, new, want in PLANTS:
            tree = os.path.join(tmp, re.sub(r"[^A-Za-z0-9]", "", name.split(":")[0]))
            copy_tree(tree)
            path = os.path.join(tree, *rel.split("/"))
            with open(path, encoding="utf-8", newline="") as f:
                before = f.read()
            assert before.count(old) == 1, name
            after = before.replace(old, new, 1)
            assert after != before, name
            with open(path, "w", encoding="utf-8", newline="") as f:
                f.write(after)
            with open(path, encoding="utf-8", newline="") as f:
                assert f.read() == after, name   # the plant is on disk before anything is measured
            out, err = run(tree, os.path.join(tree, "a.exe"))
            if out is None:
                print(f"RIG: {name} did not compile: {err}")
                return 2
            code, rows, _ = out
            red = sorted(i for s, i in rows if s == "FAIL")
            ids = [i for _, i in rows]
            good = red == sorted(want) and code == 1 and ids == ALL
            bad += not good
            print(f"  {'ok  ' if good else 'FAIL'}  {name}: red {red} (want {sorted(want)}), exit {code}"
                  + ("" if ids == ALL else f", rows {ids}"))
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    print("ANCHOR PLANTS AS PREDICTED" if not bad else f"ANCHOR PLANTS NOT AS PREDICTED ({bad})")
    return 1 if bad else 0


if __name__ == "__main__":
    if sys.argv[1:] == ["--control"]:
        sys.exit(control())
    if sys.argv[1:] == ["--anchors"]:
        sys.exit(anchors())
    sys.exit(main())
