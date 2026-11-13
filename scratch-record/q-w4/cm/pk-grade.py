# SCRATCH: the code PR's package rows on the store with no sea layer (5.6; docs/basemap-dense-code.md, "The code PR's package row,
# narrowed point by point"), at the 13. For each point: QW4-1's package rebuilt by pq4b at QW4-1's knobs (dr-q4c3.mts, QW4-C's child,
# unchanged) and checked against QW4-1's sha FIRST (else STOP: the references moved); then the code PR's build (cm/pk-cp.mts, the tree
# from its commit), graded "equal": every section but PROVENANCE identical by dr-q4c3's per-section digest; PROVENANCE the same number of
# lines and differing ONLY in collapsed.water, which equals the frozen value (KJAC: PROVENANCE unchanged); PROVENANCE's byte change and
# the non-terrain bytes as frozen; the rung, radius and tolerance QW4-1's; kOk, at most 32,768, 0 proper crossings. A cap edge anywhere
# (a section other than PROVENANCE differing) STOPS, as does anything else outside "equal". One process per build, sequential.
#   python -X utf8 -I cm/pk-grade.py
import hashlib, json, os, shutil, subprocess, sys, time
D = os.path.dirname(os.path.dirname(os.path.abspath(__file__))); P = os.path.join(D, "wt-main", "proxy")
now = lambda: time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())
def stop(why): print(f"STOP: {why}"); print(f"5.6: OFF -- stopped at {now()}"); sys.exit(1)
def row(label, ok, detail):
    print(f"  {label}: {'AS PREDICTED' if ok else 'OFF'} ({detail})")
    if not ok: stop(label)
def ps(cmd): return subprocess.run(["powershell", "-NoProfile", "-Command", cmd], capture_output=True, text=True).stdout.strip()
tw = int(ps("(Get-NetTCPConnection -State TimeWait -ErrorAction SilentlyContinue | Measure-Object).Count") or "-1")
busy = int(ps("(Get-CimInstance Win32_Process | Where-Object { $_.CommandLine -match 'vitest|sabotage-' -and $_.CommandLine -notmatch 'Get-CimInstance Win32_Process' } | Measure-Object).Count") or "-1")
free = shutil.disk_usage("C:/").free / 1e9
print(f"5.6 at {now()}: TIME_WAIT {tw}, vitest/sabotage processes {busy}, C: free {free:.1f} GB")
if not (0 <= tw < 2000) or busy != 0 or free < 30: stop("a gate")
sha = lambda p: hashlib.sha256(open(p, "rb").read()).hexdigest()[:12]
for k, want in {"dr-q4c3.mts": "b02b5a66d240", "cm/cp/proxy/src/basemap/build.ts": "15c19c728dea", "cm/pk-cp.mts": None}.items():
    got = sha(os.path.join(D, k)); print(f"  pin {k} {got}" + ("" if want is None else f" (registered {want})"))
    if want and got != want: stop(f"pin {k}")
QW41 = {"KBOS": "fee6825e1fd0", "KJFK": "04797ddc40a9", "KLGA": "d858d75eda6f", "KEWR": "7c9d29b0e136", "MIDTOWN": "9a9f002197a4", "KMIA": "a39dcd96532d",
        "KSFO": "fc6c3e960acc", "KSEA": "e51bac59de43", "KORD": "fe18e4224137", "KDTW": "dff7ad0acedb", "KMSP": "0b4b403134bb", "KIND": "a6943f185ad3", "KJAC": "a6a475745804"}
# point: (QW4-1's collapsed.water, the code PR's, PROVENANCE byte change, non-terrain bytes after)
ROW = {"KBOS": (4, 140, 2, 32719), "KJFK": (0, 96, 1, 32768), "KLGA": (3, 98, 1, 32767), "KEWR": (1, 89, 1, 32747), "MIDTOWN": (0, 91, 1, 32757), "KMIA": (1, 80, 1, 29183),
       "KSFO": (1, 35, 1, 32767), "KSEA": (1, 87, 1, 32767), "KORD": (3, 80, 1, 32760), "KDTW": (3, 66, 1, 32760), "KMSP": (3, 273, 2, 32745), "KIND": (0, 24, 1, 32761), "KJAC": (0, 0, 0, 30124)}
def child(args, env_add):
    env = {k: v for k, v in os.environ.items() if not k.startswith(("Q4_", "Q4H_", "DENSE_"))}; env.update(env_add)
    x = subprocess.run(["node", "--import", "tsx", *args], cwd=P, env=env, capture_output=True, text=True, encoding="utf-8")
    ls = [l for l in x.stdout.splitlines() if l.startswith("RESULT ")]
    if x.returncode != 0 or len(ls) != 1: stop(f"{args}: exit {x.returncode}, {len(ls)} RESULT lines, stderr {len(x.stderr)} B: {x.stderr[-400:]}")
    r = json.loads(ls[0][7:])
    if "error" in r or r.get("missing"): stop(f"{args}: {r.get('error')} missing {r.get('missing')}")
    return r
cv = lambda prov: [l for l in prov if l.startswith("collapsed.water: ")]
out = {}
print("The 13 package rows on the store with no sea layer: QW4-1's reference first, then the code PR's, graded \"equal\"")
for h, (old, new, dprov, ntb) in ROW.items():
    ref = child(["../../dr-q4c3.mts", "../../bm-obj", h, "b"], {"Q4_LADDER": "b", "Q4_D2": "1", "Q4_TP": "1"})
    row(f"{h}: QW4-1's reference rebuilt by pq4b", ref["ok"] and ref["sha"] == QW41[h] and cv(ref["prov"]) == [f"collapsed.water: {old}"], f"{ref.get('sha')} vs {QW41[h]}; {cv(ref.get('prov', []))}")
    c = child(["../../cm/pk-cp.mts", "../../bm-obj", h], {})
    out[h] = {"ref": ref, "cp": c}
    dl = [i for i, (a, b) in enumerate(zip(ref["prov"], c["prov"])) if a != b]
    sec_diff = sorted(set(ref["digests"].items()) ^ set(c["digests"].items()))
    prov_ok = len(ref["prov"]) == len(c["prov"]) and (dl == [] if h == "KJAC" else len(dl) == 1 and ref["prov"][dl[0]].startswith("collapsed.water: ")) and cv(c["prov"]) == [f"collapsed.water: {new}"]
    row(f"{h}: equal -- every section but PROVENANCE identical; PROVENANCE only collapsed.water = {new}; +{dprov} B; non-terrain {ntb}; rung, radius and tolerance QW4-1's; kOk, 0 crossings",
        c["ok"] and c["st"] == "kOk" and not sec_diff and prov_ok and c["provBytes"] - ref["provBytes"] == dprov and c["ntb"] == ntb and c["ntb"] <= 32768
        and (c["rung"], c["radiusMm"], c["toleranceMm"]) == (ref["rung"], ref["radiusMm"], ref["toleranceMm"]) and c["wCross"] == 0,
        f"sections differing {len(sec_diff) // 2 if sec_diff else 0} {sorted({k for k, _ in sec_diff})}; PROVENANCE lines differing {dl}, {cv(c['prov'])}, {c['provBytes'] - ref['provBytes']:+d} B; "
        f"ntb {c['ntb']}; rung {c['rung']}/{ref['rung']}, radius {c['radiusMm']}/{ref['radiusMm']}, tolerance {c['toleranceMm']}/{ref['toleranceMm']}; {c['st']}; crossings {c['wCross']}; package {c['sha']}")
json.dump(out, open(os.path.join(D, "cm", "pk-out.json"), "w", encoding="utf-8"))
print(f"5.6: ALL AS PREDICTED at {now()} (13 points, 26 builds)")
