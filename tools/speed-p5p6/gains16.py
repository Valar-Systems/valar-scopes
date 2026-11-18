#!/usr/bin/env python3
"""docs/speed-p5p6.md section 16 (accepted at beb11f43): the gains bench rows, graded AFTER the window, no board.

    gains16.py --g5-read F --g6-read F --g5-seal F --g6-seal F --g5-bin F --g6-bin F --a0a F --pix S6EQ,S7EQ,PAIR
        exit 0 every row as predicted; 1 a cut off (its first off: to the reviewer); 2 a STOP row; 3 outside the set
    gains16.py --selftest     the frozen plants on synthetic rows in gains_read.py's own format

Inputs: gains_read.py ccd198d75058's outputs (each `R-<arm> REPORTED kind <k>; fr p50 ..; fr-wait p50 ..; upd=.. ..
flush=..; push avg ..; rebuild frames n`, gains_read.py:207-210, and its `[gains] VERDICT:` line); the sealed traces'
whole [speed-p5]/[speed-p6] lines; the two flashed .bin files; A0a's a0a-read-wx-c3.out (information row); and the
three pixel exits (anchor_compare --equal on S6's and S7's three grabs, then --pair S6 grab 1 vs S7 grab 1).
The rows run in section 16's order; a global STOP ends the read; each cut is graded on its own after that.
"""
import hashlib
import re
import sys

SHAS = {"g5": "26d917a2baed", "g6": "ee3f7fd3fe0f", "a0a": "30734bc77e4b"}
READ_OK = "[gains] VERDICT: READ AS GRADED (all graded rows PASS)"
PHASES = ["upd", "wait", "under", "sweep", "sw_raster", "sw_blend", "sw_beam", "static", "contacts", "outline",
          "lbl_layout", "lbl_draw", "rest", "handover", "flush"]
S0_NAMES = {"g5": [b"clear-nomark", b"1a-OLD1", b"MK-OFF2", b"blipscope-s3-128-gains-p5"],
            "g6": [b"labels-today-still", b"6-STILL-OLD", b"6-STILL-NEW", b"blipscope-s3-128-gains-p6"]}
# (cut, image, own phase, OLD arms, NEW arms, band lo, band hi, A0a's P1@40 value, the base-gap tolerance (NEW))
CUTS = [("1a", "g5", "outline", ("1a-OLD1", "1a-OLD2"), ("1a-NEW1", "1a-NEW2"), 3.77, 6.42, 7.55, 1.51),
        ("1b", "g5", "sw_blend", ("1b-OLD1", "1b-OLD2"), ("1b-NEW1", "1b-NEW2"), 1.53, 3.06, 3.83, 0.77),
        ("1c", "g5", "under", ("1c-OLD1", "1c-OLD2"), ("1c-NEW1", "1c-NEW2"), 2.20, 4.67, 5.49, 1.10),
        ("P6", "g6", "lbl_layout", ("6-OLD1", "6-OLD2"), ("6-NEW1", "6-NEW2"), 3.31, 5.96, 6.62, 1.32)]
# "every other phase" EXCLUDES (ruled by the reviewer ~07:5xZ, section 16a, below the frozen text): (a) the cut's own
# phase; (b) its PARENT span; (c) for P6, lbl_draw (15b-1's one-sided guard governs it). The span tree, from the
# instrument (include/FrameProfile.h at 1d9e91740e1e): sweep = Span(B_UNDER, B_SWEEP) (:144) holds sw_raster /
# sw_blend / sw_beam = Span(B_SWR..B_SWB / B_SWB..B_SWM / B_SWM..B_SWE) (:146-148; the reader's I-8 holds their sum to
# sweep within 0.3, gains_read.py:142-143); contacts = loop - outline - lbl_layout - lbl_draw (:160) and rest = draw -
# static - loop (:161) are REMAINDERS, so a cycle span's cut moves neither; loop and draw are never reported. So sweep is
# the ONLY parent of any cut's phase. TOTALS (fr, fr-wait) are not in PHASES: never bounded, reported per cut.
OTHER_EXCLUDE = {"1a": {"outline"}, "1b": {"sw_blend", "sweep"}, "1c": {"under"}, "P6": {"lbl_layout", "lbl_draw"}}
MK_ARMS = ("MK-ON1", "MK-OFF1", "MK-ON2", "MK-OFF2")
MK_PHASES = ("contacts", "outline", "lbl_draw")
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
        arms[arm] = {"kind": kind, "ph": vals, "rebuild": int(n), "tot": tot}
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
    # X: the inputs are the registered ones
    for k in ("g5", "g6", "a0a"):
        if inp["sha"][k] != inp["expect"][k]:
            g.row("G-SHA", 3, False, f"{k} sha {inp['sha'][k]} is not {inp['expect'][k]}")
            return g
    g.row("G-SHA", 3, True, "the flashed images and A0a's output are the registered ones")
    try:
        r5, v5 = parse_read(inp["g5_read"], "g5")
        r6, v6 = parse_read(inp["g6_read"], "g6")
    except Outside as e:
        g.row("G-PARSE", 3, False, str(e))
        return g
    if not g.row("G-READER", 2, v5 == READ_OK and v6 == READ_OK, f"the reader: g5 {v5!r}; g6 {v6!r}"):
        return g
    miss5 = [n for n in S0_NAMES["g5"] if n not in inp["g5_bin"]]
    miss6 = [n for n in S0_NAMES["g6"] if n not in inp["g6_bin"]]
    if not g.row("G-S0", 2, not miss5 and not miss6, f"each image carries its list's names (missing g5 {miss5}, g6 {miss6})"):
        return g
    c5 = re.findall(r"\[speed-p5\] clear S9 frames (\d+) mismatched-px (\d+)", inp["g5_seal"])
    c6 = re.findall(r"\[speed-p6\] labels S0 checked (\d+) mismatches (\d+)", inp["g6_seal"])
    ok = len(c5) == 1 and int(c5[0][0]) > 0 and c5[0][1] == "0" and len(c6) == 1 and int(c6[0][0]) > 0 and c6[0][1] == "0"
    if not g.row("G-UNTIMED", 2, ok, f"1c-CHECK {c5}; 6-CHECK {c6} (each exactly one, N > 0, 0 mismatched)"):
        return g
    need = {"g5": [a for c in CUTS if c[1] == "g5" for a in c[3] + c[4]] + list(MK_ARMS),
            "g6": [a for c in CUTS if c[1] == "g6" for a in c[3] + c[4]]}
    miss = [a for a in need["g5"] if a not in r5] + [a for a in need["g6"] if a not in r6]
    if not g.row("G-PARSE", 3, not miss, f"every graded arm present (missing {miss})"):
        return g
    a0a = inp["a0a_p1_40"]
    for cut, img, ph, olds, news, lo, hi, a0v, tol in CUTS:
        R = r5 if img == "g5" else r6
        arms4 = list(olds) + list(news)
        rb = [R[a]["rebuild"] for a in arms4]
        if not g.row(f"{cut}-REBUILD", 2, all(x <= 1 for x in rb), f"rebuild frames {rb} (each <= 1, else that arm VOID)"):
            continue
        if not g.row(f"{cut}-EQUALREB", 1, len(set(rb)) == 1, f"rebuild frames {rb} equal across the cut's four arms"):
            continue
        o1, o2 = R[olds[0]]["ph"][ph], R[olds[1]]["ph"][ph]
        n1, n2 = R[news[0]]["ph"][ph], R[news[1]]["ph"][ph]
        base = abs((o1 + o2) / 2 - a0v)
        if not g.row(f"{cut}-BASE", 2, base <= tol, f"mean OLD {(o1 + o2) / 2:.2f} vs A0a {a0v:.2f}: {base:.2f} (<= {tol:.2f})"):
            continue
        N = abs(o1 - o2)
        g1, g2 = o1 - n1, o2 - n2
        gain = (g1 + g2) / 2
        if gain > (o1 + o2) / 2:
            g.row(f"{cut}-X", 3, False, f"a gain {gain:.2f} above OLD")
            continue
        stop = max(N, 0.3)
        if not g.row(f"{cut}-STOP", 2, -g1 <= stop and -g2 <= stop, f"NEW slower than OLD by {-g1:+.2f} / {-g2:+.2f} (<= {stop:.2f})"):
            continue
        g.row(f"{cut}-COUNT", 1, g1 > N and g2 > N, f"g1 {g1:.2f}, g2 {g2:.2f} > N {N:.2f}")
        g.row(f"{cut}-BAND", 1, lo <= gain <= hi, f"gain {gain:.2f} in [{lo:.2f}, {hi:.2f}]")
        bound = max(N, 0.5)
        bounded = [p for p in PHASES if p not in OTHER_EXCLUDE[cut]]
        print(f"  info  {cut}-BOUNDS     bounded: {bounded}; excluded: {sorted(OTHER_EXCLUDE[cut])}; totals reported only")
        moved = [(p, round(abs(R[o]["ph"][p] - R[n]["ph"][p]), 2)) for p in bounded
                 for o, n in zip(olds, news) if abs(R[o]["ph"][p] - R[n]["ph"][p]) > bound]
        g.row(f"{cut}-OTHER", 2, not moved, f"other phases within {bound:.2f} (over: {moved})")
        tots = {k: [round(R[o]["tot"][k] - R[n]["tot"][k], 2) for o, n in zip(olds, news)] for k in ("fr", "fr-wait")}
        print(f"  info  {cut}-TOTALS     OLD - NEW per pair: {tots} (reported, never bounded)")
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
            fell = any(f > bound for f in falls)
            g.row("P6-FALL", 1, (not fell) or pix_equal,
                  f"lbl_draw fall {falls} ms: " + ("attributed to format() (the render EQUAL)" if fell and pix_equal else
                                                    "UNEXPLAINED (the render not shown EQUAL)" if fell else "no fall beyond the bound"))
    # 1c's marks, graded within the run (15b-2)
    mk = [r5[a]["ph"] for a in MK_ARMS]
    over = []
    for p in MK_PHASES:
        nmk = abs(mk[0][p] - mk[2][p])
        for on, off in ((mk[0], mk[1]), (mk[2], mk[3])):
            if on[p] - off[p] > max(nmk, 0.5):
                over.append((p, round(on[p] - off[p], 2)))
    g.row("MK-MARKS", 1, not over, f"marks on - off in {MK_PHASES} within max(N_MK, 0.5) (over: {over})")
    # information: OLD against A0a, every phase, per cut
    for cut, img, ph, olds, news, *_ in CUTS:
        R = r5 if img == "g5" else r6
        if all(a in R for a in olds) and a0a:
            d = {p: round((R[olds[0]]["ph"][p] + R[olds[1]]["ph"][p]) / 2 - a0a.get(p, 0.0), 2) for p in PHASES}
            print(f"  info  {cut}-OLDvsA0A   {d}")
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
    g5b, g6b, a0 = rd("--g5-bin"), rd("--g6-bin"), rd("--a0a")
    pix = tuple(int(x) for x in opt["--pix"].split(","))
    inp = {"sha": {"g5": sha(g5b), "g6": sha(g6b), "a0a": sha(a0)}, "expect": SHAS,
           "g5_read": rd("--g5-read").decode("utf-8", "replace"), "g6_read": rd("--g6-read").decode("utf-8", "replace"),
           "g5_seal": rd("--g5-seal").decode("utf-8", "replace"), "g6_seal": rd("--g6-seal").decode("utf-8", "replace"),
           "g5_bin": g5b, "g6_bin": g6b, "a0a_p1_40": a0a_p1_40(a0.decode("utf-8", "replace")), "pix": pix}
    g = grade(inp)
    print(f"GAINS16: exit {g.worst} ({['every row as predicted', 'a cut off', 'a STOP row', 'outside the set'][g.worst]})")
    return g.worst


# ---------------- the selftest: the frozen plants ----------------
BASE = {"upd": 14.88, "wait": 0.01, "under": 5.49, "sweep": 6.02, "sw_raster": 2.07, "sw_blend": 3.83, "sw_beam": 0.09,
        "static": 3.23, "contacts": 12.63, "outline": 7.55, "lbl_layout": 6.62, "lbl_draw": 9.55, "rest": 1.00,
        "handover": 0.50, "flush": 0.30}


def synth(mod=None):
    """A clean synthetic pair of reader outputs; mod(arms5, arms6) edits it before it is printed."""
    a5 = {a: dict(BASE) for a in ["O-CTRL", "1a-OLD1", "1a-NEW1", "1a-OLD2", "1a-NEW2", "1b-OLD1", "1b-NEW1",
                                  "1b-OLD2", "1b-NEW2", "1c-OLD1", "1c-NEW1", "1c-OLD2", "1c-NEW2"] + list(MK_ARMS)}
    a6 = {a: dict(BASE) for a in ["P-CTRL", "6-OLD1", "6-NEW1", "6-OLD2", "6-NEW2"]}
    for a, v in (("1a-OLD1", 7.60), ("1a-OLD2", 7.50), ("1a-NEW1", 2.40), ("1a-NEW2", 2.50)):
        a5[a]["outline"] = v
    for a, b, s in (("1b-OLD1", 3.83, 6.02), ("1b-OLD2", 3.80, 5.99), ("1b-NEW1", 1.50, 3.69), ("1b-NEW2", 1.55, 3.74)):
        a5[a]["sw_blend"], a5[a]["sweep"] = b, s
    for a, v in (("1c-OLD1", 5.49), ("1c-OLD2", 5.45), ("1c-NEW1", 2.20), ("1c-NEW2", 2.25)):
        a5[a]["under"] = v
    for a, c, o, d in (("MK-ON1", 12.63, 2.45, 9.55), ("MK-OFF1", 12.40, 2.40, 9.40), ("MK-ON2", 12.60, 2.45, 9.55),
                       ("MK-OFF2", 12.38, 2.41, 9.42)):
        a5[a]["contacts"], a5[a]["outline"], a5[a]["lbl_draw"] = c, o, d
    for a, l, d in (("6-OLD1", 6.62, 9.55), ("6-OLD2", 6.60, 9.55), ("6-NEW1", 2.00, 8.00), ("6-NEW2", 1.95, 8.05)):
        a6[a]["lbl_layout"], a6[a]["lbl_draw"] = l, d
    reb5 = {a: 1 for a in a5}
    reb6 = {a: 1 for a in a6}
    drop = set()
    if mod:
        mod(a5, a6, reb5, reb6, drop)

    def text(arms, reb):
        out = []
        for a, ph in arms.items():
            if a in drop:
                continue
            s = " ".join(f"{p}={ph[p]:.2f}" for p in PHASES)
            out.append(f"R-{a:<11} {'REPORTED':<10} kind x; fr p50 60.00; fr-wait p50 59.99; {s}; push avg 24.17; "
                       f"rebuild frames {reb[a]}")
        out.append(READ_OK)
        return "\n".join(out) + "\n"
    return text(a5, reb5), text(a6, reb6)


def selftest():
    def inp(mod=None, pix=(0, 0, 0)):
        r5, r6 = synth(mod)
        return {"sha": dict(SHAS), "expect": dict(SHAS), "g5_read": r5, "g6_read": r6,
                "g5_seal": "x [speed-p5] clear S9 frames 500 mismatched-px 0\n",
                "g6_seal": "x [speed-p6] labels S0 checked 900 mismatches 0\n",
                "g5_bin": b"..".join(S0_NAMES["g5"]), "g6_bin": b"..".join(S0_NAMES["g6"]),
                "a0a_p1_40": dict(BASE), "pix": pix}

    def setv(img, arm, key, val):
        def f(a5, a6, reb5, reb6, drop):
            (a5 if img == 5 else a6)[arm][key] = val
        return f

    def setrb(arm, val):
        def f(a5, a6, reb5, reb6, drop):
            reb5[arm] = val
        return f

    def both(*fs):
        def f(*a):
            for x in fs:
                x(*a)
        return f

    def dropper(arm):
        def f(a5, a6, reb5, reb6, drop):
            drop.add(arm)
        return f

    def CORRECT_1B(a5, a6, reb5, reb6, drop):
        """Every 1b arm at BASE, then NEW: sw_blend -2.3 and sweep -2.3; nothing else moves."""
        for a in ("1b-OLD1", "1b-OLD2", "1b-NEW1", "1b-NEW2"):
            a5[a] = dict(BASE)
        for a in ("1b-NEW1", "1b-NEW2"):
            a5[a]["sw_blend"] = BASE["sw_blend"] - 2.3
            a5[a]["sweep"] = BASE["sweep"] - 2.3

    # (name, input, the expected exit, the expected set of rows NOT ok) -- frozen before the first run
    PLANTS = [
        ("CLEAN", inp(), 0, set()),
        ("P1 a gain below its band", inp(both(setv(5, "1a-NEW1", "outline", 4.00), setv(5, "1a-NEW2", "outline", 4.10))), 1, {"1a-BAND"}),
        ("P2 a gain above its band", inp(both(setv(5, "1a-NEW1", "outline", 1.00), setv(5, "1a-NEW2", "outline", 1.00))), 1, {"1a-BAND"}),
        ("P3 g2 <= N", inp(both(setv(5, "1c-OLD2", "under", 6.70), setv(5, "1c-NEW2", "under", 5.50))), 1, {"1c-COUNT"}),
        ("P4 NEW slower than the stop", inp(setv(5, "1b-NEW1", "sw_blend", 4.50)), 2, {"1b-STOP"}),
        ("P5 another phase over its bound", inp(setv(5, "1a-NEW1", "contacts", 13.50)), 2, {"1a-OTHER"}),
        ("P6 lbl_draw rising", inp(setv(6, "6-NEW1", "lbl_draw", 10.50)), 2, {"P6-DRAWRISE"}),
        ("P7 lbl_draw falling, the render NOT EQUAL", inp(pix=(0, 0, 1)), 1, {"P6-PIX", "P6-FALL"}),
        ("P8 a rebuild count of 2", inp(setrb("1b-OLD1", 2)), 2, {"1b-REBUILD"}),
        ("P9 unequal rebuild counts", inp(setrb("1c-NEW1", 0)), 1, {"1c-EQUALREB"}),
        ("P10 a base gap over its tolerance", inp(both(setv(6, "6-OLD1", "lbl_layout", 8.20), setv(6, "6-OLD2", "lbl_layout", 8.20),
                                                       setv(6, "6-NEW1", "lbl_layout", 3.60), setv(6, "6-NEW2", "lbl_layout", 3.60))), 2, {"P6-BASE"}),
        ("P11 the MK marks over their bound", inp(setv(5, "MK-OFF1", "contacts", 11.50)), 1, {"MK-MARKS"}),
        ("P12 a missing arm", inp(dropper("1a-NEW2")), 3, {"G-PARSE"}),
        # 16a's two (the reviewer's): the parent excluded, the sibling still bounded
        ("P13 a correct 1b (sw_blend -2.3, sweep -2.3, all else 0)", inp(CORRECT_1B), 0, set()),
        ("P14 P13 with sw_raster +1.0", inp(both(CORRECT_1B, setv(5, "1b-NEW1", "sw_raster", BASE["sw_raster"] + 1.0))),
         2, {"1b-OTHER"}),
    ]
    bad = 0
    for name, i, want_exit, want_off in PLANTS:
        print(f"--- {name}")
        g = grade(i)
        got = set(g.off())
        ok = g.worst == want_exit and got == want_off
        bad += not ok
        print(f"{'ok  ' if ok else 'FAIL'}  {name}: exit {g.worst} (want {want_exit}), off {sorted(got)} (want {sorted(want_off)})")
    print(f"GAINS16 SELFTEST: {len(PLANTS)} cases, {bad} FAIL")
    return 1 if bad else 0


if __name__ == "__main__":
    a = sys.argv[1:]
    if a == ["--selftest"]:
        sys.exit(selftest())
    if len(a) == 16:
        sys.exit(run(a))
    print(__doc__)
    sys.exit(3)
