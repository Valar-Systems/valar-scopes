# SCRATCH: SEA-FIG, graded as fixed at 313add48 (docs/basemap-dense-code.md, "SEA-FIG's grading fixed before it runs"). The 16 builds by
# sf-run.mts on the prototype's sea store (sea-obj), each against run 10's figures with run 10's definitions (dr-p2c.mts:57: fate counts
# after the ladder's relabel), plus the consistency check and run 10's own pieces (p2-out/<home>-sea-p21.json) in order.
# Sources: p2-runs/p2-read.run10.out (must hash fffce461d302) P2-1 rows (lines 56-71), (B') rows (74-77), VISIBLE COSTS (211).
# Any difference, a missing file, or a grader error -> "OFF: stop". 4 builds at a time, one per process. Counts and ids only.
#   python -X utf8 -I sf-grade.py
import ast, hashlib, json, os, re, subprocess
from concurrent.futures import ThreadPoolExecutor
D = os.path.dirname(os.path.abspath(__file__)); P = os.path.join(D, "wt-main", "proxy")
HOMES = ["KBOS", "KJFK", "KLGA", "KEWR", "MIDTOWN", "KMIA", "KSFO", "KSEA", "KORD", "KDTW", "KMSP", "KIND", "KJAC", "BUFFALO", "ROUSES", "ZAPATA"]
NY = {"KJFK", "KLGA", "KEWR", "MIDTOWN"}
offs = []
def off(msg): offs.append(msg); print("  OFF: " + msg)
# ---- run 10's figures, from its own output
R10 = os.path.join(D, "p2-runs", "p2-read.run10.out")
raw = open(R10, "rb").read(); h = hashlib.sha256(raw).hexdigest()[:12]
print(f"run 10's output {h} (pinned fffce461d302): {'ok' if h == 'fffce461d302' else 'MISMATCH'}")
if h != "fffce461d302": raise SystemExit("OFF: stop -- run 10's output is not the pinned file")
lines = raw.decode("utf-8").splitlines()
p21 = {}; bp = {}; vis = {}
for i, l in enumerate(lines, 1):
    m = re.match(r"^  P2-1 (\w+): kOk, 0 proper crossings: AS PREDICTED \(rung (\d+), radius ([\d.]+) km, crossings (\d+), sea (\{.*\})\)$", l)
    if m:
        assert 56 <= i <= 71, i
        p21[m[1]] = {"rung": int(m[2]), "radius": m[3], "sea": ast.literal_eval(m[5])}
    m = re.match(r"^  \(B'\) at (\w+): record 18 repaired .*AS PREDICTED \(rung (\d+), radius ([\d.]+) km, non-terrain (\d+) B, dropped\.water (\d+), rings (\d+), sea ", l)
    if m:
        assert 74 <= i <= 77, i
        bp[m[1]] = {"rung": int(m[2]), "radius": m[3], "ntb": int(m[4]), "droppedWater": int(m[5]), "waterRings": int(m[6])}
    if l.startswith("VISIBLE COSTS"):
        assert i == 211, i
        m2 = re.search(r"KBOS: rung 3 -> 4; dropped\.water 0 -> (\d+);", l); vis["KBOS"] = int(m2[1])
assert sorted(p21) == sorted(HOMES), sorted(p21)
assert sorted(bp) == sorted(NY), sorted(bp)
assert vis == {"KBOS": 45}, vis
print(f"run 10's figures parsed: P2-1 {len(p21)}, (B') {len(bp)}, VISIBLE COSTS KBOS dropped.water {vis['KBOS']}")
# ---- the builds
def one(hm):
    env = {k: v for k, v in os.environ.items() if not k.startswith(("Q4_", "Q4H_", "DENSE_"))}
    x = subprocess.run(["node", "--import", "tsx", "../../sf-run.mts", "../../sea-obj", hm], cwd=P, env=env, capture_output=True, text=True, encoding="utf-8")
    ls = [l for l in x.stdout.splitlines() if l.startswith("RESULT ")]
    if len(ls) != 1: return hm, {"error": f"exit {x.returncode}, {len(ls)} RESULT lines, stderr {len(x.stderr)} B", "stderr_tail": x.stderr[-600:]}
    return hm, json.loads(ls[0][7:])
with ThreadPoolExecutor(4) as ex: res = dict(ex.map(one, HOMES))
os.makedirs(os.path.join(D, "seafig"), exist_ok=True)
json.dump(res, open(os.path.join(D, "seafig", "sf-out.json"), "w", encoding="utf-8"))
RUN10SHA = {"KBOS": "f21d705d8b8a", "KJFK": "52ac9dc39de0", "KLGA": "feb6ef3bdf04", "KEWR": "dff28487eaf4", "MIDTOWN": "20a22bed5510", "KMIA": "1a338238fbc8",
            "KSFO": "4d03beecdb4b", "KSEA": "9bd5b34d6718", "KORD": "cb59bb06a506", "KDTW": "682208bf2fb2", "KMSP": "b694c01dc502", "KIND": "89a4195fa4be",
            "KJAC": "42c32cd8578e", "BUFFALO": "52fad5e317f3", "ROUSES": "afe7554245b8", "ZAPATA": "e2ddaa00ecaf"}
graded = 0
for hm in HOMES:
    r = res[hm]
    print(f"{hm}:")
    if "error" in r or not r.get("ok") or r.get("sea") is None: off(f"{hm} build failed or no sea report: {json.dumps(r)[:600]}"); continue
    s = r["sea"]; want = p21[hm]; ws = want["sea"]
    def chk(name, got, exp):
        global graded
        graded += 1
        if got != exp: off(f"{hm} {name}: code {got!r}, run 10 {exp!r}")
    # the package (carried by SEA-PKG; printed so a stale build cannot pass silently)
    chk("package sha (SEA-PKG)", r["sha"], RUN10SHA[hm])
    # rung, radius, D2's steps
    chk("rung", r["rung"], want["rung"])
    chk("radius", f"{r['radiusMm'] / 1e6:.2f}", want["radius"])
    chk("D2 steps", len(r["radii"]), 2 if hm in NY else 1)
    # the sea counts with run 10's definitions
    fn = lambda k: sum(1 for f in s["fates"] if f[2] == k)
    mine = {"drawn": fn("drawn"), "refused": fn("refused"), "collapsed": fn("collapsed"), "outside": fn("outside"), "dropped": fn("dropped"),
            "islandsDropped": s["islandsDropped"], "islandsSmall": s["islandsSmall"], "bridges": s["bridges"], "bytes": s["bytes"], "rings": len(s["ringIdx"]),
            "refused5": s["refused5"], "repairs": s["repairs"], "repairVertices": s["repairVertices"]}
    for k, v in mine.items(): chk(f"sea.{k}", v, ws[k])
    other = sorted(set(ws) - set(mine) - {"repairLog"})
    if other: off(f"{hm} run 10's sea dict has keys this grader does not grade: {other}")
    # the consistency check
    chk("consistency: SeaCounts.drawn == fate drawn + fate dropped", s["drawn"], fn("drawn") + fn("dropped"))
    chk("consistency: report dropped == fate dropped", s["dropped"], fn("dropped"))
    for k in ("refused", "collapsed", "outside"): chk(f"consistency: SeaCounts.{k} == fate {k}", s[k], fn(k))
    # run 10's own pieces, in order
    pf = os.path.join(D, "p2-out", f"{hm}-sea-p21.json")
    if not os.path.exists(pf): off(f"{hm} run 10's p21 file is missing"); continue
    q = json.load(open(pf, encoding="utf-8"))
    r10f = [[f["file"], f["record"], f["fate"]] for f in q["fates"]]
    if s["fates"] != r10f:
        same_multiset = sorted(map(tuple, s["fates"])) == sorted(map(tuple, r10f))
        off(f"{hm} pieces differ from run 10's ({len(s['fates'])} vs {len(r10f)}; {'ONLY IN ORDER' if same_multiset else 'in content'})")
    graded += 1
    chk("ringIdx == run 10's seaIdx", s["ringIdx"], q["seaIdx"])
    chk("radiusMm == run 10's p21 radiusMm", r["radiusMm"], q["radiusMm"])
    # New York's (B') figures, KBOS's VISIBLE COSTS figure
    if hm in NY:
        chk("non-terrain bytes", r["ntb"], bp[hm]["ntb"]); chk("dropped.water", r["droppedWater"], bp[hm]["droppedWater"]); chk("water rings", r["waterRings"], bp[hm]["waterRings"])
    if hm == "KBOS": chk("dropped.water", r["droppedWater"], vis["KBOS"])
    rl = "same" if s["repairLog"] == ws.get("repairLog") else f"DIFFERS (code {len(s['repairLog'])} lines, run 10 {len(ws.get('repairLog') or [])})"
    print(f"  rung {r['rung']} radius {r['radiusMm'] / 1e6:.2f} steps {len(r['radii'])} ntb {r['ntb']} dropped.water {r['droppedWater']} water rings {r['waterRings']}; "
          f"sea {json.dumps(mine, separators=(',', ':'))}; pieces {len(s['fates'])}; repairLog {rl} (recorded, not graded)")
print(f"SEA-FIG: {graded} checks, {len(offs)} off" + (" -- AS PREDICTED" if not offs else " -- OFF: stop"))
