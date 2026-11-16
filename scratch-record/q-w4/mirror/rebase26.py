# SCRATCH (map-server): step8_26.pred Amendment 5b, keep26/b's comparison RE-BASED. READ-ONLY.
# <keepA>/files.json is tree B before the coverage and tile-bits steps (keep26/b, c06f40a99549); <keepB>/files.json is tree B after them
# (tree25_cmp.py keep). Each is {"files": {path: [bytes, sha256]}, "count": n}.
# Graded: the paths only in B are EXACTLY the four the two steps write; none only in A; every common path's [bytes, sha256] equal.
# GREEN: A against itself reads 0 added, 0 removed, 0 changed. RED: A against A with one entry's sha256 changed in memory reads 1 changed.
# The four are named (top-level files, no cell paths); any other added or removed path is COUNTED, never printed.
# Exit 0 as predicted; 1 off; 3 INSTRUMENT.
#   python -X utf8 -I mirror/rebase26.py <keepA> <keepB>
import copy, hashlib, json, os, sys

FOUR = {"coverage-tiles.json", "store/coverage.bin", "store/regions.json", "store/terrain-sources.bin"}


def load(k):
    d = json.load(open(os.path.join(k, "files.json"), encoding="utf-8"))
    if d["count"] != len(d["files"]):
        raise ValueError("count does not match the list")
    return d["files"]


def diff(a, b):
    added = set(b) - set(a)
    removed = set(a) - set(b)
    changed = sum(1 for p in set(a) & set(b) if a[p] != b[p])
    return added, removed, changed


def main():
    ka, kb = sys.argv[1], sys.argv[2]
    if hashlib.sha256(open(os.path.join(ka, "files.json"), "rb").read()).hexdigest()[:12] != "c06f40a99549":
        print("INSTRUMENT: keepA's files.json is not c06f40a99549"); return 3
    try:
        a, b = load(ka), load(kb)
    except (OSError, ValueError, KeyError) as e:
        print(f"INSTRUMENT: {e.__class__.__name__}"); return 3
    off = 0
    g = diff(a, a)
    ok = not g[0] and not g[1] and g[2] == 0
    off += not ok
    print(f"GREEN: A against itself: added {len(g[0])}, removed {len(g[1])}, changed {g[2]}; {'AS PREDICTED' if ok else 'OFF'}")
    r_b = copy.deepcopy(a)
    p0 = sorted(r_b)[0]
    r_b[p0] = [r_b[p0][0], "0" * 64]
    r = diff(a, r_b)
    ok = not r[0] and not r[1] and r[2] == 1
    off += not ok
    print(f"RED: one sha256 changed in memory: added {len(r[0])}, removed {len(r[1])}, changed {r[2]}; {'AS PREDICTED' if ok else 'OFF'}")
    added, removed, changed = diff(a, b)
    named = sorted(added & FOUR)
    other = len(added - FOUR)
    ok = added == FOUR and not removed and changed == 0
    off += not ok
    print(f"RE-BASED: A {len(a)} files, B {len(b)} files; added {len(added)} (the four: {named}; others {other}); removed {len(removed)}; changed {changed}")
    print(f"RE-BASED: {'AS PREDICTED' if ok else 'OFF'}")
    return 1 if off else 0


if __name__ == "__main__":
    sys.exit(main())
