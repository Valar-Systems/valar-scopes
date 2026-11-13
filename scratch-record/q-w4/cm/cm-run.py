# SCRATCH: the code PR's local run rows, one group per invocation, in the ruled order (docs/basemap-dense-code.md, "The code PR's runs
# after SEA-STORE"): rkmsp (R-KMSP), rd2 (R-D2), seamem (SEA-MEM with its 8 MiB plant first), cm (CM-0, CM-P, the memory rows, CM-M).
# Every build is one process of cm/dr-cm.mts (QW4's instrument), sequential. Each group grades its frozen rows and STOPS at the first off
# reading (exit 1). Every build must hold its controls, settle every sample and read every object locally, else STOP (the instrument).
# Before each group: TIME_WAIT < 2,000, no vitest or sabotage- process, C: free >= 30 GB; the pins below re-hashed.
#   python -X utf8 -I cm/cm-run.py <rkmsp|rd2|seamem|cm>
import hashlib, json, os, shutil, subprocess, sys, time
D = os.path.dirname(os.path.dirname(os.path.abspath(__file__))); P = os.path.join(D, "wt-main", "proxy")
GROUP = sys.argv[1] if len(sys.argv) > 1 else ""
if GROUP not in ("rkmsp", "rd2", "seamem", "cm", "gate"): print("usage: rkmsp|rd2|seamem|cm|gate"); sys.exit(2)
B, MiB = 0.785, 1024 * 1024
NY = ["KJFK", "KLGA", "KEWR", "MIDTOWN"]
P13 = ["KBOS", "KJFK", "KLGA", "KEWR", "MIDTOWN", "KMIA", "KSFO", "KSEA", "KORD", "KDTW", "KMSP", "KIND", "KJAC"]
P16 = P13 + ["BUFFALO", "ROUSES", "ZAPATA"]
now = lambda: time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())
builds = []
def stop(why):
    print(f"STOP: {why}"); print(f"{GROUP}: OFF -- stopped at {now()}"); sys.exit(1)
def row(label, ok, detail):
    print(f"  {label}: {'AS PREDICTED' if ok else 'OFF'} ({detail})")
    if not ok: stop(label)
# ---- the gates and the pins
def ps(cmd): return subprocess.run(["powershell", "-NoProfile", "-Command", cmd], capture_output=True, text=True).stdout.strip()
tw = int(ps("(Get-NetTCPConnection -State TimeWait -ErrorAction SilentlyContinue | Measure-Object).Count") or "-1")
# the query's own command line holds the pattern, so it (and any shell wrapping it) is excluded by its own text (fixed 07:2xZ: the first
# rkmsp attempt counted itself, 1, and stopped at the gate with no build run)
busy = int(ps("(Get-CimInstance Win32_Process | Where-Object { $_.CommandLine -match 'vitest|sabotage-' -and $_.CommandLine -notmatch 'Get-CimInstance Win32_Process' } | Measure-Object).Count") or "-1")
free = shutil.disk_usage("C:/").free / 1e9
print(f"{GROUP} at {now()}: TIME_WAIT {tw}, vitest/sabotage processes {busy}, C: free {free:.1f} GB")
if not (0 <= tw < 2000) or busy != 0 or free < 30: stop("a gate (TIME_WAIT < 2,000, no vitest/sabotage process, >= 30 GB free)")
if GROUP == "gate": print("gate: PASSED"); sys.exit(0)
sha = lambda p: hashlib.sha256(open(p, "rb").read()).hexdigest()[:12]
PINS = {"cm/dr-cm.mts": None, "cm/mk-cm.mjs": None, "cm/cp/proxy/src/basemap/build.ts": "15c19c728dea", "cm/main/proxy/src/basemap/build.ts": "f60ae55f45d9"}
for k, want in PINS.items():
    got = sha(os.path.join(D, k)); print(f"  pin {k} {got}" + ("" if want is None else f" (registered {want})"))
    if want is not None and got != want: stop(f"pin {k}")
# ---- one build
def build(tree, which, ap, plant=0, mode="client"):
    env = {k: v for k, v in os.environ.items() if not k.startswith(("Q4_", "Q4H_", "DENSE_"))}
    x = subprocess.run(["node", "--expose-gc", "--import", "tsx", "../../cm/dr-cm.mts", tree, which, ap, str(plant), mode], cwd=P, env=env, capture_output=True, text=True, encoding="utf-8")
    ls = [l for l in x.stdout.splitlines() if l.startswith("RESULT ")]
    if x.returncode != 0 or len(ls) != 1: stop(f"{tree}/{which}/{ap}/{plant}/{mode}: exit {x.returncode}, {len(ls)} RESULT lines, stderr {len(x.stderr)} B: {x.stderr[-400:]}")
    r = json.loads(ls[0][7:])
    if "error" in r: stop(f"{tree}/{which}/{ap}: {r['error']}")
    if not r["ctlOk"] or r["unsettled"] or r["missing"]: stop(f"{tree}/{which}/{ap}: the instrument (ctl {r['ctl']}, unsettled {r['unsettled']}, missing {r['missing']})")
    if r["peak"] * MiB / 1e6 > 96: stop(f"{tree}/{which}/{ap}: a build over 96 MB ({r['peak'] * MiB / 1e6:.1f} MB) -- CM-M's stop")
    r["label"] = f"{tree}/{which}/{ap}" + (f"/plant{plant}" if plant else "") + ("" if mode == "client" else f"/{mode}")
    builds.append(r)
    ph = ", ".join(f"{k} {v:.2f}" for k, v in r["phases"].items())
    print(f"    {r['label']}: {r['status']} rung {r.get('rung')} {r.get('sha', '/'.join(map(str, r.get('nt') or [])))} peak {r['peak']:.2f} MiB at {r['peakTag']} ({r['peak'] * MiB / 1e6:.1f} MB); "
          f"steps {[round(s, 2) for s in r['stepPeaks']]}; phases {ph}; {r['ms']} ms")
    return r
def plant_control(name, which):
    pl, base = build("cp", which, "KJFK", 8), build("cp", which, "KJFK", 0)
    dc = pl["phases"]["cell"] - base["phases"]["cell"]
    late = {k: pl["phases"][k] - base["phases"][k] for k in ("tile", "tiles", "terrain", "layers", "ladder")}
    made = pl["plantMade"] == 8 * MiB and pl["plantRead"] == 10 and not pl["plantHeldToEnd"]
    row(f"{name}: the 8 MiB plant: +8 +/- 0.785 in the cell phase, 0 +/- 0.785 later; made and read back", abs(dc - 8) <= B and all(abs(v) <= B for v in late.values()) and made,
        f"cell {dc:+.2f}; later " + ", ".join(f"{k} {v:+.2f}" for k, v in late.items()) + f"; made {pl['plantMade']} B, read back {pl['plantRead']}, held to the end {pl['plantHeldToEnd']}")
SEAPKG = {"KBOS": "f21d705d8b8a", "KJFK": "52ac9dc39de0", "KLGA": "feb6ef3bdf04", "KEWR": "dff28487eaf4", "MIDTOWN": "20a22bed5510", "KMIA": "1a338238fbc8",
          "KSFO": "4d03beecdb4b", "KSEA": "9bd5b34d6718", "KORD": "cb59bb06a506", "KDTW": "682208bf2fb2", "KMSP": "b694c01dc502", "KIND": "89a4195fa4be",
          "KJAC": "42c32cd8578e", "BUFFALO": "52fad5e317f3", "ROUSES": "afe7554245b8", "ZAPATA": "e2ddaa00ecaf"}

if GROUP == "rkmsp":
    print("R-KMSP: KMSP's collapsed.water and refused equal those of KMSP built at eps x 8 with no cap (rung 0); the control refusedBuilt >= 1")
    a = build("cp", "orig", "KMSP"); b = build("cp", "orig", "KMSP", 0, "nocap8")
    row("the comparison build is what the row names: eps x 8, no cap, rung 0, one step", b["ok"] and b["rung"] == 0 and b["report"]["radii"] == [162], f"rung {b.get('rung')}, radii {b['report']['radii']}, tolerance {b.get('toleranceMm')} mm")
    row("R-KMSP's control: refusedBuilt >= 1 on the client's build", a["ok"] and a["report"]["refusedBuilt"] >= 1, f"refusedBuilt {a['report']['refusedBuilt']}")
    row("R-KMSP: collapsed.water equal", a["prov"].get("collapsed.water") == b["prov"].get("collapsed.water") and "collapsed.water" in a["prov"], f"{a['prov'].get('collapsed.water')} vs {b['prov'].get('collapsed.water')}")
    row("R-KMSP: refused equal", a["report"]["refused"] == b["report"]["refused"] and a["report"]["refused"] is not None, f"{a['report']['refused']} vs {b['report']['refused']}")
    print(f"  recorded, not graded: the prototype's QW4-C1 figures refused 0, refusedBuilt 2, the drawn set's collapsed.water 273; the code's: refused {a['report']['refused']}, "
          f"refusedBuilt {a['report']['refusedBuilt']}, collapsed.water {a['prov'].get('collapsed.water')}; rung {a['rung']}, ntb {a['ntb']}, crossings {a['wCross']}")
elif GROUP == "rd2":
    print("R-D2: at New York's 4, at most 2 steps, and the call's largest sample in its first step")
    for h in NY:
        r = build("cp", "orig", h); sp = r["stepPeaks"]; radii = (r.get("report") or {}).get("radii") or []
        if len(sp) != len(radii): stop(f"R-D2 {h}: the instrument: {len(sp)} step peaks against {len(radii)} radii")
        row(f"R-D2 {h}: at most 2 steps; the first step's largest sample is the call's largest", r["ok"] and len(radii) <= 2 and max(sp) == sp[0],
            f"status {r['status']}, radii {radii}, step peaks {[round(s, 2) for s in sp]} MiB")
elif GROUP == "seamem":
    print("SEA-MEM, its 8 MiB plant control first (scope ruling u2), on the sea store")
    plant_control("SEA-MEM's control", "sea")
    P2M = {"KBOS": 0.82, "KJFK": 0.78, "KLGA": 0.75, "KEWR": 0.71, "MIDTOWN": 0.72, "KMIA": 0.61, "KSFO": 0.40, "KSEA": 0.99, "KORD": 0.01, "KDTW": 0.36, "KMSP": 0.05,
           "KIND": 0.01, "KJAC": 0.02, "BUFFALO": 0.45, "ROUSES": 0.46, "ZAPATA": 0.28}
    # the orchestrator's addition (after 07:23Z), recorded and NOT graded: run 10 recorded P2-M only as per-phase differences (sea minus
    # no-sea) and peaks, never its no-sea build's absolute cell phase, so the check is made on the differences, cell loop included
    R10 = os.path.join(D, "p2-runs", "p2-read.run10.out")
    if sha(R10) != "fffce461d302": stop("run 10's output is not the pinned file")
    P2MD = {}
    for l in open(R10, encoding="utf-8"):
        if "P2-M " in l and "RECORDED sea minus no-sea" in l:
            h = l.split("P2-M ")[1].split(":")[0]
            P2MD[h] = {kv.split()[0]: float(kv.split()[1]) for kv in l.split("per phase: ")[1].split(";")[0].split(", ")}
    if sorted(P2MD) != sorted(P16): stop("run 10's P2-M rows did not parse at all 16")
    for h in P16:
        a, b = build("cp", "sea", h), build("cp", "orig", h)
        PH6 = ("cell", "tile", "tiles", "terrain", "layers", "ladder")
        print(f"  recorded, not graded ({h}): sea minus no-sea per phase, the code PR [P2-M run 10]: " + ", ".join(f"{k} {a['phases'][k] - b['phases'][k]:+.2f} [{P2MD[h][k]:+.2f}]" for k in PH6)
              + f"; the code PR's no-sea build (no sea layer): cell {b['phases']['cell']:.2f}, ladder {b['phases']['ladder']:.2f} MiB")
        row(f"SEA-MEM {h}: the sea build is SEA-PKG's package (the tree from the commit builds what SEA-PKG built)", a["ok"] and a["sha"] == SEAPKG[h], f"{a.get('sha')} vs {SEAPKG[h]}")
        d = a["phases"]["ladder"] - b["phases"]["ladder"]
        row(f"SEA-MEM {h}: sea minus no-sea at the ladder phase within +/- 0.785 of P2-M's {P2M[h]:+.2f}", abs(d - P2M[h]) <= B,
            f"{d:+.2f} MiB; every phase " + ", ".join(f"{k} {a['phases'][k] - b['phases'][k]:+.2f}" for k in ("cell", "tile", "tiles", "terrain", "layers", "ladder")) + f"; no-sea package {b.get('sha')}")
elif GROUP == "cm":
    tm = subprocess.run(["git", "-C", os.path.join(D, "wt-code"), "fetch", "-q", "origin", "main"], capture_output=True, text=True)
    th = {c: subprocess.run(["git", "-C", os.path.join(D, "wt-code"), "rev-parse", f"{c}:proxy/src/basemap"], capture_output=True, text=True).stdout.strip()[:12] for c in ("e03e6c4d", "origin/main")}
    mh = subprocess.run(["git", "-C", os.path.join(D, "wt-code"), "rev-parse", "origin/main"], capture_output=True, text=True).stdout.strip()[:12]
    row("u4: proxy/src/basemap's tree at e03e6c4d equals main's head's", tm.returncode == 0 and th["e03e6c4d"] == th["origin/main"] and len(th["e03e6c4d"]) == 12, f"e03e6c4d {th['e03e6c4d']}, main {mh} {th['origin/main']}")
    REF = {}
    for l in open(os.path.join(D, "rv-meas.log"), encoding="utf-8"):
        f = l.split()
        if len(f) > 3 and f[0] in P13:
            REF[f[0]] = {"status": int(f[1]), "answer": f[2], "peak": float(f[4]), "phases": json.loads(l[l.index("phases ") + 7:])}
    if sorted(REF) != sorted(P13) or sha(os.path.join(D, "rv-meas.log")) != "fc43b98c419e": stop("#639's measurement (rv-meas.log fc43b98c419e) did not parse at all 13")
    print("CM-0: main's hooked builder at all 13, its peak and its cell phase within 0.785 of #639's measurement, and #639's answers")
    m = {}
    for h in P13:
        r = m[h] = build("main", "orig", h); e = REF[h]
        ans = r.get("sha") if r["ok"] else "/".join(map(str, r.get("nt") or []))
        row(f"CM-0 {h}", r["status"] == e["status"] and ans == e["answer"] and abs(r["peak"] - e["peak"]) <= B and abs(r["phases"]["cell"] - e["phases"]["cell"]) <= B,
            f"{r['status']} {ans} vs {e['status']} {e['answer']}; peak {r['peak']:.2f} vs {e['peak']:.2f}; cell {r['phases']['cell']:.2f} vs {e['phases']['cell']:.2f}")
    print("CM-P: KJFK on the code PR's builder, the 8 MiB plant against the same build without it")
    plant_control("CM-P", "orig")
    print("The memory rows: the code PR minus main, +/- 0.785 MiB")
    c = {h: build("cp", "orig", h) for h in P13}
    d = {h: c[h]["peak"] - m[h]["peak"] for h in P13}; dcell = {h: c[h]["phases"]["cell"] - m[h]["phases"]["cell"] for h in P13}
    def world(v, o1, o2): return "O1" if abs(v - o1) <= B else "O2" if abs(v - o2) <= B else None
    w = {"KJFK": world(dcell["KJFK"], 9.94, 16.29), "KORD": world(d["KORD"], 6.81, 0.89), "KIND": world(d["KIND"], 7.62, 0.75)}
    print(f"  the three: KJFK cell {dcell['KJFK']:+.2f} -> {w['KJFK']}; KORD peak {d['KORD']:+.2f} -> {w['KORD']}; KIND peak {d['KIND']:+.2f} -> {w['KIND']}")
    if None in w.values(): stop("neither O1 nor O2 at one of the three: the offset interacts with B")
    W = "O1" if set(w.values()) == {"O1"} else "O2" if set(w.values()) == {"O2"} else "mix"
    print(f"  the world: {W}")
    row("KJFK's peak, one value +13.24", abs(d["KJFK"] - 13.24) <= B, f"{d['KJFK']:+.2f}")
    lo, hi = {"O1": (11.965, 13.535), "O2": (11.285, 12.855), "mix": (11.285, 13.535)}[W]
    row(f"KBOS's peak, graded by the world {W}: {lo} to {hi}", lo <= d["KBOS"] <= hi, f"{d['KBOS']:+.2f}")
    ONE = {"KLGA": 13.98, "KEWR": 13.23, "MIDTOWN": 13.01, "KMIA": 1.62, "KSFO": 2.16, "KSEA": 3.94, "KDTW": 5.63, "KMSP": 5.09, "KJAC": 1.78}
    for h, v in ONE.items(): row(f"{h}'s peak, one value {v:+.2f}", abs(d[h] - v) <= B, f"{d[h]:+.2f}")
    print("  recorded beside, not graded: the cell phase, the code PR minus main: " + ", ".join(f"{h} {dcell[h]:+.2f}" for h in ("KBOS", "KLGA", "KEWR", "MIDTOWN")))
    print("CM-M: none over 96 MB (each build above was checked as it ran); each code PR build's largest against the 77.2 MB the owner took with B: "
          + ", ".join(f"{h} {c[h]['peak'] * MiB / 1e6:.1f}" for h in P13))
json.dump(builds, open(os.path.join(D, "cm", f"{GROUP}-builds.json"), "w", encoding="utf-8"))
print(f"{GROUP}: ALL AS PREDICTED at {now()} ({len(builds)} builds)")
