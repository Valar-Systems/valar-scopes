# SCRATCH: the census mirror switch, prepared 2026-11-13 (the orchestrator, after 09:45:46Z), NOT run until the retry's last round
# (11-14 ~06:38Z) has ended with files still refused. Modes:
#   --rehearse  the plan edit applied to a COPY (mirror/rehearse/plan.json); the cache is not touched
#   --apply     preconditions, then the plan edit in the cache (a byte copy kept, read back, checked)
#   --check     after the ingest's own retry fetched the 8 mirror urls: the identity check, graded (table in the doc message)
#   python -X utf8 -I mirror/switch.py --rehearse|--apply|--check
import hashlib, json, os, shutil, subprocess, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import C, S, WHY, bm_records, eight, mirror_of
mode = sys.argv[1] if len(sys.argv) > 1 else ""
sha = lambda b: hashlib.sha256(b).hexdigest()
def stop(why): print(f"STOP: {why}"); sys.exit(1)
E = eight(); M = {u: mirror_of(u) for u in E}; BM = bm_records()
for u in E:
    if u not in BM: stop(f"bm-obj's manifest has no record for {u}")
def edited(text):
    d = json.loads(text); n = 0
    for f in d["files"]:
        if f["url"] in M: f["url"] = M[f["url"]]; f["note"] = WHY; n += 1
    if n != 8: stop(f"the plan edit touched {n} entries, not 8")
    out = json.dumps(d, indent=1, sort_keys=True) + "\n"
    a, b = json.loads(text)["files"], json.loads(out)["files"]
    diffs = [i for i, (x, y) in enumerate(zip(a, b)) if x != y]
    if len(a) != len(b) or len(diffs) != 8: stop(f"entries {len(a)} -> {len(b)}, differing {len(diffs)}")
    for i in diffs:
        x, y = a[i], b[i]
        if not (y == {"layer": x["layer"], "note": WHY, "url": M[x["url"]]}): stop(f"entry {i} is not exactly the mirror edit")
    return out, diffs
plan = os.path.join(C, "plan.json")
src = open(plan, "rb").read()
if json.dumps(json.loads(src), indent=1, sort_keys=True).encode() + b"\n" != src: stop("plan.json does not re-serialize to its own bytes")
if mode == "--rehearse":
    out, diffs = edited(src.decode())
    d = os.path.join(S, "mirror", "rehearse"); os.makedirs(d, exist_ok=True)
    open(os.path.join(d, "plan.json"), "wb").write(out.encode())
    print(f"rehearsal: plan.json {sha(src)[:12]} unchanged on disk ({sha(open(plan, 'rb').read())[:12]}); the copy {sha(out.encode())[:12]}; entries changed {len(diffs)} at {diffs}")
    for i in diffs: print("   ", json.loads(out)["files"][i]["url"])
elif mode == "--apply":
    ps = subprocess.run(["powershell", "-NoProfile", "-Command", "(Get-CimInstance Win32_Process | Where-Object { $_.CommandLine -match 'ingest-basemap.ts' -and $_.CommandLine -notmatch 'Get-CimInstance Win32_Process' } | Measure-Object).Count"], capture_output=True, text=True).stdout.strip()
    if ps != "0": stop(f"{ps} ingest-basemap processes still running: the retry must have ended first")
    rf = os.path.join(C, "retry-fetched.json")
    got = json.load(open(rf, encoding="utf-8")) if os.path.exists(rf) else {}
    if any(u in got for u in E): stop("a retry round fetched a primary file: the mirror is not used for it (the orchestrator)")
    keep = os.path.join(S, "mirror", f"plan.before-mirror.{sha(src)[:12]}.json")
    shutil.copyfile(plan, keep)
    if sha(open(keep, "rb").read()) != sha(src): stop("the byte copy differs")
    out, diffs = edited(src.decode())
    open(plan, "wb").write(out.encode())
    back = open(plan, "rb").read()
    if back != out.encode(): stop("plan.json read back differs from what was written")
    print(f"applied: plan.json {sha(src)[:12]} -> {sha(back)[:12]}; copy kept at {keep}; entries changed {len(diffs)}")
    print("the fetch, the ingest's own retry, one round, one file a minute (via runlog.sh, from sab-ingest/proxy):")
    print(f"  npx tsx scripts/ingest-basemap.ts retry --cache <bm-cache-qw4> --schedule-min 0 --urls {','.join(M[u] for u in E)}")
elif mode == "--check":
    got = json.load(open(os.path.join(C, "retry-fetched.json"), encoding="utf-8"))
    bad = 0
    for u in E:
        m, r, b = M[u], got.get(M[u]), BM[u]
        p = os.path.join(C, "files", *m.split("://")[1].split("/"))
        if not r or not os.path.exists(p): print(f"  {m.split('/')[-1]}: NOT FETCHED"); bad += 1; continue
        data = open(p, "rb").read()
        ok = (m.rsplit("/", 1)[1] == u.rsplit("/", 1)[1] and data[:4] in (b"PK\x03\x04", b"PK\x05\x06") and len(data) == r["bytes"] == b["bytes"]
              and sha(data) == r["sha256"] == b["sha256"] and r["vintage"] == b["vintage"] == "2024")
        print(f"  {m.split('/')[-1]}: {'SAME as bm-obj' if ok else 'DIFFERS'} (bytes {len(data)}/{b['bytes']}, sha {sha(data)[:12]}/{b['sha256'][:12]}, vintage {r['vintage']})")
        bad += not ok
    print("identity: ALL 8 SAME" if bad == 0 else f"identity: {bad} of 8 not same -- STOP before any dry run")
    sys.exit(1 if bad else 0)
else:
    stop("mode: --rehearse | --apply | --check")
