#!/usr/bin/env python3
"""No new coordinate near Bend past 2 dp enters the tree, and the tree's count of them only falls (the owner's rule,
2026-11-13 21:57Z; Step R).

WHY. A number with 3 or more decimals inside +-0.5 deg of Bend's 2-dp centre (44.06, -121.32) can place a home on a
street; 2 dp is a neighbourhood. The rule has no carve-out: alone or in a pair, signed or unsigned, any tracked file.

COUNTED: every number with 3 or more decimals whose absolute value lies in the latitude band [43.56, 44.56] or the
longitude band [120.82, 121.82], in every tracked text file. A digit or a dot directly before or after it makes it part
of a different token (a version string, a longer number), so it is not one. A C float suffix (44.0f style) is fine.

NOT COUNTED, by the owner's ruling (Daniel, 2026-11-14 01:43Z), and nothing else:
  clock time  the seconds of an EXACT H:MM:SS or HH:MM:SS: a non-digit (or the line's start), 1-2 digits, a colon, 2
              digits, a colon, then the 2-digit seconds with an optional fraction. Only that shape: "12:44.1" (no hour)
              or "123:45:44.1" (an hour preceded by a digit) still count, and so does "lat: 44.1".
  .step .stp  excluded BY FILE TYPE: millimetre CAD geometry that cannot be rounded without changing the part. This is
              the one exception to "no exceptions" (Daniel, 2026-11-14 01:43Z).

DECODED FIRST, line by line: every \\xHH, \\uHHHH and C octal \\NNN escape becomes the character it spells, so a number
written partly or wholly in escapes is still a number (a test once wrote one as hex escapes to keep a \\x92 from
swallowing its digits). An escaped backslash stays one literal backslash and starts nothing.

STILL BLIND TO, stated: HTML numeric entities, percent-encoding, a number split across lines or string pieces, a number
built in code, and binary files (NUL in the first 8000 bytes).

REFUSED:
  added     a counted number on a line ADDED since BASE (--diff BASE; renames are followed, so a moved file is not new)
  count     the tree's count above the baseline in scripts/precise-coords-baseline.txt
  stale     the tree's count BELOW the baseline: lower the baseline in the same change, so the ratchet cannot slip back
  raised    the baseline above BASE's baseline (--diff BASE): it may only fall

PRINTS NO VALUE: a finding is path:line. The whole-tree count prints on every run.

    python scripts/check_precise_coords.py                 the tree against the baseline
    python scripts/check_precise_coords.py --diff BASE     and: lines added since BASE, and the baseline against BASE's
    python scripts/check_precise_coords.py --list          every counted path:line, then the count (the inventory)
    python scripts/check_precise_coords.py --selftest      rows PC1-PC22 on values generated at run time
    python scripts/check_precise_coords.py --sabotage      each plant, in a copy of this file, turns exactly its rows red

Exit 0 clean; 1 refused; 2 BLIND (no tracked text file read, or the baseline unreadable).
"""
import os
import random
import re
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
BASELINE = "scripts/precise-coords-baseline.txt"
LAT_BAND = (43.56, 44.56)      # Bend's 2-dp centre +-0.5; never written past 2 dp
LON_BAND = (120.82, 121.82)
MIN_DP = 3
NUM = re.compile(rb"(?<![0-9.])-?([0-9]{1,3}\.([0-9]+))(?![0-9.])")
CLOCK = re.compile(rb"(?<![0-9])[0-9]{1,2}:[0-9]{2}:$")      # what must sit directly before a clock time's seconds
EXEMPT_TYPES = (".step", ".stp")
ESC =re.compile(rb"\\\\|\\x([0-9A-Fa-f]{2})|\\u([0-9A-Fa-f]{4})|\\([0-7]{1,3})")


def decode(line):
    """One line with every \\xHH, \\uHHHH and \\NNN escape turned into what it spells (an escaped backslash: one
    backslash). Only for counting: positions in the decoded line are not positions in the file."""
    def r(m):
        if m.group(1):
            return bytes([int(m.group(1), 16)])
        if m.group(2):
            return chr(int(m.group(2), 16)).encode("utf-8", "replace")
        if m.group(3):
            return bytes([int(m.group(3), 8) & 0xFF])
        return b"\\"
    return ESC.sub(r, line)


def is_clock(text, m):
    """The number is the 2-digit seconds of an exact H:MM:SS / HH:MM:SS (ruled), unsigned."""
    return (len(m.group(1).split(b".")[0]) == 2 and not m.group(0).startswith(b"-")
            and CLOCK.search(text[max(0, m.start() - 8):m.start()]) is not None)


def classify(text):
    """(counted matches, how many in-band 3+ dp numbers were exempted as clock times). Never printed."""
    out, clock = [], 0
    for m in NUM.finditer(text):
        if len(m.group(2)) < MIN_DP:
            continue
        v = float(m.group(1))
        if LAT_BAND[0] <= v <= LAT_BAND[1] or LON_BAND[0] <= v <= LON_BAND[1]:
            if is_clock(text, m):
                clock += 1
            else:
                out.append(m)
    return out, clock


def counted(text):
    """The counted numbers in one bytes string, as match objects (never printed)."""
    return classify(text)[0]


def git(root, *args):
    p = subprocess.run(["git", "-C", root] + list(args), capture_output=True)
    if p.returncode != 0:
        raise RuntimeError("git %s exit %d" % (args[0], p.returncode))
    return p.stdout


def exempt_type(name):
    return name.lower().endswith(EXEMPT_TYPES)


def scan_tree(root, stats=None):
    """(count, ["path:line", ...], text files read). `stats`, if given, gets the files excluded by type and the numbers
    exempted as clock times."""
    names = [n for n in git(root, "ls-files", "-z").split(b"\0") if n]
    total, where, read = 0, [], 0
    st = {"type_excluded": 0, "clock_exempt": 0}
    for n in names:
        if exempt_type(n.decode("utf-8", "replace")):
            st["type_excluded"] += 1
            continue
        path = os.path.join(root, n.decode("utf-8", "replace"))
        try:
            with open(path, "rb") as fh:
                b = fh.read()
        except OSError:
            continue                                   # a deleted-but-tracked or unreadable path holds nothing to count
        if b"\0" in b[:8000]:
            continue                                   # binary: not text this gate can read
        read += 1
        for i, line in enumerate(b.split(b"\n"), 1):
            c, k = classify(decode(line))
            st["clock_exempt"] += k
            if c:
                total += len(c)
                where.append("%s:%d" % (n.decode("utf-8", "replace"), i))
    if stats is not None:
        stats.update(st)
    return total, where, read


def added_lines(root, base):
    """["path:line", ...] for counted numbers on lines added between BASE and HEAD (renames followed)."""
    diff = git(root, "diff", "-M", "-U0", "--no-color", "--no-ext-diff", base, "HEAD")
    out, path, line = [], None, 0
    for raw in diff.split(b"\n"):
        if raw.startswith(b"+++ "):
            path = raw[6:].decode("utf-8", "replace") if raw.startswith(b"+++ b/") else None
            if path is not None and exempt_type(path):
                path = None                            # .step/.stp: excluded by type (ruled)
        elif raw.startswith(b"@@"):
            m = re.match(rb"@@ -\S+ \+(\d+)", raw)
            line = int(m.group(1)) if m else 0
        elif raw.startswith(b"+") and path is not None:
            if counted(decode(raw[1:])):
                out.append("%s:%d" % (path, line))
            line += 1
    return out


def read_baseline(text):
    for l in reversed(text.decode("utf-8", "replace").splitlines()):
        l = l.strip()
        if l and not l.startswith("#"):
            return int(l)
    raise ValueError("no count")


def check(root, base=None, out=print):
    """0 clean, 1 refused, 2 blind. Prints the count, then findings as path:line."""
    st = {}
    total, where, read = scan_tree(root, st)
    if read == 0:
        out("BLIND: no tracked text file read"); return 2
    try:
        with open(os.path.join(root, BASELINE), "rb") as fh:
            want = read_baseline(fh.read())
    except (OSError, ValueError):
        out("BLIND: %s unreadable" % BASELINE); return 2
    out("precise coordinates near Bend in the tree: %d (baseline %d; %d text files read; not counted by ruling: "
        "%d clock-time seconds, %d .step/.stp files)" % (total, want, read, st["clock_exempt"], st["type_excluded"]))
    rc = 0
    if total > want:
        out("REFUSED count: %d above the baseline %d" % (total, want)); rc = 1
    if total < want:
        out("REFUSED stale: %d below the baseline %d -- lower %s to %d in this change" % (total, want, BASELINE, total))
        rc = 1
    if base:
        for w in added_lines(root, base):
            out("REFUSED added %s" % w); rc = 1
        try:
            old = read_baseline(git(root, "show", "%s:%s" % (base, BASELINE)))
        except (RuntimeError, ValueError):
            old = None                                  # the change that introduces the baseline
        if old is not None and want > old:
            out("REFUSED raised: the baseline went from %d to %d; it may only fall" % (old, want)); rc = 1
    return rc


# ------------------------------------------------------------------------------------------------- selftest
def _in_band(rng, band, dp, sign=""):
    lo, hi = band
    return "%s%.*f" % (sign, dp, rng.uniform(lo + 0.01, hi - 0.01))


def selftest():
    """PC1-PC9, PC15-PC21: the matcher, decode and the clock rule on generated values. PC10-PC14, PC22: throwaway repos."""
    rng = random.Random()
    rows = []

    def row(name, ok):
        rows.append((name, ok))
        print("  %s %s" % ("ok " if ok else "BAD", name))
    la4, lo5 = _in_band(rng, LAT_BAND, 4), _in_band(rng, LON_BAND, 5)
    n = lambda s: len(counted(s.encode()))                                # noqa: E731
    row("PC1 a latitude-band number at 4 dp, alone, is counted", n("x = %s;" % la4) == 1)
    row("PC2 a longitude-band number, signed, at 5 dp, is counted", n("lon: -%s," % lo5) == 1)
    row("PC3 the same number unsigned is counted", n("lon: %s," % lo5) == 1)
    row("PC4 a pair counts twice", n("pt = (%s, -%s)" % (la4, lo5)) == 2)
    row("PC5 a C float literal is counted", n("const float k = %sf;" % la4) == 1)
    row("PC6 the same values at 2 dp are not counted",
        n("(%s, -%s)" % (_in_band(rng, LAT_BAND, 2), _in_band(rng, LON_BAND, 2))) == 0)
    edge_in = "%.3f" % LAT_BAND[1]                                          # the band is inclusive
    edge_out = "%.3f" % (LAT_BAND[1] + 0.001)
    row("PC7 the band edge is in, one thousandth past it is out", n(edge_in) == 1 and n(edge_out) == 0)
    row("PC8 a 3-dp number outside both bands is not counted", n("%.3f %.4f" % (rng.uniform(10, 40), rng.uniform(60, 100))) == 0)
    row("PC9 a version string or a longer digit run is not counted",
        n("v%s.5 and 1%s" % (la4, la4)) == 0 and n("%s." % la4) == 0)
    # PC15-PC18: escapes, built at run time from generated values; counted through decode() as the tree scan does.
    bs = chr(92)
    nd = lambda s: len(counted(decode(s.encode())))                       # noqa: E731
    row("PC15 a number written in \\xHH escapes is counted", nd("".join(bs + "x%02x" % ord(c) for c in la4)) == 1)
    row("PC16 a number written in \\uHHHH escapes is counted", nd("".join(bs + "u%04x" % ord(c) for c in lo5)) == 1)
    row("PC17 a number written in octal \\NNN escapes is counted", nd("".join(bs + "%03o" % ord(c) for c in la4)) == 1)
    ip, fp = lo5.split(".")
    minus = "".join(bs + "x%02x" % b for b in "−".encode("utf-8"))
    row("PC18 an escaped minus, then escaped integer digits and a literal fraction, is counted",
        nd(minus + "".join(bs + "x%02x" % ord(c) for c in ip) + "." + fp) == 1)
    # PC19-PC21: the clock-time exemption (ruled 2026-11-14 01:43Z), both ways, on fake values only.
    ss3 = _in_band(rng, LAT_BAND, 3)                                    # in-band seconds, e.g. a time at :44.xxx
    row("PC19 an exact clock time's seconds are not counted (HH:MM:SS.fff and H:MM:SS.fff)",
        n("t=%02d:%02d:%s" % (rng.randint(0, 23), rng.randint(0, 59), ss3)) == 0
        and n("at %d:%02d:%s" % (rng.randint(0, 9), rng.randint(0, 59), ss3)) == 0)
    row("PC20 \"lat: <in-band>\" is counted", n("lat: %s" % la4) == 1)
    row("PC21 near-misses are counted: MM:SS with no hour, and an hour preceded by a digit",
        n("%02d:%s" % (rng.randint(0, 59), la4)) == 1 and n("1%02d:%02d:%s" % (rng.randint(10, 23), rng.randint(0, 59), la4)) == 1)
    d = tempfile.mkdtemp(prefix="pcoords-")
    try:
        g = ["-c", "user.name=pc", "-c", "user.email=pc@example.invalid"]
        git(d, "init", "-q", "-b", "main")
        os.makedirs(os.path.join(d, "scripts"))
        old_line = "a = %s\n" % _in_band(rng, LAT_BAND, 4)
        with open(os.path.join(d, "keep.txt"), "w") as fh:
            fh.write("intro\n" + old_line)
        with open(os.path.join(d, "move.txt"), "w") as fh:
            fh.write("m = -%s\n" % _in_band(rng, LON_BAND, 4))
        with open(os.path.join(d, BASELINE), "w") as fh:
            fh.write("# test\n2\n")
        git(d, "add", "-A"); git(d, *g, "commit", "-q", "-m", "base")
        base = git(d, "rev-parse", "HEAD").decode().strip()
        quiet = lambda *a: None                                               # noqa: E731
        row("PC10 the tree at its baseline passes", check(d, None, quiet) == 0)
        git(d, "mv", "move.txt", "moved.txt")
        with open(os.path.join(d, "keep.txt"), "a") as fh:
            fh.write("b = %s\n" % _in_band(rng, LAT_BAND, 3))
        with open(os.path.join(d, BASELINE), "w") as fh:
            fh.write("# test\n3\n")
        git(d, "add", "-A"); git(d, *g, "commit", "-q", "-m", "head")
        got = []
        rc = check(d, base, got.append)
        row("PC11 --diff refuses exactly the added line, not the moved file",
            rc == 1 and [x for x in got if x.startswith("REFUSED added")] == ["REFUSED added keep.txt:3"])
        row("PC12 --diff refuses a raised baseline", any(x.startswith("REFUSED raised") for x in got))
        with open(os.path.join(d, BASELINE), "w") as fh:
            fh.write("# test\n2\n")
        got = []
        check(d, None, got.append)
        row("PC13 a count above the baseline is refused", any(x.startswith("REFUSED count") for x in got))
        with open(os.path.join(d, BASELINE), "w") as fh:
            fh.write("# test\n9\n")
        got = []
        check(d, None, got.append)
        row("PC14 a count below the baseline is refused as stale", any(x.startswith("REFUSED stale") for x in got))
    finally:
        def _onerr(fn, p, exc):
            os.chmod(p, 0o700); fn(p)
        shutil.rmtree(d, onerror=_onerr)
    # PC22: .step/.stp excluded by type (ruled): the same in-band number counts in a .txt and not in a .stp/.STEP.
    d2 = tempfile.mkdtemp(prefix="pcoords-type-")
    try:
        git(d2, "init", "-q", "-b", "main")
        v = "x = %s\n" % la4
        for name in ("part.stp", "part.STEP", "note.txt"):
            with open(os.path.join(d2, name), "w") as fh:
                fh.write(v)
        git(d2, "add", "-A")
        st = {}
        tot, where, _ = scan_tree(d2, st)
        row("PC22 an in-band number in .stp/.STEP is not counted; the same number in a .txt is",
            tot == 1 and where == ["note.txt:1"] and st["type_excluded"] == 2)
    finally:
        def _onerr2(fn, p, exc):
            os.chmod(p, 0o700); fn(p)
        shutil.rmtree(d2, onerror=_onerr2)
    off = [r for r, ok in rows if not ok]
    print("PRECISE-COORDS SELFTEST %s" % ("PASSED (%d rows)" % len(rows) if not off else "FAILED: %d row(s) off" % len(off)))
    return 0 if not off else 1


# Each sabotage edits a copy of this file; the rows it must turn red are frozen here, and nothing else may move.
SABOTAGES = [
    ("S1 the band test always false", "if LAT_BAND[0] <= v <= LAT_BAND[1] or LON_BAND[0] <= v <= LON_BAND[1]:",
     "if False:", {"PC1", "PC2", "PC3", "PC4", "PC5", "PC7", "PC10", "PC11", "PC13", "PC15", "PC16", "PC17", "PC18",
                   "PC20", "PC21", "PC22"}),
    ("S2 3 dp no longer enough (MIN_DP 4)", "MIN_DP = 3", "MIN_DP = 4", {"PC7", "PC11", "PC13"}),
    ("S3 renames not followed", '"diff", "-M", "-U0"', '"diff", "--no-renames", "-U0"', {"PC11"}),
    ("S4 the ratchet's stale direction off", "    if total < want:", "    if False:", {"PC14"}),
    ("S5 escapes not decoded", "    return ESC.sub(r, line)", "    return line", {"PC15", "PC16", "PC17", "PC18"}),
    ("S6 the clock exemption widened to any colon directly before",
     'CLOCK = re.compile(rb"(?<![0-9])[0-9]{1,2}:[0-9]{2}:$")', 'CLOCK = re.compile(rb":$")', {"PC21"}),
    ("S7 the .step/.stp type exclusion off", 'EXEMPT_TYPES = (".step", ".stp")', "EXEMPT_TYPES = ()", {"PC22"}),
]


def sabotage():
    me = open(os.path.abspath(__file__), encoding="utf-8").read()
    bad = 0
    for name, a, b, want in SABOTAGES:
        table = me.index("SABOTAGES = [")
        if me.count(a) != 2 or me.index(a) > table:      # once in the code, once in this table
            print("BAD %s: its anchor is not once in the code (stale plant)" % name); bad += 1; continue
        d = tempfile.mkdtemp(prefix="pcsab-")
        try:
            os.makedirs(os.path.join(d, "scripts"))
            cp = os.path.join(d, "scripts", "check_precise_coords.py")
            with open(cp, "w", encoding="utf-8") as fh:
                fh.write(me.replace(a, b, 1))
            p = subprocess.run([sys.executable, cp, "--selftest"], capture_output=True, text=True)
            red = {l.split()[1] for l in p.stdout.splitlines() if l.startswith("  BAD ")}
        finally:
            shutil.rmtree(d, ignore_errors=True)
        ok = red == want
        bad += not ok
        print("  %s %s -> red %s%s" % ("ok " if ok else "BAD", name, ",".join(sorted(red)) or "none",
                                       "" if ok else " (frozen: %s)" % ",".join(sorted(want))))
    print("PRECISE-COORDS SABOTAGE %s" % ("PASSED" if not bad else "FAILED: %d plant(s) off" % bad))
    return 0 if not bad else 1


if __name__ == "__main__":
    a = sys.argv[1:]
    if a == ["--selftest"]:
        sys.exit(selftest())
    if a == ["--sabotage"]:
        sys.exit(sabotage())
    if a == ["--list"]:
        st = {}
        total, where, read = scan_tree(ROOT, st)
        for w in where:
            print(w)
        print("count %d in %d line(s); %d text files read; not counted by ruling: %d clock-time seconds, %d .step/.stp files"
              % (total, len(where), read, st["clock_exempt"], st["type_excluded"]))
        sys.exit(0 if read else 2)
    if len(a) == 2 and a[0] == "--diff":
        sys.exit(check(ROOT, a[1]))
    if not a:
        sys.exit(check(ROOT))
    print(__doc__)
    sys.exit(2)
