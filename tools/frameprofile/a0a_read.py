"""A0a's reader (docs/frame-profile.md on p4/big-screens-plan: section 0 (c)-(f2) and section 4), for the frame-profile
image `blipscope-s3-128-frameprofile` on map-client's levels bench (FrameBench.h + FrameProfileScenes.h at
bench/frame-profile-p1-final 1d9e91740e1e). Read-only: it reads one bench-trace log and prints rows; it writes nothing.

    python -I a0a_read.py <trace.log> [--pix ARM=EXIT ...]     # PIX-WX: map-client's pix_wx.py exit code per weather arm
    python -I a0a_read.py --selftest                          # every graded row's plant reads red; the clean control passes

THE FORMATS, read from the source at 1d9e91740e1e (quoted, not assumed):
  [frame-bench] the frame bench C1: %d scenes, settle %lu s, measure %lu s, push task %s          main.cpp:2157
  [frame-bench] scene S%d %s home=Bend level=%d wx=%s rung=%dmi settle                            main.cpp:2195/2226
  [frame-bench] scene S%d measure                                                                 main.cpp:2212
  [frame-bench] scene S%d end frames=%d over-cap=%d                                               FrameBench.h:487
  [frame-bench] push S%d: n %lu avg_us %lu sync_avg_us %lu; fb submits %lu                         FrameBench.h:500
  [frame-bench] dump S%d %d <hex of Rec{u16 fr10,u16 p2p10,u8 tracked,u8 drawn,u8 flags,u8 pres}>  FrameBench.h:536
  [frame-bench] dump S%d done %d                                                                  FrameBench.h:540
  [frame-profile] cost %lu ns/frame cpu=%lu                                                       FrameProfile.h:251
  [frame-profile] answered S%d %d of %d; missing:%s                                               FrameProfile.h:190
  [frame-profile] scene S%d end arm=%s frames=%d partial=%lu neg-contacts=%lu neg-rest=%lu        FrameProfile.h:265
  [frame-profile] dump S%d %d <hex of ProfRec{u16 ph[16]}, 10 us units, kPhaseName order>          FrameProfile.h:291
  [frame-profile] dump S%d done %d                                                                FrameProfile.h:295
  [m3] pushes=%lu push_avg_us=%lu push_max_us=%lu sync_avg_us=%lu sync_err=%lu loop_wait_avg_us=%lu ...   main.cpp:271
  [wx] layer rebuilt in N ms (look A) / the layer stays off / [wx] buffers FAILED / [wx] status=... outcome=...

THE ROWS, in the registered order. The first row that is OFF stops the read ("goes to the orchestrator"); an
INSTRUMENT row means nothing after it is graded. Every row below marked GRADED has a plant in --selftest.
  I-1  banner env=blipscope-s3-128-frameprofile, and the bench line "7 scenes ... push task on"            INSTRUMENT
  I-2  PC-1: "[frame-profile] cost" <= 100,000 ns/frame (section 4; > 100,000 is outside the set)            GRADED
  I-3  per scene: both dumps complete (no gap; done == records), bench frames == profile frames == end line  INSTRUMENT
  I-4  FIX-n: every measured frame's DRAWN == the scene's level (section 0, the STOP row)                     GRADED
  I-5  answered: "21 of 21; missing: none" in every scene (section 0 (e)); a scene below 21 is not graded    GRADED
  I-6  (c)-3: partial == 0 in every scene's end line, and no partial marker in its records                  GRADED
  I-7  (c)-1: neg-contacts == 0 and neg-rest == 0 in every scene's end line                                  GRADED
  I-8  (c)-2: weather arms: |p50(sw_raster + sw_blend + sw_beam) - p50(sweep)| <= 0.3 ms                     GRADED
  I-9  PG-1, weather arms: p50(outline) > 0 and p50(sw_blend) > 0; the STATE bit (flags & 4) on every
       measured frame; (b) exact: (i) a "layer rebuilt in ... (look A)" before the span ends, (ii) zero
       "the layer stays off" / "[wx] buffers FAILED" up to the span's end, (iii) every "[wx] status=" up to
       the span's end is 200 or 304 with an outcome that is not error; PIX-WX exit 0 for the arm            INSTRUMENT
  I-10 the guard: p50(lbl_draw) > 0 in every arm with contacts                                               INSTRUMENT
  I-11 (f2)'s cross-instrument wait: per measured span, the profile's mean `wait` against M3's own
       loop_wait_avg_us WINDOWED between the first and last [m3] lines inside the span, within 0.2 ms.
       ASSUMED (stated, not hidden): the window's wait count is its push count, since [m3] prints pushes and
       not g_waits; one frame's wait is one TakeForDrawing and one hand-over. A window whose reconstructed
       sum (avg x pushes) or push count decreases is refused (a wrap)                                       GRADED
  C-1  P-CTRL (f2): p50(lbl_draw) in P-CTRL@40 minus P1@40 = +5.0 +/- 0.75 ms                                 GRADED
  REPORTED, never graded: fr, wait, fr - wait and every phase, per arm and as P-CTRL - P1@40 deltas; the
       spin's earliest start t0 = upd + wait + under + sweep + static (p50, P-CTRL); section 4's table rows.
"""
import math
import os
import re
import struct
import sys

PHASES = ["upd", "wait", "under", "sweep", "sw_raster", "sw_blend", "sw_beam", "static", "contacts", "outline",
          "lbl_layout", "lbl_draw", "rest", "handover", "flush", "tail"]
PARTIAL = 0xFFFE
ARMS = ["P0@20", "P0@40", "P0@60", "P1@20", "P1@40", "P-CTRL@40", "P1@60"]    # FrameProfileScenes.h kArmName
LEVEL = [20, 40, 60, 20, 40, 40, 60]
WX = [False, False, False, True, True, True, True]
CTRL, CTRL_BASE = 5, 4

BANNER = "[build] env=blipscope-s3-128-frameprofile"
BENCH = re.compile(r"\[frame-bench\] the frame bench C1: (\d+) scenes, settle (\d+) s, measure (\d+) s, push task (on|off)")
SC_START = re.compile(r"\[frame-bench\] scene S(\d+) (\S+) home=Bend level=(\d+) wx=(true|false) rung=(\d+)mi settle")
SC_MEAS = re.compile(r"\[frame-bench\] scene S(\d+) measure\s*$")
SC_END = re.compile(r"\[frame-bench\] scene S(\d+) end frames=(\d+) over-cap=(\d+)")
FB_DUMP = re.compile(r"\[frame-bench\] dump S(\d+) (\d+) ([0-9a-f]+)\s*$")
FB_DONE = re.compile(r"\[frame-bench\] dump S(\d+) done (\d+)")
PUSH = re.compile(r"\[frame-bench\] push S(\d+): n (\d+) avg_us (\d+) sync_avg_us (\d+); fb submits (\d+)")
COST = re.compile(r"\[frame-profile\] cost (\d+) ns/frame cpu=(\d+)")
ANSW = re.compile(r"\[frame-profile\] answered S(\d+) (\d+) of (\d+); missing:(.*)$")
FP_END = re.compile(r"\[frame-profile\] scene S(\d+) end arm=(\S+) frames=(\d+) partial=(\d+) neg-contacts=(\d+) neg-rest=(\d+)")
FP_DUMP = re.compile(r"\[frame-profile\] dump S(\d+) (\d+) ([0-9a-f]+)\s*$")
FP_DONE = re.compile(r"\[frame-profile\] dump S(\d+) done (\d+)")
M3 = re.compile(r"\[m3\] pushes=(\d+) push_avg_us=(\d+) push_max_us=(\d+) sync_avg_us=(\d+) sync_err=(\d+) "
                r"loop_wait_avg_us=(\d+)")
WX_REBUILT = re.compile(r"\[wx\] layer rebuilt in \d+ ms \(look A\)")
WX_STATUS = re.compile(r"\[wx\] status=(-?\d+) .*outcome=(\S+)")
WX_OFF = ("the layer stays off", "[wx] buffers FAILED")


class Instrument(Exception):
    pass


def rank(xs, p):
    s = sorted(xs)
    return s[max(0, math.ceil(p / 100.0 * len(s)) - 1)]


def lines_of(path):
    b = open(path, "rb").read()
    t = b.decode("utf-16") if b[:2] in (b"\xff\xfe", b"\xfe\xff") else b.decode("utf-8", errors="replace")
    return t.splitlines()


def join_dump(parts, done, size, tag):
    blob, pos = b"", 0
    for start in sorted(parts):
        if start != pos:
            raise Instrument(f"I-3 {tag}: the dump has a gap at record {pos}")
        blob += parts[start]
        pos += len(parts[start]) // size
    if len(blob) % size:
        raise Instrument(f"I-3 {tag}: the dump is not whole records")
    n = len(blob) // size
    if done != n:
        raise Instrument(f"I-3 {tag}: the dump holds {n} records, its done line says {done}")
    return blob


def parse(lines):
    T = {"banner": 0, "bench": None, "cost": None, "sc": {}, "fbd": {}, "fbdone": {}, "fpd": {}, "fpdone": {},
         "answ": {}, "fpend": {}, "push": {}, "m3": [], "wx": []}
    for i, l in enumerate(lines):
        if BANNER in l:
            T["banner"] += 1
        m = BENCH.search(l)
        if m:
            T["bench"] = (int(m.group(1)), m.group(4))
        m = COST.search(l)
        if m:
            T["cost"] = (int(m.group(1)), int(m.group(2)))
        m = SC_START.search(l)
        if m:
            T["sc"].setdefault(int(m.group(1)), {}).update(start=i, level=int(m.group(3)), wx=m.group(4) == "true")
        m = SC_MEAS.search(l)
        if m:
            T["sc"].setdefault(int(m.group(1)), {})["meas"] = i
        m = SC_END.search(l)
        if m:
            T["sc"].setdefault(int(m.group(1)), {}).update(end=i, frames=int(m.group(2)), overcap=int(m.group(3)))
        m = FB_DUMP.search(l)
        if m:
            T["fbd"].setdefault(int(m.group(1)), {})[int(m.group(2))] = bytes.fromhex(m.group(3))
        m = FB_DONE.search(l)
        if m:
            T["fbdone"][int(m.group(1))] = int(m.group(2))
        m = FP_DUMP.search(l)
        if m:
            T["fpd"].setdefault(int(m.group(1)), {})[int(m.group(2))] = bytes.fromhex(m.group(3))
        m = FP_DONE.search(l)
        if m:
            T["fpdone"][int(m.group(1))] = int(m.group(2))
        m = ANSW.search(l)
        if m:
            T["answ"][int(m.group(1))] = (int(m.group(2)), int(m.group(3)), m.group(4).strip())
        m = FP_END.search(l)
        if m:
            T["fpend"][int(m.group(1))] = dict(arm=m.group(2), frames=int(m.group(3)), partial=int(m.group(4)),
                                               negc=int(m.group(5)), negr=int(m.group(6)))
        m = PUSH.search(l)
        if m:
            T["push"][int(m.group(1))] = dict(n=int(m.group(2)), avg=int(m.group(3)), sync=int(m.group(4)))
        m = M3.search(l)
        if m:
            T["m3"].append((i, int(m.group(1)), int(m.group(6))))
        if "[wx]" in l:
            T["wx"].append((i, l))
    return T


def ms(x10):
    return x10 / 100.0


def read(lines, pix):
    """Returns (rows, verdict). rows: list of (id, status, text); status in PASS / OFF / INSTRUMENT / REPORTED."""
    rows = []
    T = parse(lines)
    def row(i, ok, text, kind="GRADED"):
        rows.append((i, "PASS" if ok else ("INSTRUMENT" if kind == "INSTRUMENT" else "OFF"), text))
        return ok
    # I-1
    if not row("I-1", T["banner"] >= 1 and T["bench"] == (7, "on"),
               f"banner seen {T['banner']}x; bench line {T['bench']}", "INSTRUMENT"):
        return rows, "INSTRUMENT at I-1"
    # I-2 PC-1
    cost = T["cost"][0] if T["cost"] else None
    if not row("I-2", cost is not None and cost <= 100000, f"PC-1 cost {cost} ns/frame (<= 100,000), cpu "
               f"{T['cost'][1] if T['cost'] else '?'} MHz"):
        return rows, "STOP at I-2"
    # I-3 dumps
    S = {}
    try:
        for k in range(7):
            sc = T["sc"].get(k, {})
            if "meas" not in sc or "end" not in sc:
                raise Instrument(f"I-3 S{k}: no measure/end line")
            if sc.get("level") != LEVEL[k] or sc.get("wx") != WX[k]:
                raise Instrument(f"I-3 S{k}: scene line level {sc.get('level')} wx {sc.get('wx')}, not {LEVEL[k]}/{WX[k]}")
            fb = join_dump(T["fbd"].get(k, {}), T["fbdone"].get(k), 8, f"S{k} bench")
            fp = join_dump(T["fpd"].get(k, {}), T["fpdone"].get(k), 32, f"S{k} profile")
            recs = [struct.unpack("<HHBBBB", fb[j:j + 8]) for j in range(0, len(fb), 8)]
            prof = [struct.unpack("<16H", fp[j:j + 32]) for j in range(0, len(fp), 32)]
            end = T["fpend"].get(k)
            if end is None or end["arm"] != ARMS[k]:
                raise Instrument(f"I-3 S{k}: profile end line {end}")
            if not (len(recs) == len(prof) == sc["frames"] == end["frames"]):
                raise Instrument(f"I-3 S{k}: frames bench {len(recs)} profile {len(prof)} end {sc['frames']}/{end['frames']}")
            if not recs:
                raise Instrument(f"I-3 S{k}: no measured frames")
            S[k] = dict(recs=recs, prof=prof, end=end, sc=sc)
        row("I-3", True, "7 scenes, both dumps whole, frame counts agree", "INSTRUMENT")
    except Instrument as e:
        row("I-3", False, str(e), "INSTRUMENT")
        return rows, "INSTRUMENT at I-3"
    # I-4 FIX-n
    bad = {k: sum(1 for r in S[k]["recs"] if r[3] != LEVEL[k]) for k in S}
    if not row("I-4", all(v == 0 for v in bad.values()), "FIX-n: frames with drawn != level per scene " +
               ", ".join(f"S{k} {v}" for k, v in bad.items())):
        return rows, "STOP at I-4"
    # I-5 answered
    an = {k: T["answ"].get(k) for k in range(7)}
    okA = all(a and a[0] == 21 and a[1] == 21 and a[2] == "none" for a in an.values())
    if not row("I-5", okA, "answered: " + ", ".join(f"S{k} {a[0] if a else '-'} of 21" for k, a in an.items())):
        return rows, "STOP at I-5 (a scene below 21 is not graded)"
    # I-6 partial
    part = {k: (S[k]["end"]["partial"], sum(1 for p in S[k]["prof"] if p[15] == PARTIAL)) for k in S}
    if not row("I-6", all(a == 0 and b == 0 for a, b in part.values()), "partial frames (end line, records): " +
               ", ".join(f"S{k} {a}/{b}" for k, (a, b) in part.items())):
        return rows, "STOP at I-6"
    # I-7 remainders
    neg = {k: (S[k]["end"]["negc"], S[k]["end"]["negr"]) for k in S}
    if not row("I-7", all(a == 0 and b == 0 for a, b in neg.values()), "neg-contacts/neg-rest: " +
               ", ".join(f"S{k} {a}/{b}" for k, (a, b) in neg.items())):
        return rows, "STOP at I-7"
    def p50(k, name):
        return ms(rank([p[PHASES.index(name)] for p in S[k]["prof"]], 50))
    # I-8 sweep split
    split = {}
    for k in S:
        if WX[k]:
            ssum = ms(rank([p[4] + p[5] + p[6] for p in S[k]["prof"]], 50))
            split[k] = (ssum, p50(k, "sweep"))
    if not row("I-8", all(abs(a - b) <= 0.3 for a, b in split.values()), "sweep split p50(sum) vs p50(sweep): " +
               ", ".join(f"S{k} {a:.2f}/{b:.2f}" for k, (a, b) in split.items())):
        return rows, "STOP at I-8"
    # I-9 PG-1
    try:
        for k in S:
            if not WX[k]:
                continue
            if not (p50(k, "outline") > 0 and p50(k, "sw_blend") > 0):
                raise Instrument(f"S{k}: outline p50 {p50(k, 'outline'):.2f} / sw_blend p50 {p50(k, 'sw_blend'):.2f}")
            if any(not (r[4] & 4) for r in S[k]["recs"]):
                raise Instrument(f"S{k}: STATE: a measured frame without the weather crop shown")
            end_i = S[k]["sc"]["end"]
            upto = [l for (i, l) in T["wx"] if i <= end_i]
            if not any(WX_REBUILT.search(l) for l in upto):
                raise Instrument(f"S{k}: (b)(i) no 'layer rebuilt ... (look A)' before the span ends")
            if any(s in l for l in upto for s in WX_OFF):
                raise Instrument(f"S{k}: (b)(ii) a 'layer stays off' / 'buffers FAILED' line")
            for l in upto:
                m = WX_STATUS.search(l)
                if m and (m.group(1) not in ("200", "304") or m.group(2) == "error"):
                    raise Instrument(f"S{k}: (b)(iii) a [wx] status {m.group(1)} outcome {m.group(2)}")
            if pix.get(ARMS[k]) != 0:
                raise Instrument(f"S{k}: PIX-WX for {ARMS[k]} is {pix.get(ARMS[k], 'not provided')}, not exit 0")
        row("I-9", True, "PG-1 on every weather arm (outline, blend, STATE, (b)(i)-(iii), PIX-WX)", "INSTRUMENT")
    except Instrument as e:
        row("I-9", False, "PG-1 " + str(e), "INSTRUMENT")
        return rows, "INSTRUMENT at I-9 (the arm measured weather off)"
    # I-10 guard
    g = {k: p50(k, "lbl_draw") for k in S}
    if not row("I-10", all(v > 0 for v in g.values()), "lbl_draw p50 > 0: " + ", ".join(f"S{k} {v:.2f}" for k, v in g.items()),
               "INSTRUMENT"):
        return rows, "INSTRUMENT at I-10"
    # I-11 cross-instrument wait
    wtxt, wok = [], True
    for k in S:
        a, b = S[k]["sc"]["meas"], S[k]["sc"]["end"]
        win = [(i, n, w) for (i, n, w) in T["m3"] if a <= i <= b]
        if len(win) < 2:
            row("I-11", False, f"S{k}: fewer than two [m3] lines inside the span", "INSTRUMENT")
            return rows, "INSTRUMENT at I-11"
        (_, n1, w1), (_, n2, w2) = win[0], win[-1]
        s1, s2 = w1 * n1, w2 * n2
        if n2 <= n1 or s2 < s1:
            row("I-11", False, f"S{k}: the [m3] window decreases (a wrap)", "INSTRUMENT")
            return rows, "INSTRUMENT at I-11"
        m3w = (s2 - s1) / (n2 - n1) / 1000.0
        pw = sum(p[1] for p in S[k]["prof"]) / len(S[k]["prof"]) / 100.0
        wok &= abs(pw - m3w) <= 0.2
        wtxt.append(f"S{k} {pw:.3f}/{m3w:.3f}")
    if not row("I-11", wok, "wait: profile mean vs [m3] windowed (ms): " + ", ".join(wtxt)):
        return rows, "STOP at I-11"
    # C-1 P-CTRL
    d = p50(CTRL, "lbl_draw") - p50(CTRL_BASE, "lbl_draw")
    if not row("C-1", abs(d - 5.0) <= 0.75, f"P-CTRL: lbl_draw p50 {p50(CTRL, 'lbl_draw'):.2f} - P1@40 "
               f"{p50(CTRL_BASE, 'lbl_draw'):.2f} = {d:+.2f} ms (+5.0 +/- 0.75)"):
        return rows, "STOP at C-1 (labels not +5: what a short plant would look like)"
    # REPORTED
    def frp50(k):
        return ms(rank([r[0] for r in S[k]["recs"]], 50))
    for k in S:
        ph = " ".join(f"{n}={p50(k, n):.2f}" for n in PHASES[:-1])
        rows.append((f"R-{ARMS[k]}", "REPORTED", f"fr p50 {frp50(k):.2f}; fr-wait p50 "
                     f"{ms(rank([r[0] - p[1] for r, p in zip(S[k]['recs'], S[k]['prof'])], 50)):.2f}; {ph}; "
                     f"push avg {T['push'].get(k, {}).get('avg', 0) / 1000.0:.2f}; "
                     f"rebuild frames {sum(1 for r in S[k]['recs'] if r[4] & 1)}"))
    dl = " ".join(f"{n}={p50(CTRL, n) - p50(CTRL_BASE, n):+.2f}" for n in PHASES[:-1])
    t0 = sum(p50(CTRL, n) for n in ("upd", "wait", "under", "sweep", "static"))
    rows.append(("R-CTRL", "REPORTED", f"P-CTRL - P1@40: fr {frp50(CTRL) - frp50(CTRL_BASE):+.2f}; {dl}; "
                 f"the spin's earliest start t0 >= {t0:.2f} ms into the frame"))
    return rows, "READ AS GRADED (all graded rows PASS)"


# ---------------- the synthetic trace and the plants ----------------
def synth(plant=None, n=200):
    L = ["[build] env=blipscope-s3-128-frameprofile fw=v19 features=cloud",
         "[frame-bench] the frame bench C1: 7 scenes, settle 100 s, measure 120 s, push task on",
         f"[frame-profile] cost {600000 if plant == 'COST' else 61000} ns/frame cpu=240"]
    pushes, wsum = 1000, 0
    L.append("[wx] status=200 bytes=1234 outcome=ok")
    L.append("[wx] layer rebuilt in 120 ms (look A)")
    if plant == "WXOFF":
        L.append("[wx] rebuild: the held crop did not decode; the layer stays off")
    if plant == "WX500":
        L.append("[wx] status=500 bytes=0 outcome=error")
    for k in range(7):
        lv, wx = LEVEL[k], WX[k]
        L.append(f"[frame-bench] scene S{k} C1 home=Bend level={lv} wx={'true' if wx else 'false'} rung=25mi settle")
        a = 20 if (plant == "ANSW" and k == 2) else 21
        L.append(f"[frame-profile] answered S{k} {a} of 21; missing:{' fade' if a == 20 else ' none'}")
        L.append(f"[frame-bench] scene S{k} measure")
        recs, prof = [], []
        for j in range(n):
            ph = [0] * 16
            ph[0] = 300; ph[1] = 2; ph[2] = 510 if wx else 300; ph[3] = 660 if wx else 200
            if wx:
                ph[4], ph[5], ph[6] = 200, 445, 11
                if plant == "SPLIT" and k == 4:
                    ph[5] += 50
            ph[7] = 300; ph[8] = int(27 * lv); ph[9] = int(16.25 * lv) if wx else 0
            ph[10] = int(30 * lv); ph[11] = int(23 * lv) + (500 if k == CTRL else 0) + (-200 if plant == "CTRLSHORT" and k == CTRL else 0)
            ph[12] = 100; ph[13] = 5; ph[14] = 1; ph[15] = 20
            if plant == "PARTIAL" and k == 1 and j == 7:
                ph = [0] * 15 + [PARTIAL]
            if plant == "WAITSHIFT" and k == 0:
                ph[1] += 50
            fr = sum(ph[:15])
            drawn = lv - 1 if (plant == "DRAWN" and k == 3 and j == 50) else lv
            flags = 4 if wx else 0
            if plant == "STATE" and k == 6 and j == 9:
                flags = 0
            recs.append(struct.pack("<HHBBBB", fr, fr + 1000, lv, drawn, flags, 1))
            prof.append(struct.pack("<16H", *ph))
            if j % 60 == 0:
                pushes += 60; wsum += 60 * 20
                L.append(f"[m3] pushes={pushes} push_avg_us=24180 push_max_us=24700 sync_avg_us=460 sync_err=0 "
                         f"loop_wait_avg_us={wsum // pushes} loop_wait_max_us=30")
        L.append(f"[frame-bench] scene S{k} end frames={n} over-cap=0")
        negc = 1 if (plant == "NEG" and k == 5) else 0
        part = 1 if (plant == "PARTIAL" and k == 1) else 0
        L.append(f"[frame-profile] scene S{k} end arm={ARMS[k]} frames={n} partial={part} neg-contacts={negc} neg-rest=0")
        L.append(f"[frame-bench] push S{k}: n {n} avg_us 24180 sync_avg_us 460; fb submits 0")
        fb, fp = b"".join(recs), b"".join(prof)
        for s in range(0, n, 32):
            if plant == "GAP" and k == 0 and s == 64:
                continue
            L.append(f"[frame-bench] dump S{k} {s} {fb[s * 8:(s + 32) * 8].hex()}")
        L.append(f"[frame-bench] dump S{k} done {n}")
        for s in range(0, n, 16):
            L.append(f"[frame-profile] dump S{k} {s} {fp[s * 32:(s + 16) * 32].hex()}")
        L.append(f"[frame-profile] dump S{k} done {n}")
    return L


def selftest():
    pix_ok = {a: 0 for a in ARMS if WX[ARMS.index(a)]}
    cases = [
        ("CONTROL, a clean run", None, pix_ok, "READ AS GRADED"),
        ("COST, PC-1 at 600,000 ns", "COST", pix_ok, "STOP at I-2"),
        ("GAP, a bench dump line missing", "GAP", pix_ok, "INSTRUMENT at I-3"),
        ("DRAWN, one frame drawn 19 at level 20 (the STOP row)", "DRAWN", pix_ok, "STOP at I-4"),
        ("ANSW, a scene answers 20 of 21", "ANSW", pix_ok, "STOP at I-5"),
        ("PARTIAL, one partial frame", "PARTIAL", pix_ok, "STOP at I-6"),
        ("NEG, one negative remainder", "NEG", pix_ok, "STOP at I-7"),
        ("SPLIT, the sweep split off by 0.5 ms", "SPLIT", pix_ok, "STOP at I-8"),
        ("STATE, one weather frame without the crop shown", "STATE", pix_ok, "INSTRUMENT at I-9"),
        ("WXOFF, a 'layer stays off' line", "WXOFF", pix_ok, "INSTRUMENT at I-9"),
        ("WX500, a [wx] status 500", "WX500", pix_ok, "INSTRUMENT at I-9"),
        ("PIX, PIX-WX exit 2 on P1@60", None, dict(pix_ok, **{"P1@60": 2}), "INSTRUMENT at I-9"),
        ("WAITSHIFT, the profile's wait +0.5 ms", "WAITSHIFT", pix_ok, "STOP at I-11"),
        ("CTRLSHORT, the plant reads +3.0 ms", "CTRLSHORT", pix_ok, "STOP at C-1"),
    ]
    off = 0
    for name, plant, pix, want in cases:
        rows, verdict = read(synth(plant), pix)
        ok = verdict.startswith(want)
        off += not ok
        print(f"  {'ok ' if ok else 'OFF'} {name}: {verdict}")
    print(f"[a0a] SELFTEST {'PASSED' if off == 0 else 'FAILED'}: {len(cases)} cases, {off} off")
    return 0 if off == 0 else 1


if __name__ == "__main__":
    a = sys.argv[1:]
    if a and a[0] == "--selftest":
        sys.exit(selftest())
    if not a:
        print(__doc__); sys.exit(2)
    pix = {}
    if "--pix" in a:
        for spec in a[a.index("--pix") + 1:]:
            arm, _, code = spec.partition("=")
            pix[arm] = int(code)
    rows, verdict = read(lines_of(a[0]), pix)
    for i, s, t in rows:
        print(f"{i:10s} {s:10s} {t}")
    print(f"[a0a] VERDICT: {verdict}")
    sys.exit(0 if verdict.startswith("READ AS GRADED") else (4 if "INSTRUMENT" in verdict else 1))
