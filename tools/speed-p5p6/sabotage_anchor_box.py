#!/usr/bin/env python3
"""Red first for section 7g's excluded boxes (docs/speed-p5p6.md, R-ANCHOR's re-registration, accepted): each plant
breaks a copy of anchor_core.h and requires `anchor_compare --selftest-box` to turn red in EXACTLY the rows named. The
sets are the predictions, frozen before the first run (X-C14 is a control: red under no plant). The test's fixture
paints the FIRMWARE's boxes as literals, so a plant that moves anchor's constants cannot move the fixture with it.

    sabotage_anchor_box.py              every plant; exit 0 all as predicted, 1 a plant read otherwise, 2 the rig
    sabotage_anchor_box.py --control    the real files: every row ok, and --equal reads 0 / 1 / 2 on emitted grabs
    sabotage_anchor_box.py --anchors    every plant's needle present exactly once in the real file

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
COPIED = [CORE, "tools/speed-p5p6/anchor_file.h", MAIN, "test/host/p5_outline_port.h", "test/host/LgfxPort.h",
          "include/OutlineMask.h"]
ALL = ["X-COST", "X-C14", "X-EDGE", "X-CLOCK", "X-EQUAL", "X-EQUAL-IND", "X-EQUAL-OUT"]

PLANTS = [
    ("XP-NOBOX: nothing excluded", CORE,
     "inline bool Excluded(int x, int y) { return InBox(kClockBox, x, y) || InBox(kIndicatorBox, x, y); }",
     "inline bool Excluded(int x, int y) { (void)x; (void)y; return false; }",
     {"X-COST", "X-EDGE", "X-CLOCK", "X-EQUAL", "X-EQUAL-IND"}),
    ("XP-SHIFT: the clock box 1 px right", CORE,
     "constexpr Box kClockBox = {105, 134, 210, 217};",
     "constexpr Box kClockBox = {106, 135, 210, 217};",
     {"X-COST", "X-EDGE", "X-CLOCK", "X-EQUAL", "X-EQUAL-OUT"}),
    ("XP-WHOLE: equality judged on the whole frame", CORE,
     "        if (Excluded(i % W, i / W)) continue;\n",
     "",
     {"X-EQUAL", "X-EQUAL-IND"}),
]

ROW = re.compile(r"^\s*(ok|FAIL)\s+(X-[A-Z0-9-]+)\s", re.M)


def copy_tree(dst):
    for rel in COPIED:
        os.makedirs(os.path.dirname(os.path.join(dst, *rel.split("/"))), exist_ok=True)
        shutil.copyfile(os.path.join(ROOT, *rel.split("/")), os.path.join(dst, *rel.split("/")))


def build(tree, exe):
    b = subprocess.run([CXX, *FLAGS, "-I" + os.path.join(tree, "include"), "-I" + os.path.join(tree, "test", "host"),
                        os.path.join(tree, *MAIN.split("/")), "-o", exe], capture_output=True, text=True)
    return None if b.returncode == 0 else b.stderr.strip()[:600]


def selftest(exe):
    r = subprocess.run([exe, "--selftest-box"], capture_output=True, text=True)
    return r.returncode, ROW.findall(r.stdout)


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
    print(f"ANCHORS sabotage_anchor_box.py: {len(PLANTS)} plant(s), "
          + ("all present exactly once" if not bad else f"{bad} not exactly once"))
    return 1 if bad else 0


def control():
    tmp = tempfile.mkdtemp(prefix="anb-ctl-")
    try:
        copy_tree(tmp)
        exe = os.path.join(tmp, "a.exe")
        err = build(tmp, exe)
        if err:
            print(f"RIG: the real files did not compile: {err}")
            return 2
        code, rows = selftest(exe)
        okn = sum(1 for s, _ in rows if s == "ok")
        ids = [i for _, i in rows]
        good = code == 0 and okn == len(ALL) and ids == ALL
        print(f"  {'ok  ' if good else 'FAIL'}  anchor_compare --selftest-box: {okn} ok, {len(rows) - okn} FAIL, "
              f"exit {code}, rows {'as listed' if ids == ALL else ids}")
        # --equal end to end, on grabs written here: identical 0, a clock-box change 0, a px outside 1, short 2
        d = os.path.join(tmp, "g")
        os.makedirs(d)
        e = subprocess.run([exe, "--emit-synthetic", d], capture_output=True, text=True)
        with open(os.path.join(d, "bg.fb"), "rb") as f:
            body = bytearray(f.read())
        def write(name, b):
            with open(os.path.join(d, name), "wb") as f:
                f.write(bytes(b))
        clock = bytearray(body)
        for y in range(210, 218):
            for x in range(105, 135):
                clock[2 * (y * 240 + x)] ^= 0x01
        out = bytearray(body)
        out[2 * (212 * 240 + 135)] ^= 0x01
        write("clock.fb", clock)
        write("out.fb", out)
        write("short.fb", body[:-1])
        for name, files, want in (("identical", ("bg.fb", "bg.fb", "bg.fb"), 0),
                                  ("clock box changed", ("bg.fb", "clock.fb", "bg.fb"), 0),
                                  ("one px outside", ("bg.fb", "bg.fb", "out.fb"), 1),
                                  ("a short grab", ("bg.fb", "short.fb", "bg.fb"), 2)):
            r = subprocess.run([exe, "--equal", *[os.path.join(d, x) for x in files]], capture_output=True, text=True)
            ok = r.returncode == want and e.returncode == 0
            good = good and ok
            last = r.stdout.strip().splitlines()[-1] if r.stdout.strip() else ""
            print(f"  {'ok  ' if ok else 'FAIL'}  --equal, {name}: exit {r.returncode} (want {want}): {last}")
        print("CONTROL sabotage_anchor_box.py: " + ("all as predicted" if good else "NOT as predicted"))
        return 0 if good else 1
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def main():
    if anchors() != 0:
        return 2
    tmp = tempfile.mkdtemp(prefix="anb-sab-")
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
            exe = os.path.join(tree, "a.exe")
            err = build(tree, exe)
            if err:
                print(f"RIG: {name} did not compile: {err}")
                return 2
            code, rows = selftest(exe)
            red = sorted(i for s, i in rows if s == "FAIL")
            ids = [i for _, i in rows]
            good = red == sorted(want) and code == 1 and ids == ALL
            bad += not good
            print(f"  {'ok  ' if good else 'FAIL'}  {name}: red {red} (want {sorted(want)}), exit {code}"
                  + ("" if ids == ALL else f", rows {ids}"))
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    print("BOX PLANTS AS PREDICTED" if not bad else f"BOX PLANTS NOT AS PREDICTED ({bad})")
    return 1 if bad else 0


if __name__ == "__main__":
    if sys.argv[1:] == ["--control"]:
        sys.exit(control())
    if sys.argv[1:] == ["--anchors"]:
        sys.exit(anchors())
    sys.exit(main())
