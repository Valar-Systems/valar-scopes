"""Grades 3.3d's board run (docs/weatherscope-stage3.md A8.20, A8.21) from run_b.py's output, and runs A8.20's five
recorded-data sabotages against the same capture.

    <penv python> -I tools/weatherscope/board/grade_b.py <run dir> <spike checkout>

Every expected value comes from the OTHER side of the contract: the predicted rectangles from layout.ref.txt (never
the board's), each frame's codes from render_ref.py's own render() with the spike's independent decoder (checked
against screens.ref.json before use), and the colours from legend_check.py's pinned-table derivation (never
WxPalette.h), through LovyanGFX's truncating color565. Writes a PNG of every graded snapshot for Daniel's look call.
Prints no coordinate.
"""
import hashlib, importlib.util, json, os, re, sys

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


legend = load("legend_check", os.path.join(WX, "legend_check.py"))
render_ref = load("render_ref", os.path.join(WX, "render_ref.py"))

PAL = {0: legend.DRY, 14: legend.NOCOV}
PAL.update(legend.RAIN)


def c565(rgb):
    r, g, b = rgb
    return (r >> 3) << 11 | (g >> 2) << 5 | b >> 3


def expected_px(code, x, y):
    if code == 255:
        return 0
    if code == 15:
        return c565(legend.HATCH if (x + y) % 4 == 0 else legend.DRY)
    return c565(PAL[code])


EXPECT = {
    "dry": dict(frame="220000", screen="Dry", type="None", warning="-1", headline=""),
    "approaching": dict(frame="212400", screen="PrecipApproaching", type="None", warning="-1",
                        headline="PRECIP WITHIN 15 MI"),
    "herenow": dict(frame="212400", screen="PrecipNow", type="Rain", warning="-1", headline="RAIN NOW"),
    "warning": dict(frame="212400", screen="Warning", type="None", warning="0", headline="", title="SEVERE T-STORM|WARNING",
                    until="UNTIL 5:09 PM", lineA="HAIL 1 IN - WIND 60 MPH", storm="STORM 14 MI E",
                    travel="MOVING NE 35 MPH"),
}


def parse_trace(run):
    out = []
    for l in open(os.path.join(run, "trace.txt"), encoding="utf-8"):
        ts, _, line = l.rstrip("\n").partition(" ")
        out.append((ts, line))
    return out


def blocks(trace):
    """Each BuildScenario block: from its [wx] pick line to its [wx] screen=scenario line."""
    res, cur = [], None
    for i, (ts, l) in enumerate(trace):
        if l.startswith("[wx] pick "):
            cur = {"start": i, "pick": l, "rects": []}
        elif cur is not None:
            if l.startswith("[wx] scn="): cur["scn"] = l
            elif l.startswith("[wx] text "): cur["text"] = l
            elif l.startswith("[wx] codes "): cur["codes"] = l.split()[2]
            elif l.startswith("[wx] rects "): cur["rectsHead"] = l
            elif l.startswith("[wx] rect "): cur["rects"].append(l[len("[wx] rect "):])
            elif l.startswith("[wx] screen=scenario "):
                cur["end"] = i
                cur["name"] = l.split()[-1]
                cur["ts"] = ts
                res.append(cur)
                cur = None
    return res


def kv(line):
    return dict(re.findall(r'(\w+)=("[^"]*"|\S+)', line))


def unq(v):
    return v[1:-1] if v.startswith('"') and v.endswith('"') else v


def layout_ref():
    rects, compared = {}, {}
    for l in open(os.path.join(LOOP, "layout.ref.txt"), encoding="utf-8"):
        l = l.rstrip("\r\n")
        if l.startswith("LAYOUT "):
            p = l.split()
            compared[p[1]] = int(dict(x.split("=") for x in p[2:])["compared"])
        elif l.startswith("RECT "):
            rects.setdefault(l.split()[1], []).append(l[len("RECT "):])
    return rects, compared


def rect_boxes(lines):
    boxes = []
    for l in lines:
        x, y, w, h = (int(v) for v in l.split()[2].split(","))
        boxes.append((x, y, w, h))
    return boxes


def mask(boxes):
    m = bytearray(N * N)
    for x0, y0, w, h in boxes:
        for y in range(max(0, y0), min(N, y0 + h)):
            for x in range(max(0, x0), min(N, x0 + w)):
                m[y * N + x] = 1
    return m


def pixel_check(snap, codes, boxes):
    m = mask(boxes)
    compared = bad = 0
    for y in range(N):
        for x in range(N):
            i = y * N + x
            if m[i]:
                continue
            compared += 1
            v = snap[2 * i] << 8 | snap[2 * i + 1]
            if v != expected_px(codes[i], x, y):
                bad += 1
    return compared, bad


def main(argv):
    if len(argv) != 3:
        print(__doc__)
        return 2
    run, spike = argv[1], argv[2]
    results = []

    def row(name, ok, detail):
        results.append((name, ok, detail))
        print(f"  {'ok  ' if ok else 'FAIL'} {name}: {detail}")

    print("== controls ==")
    vals = {c: c565(PAL[c]) for c in range(15)}
    row("palette distinct", len(set(vals.values())) == 15, f"codes 0-14 give {len(set(vals.values()))} distinct RGB565 values")
    sref = {f["name"][14:20]: f for f in json.load(open(os.path.join(LOOP, "screens.ref.json")))["frames"]}
    decode = render_ref.load_decoder(spike)
    codes_of = {}
    for fr in ("212400", "220000"):
        buf, _ = render_ref.render(os.path.join(LOOP, f"shsr-20240521-{fr}.wxc"), decode)
        codes_of[fr] = buf
        row(f"reference {fr}", hashlib.sha256(buf).hexdigest() == sref[fr]["screenSha256"],
            f"render_ref.py's codes hash to screens.ref.json's {sref[fr]['screenSha256'][:12]}")
    pred_rects, pred_compared = layout_ref()

    trace = parse_trace(run)
    bl = blocks(trace)
    caps = json.load(open(os.path.join(run, "captures.json")))
    lines = [l for _, l in trace]

    def block_for(name, ts):
        cand = [b for b in bl if b["name"] == name and b["ts"] <= ts]
        return cand[-1] if cand else None

    def grade_screen(label, cap, name):
        if cap["verdict"] != "graded":
            row(f"{label} snapshot", False, f"{cap['verdict']} after {len(cap['fetches'])} fetches")
            return None
        first = cap["fetches"][0]
        ts = __import__("time").strftime("%Y-%m-%dT%H:%M:%S", __import__("time").gmtime(first["utc"])) + ".999Z"
        b = block_for(name, ts)
        if b is None:
            row(f"{label} block", False, f"no scenario block for {name} before the capture")
            return None
        torn = sum(1 for f in cap["fetches"] if f.get("headers", {}).get("X-Blipscope-Frame-Torn") not in (None, "false"))
        row(f"{label} snapshot", True, f"graded {cap['graded']} after {len(cap['fetches'])} fetches, {torn} torn")
        e = EXPECT[name]
        p, s, t = kv(b["pick"]), kv(b["scn"]), kv(b.get("text", ""))
        row(f"{label} pick", p.get("approaching") == "212400" and p.get("dry") == "220000",
            f"approaching={p.get('approaching')} dry={p.get('dry')}")
        ok = s.get("frame") == e["frame"] and s.get("screen") == e["screen"] and s.get("type") == e["type"] and \
            s.get("warning") == e["warning"] and unq(t.get("headline", '""')) == e["headline"]
        for k in ("title", "until", "lineA", "storm", "travel"):
            if k in e:
                ok &= unq(t.get(k, '""')) == e[k]
        row(f"{label} screen and text", ok, f"{b['scn'][5:]} | {b.get('text', '')[5:]}")
        want_sha = sref[e["frame"]]["screenSha256"]
        row(f"{label} codes", b.get("codes") == want_sha, f"board {str(b.get('codes'))[:12]} vs reference {want_sha[:12]}")
        row(f"{label} rects", b["rects"] == pred_rects[name],
            f"{len(b['rects'])} printed vs {len(pred_rects[name])} predicted" + ("" if b["rects"] == pred_rects[name] else " -- DIFFER"))
        snap = open(os.path.join(run, cap["graded"]), "rb").read()
        compared, bad = pixel_check(snap, codes_of[e["frame"]], rect_boxes(pred_rects[name]))
        row(f"{label} pixels", bad == 0 and compared == pred_compared[name],
            f"{compared} compared (predicted {pred_compared[name]}), {bad} differ")
        legend.png(os.path.join(run, f"{label}.png"), snap)
        return dict(block=b, snap=snap, name=name)

    print("== B1: the four scenarios ==")
    graded = {}
    for cap in caps:
        if cap.get("step") == "B1":
            graded[cap["scenario"]] = grade_screen(f"B1 {cap['scenario']}", cap, cap["scenario"])
    for name in ("dry", "approaching", "herenow", "warning"):
        if name not in graded:
            row(f"B1 {name} snapshot", False, "never captured")

    print("== B2: the loop ==")
    try:
        i0 = max(i for i, l in enumerate(lines) if l.startswith("[wx] loop records="))
    except ValueError:
        i0 = None
    recs = []
    if i0 is not None:
        for l in lines[i0 + 1:i0 + 16]:
            if l.startswith("[wx] lrec "):
                k = kv(l)
                recs.append(dict(i=int(l.split()[2]), epoch=int(k["epoch"]), sha=k["sha"], start=int(k["start_us"]),
                                 draw=int(k["draw_us"]), hash=int(k["hash_us"])))
    manifest = json.load(open(os.path.join(LOOP, "MANIFEST.json")))["frames"]
    ref_shas = [sref[f["name"][14:20]]["screenSha256"] for f in manifest]
    epochs = [f["frameEpoch"] for f in manifest]

    def b2(recs, ref_shas, label=""):
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

    b2r, iv, fps = b2(recs, ref_shas)
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
            grade_screen("B2 after the loop", cap, cap["scenario"])

    print("== B3: the legend ==")
    for cap in caps:
        if cap["label"] == "legend":
            if cap["verdict"] != "graded":
                row("B3 legend snapshot", False, cap["verdict"])
                continue
            import subprocess
            r = subprocess.run([sys.executable, "-I", os.path.join(WX, "legend_check.py"), "--fb",
                                os.path.join(run, cap["graded"]), os.path.join(run, "serial.txt"), "--png",
                                os.path.join(run, "legend.png")], capture_output=True, text=True)
            print("    " + r.stdout.replace("\n", "\n    ").rstrip())
            row("B3 legend_check --fb", r.returncode == 0, f"exit {r.returncode}")
        if cap["label"] == "postlegend":
            grade_screen("B3 after the legend", cap, cap["scenario"])

    print("== the recorded-data sabotages (each must go RED) ==")
    sab = []
    if "approaching" in graded and graded["approaching"]:
        g = graded["approaching"]
        hash_red = g["block"].get("codes") != sref["220000"]["screenSha256"]
        _, bad = pixel_check(g["snap"], codes_of["220000"], rect_boxes(pred_rects["approaching"]))
        sab.append(("S-B1a", hash_red and bad > 0, f"hash red {hash_red}, pixels differing {bad}"))
    if "dry" in graded and graded["dry"]:
        rects = list(graded["dry"]["block"]["rects"])
        p = rects[0].split()
        x, y, w, h = p[2].split(",")
        p[2] = f"{x},{y},{int(w) + 1},{h}"
        rects[0] = " ".join(p)
        sab.append(("S-B1b", rects != pred_rects["dry"], "one reported rect widened by 1 px"))
    if len(recs) == 15:
        assert ref_shas[3] != ref_shas[4], "S-B2a needs references 3 and 4 to differ"
        sab.append(("S-B2a", recs[3]["sha"] != ref_shas[4], "record 3 graded against reference 4"))
        r3 = [dict(r) for r in recs]
        r3[2]["epoch"], r3[3]["epoch"] = r3[3]["epoch"], r3[2]["epoch"]
        sab.append(("S-B2b", not b2(r3, ref_shas)[0]["epoch"], "epochs 2 and 3 swapped"))
        r4 = [dict(r) for r in recs]
        for j in range(8, 15):
            r4[j]["start"] += 11000
        sab.append(("S-B2c", not b2(r4, ref_shas)[0]["pacing"], "11,000 us added to interval 7->8"))
    for name, red, detail in sab:
        print(f"  {'RED  ' if red else 'GREEN'} {name}: {detail} -> {'AS PREDICTED' if red else 'OFF-PREDICTION'}")

    fails = [r for r in results if not r[1]]
    sab_ok = len(sab) == 5 and all(red for _, red, _ in sab)
    print(f"\n{len(results)} checks, {len(fails)} failing; sabotages {sum(1 for s in sab if s[1])}/{len(sab)} red (5 expected)")
    return 0 if not fails and sab_ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
