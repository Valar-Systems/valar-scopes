# SCRATCH: the census mirror switch, form (d) (the orchestrator, after 12:53:29Z): fetch from ftp2 through the ingest's OWN retry, prove
# the bytes are the primary's (bm-obj's record), then RE-KEY to the PRIMARY url: the cache file at the primary path, the retry record
# under the primary url, and plan.json back to its original bytes plus one `note` on each of the 8 entries. No code change to #696.
# NOT run on the real cache until the retry's schedule has ended (11-14 ~06:46Z) with files still refused. Stages, in order:
#   apply   plan.json: the 8 entries' url -> the mirror url (a byte copy of the original kept), so the ingest's retry can fetch them
#   (then:  npx tsx scripts/ingest-basemap.ts retry --cache <cache> --schedule-min 0 --urls <8 mirror urls>, via runlog.sh)
#   check   identity, per file: same name; PK zip; bytes AND sha256 equal bm-obj's record for the PRIMARY url; vintage 2024
#   rekey   refuses unless check passes; then the re-key above, every write read back
#   final   the end state: plan == original + 8 notes; each primary url has a record, its file at the primary path matching it, and the
#           record equal to bm-obj's; no plan entry names ftp2; no mirror-url record left
#   m4-strip / m4-restore   around M-4's control dry run: the plan's ORIGINAL bytes in, then the noted plan back (by sha)
#   python -X utf8 -I mirror/switch_d.py <stage> [--cache <rehearsal dir> [--rehearsal-skip-proc] [--rehearsal-skip-identity]]
import hashlib, json, os, shutil, subprocess, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import C as REAL, S, bm_records, eight, mirror_of

a = sys.argv[1:]
stage = a[0] if a else ""
cache = os.path.abspath(a[a.index("--cache") + 1]) if "--cache" in a else REAL
rehearsal = os.path.normcase(cache) != os.path.normcase(REAL)
skip_proc, skip_id = "--rehearsal-skip-proc" in a, "--rehearsal-skip-identity" in a


def stop(why):
    print(f"STOP: {why}")
    sys.exit(1)


if (skip_proc or skip_id) and not rehearsal:
    stop("the rehearsal flags are refused on the real cache")
sha = lambda b: hashlib.sha256(b).hexdigest()
E = eight()
M = {u: mirror_of(u) for u in E}
BM = bm_records()
BMID = json.load(open(os.path.join(S, "bm-obj", "current"), encoding="utf-8"))["storeId"]
if sha(open(os.path.join(S, "bm-obj", "manifest.json"), "rb").read())[:12] != BMID:
    stop("bm-obj's manifest does not hash to its current id")
NOTE = lambda u: (f"fetched via the Census Bureau's mirror {M[u]} after www2.census.gov's firewall refused it; "
                  f"its bytes and sha256 equal this url's record in store {BMID}")
local = lambda u: os.path.join(cache, "files", *u.split("://")[1].split("/"))
plan_p, rf_p = os.path.join(cache, "plan.json"), os.path.join(cache, "retry-fetched.json")
keep_dir = os.path.join(os.path.dirname(cache), "mirror-d-keep") if rehearsal else os.path.join(S, "mirror")
dump = lambda d: (json.dumps(d, indent=1, sort_keys=True) + "\n").encode()


def write_back(p, b):
    open(p, "wb").write(b)
    if open(p, "rb").read() != b:
        stop(f"{p} read back differs")


def original_plan():
    ks = [f for f in os.listdir(keep_dir) if f.startswith("plan.original.")] if os.path.isdir(keep_dir) else []
    if len(ks) != 1:
        stop(f"{len(ks)} kept original plans, not 1")
    b = open(os.path.join(keep_dir, ks[0]), "rb").read()
    if sha(b)[:12] != ks[0].split(".")[2]:
        stop("the kept original plan does not match its name")
    return b


def identity():
    got = json.load(open(rf_p, encoding="utf-8")) if os.path.exists(rf_p) else {}
    bad = 0
    for u in E:
        m, r, b = M[u], got.get(M[u]), BM[u]
        if not r or not os.path.exists(local(m)):
            print(f"  {m.split('/')[-1]}: NOT FETCHED")
            bad += 1
            continue
        d = open(local(m), "rb").read()
        ok = (m.rsplit("/", 1)[1] == u.rsplit("/", 1)[1] and d[:4] in (b"PK\x03\x04", b"PK\x05\x06")
              and len(d) == r["bytes"] == b["bytes"] and sha(d) == r["sha256"] == b["sha256"] and r["vintage"] == b["vintage"] == "2024")
        print(f"  {m.split('/')[-1]}: {'SAME as bm-obj' if ok else 'DIFFERS'} "
              f"(bytes {len(d)}/{b['bytes']}, sha {sha(d)[:12]}/{b['sha256'][:12]}, vintage {r['vintage']})")
        bad += not ok
    return bad


if stage == "apply":
    if not skip_proc:
        n = subprocess.run(["powershell", "-NoProfile", "-Command",
                            "(Get-CimInstance Win32_Process | Where-Object { $_.CommandLine -match 'ingest-basemap.ts' -and "
                            "$_.CommandLine -notmatch 'Get-CimInstance Win32_Process' } | Measure-Object).Count"],
                           capture_output=True, text=True).stdout.strip()
        if n != "0":
            stop(f"{n} ingest-basemap processes running: the retry's schedule must have ended first")
    got = json.load(open(rf_p, encoding="utf-8")) if os.path.exists(rf_p) else {}
    if any(u in got for u in E):
        stop("a retry round fetched a primary file: no mirror for it (the orchestrator)")
    src = open(plan_p, "rb").read()
    d = json.loads(src)
    if dump(d) != src:
        stop("plan.json does not re-serialize to its own bytes")
    if any("note" in f or f["url"].startswith("https://ftp2.") for f in d["files"]):
        stop("plan.json already carries a note or a mirror url")
    os.makedirs(keep_dir, exist_ok=True)
    kp = os.path.join(keep_dir, f"plan.original.{sha(src)[:12]}.json")
    write_back(kp, src)
    n = 0
    for f in d["files"]:
        if f["url"] in M:
            f["url"] = M[f["url"]]
            n += 1
    if n != 8:
        stop(f"{n} entries matched, not 8")
    write_back(plan_p, dump(d))
    print(f"apply: plan.json {sha(src)[:12]} -> {sha(dump(d))[:12]} (8 urls to the mirror); original kept at {kp}")
elif stage == "check":
    bad = identity()
    print("identity: ALL 8 SAME" if bad == 0 else f"identity: {bad} of 8 not same -- STOP before the re-key")
    sys.exit(1 if bad else 0)
elif stage == "rekey":
    if not skip_id and identity() != 0:
        stop("identity did not pass")
    got = json.load(open(rf_p, encoding="utf-8"))
    for u in E:
        if u in got:
            stop(f"a record already exists under the primary url {u}")
        if os.path.exists(local(u)):
            stop(f"a file already exists at the primary path of {u}")
        d = open(local(M[u]), "rb").read()
        if sha(d) != got[M[u]]["sha256"]:
            stop(f"the mirror file no longer matches its record: {M[u]}")
        os.makedirs(os.path.dirname(local(u)), exist_ok=True)
        shutil.copyfile(local(M[u]), local(u))
        if sha(open(local(u), "rb").read()) != got[M[u]]["sha256"]:
            stop(f"the copy at the primary path differs: {u}")
        got[u] = got.pop(M[u])
    write_back(rf_p, dump(got))
    orig = original_plan()
    d = json.loads(orig)
    for f in d["files"]:
        if f["url"] in M:
            f["note"] = NOTE(f["url"])
    write_back(plan_p, dump(d))
    print(f"rekey: 8 records moved to the primary urls; 8 files copied to the primary paths; "
          f"plan.json = original {sha(orig)[:12]} + 8 notes -> {sha(dump(d))[:12]}")
elif stage == "final":
    orig = json.loads(original_plan())
    now = json.loads(open(plan_p, "rb").read())
    if len(orig["files"]) != len(now["files"]):
        stop("plan entry count changed")
    diffs = [i for i, (x, y) in enumerate(zip(orig["files"], now["files"])) if x != y]
    if len(diffs) != 8 or any(now["files"][i] != {**orig["files"][i], "note": NOTE(orig["files"][i]["url"])} for i in diffs):
        stop(f"plan is not original + 8 notes ({len(diffs)} entries differ)")
    if any("ftp2." in f["url"] for f in now["files"]):
        stop("a plan entry names the mirror")
    got = json.load(open(rf_p, encoding="utf-8"))
    if any(M[u] in got for u in E):
        stop("a mirror-url record is still in retry-fetched.json")
    bad = 0
    for u in E:
        r = got.get(u)
        d = open(local(u), "rb").read() if os.path.exists(local(u)) else None
        ok = (bool(r) and d is not None and len(d) == r["bytes"] and sha(d) == r["sha256"]
              and (skip_id or (r["bytes"], r["sha256"], r["vintage"]) == (BM[u]["bytes"], BM[u]["sha256"], BM[u]["vintage"])))
        print(f"  {u.split('/')[-1]}: {'OK' if ok else 'NOT OK'}")
        bad += not ok
    print(("final: plan = original + 8 notes; 8 primary records, files and bm-obj records agree"
           + (" (bm-obj equality SKIPPED: rehearsal)" if skip_id else "")) if bad == 0 else f"final: {bad} NOT OK")
    sys.exit(1 if bad else 0)
elif stage == "m4-strip":
    # M-4's control dry run reads the ORIGINAL plan bytes (no notes): what a primary-fetched store's plan is. The noted plan is kept.
    cur = open(plan_p, "rb").read()
    if sum(1 for f in json.loads(cur)["files"] if "note" in f) != 8:
        stop("plan.json does not carry the 8 notes: run final first")
    kp = os.path.join(keep_dir, f"plan.noted.{sha(cur)[:12]}.json")
    write_back(kp, cur)
    write_back(plan_p, original_plan())
    print(f"m4-strip: plan.json {sha(cur)[:12]} -> {sha(original_plan())[:12]} (the original); noted plan kept at {kp}")
elif stage == "m4-restore":
    ks = [f for f in os.listdir(keep_dir) if f.startswith("plan.noted.")]
    if len(ks) != 1:
        stop(f"{len(ks)} kept noted plans, not 1")
    b = open(os.path.join(keep_dir, ks[0]), "rb").read()
    if sha(b)[:12] != ks[0].split(".")[2]:
        stop("the kept noted plan does not match its name")
    if open(plan_p, "rb").read() != original_plan():
        stop("plan.json is not the original: m4-strip did not run, or something wrote it since")
    write_back(plan_p, b)
    print(f"m4-restore: plan.json -> {sha(b)[:12]} (the noted plan)")
else:
    stop("stage: apply | check | rekey | final | m4-strip | m4-restore")
