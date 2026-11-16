# SCRATCH (map-server): step8_25.pred step 6, THE REINDEX, read on tree B's MANIFEST (step8_26.pred Amendment 5a). READ-ONLY.
# A store file's index is its position in the URL-sorted manifest. Each common file's index in tree B's manifest is compared with its
# index in sorted(the ORIGINAL plan's urls + tiles.json's urls), the order the served store's files had.
# Before reading: tiles.json's sha256 must be 5d868a475cd5 (coverage, which runs in 5b, rewrites it), and the manifest's urls must be
# in sorted order (else a position is not an index: INSTRUMENT).
# One function measures every case, so the controls test the code that makes the reading:
#   GREEN: the original order against itself -> 0 moved, 0 new, 0 dropped.
#   RED:   the original order against itself with one url planted that sorts first -> every file moves (len of the order), 1 new.
#   AGREE: the 2026-11-15T16:52:24Z method (the PLACED plan + tiles instead of the manifest) gives the same moved count and layers.
# Prints counts and layer names only. Exit 0 read; 1 a control off; 3 INSTRUMENT.
#   python -X utf8 -I reindex26.py
import hashlib, json, os, sys
from collections import Counter

HERE = os.path.dirname(os.path.abspath(__file__))
C = os.path.join(os.path.dirname(os.path.dirname(HERE)), "bm-cache-qw4")
sha = lambda p: hashlib.sha256(open(p, "rb").read()).hexdigest()[:12]
load = lambda p: json.load(open(p, encoding="utf-8"))["files"]


def measure(old, new):
    """old, new: url lists, each sorted. Returns (in both, moved urls, positions of urls new to old, count dropped from old)."""
    io = {u: i for i, u in enumerate(old)}
    inn = {u: i for i, u in enumerate(new)}
    both = [u for u in new if u in io]
    moved = [u for u in both if io[u] != inn[u]]
    return len(both), moved, sorted(inn[u] for u in new if u not in io), sum(1 for u in old if u not in inn)


def main():
    tj = os.path.join(C, "tiles.json")
    if sha(tj) != "5d868a475cd5":
        print(f"INSTRUMENT: tiles.json is {sha(tj)}, not 5d868a475cd5"); return 3
    orig = load(os.path.join(HERE, "plan.original.f12d3eb56040.json"))
    placed = load(os.path.join(C, "plan.json"))
    tiles = load(tj)
    man = load(os.path.join(C, "dry-run", "store", "manifest.json"))
    murls = [f["url"] for f in man]
    if murls != sorted(murls):
        print("INSTRUMENT: the manifest's urls are not sorted, so a position is not a store index"); return 3
    lay = {f["url"]: f["layer"] for f in orig + placed + tiles + man}
    uo = sorted(f["url"] for f in orig + tiles)
    off = 0
    g = measure(uo, uo)
    ok = g[0] == len(uo) and not g[1] and not g[2] and g[3] == 0
    off += not ok
    print(f"GREEN: the original order against itself: moved {len(g[1])}, new {len(g[2])}, dropped {g[3]}; {'AS PREDICTED' if ok else 'OFF'}")
    plant = "!planted-sorts-first"
    r = measure(uo, [plant] + uo)
    ok = len(r[1]) == len(uo) and r[2] == [0] and r[3] == 0
    off += not ok
    print(f"RED: one url planted first: moved {len(r[1])} of {len(uo)}, new at {r[2]}; {'AS PREDICTED' if ok else 'OFF'}")
    n, moved, new, dropped = measure(uo, murls)
    by = dict(sorted(Counter(lay[u] for u in moved).items()))
    print(f"REINDEX: manifest entries {len(murls)}; original order {len(uo)}; in both {n}; dropped {dropped}; index changed {len(moved)}")
    print(f"  by layer: {by}")
    print(f"  the files new to the order sit at {new} of {len(murls)}")
    un = sorted(f["url"] for f in placed + tiles)
    a = measure(uo, un)
    aby = dict(sorted(Counter(lay[u] for u in a[1]).items()))
    ok = len(a[1]) == len(moved) and aby == by and un == murls
    off += not ok
    print(f"AGREE: the placed-plan method moves {len(a[1])}, same layers {aby == by}; placed order equals the manifest's {un == murls}; "
          f"{'AS PREDICTED' if ok else 'OFF'}")
    return 1 if off else 0


if __name__ == "__main__":
    sys.exit(main())
