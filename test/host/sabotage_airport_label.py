"""Runs test_airport_label.cpp against SABOTAGED copies of include/AirportLabel.h and include/OverlapCount.h. Each plant
must fail EXACTLY the frozen set of named rows (docs/v20-label-collision.md, row a), and nothing else.

    python -I test/host/sabotage_airport_label.py              every plant; exit 0 as predicted, 1 off, 2 the rig
    python -I test/host/sabotage_airport_label.py --anchors    run.sh's anchors pass: each plant's anchor, nothing run
    python -I test/host/sabotage_airport_label.py --control    run.sh's control pass: the unplanted run alone

Method, fixed before any run:
  - The real files are never edited. Both headers and the test are copied into a temporary tree with the repo's
    layout, and the plant goes into exactly one copy. Each plant asserts its anchor occurs exactly once and that the
    copy differs. The real files' hashes are compared before and after.
  - The unplanted copies run first, as the control: ROWS rows, 0 failures. A control that cannot run ends the driver
    (exit 2); a plant that cannot be graded is recorded and the next plant runs (sabotage_common.Rigs, Q70 (b)).
  - A plant keeps every parameter used: under -Werror an unused one is a compile failure, which is the RIG, not a result.
"""
import hashlib, os, re, shutil, subprocess, sys, tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sabotage_common as sc   # run.sh's --anchors and --control passes, and the RIG record

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
HEADERS = {"AirportLabel.h": os.path.join(ROOT, "include", "AirportLabel.h"),
           "OverlapCount.h": os.path.join(ROOT, "include", "OverlapCount.h"),
           "DiscGeometry.h": os.path.join(ROOT, "include", "DiscGeometry.h")}   # copied, never planted
TEST = os.path.join(ROOT, "test", "host", "test_airport_label.cpp")
CXX = os.environ.get("CXX") or os.path.expanduser("~/.platformio/packages/toolchain-gccmingw32/bin/g++.exe")
FLAGS = ["-std=c++11", "-Wall", "-Wextra", "-Werror", "-O1", "-static-libgcc", "-static-libstdc++"]
# The one place the suite's count lives: the full run's control and --control's table both read it.
ROWS = 24
CONTROL_TABLE = {"test_airport_label": ("ok", ROWS)}
ROW = re.compile(r"^\s*(ok|BAD)\s+(AL\d+)\s")

RIGHT_TEST = "    if (OnGlass(right, screenSize) && !text(right) && !symbol(right))\n"
LEFT_TEST = "    if (OnGlass(left, screenSize) && !text(left) && !symbol(left))\n"
CHORD = "    const int avail = discgeom::ChordWidthPx(b.y0, b.y1 - b.y0, screenSize);\n"
CENTRE = "    const int c = screenSize / 2;\n"
SABOTAGES = {
    "S-AL-BLIND": ("AirportLabel.h",
                   [(RIGHT_TEST + "        return Placement{ Side::Right, (int16_t)rx, (int16_t)y };\n",
                     "    (void)right;   // SABOTAGE S-AL-BLIND: always right, obstacles and the rim ignored\n"
                     "    return Placement{ Side::Right, (int16_t)rx, (int16_t)y };\n")],
                   {"AL9", "AL10", "AL11", "AL13", "AL14", "AL17", "AL18", "AL19", "AL20", "AL21", "AL23", "AL24"}),
    "S-AL-LEFT": ("AirportLabel.h",
                  [(LEFT_TEST + "        return Placement{ Side::Left, (int16_t)lx, (int16_t)y };\n",
                    "    (void)left;   // SABOTAGE S-AL-LEFT: no left candidate\n")],
                  {"AL11", "AL13", "AL17", "AL19", "AL24"}),
    "S-AL-SYM": ("AirportLabel.h",
                 [(RIGHT_TEST,
                   "    (void)symbol;   // SABOTAGE S-AL-SYM: symbols are not obstacles\n"
                   "    if (OnGlass(right, screenSize) && !text(right))\n"),
                  (LEFT_TEST, "    if (OnGlass(left, screenSize) && !text(left))\n")],
                 {"AL10", "AL11"}),
    "S-AL-DISC": ("AirportLabel.h",
                  [(CHORD + "    if (avail <= 0) return false;\n" + CENTRE
                    + "    return b.x0 >= c - avail / 2 && b.x1 <= c + avail / 2;\n",
                    "    (void)b; (void)screenSize;   // SABOTAGE S-AL-DISC: every box is on the glass\n"
                    "    return true;\n")],
                  {"AL18", "AL19", "AL20", "AL21", "AL23", "AL24"}),
    "S-AL-FACE": ("AirportLabel.h",
                  [(CHORD, "    (void)screenSize;   // SABOTAGE S-AL-FACE: a fixed 240 px face\n"
                           "    const int avail = discgeom::ChordWidthPx(b.y0, b.y1 - b.y0, 240);\n"),
                   (CENTRE, "    const int c = 240 / 2;\n")],
                  {"AL15"}),
    "S-AL-FRAME": ("OverlapCount.h",
                   [("        if (dropped_) return true;\n", "        // SABOTAGE S-AL-FRAME: a dropped rectangle is ignored\n")],
                   {"AL5"}),
    "S-AL-OVF": ("AirportLabel.h",
                 [("        if (overflow_) return true;\n", "        // SABOTAGE S-AL-OVF: an overflowed list is ignored\n")],
                 {"AL6"}),
}


def sha(p):
    return hashlib.sha256(open(p, "rb").read()).hexdigest()


def run(plant):
    """(exit code, red set, rows seen) of the test built against copies, with `plant` = (file, [(anchor, repl)]).
    Raises sc.Rig, its reason printed, when the plant cannot be graded."""
    with tempfile.TemporaryDirectory(prefix="sab-airlabel-") as td:
        inc = os.path.join(td, "include")
        th = os.path.join(td, "test", "host")
        os.makedirs(inc)
        os.makedirs(th)
        for name, src in HEADERS.items():
            text = sc.read(src)
            if text is None:
                print("  RIG: %s cannot be read" % name)
                raise sc.Rig(name)
            if plant and plant[0] == name:
                before = text
                for anchor, repl in plant[1]:
                    n = text.count(anchor)
                    if n != 1:
                        print("  RIG: an anchor occurs %d times in %s, need exactly 1: %r" % (n, name, anchor[:60]))
                        raise sc.Rig(name)
                    text = text.replace(anchor, repl)
                if text == before:
                    print("  RIG: the plant did not change the copy of %s" % name)
                    raise sc.Rig(name)
            open(os.path.join(inc, name), "w", encoding="utf-8", newline="\n").write(text)
        shutil.copy(TEST, os.path.join(th, "test_airport_label.cpp"))
        exe = os.path.join(td, "t.exe")
        b = subprocess.run([CXX] + FLAGS + [os.path.join(th, "test_airport_label.cpp"), "-o", exe],
                           capture_output=True, text=True)
        if b.returncode != 0:
            print("  RIG: the copy did not compile:\n" + b.stderr[-1500:])
            raise sc.Rig("compile")
        r = subprocess.run([exe], capture_output=True, text=True)
    marks = [m for m in (ROW.match(l) for l in r.stdout.splitlines()) if m]
    return r.returncode, {m.group(2) for m in marks if m.group(1) == "BAD"}, len(marks)


def by_row(s):
    return sorted(s, key=lambda n: int(n[2:]))


def main():
    before = {n: sha(p) for n, p in HEADERS.items()}
    before[TEST] = sha(TEST)
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
    for name, (f, edits, want) in SABOTAGES.items():
        got = rigs.run(name, run, (f, edits))
        if got is None:
            continue
        rc, red, n = got
        good = rc == 1 and red == want and n == ROWS
        off += 0 if good else 1
        print("  %s %s: exit %d, red %s (want %s)" % ("ok  " if good else "FAIL", name, rc, by_row(red), by_row(want)))
    after = {n: sha(p) for n, p in HEADERS.items()}
    after[TEST] = sha(TEST)
    unchanged = before == after
    off += 0 if unchanged else 1
    print("  %s the real files are byte-identical before and after" % ("ok  " if unchanged else "FAIL"))
    print("AIRPORT LABEL SABOTAGES (%d) %s" % (len(SABOTAGES), "AS PREDICTED" if off == 0 else "NOT AS PREDICTED"))
    return rigs.close(0 if off == 0 else 1)


def anchor_entries():
    """Every plant's anchor in the real file it plants, for run.sh's --anchors pass (no plant runs)."""
    return [(name, "include/" + f, sc.read(HEADERS[f]), anchor)
            for name, (f, edits, _) in SABOTAGES.items() for anchor, _ in edits]


def control():
    """The unplanted run alone, for run.sh's --control pass: the suite against CONTROL_TABLE."""
    run(None)


if __name__ == "__main__":
    if sc.wants_control():
        sys.exit(sc.control_mode(os.path.basename(__file__), control, CONTROL_TABLE))
    if sc.wants_anchors():
        sys.exit(sc.anchors_mode(os.path.basename(__file__), anchor_entries()))
    sys.exit(main())
