# SCRATCH (map-server): step8_26.pred Amendment 5c.1, D1's comparator. READ-ONLY.
# A = sea-obj's pieces (pieces.mts on the bare store), B = tree B's (mirror/pieces26.json 09db20fc5a86). Each is {cell: [{src, outer, holes}]}
# in pieces.mts's order (cells sorted; within a cell, by store record number).
# sea-obj is a PARTIAL store (the cells around the 16 points), so only the cell keys present in A are compared, and each is classed:
#   identical    the same pieces in the same order, src included
#   reordered    the same pieces as a multiset, src included, in another order
#   geom-only    equal as a multiset once src is ignored, unequal with it (a name difference only)
#   subset       A's pieces a strict sub-multiset of B's (coverage: a store cell sea-obj does not hold), geometry and src
#   different    pieces on both sides that the other lacks (geometry differs)
# Also: the files' sha256; cell keys in A but not in B. Prints counts only: no coordinate, no cell key, no name.
# CONTROLS first, on A itself: A vs A all identical; A vs A with one piece's first x changed by 1e-9 -> 1 different;
# A vs A with the first two pieces of a cell holding >= 2 swapped -> 1 reordered.
#   python -X utf8 -I mirror/d1_26.py <A pieces.json> <B pieces.json>
import copy, hashlib, json, sys
from collections import Counter


def key(p, src=True):
    return json.dumps({"o": p["outer"], "h": p["holes"], **({"s": p["src"]} if src else {})}, separators=(",", ":"))


def classify(A, B):
    c = Counter()
    for k, a in A.items():
        b = B.get(k)
        if b is None:
            c["missing_in_B"] += 1; continue
        if [key(p) for p in a] == [key(p) for p in b]: c["identical"] += 1; continue
        ca, cb = Counter(key(p) for p in a), Counter(key(p) for p in b)
        if ca == cb: c["reordered"] += 1; continue
        if Counter(key(p, False) for p in a) == Counter(key(p, False) for p in b): c["geom-only"] += 1; continue
        if not (ca - cb): c["subset"] += 1; continue
        c["different"] += 1
    return c


def main():
    pa, pb = sys.argv[1], sys.argv[2]
    A = json.load(open(pa, encoding="utf-8")); B = json.load(open(pb, encoding="utf-8"))
    sha = lambda p: hashlib.sha256(open(p, "rb").read()).hexdigest()[:12]
    g = classify(A, A)
    a1 = copy.deepcopy(A); k1 = next(iter(a1)); a1[k1][0]["outer"][0][0] += 1e-9
    r1 = classify(A, a1)
    a2 = copy.deepcopy(A); k2 = next((k for k, v in a2.items() if len(v) >= 2), None)
    if k2 is not None: a2[k2][0], a2[k2][1] = a2[k2][1], a2[k2][0]
    r2 = classify(A, a2) if k2 is not None else Counter()
    ok = g == Counter({"identical": len(A)}) and r1["different"] == 1 and r1["identical"] == len(A) - 1 and r2["reordered"] == 1 and r2["identical"] == len(A) - 1
    print(f"CONTROLS: self {dict(g)}; one x moved {dict(r1)}; two swapped {dict(r2)}; {'AS PREDICTED' if ok else 'OFF: the comparator is not used'}")
    if not ok: return 1
    c = classify(A, B)
    na, nb = sum(len(v) for v in A.values()), sum(len(v) for v in B.values())
    print(f"D1: A {sha(pa)} ({len(A)} cells, {na} pieces); B {sha(pb)} ({len(B)} cells, {nb} pieces); A's cells: {dict(sorted(c.items()))}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
