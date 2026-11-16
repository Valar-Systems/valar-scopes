"""Runs test_airport_label_frame.cpp against SABOTAGED copies of src/AirportLabelFrame.cpp, the airport label's own
translation unit (#719 row (d)). Each plant must fail EXACTLY the frozen set of named rows, and nothing else.

    python -I test/host/sabotage_airport_label_frame.py              every plant; exit 0 as predicted, 1 off, 2 the rig
    python -I test/host/sabotage_airport_label_frame.py --anchors    run.sh's anchors pass: each plant's anchor, nothing run
    python -I test/host/sabotage_airport_label_frame.py --control    run.sh's control pass: the unplanted run alone

Method, fixed before any run (the same as sabotage_airport_label.py):
  - The real files are never edited. The unit, its headers and the test are copied into a temporary tree with the
    repo's layout, and the plant goes into the copy of the unit. Each plant asserts its anchor occurs exactly once and
    that the copy differs. The real files' hashes are compared before and after.
  - The unplanted copies run first, as the control: ROWS rows, 0 failures. A control that cannot run ends the driver
    (exit 2); a plant that cannot be graded is recorded and the next plant runs (sabotage_common.Rigs).
  - A plant keeps every parameter used: under -Werror an unused one is a compile failure, which is the RIG, not a result.
  - The red sets assume the test's fixed row order; each row begins with FrameReset() and reads the mark it added last.
"""
import hashlib, os, re, shutil, subprocess, sys, tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sabotage_common as sc   # run.sh's --anchors and --control passes, and the RIG record

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
HEADERS = {name: os.path.join(ROOT, "include", name)
           for name in ("AirportLabelFrame.h", "AirportLabel.h", "OverlapCount.h", "DiscGeometry.h")}   # never planted
UNIT = os.path.join(ROOT, "src", "AirportLabelFrame.cpp")
TEST = os.path.join(ROOT, "test", "host", "test_airport_label_frame.cpp")
CXX = os.environ.get("CXX") or os.path.expanduser("~/.platformio/packages/toolchain-gccmingw32/bin/g++.exe")
FLAGS = ["-std=c++11", "-Wall", "-Wextra", "-Werror", "-O1", "-static-libgcc", "-static-libstdc++"]
# The one place the suite's count lives: the full run's control and --control's table both read it.
ROWS = 12
CONTROL_TABLE = {"test_airport_label_frame": ("ok", ROWS)}
ROW = re.compile(r"^\s*(ok|BAD)\s+(ALF\d+)\s")

SABOTAGES = {
    "S-ALF-NOADD": [("    g_obstacles.Add(r);\n",
                     "    (void)r;   // SABOTAGE S-ALF-NOADD: nothing DrawRadar records reaches the decision\n")],
    "S-ALF-NOTEXT": [("                     [&](const Rect& b) { return textHits(b, tctx); },\n",
                      "                     [&](const Rect& b) { (void)b; (void)textHits; (void)tctx; return false; },"
                      "   // SABOTAGE S-ALF-NOTEXT\n")],
    "S-ALF-WIDTH": [("        m.at = Place(m.x, m.y, width(m.code, wctx), h, screenSize,\n",
                     "        m.at = Place(m.x, m.y, ((void)width, (void)wctx, 18), h, screenSize,   // SABOTAGE S-ALF-WIDTH\n")],
    "S-ALF-RESETMARKS": [("    g_marks.clear();   // capacity is kept: one allocation, reused every frame\n",
                          "    // SABOTAGE S-ALF-RESETMARKS: last frame's marks are kept\n")],
    "S-ALF-RESETOBS": [("    g_obstacles.Reset();\n",
                        "    // SABOTAGE S-ALF-RESETOBS: last frame's obstacles are kept\n")],
}
# FROZEN before the first plant ran (2026-11-16, #719 row (d) F1).
PREDICTED = {
    "S-ALF-NOADD": {"ALF3", "ALF4", "ALF7", "ALF8", "ALF10"},
    "S-ALF-NOTEXT": {"ALF5"},
    "S-ALF-WIDTH": {"ALF6"},
    "S-ALF-RESETMARKS": {"ALF7"},
    "S-ALF-RESETOBS": {"ALF7", "ALF9"},
}


def sha(p):
    return hashlib.sha256(open(p, "rb").read()).hexdigest()


def run(plant):
    """(exit code, red set, rows seen) of the test built against copies, with `plant` = [(anchor, repl)] in the unit.
    Raises sc.Rig, its reason printed, when the plant cannot be graded."""
    with tempfile.TemporaryDirectory(prefix="sab-alframe-") as td:
        inc, src, th = (os.path.join(td, d) for d in ("include", "src", os.path.join("test", "host")))
        for d in (inc, src, th):
            os.makedirs(d)
        for name, path in HEADERS.items():
            text = sc.read(path)
            if text is None:
                print("  RIG: %s cannot be read" % name)
                raise sc.Rig(name)
            open(os.path.join(inc, name), "w", encoding="utf-8", newline="\n").write(text)
        text = sc.read(UNIT)
        if text is None:
            print("  RIG: AirportLabelFrame.cpp cannot be read")
            raise sc.Rig("unit")
        if plant:
            before = text
            for anchor, repl in plant:
                n = text.count(anchor)
                if n != 1:
                    print("  RIG: an anchor occurs %d times in AirportLabelFrame.cpp, need exactly 1: %r" % (n, anchor[:60]))
                    raise sc.Rig("anchor")
                text = text.replace(anchor, repl)
            if text == before:
                print("  RIG: the plant did not change the copy of AirportLabelFrame.cpp")
                raise sc.Rig("unchanged")
        unit = os.path.join(src, "AirportLabelFrame.cpp")
        open(unit, "w", encoding="utf-8", newline="\n").write(text)
        shutil.copy(TEST, os.path.join(th, "test_airport_label_frame.cpp"))
        exe = os.path.join(td, "t.exe")
        b = subprocess.run([CXX] + FLAGS + ["-I" + inc, os.path.join(th, "test_airport_label_frame.cpp"), unit, "-o", exe],
                           capture_output=True, text=True)
        if b.returncode != 0:
            print("  RIG: the copy did not compile:\n" + b.stderr[-1500:])
            raise sc.Rig("compile")
        r = subprocess.run([exe], capture_output=True, text=True)
    marks = [m for m in (ROW.match(l) for l in r.stdout.splitlines()) if m]
    return r.returncode, {m.group(2) for m in marks if m.group(1) == "BAD"}, len(marks)


def by_row(s):
    return sorted(s, key=lambda n: int(n[3:]))


def real_files():
    return [UNIT, TEST] + list(HEADERS.values())


def main():
    before = {p: sha(p) for p in real_files()}
    try:
        rc, red, n = run(None)
    except sc.Rig:
        print("  FAIL control: the unplanted copies could not run (the RIG)")
        return 2
    if not (rc == 0 and not red and n == ROWS):
        print("  FAIL control: the unplanted copies: exit %d, %d rows (want %d), red %s" % (rc, n, ROWS, by_row(red)))
        return 2
    print("  ok   control: the unplanted copies: exit %d, %d rows, red -" % (rc, n))
    rigs = sc.Rigs()
    off = 0
    for name, edits in SABOTAGES.items():
        got = rigs.run(name, run, edits)
        if got is None:
            continue
        rc, red, n = got
        want = PREDICTED[name]
        good = rc == 1 and red == want and n == ROWS
        off += 0 if good else 1
        print("  %s %s: exit %d, red %s (want %s)" % ("ok  " if good else "FAIL", name, rc, by_row(red), by_row(want)))
    after = {p: sha(p) for p in real_files()}
    unchanged = before == after
    off += 0 if unchanged else 1
    print("  %s the real files are byte-identical before and after" % ("ok  " if unchanged else "FAIL"))
    print("AIRPORT LABEL FRAME SABOTAGES (%d) %s" % (len(SABOTAGES), "AS PREDICTED" if off == 0 else "NOT AS PREDICTED"))
    return rigs.close(0 if off == 0 else 1)


def anchor_entries():
    """Every plant's anchor in the real unit, for run.sh's --anchors pass (no plant runs)."""
    return [(name, "src/AirportLabelFrame.cpp", sc.read(UNIT), anchor) for name, edits in SABOTAGES.items()
            for anchor, _ in edits]


def control():
    """The unplanted run alone, for run.sh's --control pass: the suite against CONTROL_TABLE."""
    run(None)


if __name__ == "__main__":
    if sc.wants_control():
        sys.exit(sc.control_mode(os.path.basename(__file__), control, CONTROL_TABLE))
    if sc.wants_anchors():
        sys.exit(sc.anchors_mode(os.path.basename(__file__), anchor_entries()))
    sys.exit(main())
