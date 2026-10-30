"""Shared by the sabotage drivers (docs/weatherscope-stage3.md A12.50):

  - the --anchors pass (Q51): each driver lists its plants' anchors, and `anchors_mode` asserts every one occurs exactly
    once in the current tree, running no plant and compiling nothing. run.sh runs it for every driver, so the PR that
    strands an anchor fails in CI, not PRs later (sabotage_wx_layout.py's were stranded from #501 until 3.5t's t3).
  - the colour proof (Q50): a colour plant must survive to the panel. For every {r, g, b} triple an edit changes, the
    planted colour's RGB565 value (5-6-5, as the panel holds it) must differ from the original's, or the plant refuses
    to run. S-KD's first plant (64 -> 65 red) shared its 5 bits and so planted nothing.

A driver imports this beside itself: drivers run under `python -I`, which leaves their own directory off sys.path.
"""
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
