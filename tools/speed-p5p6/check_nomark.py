#!/usr/bin/env python3
"""docs/speed-p5p6.md 15c (1): CLEAR_NOMARK leaves CLEAR_OLD's pixels unchanged, BY CONSTRUCTION, checked statically.

  NM-WHOLE   in AircraftManager::DrawWeatherUnder, exactly once each and in this order: `bool whole = dirty == nullptr;`,
             then `if (framebench::ClearOld()) whole = true;`, then `if (whole) {` whose block holds the layer's
             `memcpy(dst, wxLayer565` -- so a frame with NO set and a CLEAR_OLD frame take the same whole copy
  NM-REGION  src/main.cpp holds `if (framebench::ClearNoMark()) canvas.SetDirty(nullptr);` exactly once, inside a
             `// THE FRAME BENCH` region, after the shipping SetDirty; ClearNoMark is the only kind that detaches the set

    python -I check_nomark.py <tree>             exit 0 both rows ok; 1 a row off; 2 usage or a file missing
    python -I check_nomark.py --selftest <tree>  the clean tree green; NM-NULL reads exactly NM-WHOLE red, NM-KIND exactly
                                                 NM-REGION red (each plant asserted applied before it is read)
"""
import os
import re
import shutil
import sys
import tempfile

AM, MAIN = "src/AircraftManager.cpp", "src/main.cpp"
WHOLE = "bool whole = dirty == nullptr;"
OLD = "if (framebench::ClearOld()) whole = true;"
SWITCH = "if (framebench::ClearNoMark()) canvas.SetDirty(nullptr);"
BEGIN, END = "// THE FRAME BENCH BEGIN", "// THE FRAME BENCH END"


def read(tree, rel):
    with open(os.path.join(tree, *rel.split("/")), encoding="utf-8", newline="") as f:
        return f.read()


def body_of(text, head):
    i = text.find(head)
    if i < 0:
        return None
    j = text.find("\n}\n", i)
    return text[i:j + 2] if j > 0 else None


def nm_whole(tree):
    b = body_of(read(tree, AM), "bool AircraftManager::DrawWeatherUnder(")
    if b is None:
        return False, "DrawWeatherUnder not found"
    counts = (b.count(WHOLE), b.count(OLD), b.count("if (whole) {"))
    if counts != (1, 1, 1):
        return False, f"counts whole/old/branch {counts}, not (1, 1, 1)"
    iw, io, ib = b.index(WHOLE), b.index(OLD), b.index("if (whole) {")
    if not iw < io < ib:
        return False, "the order is not whole, ClearOld, branch"
    blk = b[ib:b.index("}", ib)]
    if "memcpy(dst, wxLayer565" not in blk:
        return False, "the whole branch does not hold the layer's memcpy"
    return True, "a null set and CLEAR_OLD both take `if (whole)` and its memcpy"


def nm_region(tree):
    m = read(tree, MAIN)
    if m.count(SWITCH) != 1:
        return False, f"the switch appears {m.count(SWITCH)} times, not 1"
    i = m.index(SWITCH)
    b, e = m.rfind(BEGIN, 0, i), m.rfind(END, 0, i)
    if b < 0 or e > b:
        return False, "the switch is not inside a FRAME BENCH region"
    ship = m.rfind("canvas.SetDirty(appManager.WxDirty(", 0, i)
    if ship < 0 or ship > b:
        return False, "the region is not after the shipping SetDirty"
    other = [ln for ln in m.splitlines() if "SetDirty(nullptr)" in ln and SWITCH not in ln]
    if other:
        return False, f"another line detaches the set: {other[0].strip()[:80]}"
    return True, "ClearNoMark alone detaches the set, in a FRAME BENCH region after the shipping SetDirty"


def run(tree):
    rows = [("NM-WHOLE",) + nm_whole(tree), ("NM-REGION",) + nm_region(tree)]
    for rid, ok, why in rows:
        print(f"  {'ok  ' if ok else 'FAIL'}  {rid}  {why}")
    return rows


def plant(src_tree, rel, old, new, dst):
    for r in (AM, MAIN):
        os.makedirs(os.path.dirname(os.path.join(dst, *r.split("/"))), exist_ok=True)
        shutil.copyfile(os.path.join(src_tree, *r.split("/")), os.path.join(dst, *r.split("/")))
    p = os.path.join(dst, *rel.split("/"))
    with open(p, encoding="utf-8", newline="") as f:
        before = f.read()
    assert before.count(old) == 1, f"needle not exactly once: {old}"
    after = before.replace(old, new, 1)
    assert after != before
    with open(p, "w", encoding="utf-8", newline="") as f:
        f.write(after)
    with open(p, encoding="utf-8", newline="") as f:
        assert f.read() == after


def selftest(tree):
    print("clean:")
    clean = run(tree)
    good = all(ok for _, ok, _ in clean)
    t = tempfile.mkdtemp(prefix="nomark-")
    try:
        for name, rel, old, new, want in (
                ("NM-NULL", AM, WHOLE, "bool whole = false;", "NM-WHOLE"),
                ("NM-KIND", MAIN, SWITCH, "if (framebench::ClearOld()) canvas.SetDirty(nullptr);", "NM-REGION")):
            d = os.path.join(t, name)
            plant(tree, rel, old, new, d)
            print(f"{name}:")
            red = [rid for rid, ok, _ in run(d) if not ok]
            hit = red == [want]
            good = good and hit
            print(f"  {'ok  ' if hit else 'FAIL'}  plant {name}: red {red} (want ['{want}'])")
    finally:
        shutil.rmtree(t, ignore_errors=True)
    print("NOMARK SELFTEST " + ("PASSED" if good else "FAILED"))
    return 0 if good else 1


if __name__ == "__main__":
    a = sys.argv[1:]
    if len(a) == 2 and a[0] == "--selftest":
        sys.exit(selftest(a[1]))
    if len(a) == 1:
        try:
            sys.exit(0 if all(ok for _, ok, _ in run(a[0])) else 1)
        except FileNotFoundError as e:
            print(f"missing: {e}")
            sys.exit(2)
    print(__doc__)
    sys.exit(2)
