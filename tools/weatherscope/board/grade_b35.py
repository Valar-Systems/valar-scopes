"""Grades 3.5a's board run (docs/weatherscope-stage3.md A9.10 B, A9.19, A9.22, A9.23) from run_b35.py's output, runs A9.10's
five recorded-data sabotages against the same capture, and reads the /diag/fb diag against A9.22's table.

    <penv python> -I tools/weatherscope/board/grade_b35.py <run dir>

Every expected value comes from the OTHER side of the contract:
  - each screen's 57,600 pixels and its time, band and label lines from ref35.py's K rows, re-rendered here and first
    checked equal to the committed test/fixtures/wx/k35.ref.txt (else nothing is graded);
  - B2's code hashes from screens.ref.json and its epochs from the loop's MANIFEST.json;
  - the legend from legend_check.py --fb.
THE BELIEF RULE (A9.21 item 2, A9.22): a capture is believed only if the last "[wx] screen=" line before its first
fetch's start is its own scene and no "[wx] screen=" line arrives before its last fetch's end; otherwise BLIND, a FAIL.
Writes a PNG of every graded snapshot (the six B1 images are kept for Daniel). Prints no coordinate.
"""
import calendar, hashlib, importlib.util, json, os, re, subprocess, sys, time

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
WX = os.path.join(ROOT, "test", "fixtures", "wx")
LOOP = os.path.join(WX, "loop")
N = 240
sys.dont_write_bytecode = True


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    m = importlib.util.module_from_spec(spec)
    sys.path.insert(0, os.path.dirname(path))
    spec.loader.exec_module(m)
    return m


ref35 = load("ref35", os.path.join(ROOT, "tools", "weatherscope", "screens", "ref35.py"))
legend = load("legend_check", os.path.join(WX, "legend_check.py"))
SCENARIOS = ["dry", "approaching", "herenow", "warning", "stale", "clear"]
PSRAM_BAND = (218032, 222032)          # A9.23: 220,032 B +- 2,000
DRAW_RANGE = (28000, 36000)            # A9.23: every loop frame at 60 mi


def ts_of(s):
    """The trace's printed UTC timestamp -> epoch seconds (only when lines.json is missing)."""
    base, frac = s.rstrip("Z").split(".")
    return calendar.timegm(time.strptime(base, "%Y-%m-%dT%H:%M:%S")) + int(frac) / 1000.0


def parse_trace(run):
    """(receive time, printed stamp, line) for every line. The receive times come from lines.json, the runner's own
    float clock, where it exists: the printed stamp's seconds and milliseconds are read at two instants."""
    printed = []
    for l in open(os.path.join(run, "trace.txt"), encoding="utf-8"):
        ts, _, line = l.rstrip("\n").partition(" ")
        printed.append((ts, line))
    lj = os.path.join(run, "lines.json")
    if os.path.exists(lj):
        exact = json.load(open(lj, encoding="utf-8"))
        if len(exact) == len(printed) and all(e[1] == p[1] for e, p in zip(exact, printed)):
            return [(e[0], p[0], p[1]) for e, p in zip(exact, printed)]
        print("  (lines.json does not match trace.txt line for line: using the printed stamps)")
    return [(ts_of(ts), ts, line) for ts, line in printed]


def blocks(trace):
    """Each BuildScenario block: from its [wx] pick line to its [wx] screen=scenario <name> R=<miles> line."""
    res, cur = [], None
    for t, ts, l in trace:
        if l.startswith("[wx] pick "):
            cur = {"pick": l, "k": []}
        elif cur is not None:
            if l.startswith("[wx] scn="):
                cur["scn"] = l
            elif l.startswith("[wx] k "):
                cur["k"].append(l[len("[wx] k "):])
            elif l.startswith("[wx] screen=scenario "):
                p = l.split()
                cur.update(name=p[2], R=int(p[3][2:]), t=t)
                res.append(cur)
                cur = None
    return res


def kv(line):
    return dict(re.findall(r"(\w+)=(\S+)", line))


def diff_px(a, b):
    return sum(1 for i in range(N * N) if a[2 * i:2 * i + 2] != b[2 * i:2 * i + 2])


FINISHED = (2, 3)   # Drawn, Pushed (FrameBuffer.h)


def tag_scene(tag):
    """A Weatherscope tag (MSRRCCCC) -> its K row's name, or None (A9.22)."""
    mode, idx, R = tag >> 28, (tag >> 24) & 15, (tag >> 16) & 255
    if mode == 1 and idx < len(SCENARIOS):
        return f"{SCENARIOS[idx]}-{R}"
    if mode == 2 and R == 60:
        return f"loop-{idx:02d}"
    return None


def diag_read(lines, screens, fetches, REF, run):
    """A9.22's table with A9.25's restated (i), (ii) and (iii) as frozen, and A9.23's refinement. `fetches` carry their
    file and headers. Returns (verdict, report lines, counts)."""
    out = []
    unfinished, clear_ends, ii_hits, unexplained, finished_bad = [], [], [], [], []
    n = 0
    for f in fetches:
        h = f.get("headers") or {}
        if not h.get("X-Blipscope-Frame-Phase") or not f.get("file"):
            continue
        n += 1
        pb, pa = (int(x) for x in h["X-Blipscope-Frame-Phase"].split(","))
        tb, ta = (int(x, 16) for x in h["X-Blipscope-Frame-Tag"].split(","))
        name = tag_scene(tb)
        if not name or name not in REF:
            continue
        body = open(os.path.join(run, f["file"]), "rb").read()
        if body == REF[name][0]:
            continue
        unfinished.append(f["file"])
        if h.get("X-Blipscope-Frame-Finished") == "true":
            finished_bad.append(f["file"])
        if pb in (1, 4) or pa in (1, 4):
            clear_ends.append(f["file"])
        elif pb in FINISHED and pa in FINISHED and tb == ta:
            # (ii), as frozen: a complete fetch whose pixels are an EARLIER scene's
            earlier = [k for k, (px, _) in REF.items() if k != name and body == px]
            (ii_hits if earlier else unexplained).append((f["file"], name, earlier))
        else:
            unexplained.append((f["file"], name, []))
    i_holds = len(clear_ends) == len(unfinished)
    pushes = [l for l in lines if l.startswith("[fb] push ")]
    push_min = min(int(re.search(r"push_us=(\d+)-", l).group(1)) for l in pushes) if pushes else None
    phases = sorted({re.search(r"phases=(\S+)", l).group(1) for l in pushes})
    seq_push = []
    for l in pushes:
        sc = int(l.split()[2], 16)
        mode, idx, R = sc >> 12, (sc >> 8) & 15, sc & 255
        item = f"{SCENARIOS[idx]} R={R}" if mode == 1 and idx < 6 else ("legend" if mode == 3 else None)
        if item and (not seq_push or seq_push[-1] != item):
            seq_push.append(item)
    seq_screen = []
    for _, l in screens:
        sl = l[len("[wx] screen="):]
        item = sl[len("scenario "):] if sl.startswith("scenario ") else ("legend" if sl == "legend" else None)
        if item and (not seq_screen or seq_screen[-1] != item):
            seq_screen.append(item)
    seq_match = seq_push == seq_screen[:len(seq_push)] and len(seq_screen) - len(seq_push) <= 1
    iii_excluded = push_min is not None and push_min >= 20000 and phases == ["2"] and seq_match
    out.append(f"fetches read: {n}; unfinished (not their tag's scene): {len(unfinished)}; marked Finished: true among them: "
               f"{finished_bad or 'none'}")
    out.append(f"(i) restated: unfinished with Clearing or Cleared at an end: {len(clear_ends)} of {len(unfinished)}")
    out.append(f"(ii) as frozen: complete fetches holding an earlier scene's pixels: {ii_hits or 'none'}")
    out.append(f"complete fetches not their scene and no other: {unexplained or 'none'}")
    out.append(f"(iii): push runs {len(pushes)}, phases at the push {phases}, shortest push {push_min} us, the scene "
               f"sequence {'matches' if seq_match else 'DOES NOT match'} ({len(seq_push)} against {len(seq_screen)})")
    if ii_hits and not unexplained:
        verdict = "(ii) an older buffer"
    elif i_holds and not ii_hits and not unexplained and iii_excluded:
        verdict = "(i) a copy inside the redraw -- (ii) and (iii) excluded"
    else:
        verdict = "the data can't decide"
    return verdict, out, dict(n=n, unfinished=len(unfinished), finished_bad=len(finished_bad), clear_ends=len(clear_ends))


def main(argv):
    if len(argv) != 2:
        print(__doc__)
        return 2
    run = argv[1]
    results = []

    def row(name, ok, detail):
        results.append((name, ok, detail))
        print(f"  {'ok  ' if ok else 'FAIL'} {name}: {detail}")

    print("== the reference ==")
    text, rows = ref35.k_text()
    committed = open(ref35.K_REF, encoding="utf-8").read().replace("\r\n", "\n")
    if text != committed:
        print("  FAIL ref35.py's K rows differ from the committed k35.ref.txt: nothing is graded")
        return 1
    print("  ok   ref35.py's K rows regenerate the committed k35.ref.txt byte for byte")
    REF = {name: (scr.rgb565(), lines) for name, scr, lines in rows}
    sref = {f["name"][14:20]: f for f in json.load(open(os.path.join(LOOP, "screens.ref.json")))["frames"]}

    trace = parse_trace(run)
    bl = blocks(trace)
    caps = json.load(open(os.path.join(run, "captures.json")))
    lines = [l for _, _, l in trace]
    screens = [(t, l) for t, _, l in trace if l.startswith("[wx] screen=")]

    def believed(cap, own):
        """The belief rule: `own` is the screen line's text after '[wx] screen='."""
        f = [x for x in cap["fetches"] if "start" in x]
        if not f:
            return False, "no timed fetch"
        t0, t1 = f[0]["start"], f[-1]["end"]
        before = [l for t, l in screens if t <= t0]
        during = [l for t, l in screens if t0 < t <= t1]
        ok = bool(before) and before[-1] == "[wx] screen=" + own and not during
        why = f"before: {before[-1][5:] if before else 'none'}; during: {[d[5:] for d in during] or 'none'}"
        return ok, why

    def block_for(name, R, cap):
        t0 = cap["fetches"][0].get("start", cap["fetches"][0].get("utc", 0))
        cand = [b for b in bl if b["name"] == name and b["R"] == R and b["t"] <= t0]
        return cand[-1] if cand else None

    graded = {}

    def grade_screen(label, cap, name, R, png=True):
        own = f"scenario {name} R={R}"
        ok, why = believed(cap, own)
        row(f"{label} believed", ok, f"own '{own}'; {why}")
        if not ok:
            return None
        if cap["verdict"] != "graded":
            row(f"{label} snapshot", False, f"{cap['verdict']} after {len(cap['fetches'])} fetches")
            return None
        key = f"{name}-{R}"
        if key not in REF:
            row(f"{label} reference", False, f"no K row {key}")
            return None
        snap = open(os.path.join(run, cap["graded"]), "rb").read()
        bad = diff_px(snap, REF[key][0]) if len(snap) == 2 * N * N else N * N
        row(f"{label} pixels", bad == 0, f"{key}: {N * N - bad} of {N * N} equal ({cap['graded']}, {len(cap['fetches'])} fetches)")
        b = block_for(name, R, cap)
        if b is None:
            row(f"{label} lines", False, f"no {key} block before the capture")
        else:
            row(f"{label} lines", b["k"] == REF[key][1],
                f"{len(b['k'])} printed vs {len(REF[key][1])} in the reference" + ("" if b["k"] == REF[key][1] else " -- DIFFER"))
            p = kv(b["pick"])
            row(f"{label} pick", p.get("approaching") == "212400" and p.get("dry") == "220000",
                f"approaching={p.get('approaching')} dry={p.get('dry')}")
        if png:
            legend.png(os.path.join(run, f"{label.replace(' ', '-')}.png"), snap)
        return dict(snap=snap, block=b, key=key)

    print("== B1: the six scenarios at 60 mi ==")
    for cap in caps:
        if cap.get("step") == "B1":
            graded[cap["scenario"]] = grade_screen(f"B1 {cap['scenario']}", cap, cap["scenario"], cap["R"])
    for name in SCENARIOS:
        if name not in graded:
            row(f"B1 {name} snapshot", False, "never captured")

    print("== the sizes (A9.23) ==")
    ps = [int(kv(l)["psram_used"]) for l in lines if l.startswith("[wx] pick ") and "psram_used=" in l]
    row("PSRAM", bool(ps) and len(set(ps)) == 1 and PSRAM_BAND[0] <= ps[0] <= PSRAM_BAND[1],
        f"psram_used {sorted(set(ps))} (A9.23: {PSRAM_BAND[0]:,}-{PSRAM_BAND[1]:,} B)")

    print("== BZ: U U U D | D H H on dry, in blocks ==")
    bz = [c for c in caps if c.get("step") == "BZ"]
    row("BZ gestures", len(bz) == 7, f"{len(bz)} of 7 captured")
    for c in bz:
        lab = f"BZ {c['label'][2:]} {c['gesture']}"
        ok = c.get("scenario") == "dry" and c.get("R") == c["wantR"]
        row(f"{lab} radius", ok, f"{c.get('scenario')} R={c.get('R')} (want dry R={c['wantR']})")
        if c.get("scenario"):
            grade_screen(lab, c, c["scenario"], c["R"], png=False)

    print("== B2: the loop, twice (A9.21: at 60 mi, and on the warning screen) ==")
    heads = [i for i, l in enumerate(lines) if l.startswith("[wx] loop records=")]
    manifest = json.load(open(os.path.join(LOOP, "MANIFEST.json")))["frames"]
    ref_shas = [sref[f["name"][14:20]]["screenSha256"] for f in manifest]
    epochs = [f["frameEpoch"] for f in manifest]

    def b2(recs):
        out = {}
        out["count"] = len(recs) == 15 and [r["i"] for r in recs] == list(range(15))
        out["sha"] = len(recs) == 15 and all(r["sha"] == ref_shas[r["i"]] for r in recs)
        out["epoch"] = len(recs) == 15 and [r["epoch"] for r in recs] == epochs and \
            all(recs[j]["epoch"] < recs[j + 1]["epoch"] for j in range(14))
        iv = [recs[j + 1]["start"] - recs[j]["start"] for j in range(len(recs) - 1)]
        out["pacing"] = len(iv) == 14 and max(iv) - min(iv) <= 10000 and all(65000 <= v <= 75000 for v in iv)
        span = recs[-1]["start"] - recs[0]["start"] if recs else 0
        fps = 14 / (span / 1e6) if span else 0
        out["rate"] = fps >= 14
        out["draw"] = len(recs) == 15 and all(r["draw"] <= 45000 for r in recs)
        out["hash"] = len(recs) == 15 and all(r["hash"] <= 5000 for r in recs)
        return out, iv, fps

    all_recs = []
    for n, i0 in enumerate(heads[:2]):
        tag = "B2 60" if n == 0 else "B2 warning"
        recs = []
        for l in lines[i0 + 1:i0 + 16]:
            if l.startswith("[wx] lrec "):
                k = kv(l)
                recs.append(dict(i=int(l.split()[2]), epoch=int(k["epoch"]), sha=k["sha"], start=int(k["start_us"]),
                                 draw=int(k["draw_us"]), hash=int(k["hash_us"])))
        all_recs.append(recs)
        lay = [l for l in lines[:i0] if l.startswith("[wx] layer ")]
        lk = kv(lay[-1]) if lay else {}
        row(f"{tag} layer", bool(lay) and int(lk.get("build_us", "999999")) <= 40000 and int(lk.get("pixels", "0")) > 0
            and lk.get("valid") == "1" and lk.get("rebuilt") == "tap",
            lay[-1][5:] if lay else "no [wx] layer line")
        b2r, iv, fps = b2(recs)
        row(f"{tag} radius", kv(lines[i0]).get("R") == "60", lines[i0][5:])
        row(f"{tag} records", b2r["count"], f"{len(recs)} records")
        row(f"{tag} hashes", b2r["sha"], f"{sum(1 for r in recs if r['sha'] == ref_shas[r['i']])} of 15 equal screens.ref.json")
        row(f"{tag} epochs", b2r["epoch"], "equal the manifest, ascending" if b2r["epoch"] else "NOT the manifest's")
        if iv:
            row(f"{tag} pacing", b2r["pacing"], f"intervals {min(iv)}-{max(iv)} us, max-min {max(iv) - min(iv)} us")
        row(f"{tag} rate", b2r["rate"], f"{fps:.2f} fps (>= 14)")
        if recs:
            row(f"{tag} draw", b2r["draw"], f"draw {min(r['draw'] for r in recs)}-{max(r['draw'] for r in recs)} us (<= 45,000)")
            row(f"{tag} draw range", all(DRAW_RANGE[0] <= r["draw"] <= DRAW_RANGE[1] for r in recs),
                f"A9.23: every frame in {DRAW_RANGE[0]:,}-{DRAW_RANGE[1]:,} us")
            row(f"{tag} hash", b2r["hash"], f"hash {min(r['hash'] for r in recs)}-{max(r['hash'] for r in recs)} us (<= 5,000)")
    if len(heads) < 2:
        row("B2 twice", False, f"{len(heads)} loops in the trace")
    for cap in caps:
        if cap["label"] in ("postloop", "postloop-warning"):
            grade_screen(f"B2 after the loop ({cap['label']})", cap, cap["scenario"], cap["R"])

    print("== B3: the legend ==")
    for cap in caps:
        if cap["label"] == "legend":
            ok, why = believed(cap, "legend")
            row("B3 legend believed", ok, why)
            if cap["verdict"] != "graded":
                row("B3 legend snapshot", False, cap["verdict"])
                continue
            r = subprocess.run([sys.executable, "-I", os.path.join(WX, "legend_check.py"), "--fb",
                                os.path.join(run, cap["graded"]), os.path.join(run, "serial.txt"), "--png",
                                os.path.join(run, "legend.png")], capture_output=True, text=True)
            print("    " + r.stdout.replace("\n", "\n    ").rstrip())
            row("B3 legend_check --fb", r.returncode == 0, f"exit {r.returncode}")
        if cap["label"] == "postlegend":
            grade_screen("B3 after the legend", cap, cap["scenario"], cap["R"])

    print("== the recorded-data sabotages (each must go RED) ==")
    sab = []
    if graded.get("dry"):
        bad = diff_px(graded["dry"]["snap"], REF["approaching-60"][0])
        sab.append(("S-B1a'", bad > 0, f"dry's snapshot against approaching-60's reference: {bad} pixels differ"))
        k = list(graded["dry"]["block"]["k"]) if graded["dry"]["block"] else []
        i = next((j for j, x in enumerate(k) if x.startswith("LABEL ")), None)
        if i is not None:
            f = k[i].split(" ")
            f[4] = str(int(f[4]) + 1)
            k[i] = " ".join(f)
            sab.append(("S-B1b'", k != REF["dry-60"][1], "one board-printed label's x0 moved by 1 px"))
    recs = all_recs[0] if all_recs else []
    if len(recs) == 15:
        sab.append(("S-B2a", recs[3]["sha"] != ref_shas[4], "record 3 graded against reference 4"))
        r3 = [dict(r) for r in recs]
        r3[2]["epoch"], r3[3]["epoch"] = r3[3]["epoch"], r3[2]["epoch"]
        sab.append(("S-B2b", not b2(r3)[0]["epoch"], "epochs 2 and 3 swapped"))
        r4 = [dict(r) for r in recs]
        for j in range(8, 15):
            r4[j]["start"] += 11000
        sab.append(("S-B2c", not b2(r4)[0]["pacing"], "11,000 us added to interval 7->8"))
    for name, red, detail in sab:
        print(f"  {'RED  ' if red else 'GREEN'} {name}: {detail} -> {'AS PREDICTED' if red else 'OFF-PREDICTION'}")

    print("== /diag/fb's finished frames (A9.26) ==")
    allf = [f for c in caps for f in c["fetches"] if f.get("headers")]
    notfin = [f["file"] for f in allf if f["headers"].get("X-Blipscope-Frame-Finished") != "true"]
    waits = [int(f["headers"].get("X-Blipscope-Frame-Wait-Us") or -1) for f in allf]
    row("every fetch finished", not notfin and bool(allf), f"{len(allf) - len(notfin)} of {len(allf)} Finished: true" +
        (f"; not: {notfin}" if notfin else ""))
    row("every wait within the bound", bool(waits) and 0 <= min(waits) and max(waits) <= 100000,
        f"waits {min(waits) if waits else '-'}-{max(waits) if waits else '-'} us (<= 100,000)")
    firstpair = [c["label"] for c in caps if c.get("verdict") == "graded" and len(c["fetches"]) == 2 and
                 c["graded"] == c["fetches"][0]["file"]]
    graded_caps = [c["label"] for c in caps if c.get("verdict") == "graded"]
    row("every capture graded at its first pair", len(firstpair) == len(caps) and graded_caps == [c["label"] for c in caps],
        f"{len(firstpair)} of {len(caps)} at 2 fetches" +
        (f"; not: {[c['label'] for c in caps if c['label'] not in firstpair]}" if len(firstpair) != len(caps) else ""))

    print("== the /diag/fb diag (A9.22's table; A9.25's restated (i); (ii) and (iii) as frozen) ==")
    verdict, report, _ = diag_read(lines, screens, allf, REF, run)
    for l in report:
        print("    " + l)
    print(f"    VERDICT: {verdict}")

    fails = [r for r in results if not r[1]]
    sab_ok = len(sab) == 5 and all(red for _, red, _ in sab)
    print(f"\n{len(results)} checks, {len(fails)} failing; sabotages {sum(1 for s in sab if s[1])}/{len(sab)} red (5 expected); "
          f"diag: {verdict}")
    return 0 if not fails and sab_ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
