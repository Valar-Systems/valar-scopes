#!/usr/bin/env python3
"""Red first for the scratch self-check (board addendum 2, docs/speed-p5p6.md section 7): each plant breaks a copy of
include/OutlineScratchCheck.h and requires `scratch_table --selftest` to turn red in EXACTLY the rows named. The sets
are the predictions, frozen before the first run. They rest on S-COUNT's control (rows 0, 1, 30, 31 of the pattern
are empty, and row 2 is not a palindrome of bytes).

    sabotage_scratch.py              every plant; exit 0 all as predicted, 1 a plant read otherwise, 2 the rig
    sabotage_scratch.py --control    the real files: every row ok, exit 0
    sabotage_scratch.py --anchors    every plant's needle present exactly once in the real file

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

HDR = "include/OutlineScratchCheck.h"
MAIN = "tools/speed-p5p6/scratch_table.cpp"
COPIED = [HDR, MAIN, "include/OutlineMask.h", "test/host/LgfxPort.h"]
ALL = ["S-COUNT", "S-ROWS", "S-SELF", "S-TABLE", "S-TABLE-FIRST"]

PLANTS = [
    # A comparator that never counts.
    ("SP-NOCOUNT: Mismatches never counts", HDR,
     "            if (Byte(s, r, b) != table[4 * r + b]) ++m;\n",
     "            if (Byte(s, r, b) != table[4 * r + b]) {}\n",
     {"S-TABLE", "S-TABLE-FIRST"}),
    # A comparator that stops halfway: the last byte is never read.
    ("SP-HALF: only rows 0..15 compared", HDR,
     "    for (int r = 0; r < outlinemask::kSide; ++r)\n        for (int b = 0; b < 4; ++b)\n",
     "    for (int r = 0; r < outlinemask::kSide / 2; ++r)\n        for (int b = 0; b < 4; ++b)\n",
     {"S-TABLE"}),
    # The pattern moved after the table was generated: one line's end one row down (row 13, byte 3 changes; rows 0
    # and 31 do not), so the table is stale.
    ("SP-STALE: the pattern changed, the table not regenerated", HDR,
     "m.Add(outlinemask::kLine, 16, 2, 29, 12);",
     "m.Add(outlinemask::kLine, 16, 2, 29, 13);",
     {"S-SELF", "S-TABLE", "S-TABLE-FIRST"}),
    # Bytes reversed within each row (generator and comparator both use Byte(), but the table was generated with the
    # real order). Row 2 is not a byte palindrome, so S-SELF reads >= 1; rows 0 and 31 are empty in both orders, so
    # each flipped-bit row reads that count + 1, never 1.
    ("SP-BYTEORDER: Byte() reversed", HDR,
     "return (uint8_t)(s.Row(r) >> (8 * b));",
     "return (uint8_t)(s.Row(r) >> (8 * (3 - b)));",
     {"S-ROWS", "S-SELF", "S-TABLE", "S-TABLE-FIRST"}),
]

ROW = re.compile(r"^\s*(ok|FAIL)\s+(S-[A-Z-]+)\s", re.M)


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
    return (r.returncode, ROW.findall(r.stdout), r.stdout), None


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
    print(f"ANCHORS sabotage_scratch.py: {len(PLANTS)} plant(s), "
          + ("all present exactly once" if not bad else f"{bad} not exactly once"))
    return 1 if bad else 0


def control():
    tmp = tempfile.mkdtemp(prefix="scr-ctl-")
    try:
        copy_tree(tmp)
        out, err = run(tmp, os.path.join(tmp, "s.exe"))
        if out is None:
            print(f"RIG: the real files did not compile: {err}")
            return 2
        code, rows, _ = out
        okn = sum(1 for s, _ in rows if s == "ok")
        ids = [i for _, i in rows]
        good = code == 0 and okn == len(ALL) and ids == ALL
        print(f"  {'ok  ' if good else 'FAIL'}  scratch_table --selftest: {okn} ok, {len(rows) - okn} FAIL, exit {code}, "
              f"rows {'as listed' if ids == ALL else ids}")
        print("CONTROL sabotage_scratch.py: " + ("all rows ok" if good else "NOT as predicted"))
        return 0 if good else 1
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def main():
    if anchors() != 0:
        return 2
    tmp = tempfile.mkdtemp(prefix="scr-sab-")
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
                assert f.read() == after, name
            out, err = run(tree, os.path.join(tree, "s.exe"))
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
    print("SCRATCH PLANTS AS PREDICTED" if not bad else f"SCRATCH PLANTS NOT AS PREDICTED ({bad})")
    return 1 if bad else 0


if __name__ == "__main__":
    if sys.argv[1:] == ["--control"]:
        sys.exit(control())
    if sys.argv[1:] == ["--anchors"]:
        sys.exit(anchors())
    sys.exit(main())
