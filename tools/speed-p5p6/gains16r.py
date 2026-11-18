#!/usr/bin/env python3
"""docs/speed-p5p6.md 16h (accepted at 5c95d8df1c32), on 16c's rules: the fresh run's grader for 1a (G5') and P6 (G6).
AFTER the window, no board.

    gains16r.py --g5p-read F --g6-read F --g6-seal F --g5p-bin F --g6-bin F --g5-bin F --a0a F --pix S6EQ,S7EQ,PAIR
        exit 0 every row as predicted; 1 a cut off (its first off: to the reviewer); 2 a STOP row; 3 outside the set
    gains16r.py --selftest     the frozen plants on synthetic rows in gains_read.py's own format

Inputs: gains_read.py 6df9e8f43b55's outputs (--list g5p and --list g6: each `R-<arm> REPORTED kind <k>; fr p50 ..;
fr-wait p50 ..; upd=.. flush=..; push avg ..; rebuild frames n` and the `[gains] VERDICT:` line); G6's sealed trace (its
whole [speed-p6] lines); the two flashed .bin files; G5's .bin (X-MARKS's control); A0a's a0a-read-wx-c3.out; the three
pixel exits (anchor_compare --equal on S6's and S7's grabs, then --pair S6-1 vs S7-1).

BLIND BY CONSTRUCTION (16c/16h): per cut, X-MARKS (1a) and X-OVERLAP run FIRST, on the OLD arms only; a cut's NEW-derived
numbers are computed and printed only after both pass. A STOPped cut prints no NEW value. G5''s four 1b arms are
INFORMATION: their reader rows print last, verbatim, and no graded row reads them.
"""
import contextlib
import hashlib
import io
import re
import sys

SHAS = {"g5p": "8147e9d883e1", "g6": "ee3f7fd3fe0f", "g5": "26d917a2baed", "a0a": "30734bc77e4b"}
READ_OK = "[gains] VERDICT: READ AS GRADED (all graded rows PASS)"
PHASES = ["upd", "wait", "under", "sweep", "sw_raster", "sw_blend", "sw_beam", "static", "contacts", "outline",
          "lbl_layout", "lbl_draw", "rest", "handover", "flush"]
S0_NAMES = {"g5p": [b"blipscope-s3-128-gains-p5b", b"1a-OLD1", b"1b-NEW2"],
            "g6": [b"labels-today-still", b"6-STILL-OLD", b"6-STILL-NEW", b"blipscope-s3-128-gains-p6"]}
MARKS = [b"clear-old", b"clear-dirty", b"clear-check", b"clear-nomark"]
# (cut, image, own phase, OLD arms, NEW arms, 15a's fraction band lo, hi, A0a's P1@40 value)
CUTS = [("1a", "g5p", "outline", ("1a-OLD1", "1a-OLD2"), ("1a-NEW1", "1a-NEW2"), 0.50, 0.85, 7.55),
        ("P6", "g6", "lbl_layout", ("6-OLD1", "6-OLD2"), ("6-NEW1", "6-NEW2"), 0.50, 0.90, 6.62)]
OTHER_EXCLUDE = {"1a": {"outline"}, "P6": {"lbl_layout", "lbl_draw"}}   # 16a: the cut's phase; for P6, 15b-1's lbl_draw
INFO_ARMS = ("1b-OLD1", "1b-NEW1", "1b-OLD2", "1b-NEW2")
NUM = re.compile(r"^-?\d+(?:\.\d+)?$")


class Outside(Exception):
    pass


def parse_read(text, tag):
    arms, verdict = {}, None
    for ln in text.splitlines():
        if ln.startswith("[gains] VERDICT:"):
            verdict = ln.strip()
        m = re.match(r"^R-(\S+)\s+REPORTED\s+kind (\S+);(.*)$", ln)
        if not m:
            continue
        arm, kind, rest = m.group(1), m.group(2), m.group(3)
        if arm in arms:
            raise Outside(f"{tag}: R-{arm} twice")
        segs = [s.strip() for s in rest.split(";")]
        ph = next((s for s in segs if s.startswith("upd=")), None)
        rb = next((s for s in segs if s.startswith("rebuild frames ")), None)
        if ph is None or rb is None:
            raise Outside(f"{tag}: R-{arm} has no phase or rebuild segment")
        vals = {}
        for tok in ph.split():
            k, _, v = tok.partition("=")
            if not NUM.match(v):
                raise Outside(f"{tag}: R-{arm} {k}={v!r} is not a number")
            vals[k] = float(v)
        miss = [p for p in PHASES if p not in vals]
        if miss:
            raise Outside(f"{tag}: R-{arm} misses {miss}")
        if any(vals[p] < 0 for p in PHASES):
            raise Outside(f"{tag}: R-{arm} has a p50 below 0")
        n = rb.split()[-1]
        if not n.isdigit():
            raise Outside(f"{tag}: R-{arm} rebuild frames {n!r}")
        tot = {}
        for key, label in (("fr", "fr p50 "), ("fr-wait", "fr-wait p50 ")):
            s = next((x for x in segs if x.startswith(label)), None)
            if s is None or not NUM.match(s[len(label):]):
                raise Outside(f"{tag}: R-{arm} has no {label.strip()} total")
            tot[key] = float(s[len(label):])
        arms[arm] = {"kind": kind, "ph": vals, "rebuild": int(n), "tot": tot, "line": ln.rstrip()}
    return arms, verdict


class Grade:
    def __init__(self):
        self.rows, self.worst = [], 0

    def row(self, rid, sev, ok, text):
        """sev: the exit this row gives when it fails (1 off, 2 STOP, 3 outside the set)."""
        self.rows.append((rid, ok, sev, text))
        print(f"  {'ok  ' if ok else ('OFF ' if sev == 1 else 'STOP' if sev == 2 else 'X   ')}  {rid:13s} {text}")
        if not ok:
            self.worst = max(self.worst, sev)
        return ok

    def off(self):
        return [r for r, ok, _, _ in self.rows if not ok]


def grade(inp, g=None):
    g = g or Grade()
    for k in ("g5p", "g6", "g5", "a0a"):
        if inp["sha"][k] != inp["expect"][k]:
            g.row("G-SHA", 3, False, f"{k} sha {inp['sha'][k]} is not {inp['expect'][k]}")
            return g
    g.row("G-SHA", 3, True, "the flashed images, G5's control image and A0a's output are the registered ones")
    try:
        rp, vp = parse_read(inp["g5p_read"], "g5p")
        r6, v6 = parse_read(inp["g6_read"], "g6")
    except Outside as e:
        g.row("G-PARSE", 3, False, str(e))
        return g
    if not g.row("G-READER", 2, vp == READ_OK and v6 == READ_OK, f"the reader: g5p {vp!r}; g6 {v6!r}"):
        return g
    missp = [n for n in S0_NAMES["g5p"] if n not in inp["g5p_bin"]]
    miss6 = [n for n in S0_NAMES["g6"] if n not in inp["g6_bin"]]
    if not g.row("G-S0", 2, not missp and not miss6, f"each image carries its list's names (missing g5p {missp}, g6 {miss6})"):
        return g
    ctl = [n for n in MARKS if n not in inp["g5_bin"]]
    if not g.row("G-XMARKS-CTL", 3, not ctl, f"X-MARKS's control: G5's image carries all four (missing {ctl})"):
        return g
    c6 = re.findall(r"\[speed-p6\] labels S0 checked (\d+) mismatches (\d+)", inp["g6_seal"])
    ok = len(c6) == 1 and int(c6[0][0]) > 0 and c6[0][1] == "0"
    if not g.row("G-UNTIMED", 2, ok, f"6-CHECK {c6} (exactly one, N > 0, 0 mismatches)"):
        return g
    need = {"g5p": [a for c in CUTS if c[1] == "g5p" for a in c[3] + c[4]] + list(INFO_ARMS),
            "g6": [a for c in CUTS if c[1] == "g6" for a in c[3] + c[4]]}
    miss = [a for a in need["g5p"] if a not in rp] + [a for a in need["g6"] if a not in r6]
    if not g.row("G-PARSE", 3, not miss, f"every graded and information arm present (missing {miss})"):
        return g
    for cut, img, ph, olds, news, lo, hi, a0v in CUTS:
        R = rp if img == "g5p" else r6
        # X first, on the OLD arms and the image only: nothing NEW is computed until both pass
        if cut == "1a":
            found = [n.decode() for n in MARKS if n in inp["g5p_bin"]]
            if not g.row("1a-XMARKS", 2, not found, f"G5''s image carries none of 1c's kind names (found {found})"):
                continue
        o1, o2 = R[olds[0]]["ph"][ph], R[olds[1]]["ph"][ph]
        old_run = (o1 + o2) / 2
        rlo, rhi = lo / hi, hi / lo
        ratio = old_run / a0v
        if not g.row(f"{cut}-XOVERLAP", 2, rlo <= ratio <= rhi,
                     f"OLD_run {old_run:.2f} / A0a {a0v:.2f} = {ratio:.3f} in [{rlo:.3f}, {rhi:.3f}]"):
            continue
        arms4 = list(olds) + list(news)
        rb = [R[a]["rebuild"] for a in arms4]
        if not g.row(f"{cut}-REBUILD", 2, all(x <= 1 for x in rb), f"rebuild frames {rb} (each <= 1, else that arm VOID)"):
            continue
        if not g.row(f"{cut}-EQUALREB", 1, len(set(rb)) == 1, f"rebuild frames {rb} equal across the cut's four arms"):
            continue
        n1, n2 = R[news[0]]["ph"][ph], R[news[1]]["ph"][ph]
        N = abs(o1 - o2)
        g1, g2 = o1 - n1, o2 - n2
        gain = (g1 + g2) / 2
        if gain > old_run:
            g.row(f"{cut}-X", 3, False, f"a gain {gain:.2f} above OLD_run {old_run:.2f}")
            continue
        stop = max(N, 0.3)
        if not g.row(f"{cut}-STOP", 2, -g1 <= stop and -g2 <= stop, f"NEW slower than OLD by {-g1:+.2f} / {-g2:+.2f} (<= {stop:.2f})"):
            continue
        g.row(f"{cut}-COUNT", 1, g1 > N and g2 > N, f"g1 {g1:.2f}, g2 {g2:.2f} > N {N:.2f}")
        f = gain / old_run
        g.row(f"{cut}-BAND", 1, lo <= f <= hi, f"f = gain {gain:.2f} / OLD_run {old_run:.2f} = {f:.3f} in [{lo:.2f}, {hi:.2f}]")
        bound = max(N, 0.5)
        bounded = [p for p in PHASES if p not in OTHER_EXCLUDE[cut]]
        print(f"  info  {cut}-BOUNDS     bounded: {bounded}; excluded: {sorted(OTHER_EXCLUDE[cut])}; totals by FR-CARRY")
        moved = [(p, round(abs(R[o]["ph"][p] - R[n]["ph"][p]), 2)) for p in bounded
                 for o, n in zip(olds, news) if abs(R[o]["ph"][p] - R[n]["ph"][p]) > bound]
        g.row(f"{cut}-OTHER", 2, not moved, f"other phases within {bound:.2f} (over: {moved})")
        nfr = abs(R[olds[0]]["tot"]["fr"] - R[olds[1]]["tot"]["fr"])
        tol = max(nfr, 0.5)
        frg = [R[o]["tot"]["fr"] - R[n]["tot"]["fr"] for o, n in zip(olds, news)]
        short = [round(gi - x, 2) for gi, x in zip((g1, g2), frg)]
        g.row(f"{cut}-FRCARRY", 1, all(s <= tol for s in short),
              f"fr gain {[round(x, 2) for x in frg]} vs g {[round(g1, 2), round(g2, 2)]}: short {short} (<= {tol:.2f})")
        if cut == "P6":
            rises = [R[n]["ph"]["lbl_draw"] - R[o]["ph"]["lbl_draw"] for o, n in zip(olds, news)]
            g.row("P6-DRAWRISE", 2, all(x <= bound for x in rises), f"lbl_draw rise {[round(x, 2) for x in rises]} (<= {bound:.2f})")
            s6, s7, pair = inp["pix"]
            if s6 != 0 or s7 != 0 or pair not in (0, 1):
                g.row("P6-PIX", 2, False, f"VOID: --equal S6 {s6}, S7 {s7}, --pair {pair}")
                pix_equal = False
            else:
                pix_equal = g.row("P6-PIX", 1, pair == 0, f"S6 vs S7 --pair exit {pair} (0 EQUAL)")
            falls = [round(-x, 2) for x in rises]
            fell = any(x > bound for x in falls)
            g.row("P6-FALL", 1, (not fell) or pix_equal,
                  f"lbl_draw fall {falls} ms: " + ("attributed to format() (the render EQUAL)" if fell and pix_equal else
                                                    "UNEXPLAINED (the render not shown EQUAL)" if fell else "no fall beyond the bound"))
    a0a = inp["a0a_p1_40"]
    for cut, img, ph, olds, news, *_ in CUTS:   # information: the OLD arms against A0a (OLD only; never a NEW value)
        R = rp if img == "g5p" else r6
        if a0a:
            d = {p: round((R[olds[0]]["ph"][p] + R[olds[1]]["ph"][p]) / 2 - a0a.get(p, 0.0), 2) for p in PHASES}
            print(f"  info  {cut}-OLDvsA0A   {d}")
    print("  info  1b, information (never graded; 16h): the reader's rows, verbatim")
    for a in INFO_ARMS:
        print(f"        {rp[a]['line']}")
    return g


def a0a_p1_40(text):
    for ln in text.splitlines():
        m = re.match(r"^\s*R-P1@40\s+REPORTED\s+(.*)$", ln)
        if m:
            seg = next(s.strip() for s in m.group(1).split(";") if s.strip().startswith("upd="))
            return {k: float(v) for k, v in (t.split("=") for t in seg.split())}
    return {}


def sha(b):
    return hashlib.sha256(b).hexdigest()[:12]


def run(a):
    opt = {a[i]: a[i + 1] for i in range(0, len(a) - 1, 2)}
    rd = lambda k: open(opt[k], "rb").read()
    bp, b6, b5, a0 = rd("--g5p-bin"), rd("--g6-bin"), rd("--g5-bin"), rd("--a0a")
    pix = tuple(int(x) for x in opt["--pix"].split(","))
    inp = {"sha": {"g5p": sha(bp), "g6": sha(b6), "g5": sha(b5), "a0a": sha(a0)}, "expect": SHAS,
           "g5p_read": rd("--g5p-read").decode("utf-8", "replace"), "g6_read": rd("--g6-read").decode("utf-8", "replace"),
           "g6_seal": rd("--g6-seal").decode("utf-8", "replace"),
           "g5p_bin": bp, "g6_bin": b6, "g5_bin": b5, "a0a_p1_40": a0a_p1_40(a0.decode("utf-8", "replace")), "pix": pix}
    g = grade(inp)
    print(f"GAINS16R: exit {g.worst} ({['every row as predicted', 'a cut off', 'a STOP row', 'outside the set'][g.worst]})")
    return g.worst


# ---------------- the selftest: the frozen plants ----------------
BASE = {"upd": 14.88, "wait": 0.01, "under": 5.49, "sweep": 6.02, "sw_raster": 2.07, "sw_blend": 3.83, "sw_beam": 0.09,
        "static": 3.23, "contacts": 12.63, "outline": 7.55, "lbl_layout": 6.62, "lbl_draw": 9.55, "rest": 1.00,
        "handover": 0.50, "flush": 0.30}
FR0 = 70.00


def synth(mod=None):
    """A clean synthetic pair of reader outputs (g5p, g6). fr is explicit per arm: FR0 for OLD and control arms, and
    FR0 minus the arm's own-phase gain for a NEW arm, unless a plant set it; so FR-CARRY moves only on its own plants."""
    ap = {a: dict(BASE) for a in ["O-CTRL", "1a-OLD1", "1a-NEW1", "1a-OLD2", "1a-NEW2"] + list(INFO_ARMS)}
    a6 = {a: dict(BASE) for a in ["P-CTRL", "6-OLD1", "6-NEW1", "6-OLD2", "6-NEW2"]}
    for a, v in (("1a-OLD1", 7.60), ("1a-OLD2", 7.50), ("1a-NEW1", 2.40), ("1a-NEW2", 2.50)):
        ap[a]["outline"] = v                                   # f = 5.10 / 7.55 = 0.675
    for a, b, s in (("1b-OLD1", 3.83, 6.02), ("1b-OLD2", 3.80, 5.99), ("1b-NEW1", 1.50, 3.69), ("1b-NEW2", 1.55, 3.74)):
        ap[a]["sw_blend"], ap[a]["sweep"] = b, s
    for a, l, d in (("6-OLD1", 6.62, 9.55), ("6-OLD2", 6.60, 9.55), ("6-NEW1", 2.00, 8.00), ("6-NEW2", 1.95, 8.05)):
        a6[a]["lbl_layout"], a6[a]["lbl_draw"] = l, d          # f = 4.635 / 6.61 = 0.701
    fr = {}
    reb = {a: 1 for a in list(ap) + list(a6)}
    drop = set()
    if mod:
        mod(ap, a6, reb, fr, drop)
    for cut, img, ph, olds, news, *_ in CUTS:
        arms = ap if img == "g5p" else a6
        for o, n in zip(olds, news):
            fr.setdefault(o, FR0)
            fr.setdefault(n, FR0 - (arms[o][ph] - arms[n][ph]))

    def text(arms):
        out = []
        for a, phs in arms.items():
            if a in drop:
                continue
            s = " ".join(f"{p}={phs[p]:.2f}" for p in PHASES)
            f = fr.get(a, FR0)
            out.append(f"R-{a:<11} {'REPORTED':<10} kind x; fr p50 {f:.2f}; fr-wait p50 {f - 0.01:.2f}; {s}; push avg 24.17; "
                       f"rebuild frames {reb[a]}")
        out.append(READ_OK)
        return "\n".join(out) + "\n"
    return text(ap), text(a6)


def selftest():
    def inp(mod=None, pix=(0, 0, 0), g5p_bin=None, g5_bin=None, seal=None):
        rp, r6 = synth(mod)
        return {"sha": dict(SHAS), "expect": dict(SHAS), "g5p_read": rp, "g6_read": r6,
                "g6_seal": seal if seal is not None else "x [speed-p6] labels S0 checked 900 mismatches 0\n",
                "g5p_bin": g5p_bin if g5p_bin is not None else b"..".join(S0_NAMES["g5p"]),
                "g6_bin": b"..".join(S0_NAMES["g6"]),
                "g5_bin": g5_bin if g5_bin is not None else b"..".join(MARKS),
                "a0a_p1_40": dict(BASE), "pix": pix}

    def setv(img, arm, key, val):
        def f(ap, a6, reb, fr, drop):
            (ap if img == "p" else a6)[arm][key] = val
        return f

    def setfr(arm, val):
        def f(ap, a6, reb, fr, drop):
            fr[arm] = val
        return f

    def setrb(arm, val):
        def f(ap, a6, reb, fr, drop):
            reb[arm] = val
        return f

    def both(*fs):
        def f(*a):
            for x in fs:
                x(*a)
        return f

    def dropper(arm):
        def f(ap, a6, reb, fr, drop):
            drop.add(arm)
        return f

    def scale(img, olds, news, ph, old, frac):
        """OLD arms at `old`, NEW arms at old * (1 - frac): a correct cut on a moved base. NEW values distinctive."""
        fs = [setv(img, o, ph, old) for o in olds] + [setv(img, n, ph, round(old * (1 - frac), 2) + 0.004 * (i + 1))
                                                       for i, n in enumerate(news)]
        return both(*fs)

    P1A = (("1a-OLD1", "1a-OLD2"), ("1a-NEW1", "1a-NEW2"))
    PP6 = (("6-OLD1", "6-OLD2"), ("6-NEW1", "6-NEW2"))
    WILD_1B = both(*[setv("p", a, k, v) for a in INFO_ARMS for k, v in (("sw_blend", 50.0), ("sweep", 60.0))],
                   *[setfr(a, 999.0) for a in INFO_ARMS])
    # (name, input, the expected exit, the expected set of rows NOT ok, the arms whose NEW values must NOT print)
    PLANTS = [
        ("CLEAN", inp(), 0, set(), ()),
        ("P1 f below its band (1a, f 0.40)", inp(both(setv("p", "1a-NEW1", "outline", 4.56), setv("p", "1a-NEW2", "outline", 4.50))), 1, {"1a-BAND"}, ()),
        ("P2 f above its band (1a, f 0.95)", inp(both(setv("p", "1a-NEW1", "outline", 0.40), setv("p", "1a-NEW2", "outline", 0.35))), 1, {"1a-BAND"}, ()),
        ("P3 X-OVERLAP, 1a at 1.75 x A0a", inp(scale("p", *P1A, "outline", 13.21, 0.675)), 2, {"1a-XOVERLAP"}, P1A[1]),
        ("P4 X-OVERLAP, 1a at 0.55 x A0a", inp(scale("p", *P1A, "outline", 4.15, 0.675)), 2, {"1a-XOVERLAP"}, P1A[1]),
        # 16c said 1.75 x STOPs "both cuts"; P6's derived bound is [0.556, 1.800], so 1.75 x is INSIDE it (found by
        # arithmetic before the first run; 16i). 1.75 x is kept as the inside control; the STOP is planted at 1.85 x.
        ("P5a X-OVERLAP, P6 at 1.75 x A0a (inside P6's bound: passes)", inp(scale("6", *PP6, "lbl_layout", 11.59, 0.70)), 0, set(), ()),
        ("P5b X-OVERLAP, P6 at 1.85 x A0a", inp(scale("6", *PP6, "lbl_layout", 12.25, 0.70)), 2, {"P6-XOVERLAP"}, PP6[1]),
        ("P6 X-OVERLAP, P6 at 0.55 x A0a", inp(scale("6", *PP6, "lbl_layout", 3.64, 0.70)), 2, {"P6-XOVERLAP"}, PP6[1]),
        ("P7 FR-CARRY short by 0.6 (N_fr 0)", inp(setfr("1a-NEW1", FR0 - 5.20 + 0.60)), 1, {"1a-FRCARRY"}, ()),
        ("P8 FR-CARRY short by 0.4 (the boundary's control)", inp(setfr("1a-NEW1", FR0 - 5.20 + 0.40)), 0, set(), ()),
        ("P9 X-MARKS: a 1c kind name in G5''s image", inp(g5p_bin=b"..".join(S0_NAMES["g5p"] + [b"clear-dirty"])), 2, {"1a-XMARKS"}, P1A[1]),
        ("P10 X-MARKS's control blind (G5's image lacks clear-nomark)", inp(g5_bin=b"..".join(MARKS[:3])), 3, {"G-XMARKS-CTL"}, P1A[1] + PP6[1]),
        ("P11 a wild 1b changes no graded row", inp(WILD_1B), 0, set(), ()),
        # OLD 12.00 / 9.00 (N 3.00), NEW 2.30 / 6.10: g2 2.90 <= N while f = 6.30 / 10.50 = 0.60 stays in the band
        ("P12 g2 <= N (1a)", inp(both(setv("p", "1a-OLD1", "outline", 12.00), setv("p", "1a-OLD2", "outline", 9.00),
                                      setv("p", "1a-NEW1", "outline", 2.30), setv("p", "1a-NEW2", "outline", 6.10))), 1, {"1a-COUNT"}, ()),
        ("P13 NEW slower than the stop (P6)", inp(setv("6", "6-NEW1", "lbl_layout", 7.10)), 2, {"P6-STOP"}, ()),
        ("P14 another phase over its bound (1a)", inp(setv("p", "1a-NEW1", "contacts", 13.50)), 2, {"1a-OTHER"}, ()),
        ("P15 lbl_draw rising", inp(setv("6", "6-NEW1", "lbl_draw", 10.50)), 2, {"P6-DRAWRISE"}, ()),
        ("P16 lbl_draw falling, the render NOT EQUAL", inp(pix=(0, 0, 1)), 1, {"P6-PIX", "P6-FALL"}, ()),
        ("P17 a rebuild count of 2", inp(setrb("1a-OLD1", 2)), 2, {"1a-REBUILD"}, ()),
        ("P18 unequal rebuild counts", inp(setrb("6-NEW1", 0)), 1, {"P6-EQUALREB"}, ()),
        ("P19 a missing arm", inp(dropper("1a-NEW2")), 3, {"G-PARSE"}, ()),
        ("P20 6-CHECK with mismatches", inp(seal="x [speed-p6] labels S0 checked 900 mismatches 3\n"), 2, {"G-UNTIMED"}, ()),
        # the blinding check's own red case: 1a is NOT stopped in CLEAN, so its NEW values print and the output MUST move
        ("C-LEAK control: an unstopped cut's NEW values move the output", inp(), 0, set(), P1A[1]),
    ]
    clean_graded = None
    bad = 0
    for name, i, want_exit, want_off, blind in PLANTS:
        print(f"--- {name}")
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            g = grade(i)
        out = buf.getvalue()
        print(out, end="")
        got = set(g.off())
        ok = g.worst == want_exit and got == want_off
        leaks = []
        if blind:   # the blinding, DIFFERENTIALLY: change the STOPped cut's NEW own-phase values; the output must not move
            alt = dict(i)

            def shift(text, arms):
                out_ = []
                for ln in text.split("\n"):
                    for a in arms:
                        if ln.startswith(f"R-{a} "):
                            ph = "outline" if a.startswith("1a") else "lbl_layout"
                            ln = re.sub(rf"\b{ph}=(\d+\.\d+)", lambda m: f"{ph}={float(m.group(1)) + 0.37:.2f}", ln)
                    out_.append(ln)
                return "\n".join(out_)
            alt["g5p_read"] = shift(i["g5p_read"], [a for a in blind if a.startswith("1a")])
            alt["g6_read"] = shift(i["g6_read"], [a for a in blind if a.startswith("6-")])
            assert alt["g5p_read"] != i["g5p_read"] or alt["g6_read"] != i["g6_read"], f"{name}: the shift did not apply"
            buf2 = io.StringIO()
            with contextlib.redirect_stdout(buf2):
                grade(alt)
            if buf2.getvalue() != out:
                leaks = [ln for ln in buf2.getvalue().splitlines() if ln not in out.splitlines()][:3]
        ok = ok and (bool(leaks) if name.startswith("C-LEAK") else not leaks)
        graded = [ln for ln in out.splitlines() if not ln.startswith("        R-") and "1b, information" not in ln]
        if name == "CLEAN":
            clean_graded = graded
        if name.startswith("P11"):
            ok = ok and graded == clean_graded   # the wild 1b changed no graded line
        bad += not ok
        print(f"{'ok  ' if ok else 'FAIL'}  {name}: exit {g.worst} (want {want_exit}), off {sorted(got)} (want {sorted(want_off)})"
              + (f"; the output MOVED with the STOPped cut's NEW values: {leaks}" if leaks else
                 ("; blind: the output is byte-identical when the STOPped cut's NEW values change" if blind else ""))
              + ("; graded lines identical to CLEAN's" if name.startswith("P11") else ""))
    print(f"GAINS16R SELFTEST: {len(PLANTS)} cases, {bad} FAIL")
    return 1 if bad else 0


if __name__ == "__main__":
    a = sys.argv[1:]
    if a == ["--selftest"]:
        sys.exit(selftest())
    if len(a) == 16:
        sys.exit(run(a))
    print(__doc__)
    sys.exit(3)
