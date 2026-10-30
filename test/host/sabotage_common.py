"""Shared by the sabotage drivers (docs/weatherscope-stage3.md A12.50):

  - the --anchors pass (Q51): each driver lists its plants' anchors, and `anchors_mode` asserts every one occurs exactly
    once in the current tree, running no plant and compiling nothing. run.sh runs it for every driver, so the PR that
    strands an anchor fails in CI, not PRs later (sabotage_wx_layout.py's were stranded from #501 until 3.5t's t3).
  - the colour proof (Q50): a colour plant must survive to the panel. For every {r, g, b} triple an edit changes, the
    planted colour's RGB565 value (5-6-5, as the panel holds it) must differ from the original's, or the plant refuses
    to run. S-KD's first plant (64 -> 65 red) shared its 5 bits and so planted nothing.
  - the --control pass (Q57, A12.61): each driver's unplanted run alone, every suite's count checked against the
    driver's own table, nothing planted. run.sh runs it for every driver, so a driver that cannot run fails the PR
    that breaks it (sabotage_wx_themed_draw.py's stale counts left it unrunnable from #507 to the harness PR).

A driver imports this beside itself: drivers run under `python -I`, which leaves their own directory off sys.path.
"""
import os
import re
import sys

TRIPLE = re.compile(r"\{\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*\}")


class Refused(Exception):
    """A colour plant that cannot reach the panel. The driver reports it as REFUSED, never as a result."""


def p565(c):
    r, g, b = c
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


def triples(text):
    return [tuple(int(v) for v in t) for t in TRIPLE.findall(text)]


def prove_colour(name, old, new, pairs=None):
    """Refuses a plant whose changed colour keeps its RGB565 value. `pairs` names the colours explicitly, for a plant
    that puts a colour where the text held none (S-NL's literal in place of a role)."""
    if pairs is None:
        a, b = triples(old), triples(new)
        if not a and not b:
            return
        if len(a) != len(b):
            raise Refused(f"{name}: {len(a)} colour(s) replaced by {len(b)}; a colour plant must name the pairs it plants")
        pairs = list(zip(a, b))
    for x, y in pairs:
        if x != y and p565(x) == p565(y):
            raise Refused(f"{name}: {x} -> {y} is RGB565 {p565(x):#06x} both ways; the plant cannot reach the panel")


def anchors_mode(driver, entries):
    """entries: (plant, label, text, anchor). `anchor` is a string that must occur exactly once in `text`, or a callable
    run on `text` that raises (AssertionError, KeyError, ...) when its target is missing or ambiguous. `text` None means
    the file could not be read. Prints one line per failure and a summary; returns the exit code (0 all present)."""
    plants, bad = set(), []
    for plant, label, text, anchor in entries:
        plants.add(plant)
        if text is None:
            bad.append(f"{plant}: {label} cannot be read")
            continue
        if callable(anchor):
            try:
                anchor(text)
            except Exception as e:   # the plant's own target check, whatever it raises
                bad.append(f"{plant}: {label}: {type(e).__name__}: {str(e)[:120]}")
            continue
        n = text.count(anchor)
        if n != 1:
            bad.append(f"{plant}: {label}: anchor found {n} times, need exactly 1: {anchor.strip()[:80]!r}")
    for b in bad:
        print(f"  FAIL {b}")
    print(f"ANCHORS {driver}: {len(plants)} plant(s), {len(entries)} anchor(s), "
          + ("all present exactly once" if not bad else f"{len(bad)} STRANDED"))
    return 1 if bad else 0


def read(path):
    try:
        return open(path, "rb").read().decode("utf-8").replace("\r\n", "\n")
    except OSError:
        return None


def wants_anchors(argv=None):
    return "--anchors" in (sys.argv if argv is None else argv)


# --------------------------------------------------------------------------- --control (Q57, A12.61)
#
# A driver's unplanted run alone, checked against its own table, so a driver that cannot run fails the PR that breaks
# it (sabotage_wx_themed_draw.py read the RIG from #507 to the harness PR because nothing ran it). The driver's
# control() calls its OWN unplanted path; control_mode records every subprocess that path starts -- each compile maps
# its output to its test source, so each later run of that output is named by its suite; a script is named by its file
# -- and checks each suite against the table:
#   ("checks", N)  the suite's last "N checks, M failures" line: N, 0 failures, exit 0
#   ("ok", N)      N "ok" lines, no "FAIL" line, exit 0 (the suites that print no count)
#   ("line", text) the text in its output, exit 0
#   ("exit", code) its exit code
# Exit 0 every suite as the table; 1 a suite off, never run, or not in the table; 2 the rig; 3 SKIPPED (Skip's reason).

COUNT = re.compile(r"(\d+) checks?, (\d+) failures?")


class Skip(Exception):
    """A driver whose control cannot run here, for a stated reason. Printed and counted apart, never a pass."""


def wants_control(argv=None):
    return "--control" in (sys.argv if argv is None else argv)


def _norm(p):
    return os.path.normcase(os.path.abspath(str(p)))


def _script(args):
    for a in args[1:]:
        if str(a).endswith((".py", ".sh", ".mts", ".ts")):
            return os.path.basename(str(a))
    return None


def control_mode(driver, control_fn, table):
    """Runs control_fn (the driver's own unplanted path) with every subprocess recorded, and checks each suite against
    `table`. Prints one line per suite and a summary; returns the exit code."""
    import subprocess
    real = subprocess.run
    built, runs, bad = {}, [], []

    def rec(args, *a, **k):
        r = real(args, *a, **k)
        try:
            argv = [str(x) for x in (args if isinstance(args, (list, tuple)) else [args])]
            if "-o" in argv and any(x.endswith(".cpp") for x in argv):
                src = next(x for x in argv if x.endswith(".cpp"))
                suite = os.path.splitext(os.path.basename(src))[0]
                built[_norm(argv[argv.index("-o") + 1])] = suite
                if r.returncode != 0:
                    bad.append(f"{suite}: did not compile")
            elif _norm(argv[0]) in built:
                runs.append((built[_norm(argv[0])], r.returncode, r.stdout if isinstance(r.stdout, str) else ""))
            elif _script(argv):
                out = r.stdout if isinstance(r.stdout, str) else (r.stdout or b"").decode("utf-8", "replace")
                runs.append((_script(argv), r.returncode, out))
        except Exception as e:   # the recorder must never change what the driver sees
            bad.append(f"the recorder: {type(e).__name__}: {e}")
        return r

    subprocess.run = rec
    try:
        control_fn()
    except Skip as e:
        print(f"CONTROL {driver}: SKIPPED -- {e}")
        return 3
    except Exception as e:
        print(f"CONTROL {driver}: the RIG -- {type(e).__name__}: {e}")
        return 2
    finally:
        subprocess.run = real
    seen = set()
    for suite, rc, out in runs:
        seen.add(suite)
        want = table.get(suite)
        if want is None:
            bad.append(f"{suite}: ran, but is not in the driver's table")
            continue
        kind, val = want
        if kind == "checks":
            m = COUNT.findall(out)
            n, f = (int(m[-1][0]), int(m[-1][1])) if m else (None, None)
            ok, got = rc == 0 and n == val and f == 0, f"{n} checks, {f} failures, exit {rc}"
        elif kind == "ok":
            n = len(re.findall(r"^\s*ok\b", out, re.M))
            f = len(re.findall(r"^\s*FAIL\b", out, re.M))
            ok, got = rc == 0 and n == val and f == 0, f"{n} ok, {f} FAIL, exit {rc}"
        elif kind == "line":
            ok, got = rc == 0 and val in out, f"the line {'present' if val in out else 'ABSENT'}, exit {rc}"
        else:
            ok, got = rc == val, f"exit {rc}"
        print(f"  {'ok  ' if ok else 'FAIL'} {suite}: {got} (the table: {kind} {val})")
        if not ok:
            bad.append(f"{suite}: {got} against {kind} {val}")
    for suite in table:
        if suite not in seen:
            bad.append(f"{suite}: in the table, never ran")
    for b in bad:
        print(f"  FAIL {b}")
    print(f"CONTROL {driver}: {len(table)} suite(s), " + ("all as the table" if not bad else f"{len(bad)} OFF"))
    return 1 if bad else 0
