#!/usr/bin/env python3
"""Red first for anchor_compare's FILE MODE (board addendum 2, docs/speed-p5p6.md section 7d): each plant breaks a copy
of one file and requires `anchor_compare --selftest-file` to turn red in EXACTLY the rows named. The sets below are the
predictions, frozen before the first run. --control also runs the file path end to end on files the tool writes.

    sabotage_anchor_file.py              every plant; exit 0 all as predicted, 1 a plant read otherwise, 2 the rig
    sabotage_anchor_file.py --control    the real files: every row ok, and the file path reads 0 / 1 / 2 as predicted
    sabotage_anchor_file.py --anchors    every plant's needle present exactly once in the real file

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
FILE = "tools/speed-p5p6/anchor_file.h"
MAIN = "tools/speed-p5p6/anchor_compare.cpp"
COPIED = [CORE, FILE, MAIN, "test/host/p5_outline_port.h", "test/host/LgfxPort.h", "include/OutlineMask.h"]
ALL = ["F-PASS", "F-FAIL", "F-STILL", "F-OVL", "F-TRK", "F-UP", "F-BG", "F-EMPTY", "F-NOPAIR", "F-NOCLIP", "F-GRAB",
       "F-MALFORMED", "F-COUNT", "F-NOPASS", "F-ORDER"]

PLANTS = [
    ("FP-NOSTILL: A-STILL never refuses", FILE,
     "    if (!still) {\n",
     "    if (false && !still) {\n",
     {"F-STILL"}),
    ("FP-NOOVL: a nonzero ovl passes", FILE,
     "        if (l.ovl != 0) return",
     "        if (false && l.ovl != 0) return",
     {"F-OVL"}),
    ("FP-NOBG: the BG scene may carry contacts", FILE,
     "    if (pb.count != 0) return",
     "    if (false && pb.count != 0) return",
     {"F-BG"}),
    # The host draws every marker white instead of with the device's col.
    ("FP-COLFIXED: col ignored", FILE,
     "l.dot != 0, (uint32_t)l.col});",
     "l.dot != 0, (uint32_t)0xFFFFu});",
     {"F-PASS", "F-FAIL"}),
    # trk read as whole degrees instead of tenths: contact 0's dart turns from 30 to 300 degrees.
    ("FP-TRKDEG: trk not divided by 10", FILE,
     "(float)(l.trk / 10)",
     "(float)l.trk",
     {"F-PASS", "F-FAIL"}),
    ("FP-NOCLIPCHK: no edge clip required", FILE,
     "    if (v.clipped < 1) {",
     "    if (false && v.clipped < 1) {",
     {"F-NOCLIP"}),
    ("FP-ORDER: the line numbers never checked", FILE,
     "            if (q.k[i].c != i) {\n",
     "            if (false && q.k[i].c != i) {\n",
     {"F-ORDER"}),
    # The overlap rule in the CORE (shared with anchor_pick): any two non-empty footprints "overlap".
    ("FP-OVERLAP-ANY: Overlap is an OR", CORE,
     "        if (a[i] && b[i]) return true;",
     "        if (a[i] || b[i]) return true;",
     {"F-NOPAIR"}),
]

ROW = re.compile(r"^\s*(ok|FAIL)\s+(F-[A-Z-]+)\s", re.M)


def copy_tree(dst):
    for rel in COPIED:
        os.makedirs(os.path.dirname(os.path.join(dst, *rel.split("/"))), exist_ok=True)
        shutil.copyfile(os.path.join(ROOT, *rel.split("/")), os.path.join(dst, *rel.split("/")))


def build(tree, exe):
    b = subprocess.run([CXX, *FLAGS, "-I" + os.path.join(tree, "include"), "-I" + os.path.join(tree, "test", "host"),
                        os.path.join(tree, *MAIN.split("/")), "-o", exe], capture_output=True, text=True)
    return None if b.returncode == 0 else b.stderr.strip()[:600]


def selftest(exe):
    r = subprocess.run([exe, "--selftest-file"], capture_output=True, text=True)
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
    print(f"ANCHORS sabotage_anchor_file.py: {len(PLANTS)} plant(s), "
          + ("all present exactly once" if not bad else f"{bad} not exactly once"))
    return 1 if bad else 0


def control():
    tmp = tempfile.mkdtemp(prefix="anf-ctl-")
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
        print(f"  {'ok  ' if good else 'FAIL'}  anchor_compare --selftest-file: {okn} ok, {len(rows) - okn} FAIL, "
              f"exit {code}, rows {'as listed' if ids == ALL else ids}")
        # The file path, end to end: PASS 0, one changed px 1, a short grab 2.
        d = os.path.join(tmp, "syn")
        os.makedirs(d)
        e = subprocess.run([exe, "--emit-synthetic", d], capture_output=True, text=True)
        with open(os.path.join(d, "bg.fb"), "rb") as f:
            body = f.read()
        with open(os.path.join(d, "bg-short.fb"), "wb") as f:
            f.write(body[:-1])
        runs = []
        for name, dev, bgf, want in (("pass", "8copy.fb", "bg.fb", 0), ("one px", "8copy-flip.fb", "bg.fb", 1),
                                     ("short grab", "8copy.fb", "bg-short.fb", 2)):
            r = subprocess.run([exe, os.path.join(d, bgf), os.path.join(d, dev), os.path.join(d, "lines.txt"), "2", "3"],
                               capture_output=True, text=True)
            ok = r.returncode == want
            good = good and ok and e.returncode == 0
            runs.append(ok)
            print(f"  {'ok  ' if ok else 'FAIL'}  file path, {name}: exit {r.returncode} (want {want}): "
                  f"{r.stdout.strip()}")
        print("CONTROL sabotage_anchor_file.py: " + ("all as predicted" if good else "NOT as predicted"))
        return 0 if good else 1
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def main():
    if anchors() != 0:
        return 2
    tmp = tempfile.mkdtemp(prefix="anf-sab-")
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
    print("FILE-MODE PLANTS AS PREDICTED" if not bad else f"FILE-MODE PLANTS NOT AS PREDICTED ({bad})")
    return 1 if bad else 0


if __name__ == "__main__":
    if sys.argv[1:] == ["--control"]:
        sys.exit(control())
    if sys.argv[1:] == ["--anchors"]:
        sys.exit(anchors())
    sys.exit(main())
