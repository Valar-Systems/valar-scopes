"""Grade build Q's own rows (docs/weatherscope-stage3.md A12.25, A12.29) from a run_q.py run; A9.19's set is
grade_b35.py's. Prints no coordinate.

    python -I tools/weatherscope/board/grade_q.py <run dir> <G's run dir>

  QBUF      "[wx] qbuf probe=57600 bendgrid=44640 psram_q=<102,240-102,400> ok=1"
  B2-G      both loops' 15 hashes each equal to G's, in order (the codes are unchanged; teal is a colour)
  B2-DRAW   every frame's draw_us <= G's largest + 2,150: 35,855 (the first loop), 35,825 (the warning loop)
  Q-READS   both loops' "[wx] qreads" equal the host's 15 counts, exactly, in order
  Q-DEC     48 "[wx] qdec" lines and "done"; every st=0; each crop's nz and fnv equal to the host's; every us in
            1,700-3,200
  Q-BOUND   B2-Bend's bound by A12.25's frozen formula, <= 38,000 (predicted about 35,900-36,500)
Exits 0 only if every row holds.
"""
import importlib.util, json, math, os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.dont_write_bytecode = True
_spec = importlib.util.spec_from_file_location("q_host", os.path.join(HERE, "q_host.py"))
qh = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(qh)

DISC, COPY_US, PEAK_PX, BAND_WRITES, CEIL = 36828, 2150, 1807, 16800, 38000
DRAW_MAX = (33705 + 2150, 33675 + 2150)   # G's largest (A9.27) + A10.3's bound, per loop


def kv(line):
    return dict(t.split("=", 1) for t in line.split() if "=" in t)


def lines_of(run):
    return [l for _, l in json.load(open(os.path.join(run, "lines.json")))]


def loops(lines):
    """[(lrec dicts, lprof dicts, qreads list or None)] per loop, in order."""
    out, cur = [], None
    for l in lines:
        if l.startswith("[wx] loop records="):
            cur = dict(lrec=[], lprof=[], qreads=None)
            out.append(cur)
        elif cur is not None and l.startswith("[wx] lrec "):
            cur["lrec"].append(kv(l))
        elif cur is not None and l.startswith("[wx] lprof "):
            cur["lprof"].append(kv(l))
        elif cur is not None and l.startswith("[wx] qreads"):
            cur["qreads"] = [int(x) for x in l.split()[2:]]
    return out


def main(argv):
    if len(argv) != 3:
        print(__doc__)
        return 2
    run, grun = argv[1], argv[2]
    L, G = lines_of(run), lines_of(grun)
    ok, rows = True, []

    def row(name, good, text):
        nonlocal ok
        ok &= bool(good)
        rows.append(f"  {'ok  ' if good else 'FAIL'}  {name}: {text}")

    qb = [kv(l) for l in L if l.startswith("[wx] qbuf ")]
    row("QBUF", len(qb) == 1 and qb[0].get("probe") == "57600" and qb[0].get("bendgrid") == "44640" and
        102240 <= int(qb[0].get("psram_q", "0")) <= 102400 and qb[0].get("ok") == "1",
        str(qb[0]) if qb else "no [wx] qbuf line (BLIND, so FAIL)")

    lq, lg = loops(L), loops(G)
    row("B2 loops", len(lq) == 2 and all(len(x["lrec"]) == 15 and len(x["lprof"]) == 15 for x in lq),
        f"{len(lq)} loops, records {[(len(x['lrec']), len(x['lprof'])) for x in lq]}")
    if len(lq) == 2 and len(lg) == 2:
        for k in range(2):
            same = [a["sha"] == b["sha"] for a, b in zip(lq[k]["lrec"], lg[k]["lrec"])]
            row(f"B2-G loop {k + 1}", len(same) == 15 and all(same), f"{sum(same)} of 15 hashes equal to G's")
            draws = [int(x["draw_us"]) for x in lq[k]["lrec"]]
            gd = [int(x["draw_us"]) for x in lg[k]["lrec"]]
            row(f"B2-DRAW loop {k + 1}", draws and max(draws) <= DRAW_MAX[k],
                f"draw {min(draws)}-{max(draws)} us (<= {DRAW_MAX[k]}); G's {min(gd)}-{max(gd)}; "
                f"mean {sum(draws) / 15:.0f} against G's {sum(gd) / 15:.0f} (+{sum(draws) / 15 - sum(gd) / 15:.0f}, informational)")
    # the host's counts, recomputed (q_host.py's own path)
    names = sorted(n for n in os.listdir(qh.r.LOOP) if n.startswith("shsr-") and n.endswith(".wxc"))
    host_reads = []
    for n in names:
        h, codes = qh.r.decode(os.path.join(qh.r.LOOP, n))
        host_reads.append(qh.dry_reads(h, codes, qh.r.View(h, 60)))
    for k, lp in enumerate(lq):
        row(f"Q-READS loop {k + 1}", lp["qreads"] == host_reads,
            f"device {lp['qreads']} {'==' if lp['qreads'] == host_reads else '!='} host {host_reads}")
    bend = []
    for n in sorted(f for f in os.listdir(qh.look.BEND) if f.endswith(".wxc")):
        h, codes = qh.r.decode(os.path.join(qh.look.BEND, n))
        bend.append(dict(nz=sum(1 for c in codes if c), fnv="%08x" % qh.fnv1a32(bytes(codes))))
    qd = [kv(l) for l in L if l.startswith("[wx] qdec r=")]
    done = any(l.startswith("[wx] qdec done") for l in L)
    good_n = len(qd) == 48 and done
    match = [d for d in qd if d.get("st") == "0" and int(d["nz"]) == bend[int(d["i"])]["nz"] and d["fnv"] == bend[int(d["i"])]["fnv"]]
    us = [int(d["us"]) for d in qd]
    row("Q-DEC lines", good_n, f"{len(qd)} qdec lines, done={done}")
    row("Q-DEC decode", len(match) == 48, f"{len(match)} of {len(qd)} with st=0 and nz, fnv equal to the host's")
    row("Q-DEC time", us and all(1700 <= u <= 3200 for u in us),
        f"us {min(us) if us else '-'}-{max(us) if us else '-'} (1,700-3,200); per crop max " +
        str([max(int(d['us']) for d in qd if d['i'] == str(i)) for i in range(16)] if len(qd) == 48 else "-"))
    # the bound, A12.25's formula
    if len(lq) == 2 and all(lp["qreads"] for lp in lq) and len(qd) == 48:
        frames = [x for lp in lq for x in lp["lrec"]]
        profs = [x for lp in lq for x in lp["lprof"]]
        D = max(int(x["draw_us"]) for x in frames)
        r_min = min(min(lp["qreads"]) for lp in lq)
        A = (DISC - r_min) * COPY_US / DISC
        dB = max(us)
        dD = min(int(x["decode"]) for x in profs)
        put_ns = max(int(x["bands"]) for x in profs) * 1000 / BAND_WRITES
        P = PEAK_PX * put_ns / 1000
        bound = int(math.ceil((D + A + dB - dD + P) / 10.0) * 10)
        row("Q-BOUND", bound <= CEIL,
            f"D {D} + A {A:.1f} (r_min {r_min}) + dB {dB} - dD {dD} + P {P:.1f} ({PEAK_PX} x {put_ns:.1f} ns) = {bound} us "
            f"(<= {CEIL}; predicted about 35,900-36,500{'' if 35900 <= bound <= 36500 else ' -- OUTSIDE the stated estimate'})")
    else:
        row("Q-BOUND", False, "cannot compute: a loop's qreads or the 48 decodes are missing (BLIND, so FAIL)")
    print("\n".join(rows))
    print("build Q: " + ("every row holds" if ok else "OFF-PREDICTION: stop and report"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
