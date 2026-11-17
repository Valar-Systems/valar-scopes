#!/usr/bin/env python3
"""Red first for section 10's marker A/B (docs/speed-p5p6.md section 10, accepted 2026-11-17): each plant breaks a copy
of anchor_file.h or anchor_core.h and requires `anchor_compare --selftest-ab` to turn red in EXACTLY the rows named.
The sets are the predictions, frozen before the first run. B-PICKS is a control: red under no plant. The test's
fixture computes its distances with explicit radii and paints the firmware's boxes as literals, so a plant that moves
anchor's constants cannot move the fixture with it.

    sabotage_anchor_ab.py              every plant; exit 0 all as predicted, 1 a plant read otherwise, 2 the rig
    sabotage_anchor_ab.py --control    the real files: every row ok, and --ab end to end on emitted grabs reads
                                       0 / 1 / 2 / 0 / 3 (identical, a marker px, a far px, the clock box, a short grab)
    sabotage_anchor_ab.py --anchors    every plant's needle present exactly once in the real file

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
# Every file the build reads, found by following MAIN's quoted includes (the fixed lists of the other drivers missed
# a new header twice): a header added later is copied without editing this list.
ALL = ["B-PICKS", "B-IDENT", "B-MARK", "B-EDGE2", "B-EDGE3", "B-BG", "B-BOTH", "B-CLOCK", "B-IND", "B-BOXED",
       "B-ASAME", "B-ASAME-COL", "B-ASAME-N", "B-STILL-M", "B-STILL-P", "B-OVL", "B-UP", "B-GRAB", "B-EMPTY",
       "B-MISSING"]

PLANTS = [
    ("ABP-NOBOX: the boxes ignored", FILE,
     "        if (anchor::Excluded(i % anchor::W, i / anchor::W)) {\n",
     "        if (false) {\n",
     {"B-CLOCK", "B-IND", "B-BOXED"}),
    ("ABP-NOREGION: every difference counted outside", FILE,
     "        if (r[i])\n            ++v.in;\n",
     "        if (false)\n            ++v.in;\n",
     {"B-MARK", "B-EDGE2", "B-BOTH"}),
    ("ABP-ALLIN: every difference counted inside", FILE,
     "        if (r[i])\n            ++v.in;\n",
     "        if (true)\n            ++v.in;\n",
     {"B-EDGE3", "B-BG", "B-BOTH"}),
    ("ABP-DIL1: the region dilated 1, not 2", CORE,
     "constexpr int kDilate = 2;",
     "constexpr int kDilate = 1;",
     {"B-EDGE2"}),
    ("ABP-PRECEDENCE: inside outranks outside", FILE,
     "    v.code = v.out ? 2 : v.in ? 1 : 0;\n",
     "    v.code = v.in ? 1 : v.out ? 2 : 0;\n",
     {"B-BOTH"}),
    ("ABP-NOSAME: A-SAME not checked", FILE,
     "    if (!same)\n        return AbMake(3, \"A-SAME",
     "    if (false)\n        return AbMake(3, \"A-SAME",
     {"B-ASAME", "B-ASAME-COL", "B-ASAME-N"}),
    ("ABP-NOSTILL: A-STILL not checked", FILE,
     "    if (!still) {\n",
     "    if (false) {\n",
     {"B-STILL-M", "B-STILL-P"}),
    ("ABP-NOOVL: ovl not checked", FILE,
     "        if (l.ovl != 0) return AbMake(3, \"ovl:",
     "        if (false) return AbMake(3, \"ovl:",
     {"B-OVL"}),
    ("ABP-NOUP: up not checked", FILE,
     "        if (l.up != 0) return AbMake(3, \"up:",
     "        if (false) return AbMake(3, \"up:",
     {"B-UP"}),
    ("ABP-NOEMPTY: an empty scene not refused", FILE,
     "    if (pm.count == 0 && pp.count == 0) return AbMake(3, \"empty:",
     "    if (false) return AbMake(3, \"empty:",
     {"B-EMPTY"}),
]

ROW = re.compile(r"^\s*(ok|FAIL)\s+(B-[A-Z0-9-]+)\s", re.M)
INC = re.compile(r'^\s*#\s*include\s+"([^"]+)"', re.M)
SEARCH = ["", "include", "test/host"]   # the quoted include's own directory first, then the build's -I list


def read(rel):
    with open(os.path.join(ROOT, *rel.split("/")), encoding="utf-8", newline="") as f:
        return f.read()


def copied():
    """MAIN and every file reachable by quoted includes, as repo-relative paths; a quoted include not found is RIG."""
    seen, todo = [], [MAIN]
    while todo:
        rel = todo.pop()
        if rel in seen:
            continue
        seen.append(rel)
        here = os.path.dirname(rel)
        for name in INC.findall(read(rel)):
            hit = None
            for base in [here] + SEARCH[1:]:
                cand = os.path.normpath(os.path.join(base, name)).replace(os.sep, "/")
                if os.path.isfile(os.path.join(ROOT, *cand.split("/"))):
                    hit = cand
                    break
            if hit is None:
                raise SystemExit(f"RIG: {rel} includes \"{name}\", found nowhere on the search path")
            todo.append(hit)
    return sorted(seen)


def copy_tree(dst):
    for rel in copied():
        os.makedirs(os.path.dirname(os.path.join(dst, *rel.split("/"))), exist_ok=True)
        shutil.copyfile(os.path.join(ROOT, *rel.split("/")), os.path.join(dst, *rel.split("/")))


def build(tree, exe):
    b = subprocess.run([CXX, *FLAGS, "-I" + os.path.join(tree, "include"), "-I" + os.path.join(tree, "test", "host"),
                        os.path.join(tree, *MAIN.split("/")), "-o", exe], capture_output=True, text=True)
    return None if b.returncode == 0 else b.stderr.strip()[:600]


def selftest(exe):
    r = subprocess.run([exe, "--selftest-ab"], capture_output=True, text=True)
    return r.returncode, ROW.findall(r.stdout)


def anchors():
    bad = 0
    for name, rel, old, _new, _want in PLANTS:
        n = read(rel).count(old)
        if n != 1:
            bad += 1
            print(f"  FAIL  {name}: its needle appears {n} times in {rel}")
    print(f"ANCHORS sabotage_anchor_ab.py: {len(PLANTS)} plant(s), "
          + ("all present exactly once" if not bad else f"{bad} not exactly once"))
    return 1 if bad else 0


def control():
    tmp = tempfile.mkdtemp(prefix="anab-ctl-")
    try:
        files = copied()
        print(f"  copied: {', '.join(files)}")
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
        print(f"  {'ok  ' if good else 'FAIL'}  anchor_compare --selftest-ab: {okn} ok, {len(rows) - okn} FAIL, "
              f"exit {code}, rows {'as listed' if ids == ALL else ids}")
        # --ab end to end, on grabs written here from the emitted 8COPY frame and its lines (scene S3)
        d = os.path.join(tmp, "g")
        os.makedirs(d)
        e = subprocess.run([exe, "--emit-synthetic", d], capture_output=True, text=True)
        with open(os.path.join(d, "8copy.fb"), "rb") as f:
            body = bytearray(f.read())

        def write(name, b):
            with open(os.path.join(d, name), "wb") as f:
                f.write(bytes(b))
        far = bytearray(body)
        far[2 * (230 * 240 + 230)] ^= 0x01        # x 230, y 230: far from every synthetic contact, outside both boxes
        clock = bytearray(body)
        for y in range(210, 218):
            for x in range(105, 135):
                clock[2 * (y * 240 + x)] ^= 0x01  # the clock box, whole
        write("far.fb", far)
        write("clock.fb", clock)
        write("short.fb", body[:-1])
        lines = os.path.join(d, "lines.txt")
        for name, m, p, want in (("identical", "8copy.fb", "8copy.fb", 0),
                                 ("a marker px", "8copy.fb", "8copy-flip.fb", 1),
                                 ("a far px", "8copy.fb", "far.fb", 2),
                                 ("the clock box", "8copy.fb", "clock.fb", 0),
                                 ("a short grab", "8copy.fb", "short.fb", 3)):
            r = subprocess.run([exe, "--ab", os.path.join(d, m), os.path.join(d, p), lines, lines, "3"],
                               capture_output=True, text=True)
            ok = r.returncode == want and e.returncode == 0
            good = good and ok
            last = r.stdout.strip().splitlines()[-1] if r.stdout.strip() else ""
            print(f"  {'ok  ' if ok else 'FAIL'}  --ab, {name}: exit {r.returncode} (want {want}): {last}")
        print("CONTROL sabotage_anchor_ab.py: " + ("all as predicted" if good else "NOT as predicted"))
        return 0 if good else 1
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def main():
    if anchors() != 0:
        return 2
    tmp = tempfile.mkdtemp(prefix="anab-sab-")
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
    print("AB PLANTS AS PREDICTED" if not bad else f"AB PLANTS NOT AS PREDICTED ({bad})")
    return 1 if bad else 0


if __name__ == "__main__":
    if sys.argv[1:] == ["--control"]:
        sys.exit(control())
    if sys.argv[1:] == ["--anchors"]:
        sys.exit(anchors())
    sys.exit(main())
