# SCRATCH: the census mirror switch, form (d) at n (the orchestrator's ruling (ii), after 06:56:14Z 2026-11-14). Its predecessor
# switch_d.py (361161bdc565) refused outright once a retry round fetched a primary file; round 6 fetched 2 of the 8 (27081 and
# 54093 linearwater), so this mirrors only the n the primary still refuses. The stages are switch_d.py's. What changes is WHICH
# entries are mirrored, and the protections the old blanket refusal gave for free are now established explicitly:
#   P1  a mirror file NEVER replaces a primary-fetched file. A primary-fetched entry is skipped; its record and file stay as they
#       were at apply (compared with the byte copy of retry-fetched.json kept at apply), to the end.
#   P2  a mirror file is applied ONLY to an entry in planned_not_fetched (a plan url in neither fetch-log.json nor
#       retry-fetched.json, at apply). A mirror record or a mirror plan entry for any other url is REFUSED.
#   P3  anything it cannot place in either set is refused outright: a record for a url that is not planned, or not one of the 8;
#       a primary record that is not a real fetch (its file missing or not matching it); planned_not_fetched other than the 8 less
#       the primary-fetched; a plan that differs from the kept original anywhere but the mirror set.
# The placement checks run in every stage BEFORE anything is written, and no rehearsal flag skips them.
#   apply   placement at apply; keeps plan.original.<sha>.json and retry.before.<sha>.json; the n entries' url -> the mirror url
#   (then:  npx tsx scripts/ingest-basemap.ts retry --cache <cache> --schedule-min 0 --urls <n mirror urls>, via runlog.sh)
#   check   placement; identity: each mirror file the primary's by name, PK zip, bytes AND sha256 = bm-obj's record, vintage
#           2024; each primary-fetched record unchanged, its file matching it, and equal to bm-obj's record
#   rekey   placement; identity (skippable only in a rehearsal); every refusal checked before the first write; then the n
#           records and files moved to the primary urls, plan.json = original + n notes, every write read back
#   final   the end state: plan == original + notes on exactly the n; no mirror url anywhere; the records are exactly the 8,
#           under primary urls; the primary-fetched ones as kept at apply; every file matching its record and bm-obj's
#   m4-strip / m4-restore   around M-4's control dry run: the plan's ORIGINAL bytes in, then the noted plan back (by sha)
#   python -X utf8 -I mirror/switch_d2.py <stage> [--cache <rehearsal dir> [--rehearsal-skip-proc] [--rehearsal-skip-identity]]
import hashlib, json, os, re, shutil, subprocess, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import C as REAL, S, MIRROR, bm_records, eight, mirror_of

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
UNM = {m: u for u, m in M.items()}
BM = bm_records()
BMID = json.load(open(os.path.join(S, "bm-obj", "current"), encoding="utf-8"))["storeId"]
if sha(open(os.path.join(S, "bm-obj", "manifest.json"), "rb").read())[:12] != BMID:
    stop("bm-obj's manifest does not hash to its current id")
NOTE = lambda u: (f"fetched via the Census Bureau's mirror {M[u]} after www2.census.gov's firewall refused it; "
                  f"its bytes and sha256 equal this url's record in store {BMID}")
local = lambda u: os.path.join(cache, "files", *u.split("://")[1].split("/"))
name = lambda u: u.rsplit("/", 1)[1]
plan_p, rf_p, fl_p = (os.path.join(cache, f) for f in ("plan.json", "retry-fetched.json", "fetch-log.json"))
keep_dir = os.path.join(os.path.dirname(cache), "mirror-d-keep") if rehearsal else os.path.join(S, "mirror")
dump = lambda d: (json.dumps(d, indent=1, sort_keys=True) + "\n").encode()
load = lambda p: json.load(open(p, encoding="utf-8")) if os.path.exists(p) else {}


def write_back(p, b):
    open(p, "wb").write(b)
    if open(p, "rb").read() != b:
        stop(f"{p} read back differs")


def kept(prefix):
    ks = [f for f in os.listdir(keep_dir) if f.startswith(prefix + ".")] if os.path.isdir(keep_dir) else []
    if len(ks) != 1:
        stop(f"{len(ks)} kept {prefix} files, not 1")
    b = open(os.path.join(keep_dir, ks[0]), "rb").read()
    if sha(b)[:12] != ks[0].split(".")[-2]:
        stop(f"the kept {prefix} file does not match its name")
    return b


def real_fetch(u, r):
    p = local(u)
    if not (isinstance(r, dict) and isinstance(r.get("sha256"), str) and re.fullmatch(r"[0-9a-f]{64}", r["sha256"])
            and os.path.exists(p)):
        return False
    d = open(p, "rb").read()
    return len(d) == r.get("bytes") and sha(d) == r["sha256"]


def placed_at_apply(plan, got):
    """P3 at apply. Returns (primary-fetched, the mirror set), both in the 8's order."""
    planned = [f["url"] for f in plan["files"]]
    ps, log = set(planned), load(fl_p)
    for k in sorted(got):
        if k not in ps:
            stop(f"retry-fetched.json holds a record it cannot place (not a planned url): {k}")
        if k not in M:
            stop(f"retry-fetched.json holds a record it cannot place (not one of the 8 the fetch refused): {k}")
        if not real_fetch(k, got[k]):
            stop(f"a primary record is not a real fetch (its file is missing or does not match it): {k}")
    pnf = sorted(u for u in planned if u not in log and u not in got)
    mset = [u for u in E if u not in got]
    if pnf != sorted(mset):
        stop(f"planned_not_fetched ({len(pnf)}) is not the 8 less the primary-fetched ({len(mset)}): it cannot be placed")
    return [u for u in E if u in got], mset


def placed_after_apply(mirror_in_plan):
    """P1, P2 and P3 after apply. Returns (primary-fetched, the mirror set, the records)."""
    before = json.loads(kept("retry.before"))
    primary, mset = [u for u in E if u in before], [u for u in E if u not in before]
    plan, orig = json.loads(open(plan_p, "rb").read()), json.loads(kept("plan.original"))
    if mirror_in_plan:
        pm, want = sorted(f["url"] for f in plan["files"] if f["url"].startswith(MIRROR)), sorted(M[u] for u in mset)
        if pm != want:
            stop(f"the plan's mirror entries are not the mirror set (extra {[name(x) for x in pm if x not in want]}, "
                 f"missing {[name(x) for x in want if x not in pm]})")
        back = {"files": [{**f, "url": UNM.get(f["url"], f["url"])} for f in plan["files"]]}
        if dump(back) != dump(orig):
            stop("the plan differs from the kept original beyond the mirror set")
    got = load(rf_p)
    for k in sorted(got):
        if k in before:
            if got[k] != before[k] or not real_fetch(k, got[k]):
                stop(f"a primary-fetched record or its file changed since apply: {k}")
        elif not (k in UNM and UNM[k] in mset):
            stop(f"retry-fetched.json holds a record it cannot place: {k}")
    for u in primary:
        if u not in got:
            stop(f"a primary-fetched record is gone since apply: {u}")
    return primary, mset, got


def identity(primary, mset, got):
    bad = 0
    for u in mset:
        m, r, b = M[u], got.get(M[u]), BM[u]
        if not r or not os.path.exists(local(m)):
            print(f"  {name(m)}: NOT FETCHED")
            bad += 1
            continue
        d = open(local(m), "rb").read()
        ok = (name(m) == name(u) and d[:4] in (b"PK\x03\x04", b"PK\x05\x06")
              and len(d) == r["bytes"] == b["bytes"] and sha(d) == r["sha256"] == b["sha256"] and r["vintage"] == b["vintage"] == "2024")
        print(f"  {name(m)}: mirror, {'SAME as bm-obj' if ok else 'DIFFERS'} "
              f"(bytes {len(d)}/{b['bytes']}, sha {sha(d)[:12]}/{b['sha256'][:12]}, vintage {r['vintage']})")
        bad += not ok
    for u in primary:
        r, b = got[u], BM[u]
        ok = real_fetch(u, r) and (r["bytes"], r["sha256"], r["vintage"]) == (b["bytes"], b["sha256"], b["vintage"]) and r["vintage"] == "2024"
        print(f"  {name(u)}: primary-fetched, {'SAME as bm-obj' if ok else 'DIFFERS'} (sha {r['sha256'][:12]}/{b['sha256'][:12]})")
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
    src = open(plan_p, "rb").read()
    d = json.loads(src)
    if dump(d) != src:
        stop("plan.json does not re-serialize to its own bytes")
    if any("note" in f or f["url"].startswith(MIRROR) for f in d["files"]):
        stop("plan.json already carries a note or a mirror url")
    rsrc = open(rf_p, "rb").read() if os.path.exists(rf_p) else dump({})
    got = json.loads(rsrc)
    primary, mset = placed_at_apply(d, got)
    if not mset:
        stop("all 8 were fetched from the primary: no mirror needed")
    if any(f.startswith(("plan.original.", "retry.before.")) for f in (os.listdir(keep_dir) if os.path.isdir(keep_dir) else [])):
        stop("a kept plan or record copy already exists: apply has run before")
    os.makedirs(keep_dir, exist_ok=True)
    kp, kr = (os.path.join(keep_dir, f"{p}.{sha(b)[:12]}.json") for p, b in (("plan.original", src), ("retry.before", rsrc)))
    write_back(kp, src)
    write_back(kr, rsrc)
    n = 0
    for f in d["files"]:
        if f["url"] in mset:
            f["url"] = M[f["url"]]
            n += 1
    if n != len(mset):
        stop(f"{n} entries matched, not {len(mset)}")
    write_back(plan_p, dump(d))
    print(f"apply: plan.json {sha(src)[:12]} -> {sha(dump(d))[:12]} ({len(mset)} urls to the mirror; {len(primary)} primary-fetched "
          f"skipped: {', '.join(name(u) for u in primary) or 'none'}); kept {os.path.basename(kp)} and {os.path.basename(kr)}")
elif stage == "check":
    primary, mset, got = placed_after_apply(True)
    bad = identity(primary, mset, got)
    print(f"identity: ALL {len(mset)} mirror + {len(primary)} primary-fetched SAME" if bad == 0
          else f"identity: {bad} of {len(mset) + len(primary)} not same -- STOP before the re-key")
    sys.exit(1 if bad else 0)
elif stage == "rekey":
    primary, mset, got = placed_after_apply(True)
    if not skip_id and identity(primary, mset, got) != 0:
        stop("identity did not pass")
    for u in mset:                                   # every refusal before the first write
        if u in got:
            stop(f"a record already exists under the primary url {u}")
        if os.path.exists(local(u)):
            stop(f"a file already exists at the primary path of {u}")
        if M[u] not in got or not os.path.exists(local(M[u])):
            stop(f"the mirror fetch left no record or no file for {name(u)}")
        if sha(open(local(M[u]), "rb").read()) != got[M[u]]["sha256"]:
            stop(f"the mirror file no longer matches its record: {M[u]}")
    pb = {u: sha(open(local(u), "rb").read()) for u in primary}
    for u in mset:
        os.makedirs(os.path.dirname(local(u)), exist_ok=True)
        shutil.copyfile(local(M[u]), local(u))
        if sha(open(local(u), "rb").read()) != got[M[u]]["sha256"]:
            stop(f"the copy at the primary path differs: {u}")
        got[u] = got.pop(M[u])
    write_back(rf_p, dump(got))
    orig = kept("plan.original")
    d = json.loads(orig)
    for f in d["files"]:
        if f["url"] in mset:
            f["note"] = NOTE(f["url"])
    write_back(plan_p, dump(d))
    if any(sha(open(local(u), "rb").read()) != pb[u] for u in primary):
        stop("a primary-fetched file changed during the re-key")
    print(f"rekey: {len(mset)} records moved to the primary urls; {len(mset)} files copied to the primary paths; "
          f"{len(primary)} primary-fetched skipped, unchanged; plan.json = original {sha(orig)[:12]} + {len(mset)} notes -> {sha(dump(d))[:12]}")
elif stage == "final":
    before = json.loads(kept("retry.before"))
    primary, mset = [u for u in E if u in before], [u for u in E if u not in before]
    orig, now = json.loads(kept("plan.original")), json.loads(open(plan_p, "rb").read())
    if len(orig["files"]) != len(now["files"]):
        stop("plan entry count changed")
    diffs = [i for i, (x, y) in enumerate(zip(orig["files"], now["files"])) if x != y]
    if (sorted(orig["files"][i]["url"] for i in diffs) != sorted(mset)
            or any(now["files"][i] != {**orig["files"][i], "note": NOTE(orig["files"][i]["url"])} for i in diffs)):
        stop(f"plan is not original + notes on exactly the {len(mset)} ({len(diffs)} entries differ)")
    if any(f["url"].startswith(MIRROR) for f in now["files"]):
        stop("a plan entry names the mirror")
    got = load(rf_p)
    if sorted(got) != sorted(E):
        stop(f"the records are not exactly the 8 under their primary urls (cannot place {[k for k in sorted(got) if k not in M]}, "
             f"missing {[name(u) for u in E if u not in got]})")
    bad = 0
    for u in E:
        r = got[u]
        ok = real_fetch(u, r) and (u in mset or r == before[u])
        if u in primary or not skip_id:
            ok = ok and (r["bytes"], r["sha256"], r["vintage"]) == (BM[u]["bytes"], BM[u]["sha256"], BM[u]["vintage"])
        print(f"  {name(u)}: {'mirror' if u in mset else 'primary-fetched'}, {'OK' if ok else 'NOT OK'}")
        bad += not ok
    print((f"final: plan = original + {len(mset)} notes; {len(mset)} mirrored + {len(primary)} primary-fetched records, files and "
           f"bm-obj records agree" + (f" (bm-obj equality SKIPPED for the {len(mset)} mirrored: rehearsal)" if skip_id else ""))
          if bad == 0 else f"final: {bad} NOT OK")
    sys.exit(1 if bad else 0)
elif stage == "m4-strip":
    # M-4's control dry run reads the ORIGINAL plan bytes (no notes): what a primary-fetched store's plan is. The noted plan is kept.
    n = sum(1 for u in E if u not in json.loads(kept("retry.before")))
    cur = open(plan_p, "rb").read()
    if sum(1 for f in json.loads(cur)["files"] if "note" in f) != n:
        stop(f"plan.json does not carry the {n} notes: run final first")
    kp = os.path.join(keep_dir, f"plan.noted.{sha(cur)[:12]}.json")
    write_back(kp, cur)
    write_back(plan_p, kept("plan.original"))
    print(f"m4-strip: plan.json {sha(cur)[:12]} -> {sha(kept('plan.original'))[:12]} (the original); noted plan kept at {os.path.basename(kp)}")
elif stage == "m4-restore":
    b = kept("plan.noted")
    if open(plan_p, "rb").read() != kept("plan.original"):
        stop("plan.json is not the original: m4-strip did not run, or something wrote it since")
    write_back(plan_p, b)
    print(f"m4-restore: plan.json -> {sha(b)[:12]} (the noted plan)")
else:
    stop("stage: apply | check | rekey | final | m4-strip | m4-restore")
