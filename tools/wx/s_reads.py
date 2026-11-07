"""Plan S's reader (docs/wx-plan-s.md §3, frozen at 22707c5): one phase of blipscope-wx-ingest-dev, judged by that
phase's frozen table. Read-only; tokens from the token file, never printed.

Two instruments, both for the dev script only:
  Logs   Workers Logs invocation records ($workers.outcome / cpuTimeMs / wallTimeMs / scriptVersion), grouped by
         $workers.event.scheduledTime, joined to the check lines (product + status, or the plant line)
  R3-6   workersInvocationsAdaptive by status (R3-6's own instrument), for scriptName blipscope-wx-ingest-dev

  python -I s_reads.py <S1|S2|S3|S4|S5> <from ISO> <to ISO> [version-id]
The window bounds scheduledTime. With a version id, only that version's invocations count (records without one print
as "unattributed"). S4's graded set is pinned in docs/wx-opuk.md before S0: the invocations whose `stored` check
counts toward N (after the last `backfilled`); the backfill invocations print beside it, ungraded.
"""
import json
import os
import statistics
import sys
import urllib.request
from datetime import datetime, timezone

ACCOUNT = "48822e896bb10c45aa6bfe139bcff3d1"
SCRIPT = "blipscope-wx-ingest-dev"
PLANT_MARKER = "wx_plant_synthetic_busy_loop"


def tf():
    d = {}
    for line in open(os.path.expanduser("~/.config/blipscope/tokens"), encoding="utf-8"):
        k, _, v = line.strip().partition("=")
        if k and v:
            d[k] = v
    return d


def post(url, body, tok):
    req = urllib.request.Request(url, data=json.dumps(body).encode(), method="POST",
                                 headers={"Authorization": "Bearer " + tok, "Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=120) as r:
        return json.loads(r.read())


ms = lambda s: int(datetime.fromisoformat(s.replace("Z", "+00:00")).timestamp() * 1000)
hms = lambda t: datetime.fromtimestamp(t, timezone.utc).strftime("%H:%M:%SZ")
phase, a, b = sys.argv[1], sys.argv[2], sys.argv[3]
want_ver = sys.argv[4] if len(sys.argv) > 4 else None
t = tf()

# Logs, a little wider than the window (lines of an invocation land up to ~40 s after its scheduledTime)
evs, t0, end = [], ms(a), ms(b) + 120_000
while t0 < end:
    t1 = min(t0 + 3600_000, end)
    off = None
    while True:
        body = {"queryId": "plan-s", "timeframe": {"from": t0, "to": t1}, "view": "events", "limit": 2000,
                "parameters": {"filters": [{"key": "$metadata.service", "operation": "eq", "type": "string", "value": SCRIPT}]}}
        if off:
            body["offset"] = off
        r = post(f"https://api.cloudflare.com/client/v4/accounts/{ACCOUNT}/workers/observability/telemetry/query", body,
                 t["observability-read"])["result"]
        page = r["events"]["events"]
        evs += page
        if len(page) < 2000:
            break
        off = page[-1]["$metadata"]["id"]
    t0 = t1
inv = {}
for e in evs:
    w = e.get("$workers") or {}
    st = (w.get("event") or {}).get("scheduledTime")
    if st is None or not (ms(a) / 1000 <= st < ms(b) / 1000):
        continue
    d = inv.setdefault(st, {"cpu": None, "wall": None, "outcome": None, "ver": None, "checks": []})
    ver = (w.get("scriptVersion") or {}).get("id")
    if ver:
        d["ver"] = ver
    if "outcome" in w:
        d["cpu"], d["wall"], d["outcome"] = w.get("cpuTimeMs"), w.get("wallTimeMs"), w.get("outcome")
    s = e.get("source") or {}
    if s.get("evt") == PLANT_MARKER:
        d["checks"].append(("plant", s.get("check"), s.get("result")))
    elif s.get("product"):
        d["checks"].append((s["product"], s.get("check"), s.get("status") or s.get("evt")))
    elif s.get("evt") in ("wx_dev_off", "wx_dev_mode_error"):
        d["checks"].append((s["evt"], s.get("check"), s.get("mode")))
rows = sorted(inv.items())
unattributed = sum(1 for _, d in rows if d["ver"] is None)
if want_ver:
    rows = [(st, d) for st, d in rows if d["ver"] == want_ver]
recs = [(st, d) for st, d in rows if d["outcome"] is not None]

# R3-6's instrument
q = """query($acct: String!, $a: Time!, $b: Time!, $s: String!) { viewer { accounts(filter: {accountTag: $acct}) {
  workersInvocationsAdaptive(limit: 100, filter: {datetime_geq: $a, datetime_lt: $b, scriptName: $s}) {
    sum { requests errors cpuTimeUs } dimensions { status } } } } }"""
g = post("https://api.cloudflare.com/client/v4/graphql", {"query": q, "variables": {"acct": ACCOUNT, "a": a, "b": b, "s": SCRIPT}},
         t["analytics-read"])
if g.get("errors"):
    raise SystemExit("GraphQL errors: " + json.dumps(g["errors"])[:300])
by_status = {x["dimensions"]["status"]: {"invocations": x["sum"]["requests"], "errors": x["sum"]["errors"]}
             for x in g["data"]["viewer"]["accounts"][0]["workersInvocationsAdaptive"]}

out = {"phase": phase, "window": [a, b], "version": want_ver, "records": len(recs), "invocations_seen": len(rows),
       "unattributed_in_window": unattributed, "outcomes": {}, "r3_6_by_status": by_status}
for _, d in recs:
    out["outcomes"][d["outcome"]] = out["outcomes"].get(d["outcome"], 0) + 1
out["per_invocation"] = [f"{hms(st)} {d['outcome']} cpu {d['cpu']} wall {d['wall']} {d['checks']}" for st, d in rows]
cpus = [d["cpu"] for _, d in recs if isinstance(d["cpu"], (int, float))]
med = lambda v: statistics.median(v) if v else None

if phase == "S1":
    c = med(cpus)
    within = c is not None and all(abs(x - c) <= 0.25 * c for x in cpus)
    out.update({"C_median_ms": c, "all_within_25pct": within})
    if c is not None and c < 500:
        out["verdict"] = "STOP: C under 500 ms"
    else:
        ok = len(recs) == 3 and out["outcomes"] == {"ok": 3} and within
        out["verdict"] = "AS PREDICTED" if ok else "OFF: outside S1's row"
elif phase == "S2":
    allowed = {"ok", "exceededCpu", "exceededResources"}
    exceeded = out["outcomes"].get("exceededCpu", 0)
    nonsuccess = sum(v["invocations"] for k, v in by_status.items() if k != "success")
    out.update({"exceededCpu": exceeded, "r3_6_non_success": nonsuccess})
    if set(out["outcomes"]) - allowed or len(recs) < 9:
        out["verdict"] = "STOP AND DECIDE: instrument unreliable at this scale"
    elif exceeded >= 1 and nonsuccess >= 1:
        out["verdict"] = f"LIVE: {exceeded} of {len(recs)} exceededCpu"
    elif exceeded == 0 and nonsuccess == 0 and out["outcomes"] == {"ok": len(recs)}:
        out["verdict"] = "BLIND: the gate cannot see a CPU-limit outcome"
    else:
        out["verdict"] = "STOP AND DECIDE: the two instruments disagree"
elif phase in ("S3", "S4"):
    prod = "cref" if phase == "S3" else "opuk"
    built = lambda d: any(c[0] == prod and c[2] in ("stored", "backfilled") for c in d["checks"])
    if phase == "S4":
        seq = [(st, c[2]) for st, d in recs for c in d["checks"] if c[0] == "opuk" and c[2] in ("stored", "backfilled")]
        last_bf = max((st for st, s in seq if s == "backfilled"), default=None)
        counted = sorted({st for st, s in seq if s == "stored" and (last_bf is None or st > last_bf)})
        graded = [d["cpu"] for st, d in recs if st in counted]
        backfill = [d["cpu"] for st, d in recs if built(d) and st not in counted]
        rest = [d["cpu"] for st, d in recs if not built(d)]
        out.update({"N_counted": len(counted), "backfill_invocations_cpu_ungraded": backfill})
        lo, hi = 1500, 3000
    else:
        graded = [d["cpu"] for _, d in recs if built(d)]
        rest = [d["cpu"] for _, d in recs if not built(d)]
        lo, hi = 900, 1700
    out.update({"building": {"n": len(graded), "median": med(graded), "max": max(graded) if graded else None},
                "rest": {"n": len(rest), "median": med(rest), "max": max(rest) if rest else None}})
    ok = (graded and lo <= med(graded) <= hi and max(graded) <= 4000 and rest and med(rest) <= 100
          and set(out["outcomes"]) == {"ok"})
    if phase == "S4" and len(counted) < 6:
        out["verdict"] = f"BLIND: N = {len(counted)} (< 6)"
    else:
        out["verdict"] = "AS PREDICTED" if ok else "OFF: outside the row -> stop and decide"
elif phase == "S5":
    # re-registered ~05:05Z: from S5's deploy (the window's start; version = S5's), invocations land only on S5's
    # version and log only wx_dev_off; the last starts within 900 s of the deploy; then 0 in [deploy+900, deploy+1500].
    dep = ms(a) / 1000
    wrong_ver = [hms(st) for st, d in inv.items() if st >= dep and want_ver and d["ver"] not in (None, want_ver)]
    not_off = [hms(st) for st, d in inv.items() if st >= dep and any(c[0] != "wx_dev_off" for c in d["checks"])]
    starts = sorted(st for st in inv if st >= dep)
    late = [hms(st) for st in starts if st > dep + 900]
    quiet = [hms(st) for st in starts if dep + 900 <= st < dep + 1500]
    covered = ms(b) / 1000 >= dep + 1500
    out.update({"after_deploy": len(starts), "last": hms(starts[-1]) if starts else None, "other_version": wrong_ver,
                "not_wx_dev_off": not_off, "after_900s": late, "in_quiet_window": quiet, "quiet_window_covered": covered})
    if not covered:
        out["verdict"] = "NOT YET: the window must reach the deploy + 1,500 s"
    else:
        ok = not wrong_ver and not not_off and not late and not quiet
        out["verdict"] = "AS PREDICTED" if ok else "OFF: outside S5's row"
print(json.dumps(out, indent=1))
