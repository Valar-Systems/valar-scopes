"""Grades 3.5a's board run (docs/weatherscope-stage3.md A9.10 B, A9.19) from run_b35.py's output, and runs A9.10's five
recorded-data sabotages against the same capture.

    <penv python> -I tools/weatherscope/board/grade_b35.py <run dir>

Every expected value comes from the OTHER side of the contract:
  - each screen's 57,600 pixels and its time, band and label lines from ref35.py's K rows, re-rendered here and
    first checked equal to the committed test/fixtures/wx/k35.ref.txt (else nothing is graded);
  - B2's code hashes from screens.ref.json (3.3's K2 reference) and its epochs from the loop's MANIFEST.json;
  - the legend from legend_check.py --fb.
Writes a PNG of every graded snapshot (the six B1 images are kept for Daniel). Prints no coordinate.
"""
import importlib.util, json, os, re, subprocess, sys, time

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


def parse_trace(run):
    out = []
    for l in open(os.path.join(run, "trace.txt"), encoding="utf-8"):
        ts, _, line = l.rstrip("\n").partition(" ")
        out.append((ts, line))
    return out


def blocks(trace):
    """Each BuildScenario block: from its [wx] pick line to its [wx] screen=scenario <name> R=<miles> line."""
    res, cur = [], None
    for ts, l in trace:
        if l.startswith("[wx] pick "):
            cur = {"pick": l, "k": []}
        elif cur is not None:
            if l.startswith("[wx] scn="):
                cur["scn"] = l
            elif l.startswith("[wx] k "):
                cur["k"].append(l[len("[wx] k "):])
            elif l.startswith("[wx] screen=scenario "):
                p = l.split()
                cur.update(name=p[2], R=int(p[3][2:]), ts=ts)
                res.append(cur)
                cur = None
    return res


def kv(line):
    return dict(re.findall(r"(\w+)=(\S+)", line))


def diff_px(a, b):
    return sum(1 for i in range(N * N) if a[2 * i:2 * i + 2] != b[2 * i:2 * i + 2])


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
    lines = [l for _, l in trace]

    def block_for(name, R, cap):
        ts = time.strftime("%Y-%m-%dT%H:%M:%S", time.gmtime(cap["fetches"][0]["utc"])) + ".999Z"
        cand = [b for b in bl if b["name"] == name and b["R"] == R and b["ts"] <= ts]
        return cand[-1] if cand else None

    graded = {}

    def grade_screen(label, cap, name, R, png=True):
        if cap["verdict"] != "graded":
            row(f"{label} snapshot", False, f"{cap['verdict']} after {len(cap['fetches'])} fetches")
            return None
        key = f"{name}-{R}"
        if key not in REF:
            row(f"{label} reference", False, f"no K row {key}")
            return None
        torn = sum(1 for f in cap["fetches"] if f.get("headers", {}).get("X-Blipscope-Frame-Torn") not in (None, "false"))
        snap = open(os.path.join(run, cap["graded"]), "rb").read()
        bad = diff_px(snap, REF[key][0]) if len(snap) == 2 * N * N else N * N
        row(f"{label} pixels", bad == 0, f"{key}: {N * N - bad} of {N * N} equal ({cap['graded']}, {len(cap['fetches'])} fetches, {torn} torn)")
        b = block_for(name, R, cap)
        if b is None:
            row(f"{label} lines", False, f"no {key} block before the capture")
        else:
            row(f"{label} lines", b["k"] == REF[key][1],
                f"{len(b['k'])} printed (ROW, TIME, BAND, {sum(1 for x in b['k'] if x.startswith('LABEL'))} labels) vs "
                f"{len(REF[key][1])} in the reference" + ("" if b["k"] == REF[key][1] else " -- DIFFER"))
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

    print("== the sizes (A9.5) ==")
    ps = [int(kv(l)["psram_used"]) for l in lines if l.startswith("[wx] pick ") and "psram_used=" in l]
    row("PSRAM", bool(ps) and len(set(ps)) == 1 and 160000 <= ps[0] <= 175000,
        f"psram_used {sorted(set(ps))} (A9.5: 160,000-175,000 B)")

    print("== BZ: U, U, U, D, D, H, H on dry ==")
    bz = [c for c in caps if c.get("step") == "BZ"]
    row("BZ gestures", len(bz) == 7, f"{len(bz)} of 7 captured")
    for c in bz:
        lab = f"BZ {c['label'][2:]} {c['gesture']}"
        ok = c.get("scenario") == "dry" and c.get("R") == c["wantR"]
        row(f"{lab} radius", ok, f"{c.get('scenario')} R={c.get('R')} (want dry R={c['wantR']})")
        if c.get("scenario"):
            grade_screen(lab, c, c["scenario"], c["R"], png=False)

    print("== B2: the loop ==")
    try:
        i0 = max(i for i, l in enumerate(lines) if l.startswith("[wx] loop records="))
    except ValueError:
        i0 = None
    recs = []
    if i0 is not None:
        row("B2 radius", kv(lines[i0]).get("R") == "60", lines[i0][5:])
        for l in lines[i0 + 1:i0 + 16]:
            if l.startswith("[wx] lrec "):
                k = kv(l)
                recs.append(dict(i=int(l.split()[2]), epoch=int(k["epoch"]), sha=k["sha"], start=int(k["start_us"]),
                                 draw=int(k["draw_us"]), hash=int(k["hash_us"])))
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

    b2r, iv, fps = b2(recs)
    row("B2 records", b2r["count"], f"{len(recs)} records, indices {[r['i'] for r in recs]}")
    row("B2 hashes", b2r["sha"], f"{sum(1 for r in recs if r['sha'] == ref_shas[r['i']])} of 15 equal screens.ref.json")
    row("B2 epochs", b2r["epoch"], "equal the manifest, strictly ascending" if b2r["epoch"] else "NOT the manifest's, or not ascending")
    if iv:
        row("B2 pacing", b2r["pacing"], f"intervals {min(iv)}-{max(iv)} us, max-min {max(iv) - min(iv)} us (<= 10,000; each 70,000 +- 5,000)")
    row("B2 rate", b2r["rate"], f"{fps:.2f} fps over 14 intervals (>= 14)")
    if recs:
        row("B2 draw", b2r["draw"], f"draw {min(r['draw'] for r in recs)}-{max(r['draw'] for r in recs)} us (<= 45,000)")
        row("B2 hash", b2r["hash"], f"hash {min(r['hash'] for r in recs)}-{max(r['hash'] for r in recs)} us (<= 5,000)")
    for cap in caps:
        if cap["label"] == "postloop":
            grade_screen("B2 after the loop", cap, cap["scenario"], cap["R"])

    print("== B3: the legend ==")
    for cap in caps:
        if cap["label"] == "legend":
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
    if len(recs) == 15:
        assert ref_shas[3] != ref_shas[4], "S-B2a needs references 3 and 4 to differ"
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

    fails = [r for r in results if not r[1]]
    sab_ok = len(sab) == 5 and all(red for _, red, _ in sab)
    print(f"\n{len(results)} checks, {len(fails)} failing; sabotages {sum(1 for s in sab if s[1])}/{len(sab)} red (5 expected)")
    return 0 if not fails and sab_ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
