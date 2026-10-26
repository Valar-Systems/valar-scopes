"""Grades item 2's runs against docs/weatherscope-stage3.md A9.42's rows.

    python -I tools/weatherscope/board/grade_serial.py W|X <M2 body.txt> <ring_probe trace.txt with 3 M1 + 3 M3 presses>

Write paths: (a) connected, us >= 15,000, short; (b) us <= 3,000 and written 0.
  W (2 ms, the sabotage):
    W-M2a  every short write is (a)                       W-M2b  no write is (b)
    W-M2c  at least one build with print >= 300,000 and frame >= 400,000
    W-M1 / W-M3  every write connected, us < 2,000, written in full; every build's print < 5,000
  X (0 ms, the fix):
    X-M2a  every write us < 1,000                         X-M2b  every build's print < 5,000
    X-M2c  every pushed build's frame <= the M1 builds' largest frame + 10,000
    X-M1 / X-M3  as W's, and no lock drop ((b)) in M1
Outside the set: a W-M2 short write that is neither (a) nor (b) is printed as such and fails W-M2a.
"""
import re, sys

BUILD = re.compile(r"\[wx\] build \d+ why=(\S+) scene=(\S+) mi=(\d+)->(\d+) .*? print=(\d+) total=(\d+) prints=(\d+) "
                   r"short=(\d+) frame=(\d+)")
WRITE = re.compile(r"\[wx\] write \d+ t=\d+ asked=(\d+) wrote=(\d+) free=(\d+) plugged=(\d) connected=(\d) us=(\d+)")


def parse(lines):
    builds = [dict(why=m[1], scene=m[2], print=int(m[5]), total=int(m[6]), short=int(m[8]), frame=int(m[9]))
              for m in (BUILD.search(l) for l in lines) if m]
    writes = [dict(asked=int(m[1]), wrote=int(m[2]), free=int(m[3]), plugged=int(m[4]), connected=int(m[5]),
                   us=int(m[6])) for m in (WRITE.search(l) for l in lines) if m]
    return builds, writes


def blocks(path):
    """The trace's 'R' blocks, each as its lines, in order; a block runs from '[wx] builds n=' to the next '#HOST'."""
    raw = [l.rstrip("\n").split(" ", 1)[1] if " " in l else l.rstrip("\n") for l in open(path, encoding="utf-8")]
    out, cur = [], None
    for l in raw:
        if l.startswith("[wx] builds n="):
            cur = []
            out.append(cur)
        if cur is not None:
            if l.startswith("#HOST") and "press" in l:
                cur = None
                continue
            cur.append(l)
    return out


def row(name, ok, detail):
    print(f"  {'ok  ' if ok else 'OFF '} {name}: {detail}")
    return ok


def main(argv):
    if len(argv) != 4 or argv[1] not in ("W", "X"):
        print(__doc__)
        return 2
    build, m2path, trace = argv[1], argv[2], argv[3]
    m2b, m2w = parse(open(m2path, encoding="utf-8").read().splitlines())
    bl = blocks(trace)
    if len(bl) < 7:
        print(f"BLIND: {len(bl)} R blocks in the trace (need the clearing one + 6 presses)")
        return 1
    m1 = [parse(b) for b in bl[1:4]]
    m3 = [parse(b) for b in bl[4:7]]
    ok = True
    print(f"build {build}; M2: {len(m2b)} builds, {len(m2w)} writes")
    shorts = [w for w in m2w if w["wrote"] < w["asked"]]
    if build == "W":
        a = [w for w in shorts if w["connected"] == 1 and w["us"] >= 15000]
        b = [w for w in m2w if w["wrote"] == 0 and w["us"] <= 3000]
        neither = [w for w in shorts if w not in a and w not in b]
        ok &= row("W-M2a", len(shorts) > 0 and not neither,
                  f"{len(shorts)} short writes, {len(a)} of them (a); neither (a) nor (b): {neither[:5]}")
        ok &= row("W-M2b", not b, f"{len(b)} writes on path (b)")
        big = [x for x in m2b if x["print"] >= 300000 and x["frame"] >= 400000]
        ok &= row("W-M2c", bool(big), f"builds with print >= 300,000 and frame >= 400,000: "
                                      f"{[(x['scene'], x['print'], x['frame']) for x in big]}")
    else:
        slow = [w for w in m2w if w["us"] >= 1000]
        ok &= row("X-M2a", bool(m2w) and not slow, f"{len(m2w)} writes; {len(slow)} at >= 1,000 us "
                                                   f"(max {max((w['us'] for w in m2w), default=0)})")
        slowp = [x for x in m2b if x["print"] >= 5000]
        ok &= row("X-M2b", bool(m2b) and not slowp, f"{len(m2b)} builds; prints {[x['print'] for x in m2b]}")
        m1max = max((x["frame"] for bb, _ in m1 for x in bb if x["frame"]), default=0)
        over = [x for x in m2b if x["frame"] and x["frame"] > m1max + 10000]
        ok &= row("X-M2c", m1max > 0 and not over, f"M1's largest build frame {m1max}; M2 build frames "
                                                    f"{[x['frame'] for x in m2b]}; over by more than 10,000: {len(over)}")
    for cond, sets in (("M1", m1), ("M3", m3)):
        ws = [w for _, ww in sets for w in ww]
        bs = [x for bb, _ in sets for x in bb]
        bad_w = [w for w in ws if not (w["connected"] == 1 and w["us"] < 2000 and w["wrote"] == w["asked"])]
        bad_b = [x for x in bs if x["print"] >= 5000]
        ok &= row(f"{build}-{cond}", bool(ws) and not bad_w and not bad_b,
                  f"{len(ws)} writes ({len(bad_w)} off: {bad_w[:3]}), {len(bs)} builds (print >= 5,000: "
                  f"{[x['print'] for x in bad_b]}); build frames {[x['frame'] for x in bs]}")
        if build == "X" and cond == "M1":
            drops = [w for w in ws if w["wrote"] == 0 and w["us"] <= 3000]
            ok &= row("X-M1 lock drops", not drops, f"{len(drops)}")
    print("AS PREDICTED" if ok else "OFF-PREDICTION: STOP")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
