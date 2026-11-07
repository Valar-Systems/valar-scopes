"""R3-0 (docs/wx-opuk.md): the cost statement, re-read. Read-only: the GraphQL analytics API with the token file's
analytics-read line (never printed). The last 30 days of the account's Workers CPU (by script) and R2 operations (by
action, classed A/B/free), the opuk ingest's projected increment, and the projected overage in USD a month.

Usage: python -I cost_read.py <out.json>
"""
import json
import os
import sys
import urllib.request
from datetime import datetime, timedelta, timezone

ACCOUNT = "48822e896bb10c45aa6bfe139bcff3d1"   # proxy/wrangler.toml (public)
INCLUDED = {"cpu_ms": 30e6, "class_a": 1e6, "class_b": 10e6}
PRICE = {"cpu_ms": 0.02 / 1e6, "class_a": 4.50 / 1e6, "class_b": 0.36 / 1e6}
# R2's operation classes (developers.cloudflare.com/r2/pricing): what is not listed is printed as "unclassed"
CLASS_A = {"ListBuckets", "PutBucket", "ListObjects", "PutObject", "CopyObject", "CompleteMultipartUpload",
           "CreateMultipartUpload", "ListMultipartUploads", "UploadPart", "UploadPartCopy", "ListParts",
           "PutBucketEncryption", "PutBucketCors", "PutBucketLifecycleConfiguration", "LifecycleStorageTierTransition"}
CLASS_B = {"HeadBucket", "HeadObject", "GetObject", "UsageSummary", "GetBucketEncryption", "GetBucketLocation",
           "GetBucketCors", "GetBucketLifecycleConfiguration"}
FREE = {"DeleteObject", "DeleteObjects", "DeleteBucket", "AbortMultipartUpload"}
# opuk's increment a 30-day month, from docs/uk-data.md section 5 with CPU from K11 (Node 0.25-0.41 s a frame) x I7's
# factor of 2, plus about 2,600 empty checks a day at about 8 ms
OPUK = {"cpu_ms": (288 * 30 * 500 + 2600 * 30 * 8, 288 * 30 * 820 + 2600 * 30 * 8),
        "class_a": (26e3, 26e3), "class_b": (430e3, 430e3)}


def token(name):
    for line in open(os.path.expanduser("~/.config/blipscope/tokens"), encoding="utf-8"):
        k, _, v = line.strip().partition("=")
        if k == name and v:
            return v
    raise SystemExit(f"{name} absent from the token file")


def gql(q, v):
    req = urllib.request.Request("https://api.cloudflare.com/client/v4/graphql",
                                 data=json.dumps({"query": q, "variables": v}).encode(), method="POST",
                                 headers={"Authorization": "Bearer " + token("analytics-read"),
                                          "Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=120) as r:
        d = json.loads(r.read())
    if d.get("errors"):
        raise SystemExit("GraphQL errors: " + json.dumps(d["errors"])[:400])
    return d["data"]["viewer"]["accounts"][0]


now = datetime.now(timezone.utc).replace(microsecond=0)
start = now - timedelta(days=30)
iso = lambda t: t.isoformat().replace("+00:00", "Z")
cpu, req, cpu_day, ops_day = {}, {}, {}, {}
# the adaptive datasets are read a day at a time (well inside their per-query range limits)
for d in range(30):
    a, b = start + timedelta(days=d), start + timedelta(days=d + 1)
    w = gql("""query($acct: String!, $a: Time!, $b: Time!) { viewer { accounts(filter: {accountTag: $acct}) {
      workersInvocationsAdaptive(limit: 1000, filter: {datetime_geq: $a, datetime_lt: $b}) {
        sum { cpuTimeUs requests } dimensions { scriptName } } } } }""", {"acct": ACCOUNT, "a": iso(a), "b": iso(b)})
    for g in w["workersInvocationsAdaptive"]:
        s = g["dimensions"]["scriptName"]
        cpu[s] = cpu.get(s, 0) + g["sum"]["cpuTimeUs"] / 1000.0
        cpu_day[(d, s)] = cpu_day.get((d, s), 0) + g["sum"]["cpuTimeUs"] / 1000.0
        req[s] = req.get(s, 0) + g["sum"]["requests"]
ops = {}
for d in range(30):
    a, b = start + timedelta(days=d), start + timedelta(days=d + 1)
    r = gql("""query($acct: String!, $a: Time!, $b: Time!) { viewer { accounts(filter: {accountTag: $acct}) {
      r2OperationsAdaptiveGroups(limit: 1000, filter: {datetime_geq: $a, datetime_lt: $b}) {
        sum { requests } dimensions { actionType bucketName } } } } }""", {"acct": ACCOUNT, "a": iso(a), "b": iso(b)})
    for g in r["r2OperationsAdaptiveGroups"]:
        key = (g["dimensions"]["actionType"], g["dimensions"]["bucketName"])
        ops[key] = ops.get(key, 0) + g["sum"]["requests"]
        ops_day[(d, key[0])] = ops_day.get((d, key[0]), 0) + g["sum"]["requests"]

classed = {"class_a": 0, "class_b": 0, "free": 0, "unclassed": 0}
unclassed = {}
for (act, _), n in ops.items():
    c = "class_a" if act in CLASS_A else "class_b" if act in CLASS_B else "free" if act in FREE else "unclassed"
    classed[c] += n
    if c == "unclassed":
        unclassed[act] = unclassed.get(act, 0) + n
now_cpu = sum(cpu.values())


def overage(cpu_ms, a, b):
    return (max(0, cpu_ms - INCLUDED["cpu_ms"]) * PRICE["cpu_ms"] + max(0, a - INCLUDED["class_a"]) * PRICE["class_a"]
            + max(0, b - INCLUDED["class_b"]) * PRICE["class_b"])


proj = [overage(now_cpu + OPUK["cpu_ms"][i], classed["class_a"] + OPUK["class_a"][i],
                classed["class_b"] + OPUK["class_b"][i]) for i in (0, 1)]

# The run rate: the last 7 full days x 30/7. The 30-day sum understates a month for anything that started inside the
# window (the wx ingest went live on 2026-10-23), so the projection is also made from the run rate.
rr_cpu = sum(v for (d, _), v in cpu_day.items() if d >= 23) * 30 / 7
rr_ops = {"class_a": 0, "class_b": 0}
for (d, act), n in ops_day.items():
    if d >= 23:
        if act in CLASS_A: rr_ops["class_a"] += n * 30 / 7
        elif act in CLASS_B: rr_ops["class_b"] += n * 30 / 7
rr_proj = [overage(rr_cpu + OPUK["cpu_ms"][i], rr_ops["class_a"] + OPUK["class_a"][i], rr_ops["class_b"] + OPUK["class_b"][i])
           for i in (0, 1)]
out = {"window": [iso(start), iso(now)],
       "now": {"cpu_ms": round(now_cpu), "cpu_ms_by_script": {k: round(v) for k, v in sorted(cpu.items(), key=lambda x: -x[1])},
               "class_a": classed["class_a"], "class_b": classed["class_b"], "free_ops": classed["free"],
               "unclassed_ops": unclassed,
               "r2_by_bucket": {bk: sum(n for (a, bb), n in ops.items() if bb == bk) for bk in sorted({b for _, b in ops})}},
       "opuk_increment": {k: list(v) for k, v in OPUK.items()},
       "overage_usd_month": {"now": round(overage(now_cpu, classed["class_a"], classed["class_b"]), 4),
                             "with_opuk_low": round(proj[0], 4), "with_opuk_high": round(proj[1], 4)},
       "run_rate_month": {"cpu_ms": round(rr_cpu), "class_a": round(rr_ops["class_a"]), "class_b": round(rr_ops["class_b"]),
                          "ingest_cpu_ms_per_day_last7": round(sum(v for (d, sc), v in cpu_day.items() if d >= 23 and sc == "blipscope-wx-ingest") / 7)},
       "overage_usd_month_run_rate": {"with_opuk_low": round(rr_proj[0], 4), "with_opuk_high": round(rr_proj[1], 4)},
       "allowance_usd_month": 2.0}
out["verdict"] = "UNDER $2" if max(proj[1], rr_proj[1]) < 2.0 else "OVER $2: stop and ask Daniel"
json.dump(out, open(sys.argv[1], "w"), indent=1)
print(json.dumps({k: out[k] for k in ("window", "overage_usd_month", "run_rate_month", "overage_usd_month_run_rate", "verdict")}))
print(json.dumps({"cpu_ms_now": out["now"]["cpu_ms"], "class_a_now": out["now"]["class_a"],
                  "class_b_now": out["now"]["class_b"], "unclassed": unclassed}))
