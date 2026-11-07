"""R3's log and CPU reads (docs/wx-opuk.md R3-2, R3-5's log half, R3-6, R3-7). Read-only; tokens from the token file,
never printed. Workers Logs are read in 1-hour windows (unsampled; a long window is sampled at ABR level 10).

  python -I r3_reads.py logs <from ISO> <to ISO>   cref's :00/:30 per minute; opuk's events, errors and `pruned`
  python -I r3_reads.py cpu  <from ISO> <to ISO>   blipscope-wx-ingest: invocations, statuses, CPU p50/p99
"""
import json
import os
import sys
import urllib.request
from datetime import datetime, timezone

ACCOUNT = "48822e896bb10c45aa6bfe139bcff3d1"


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
mode, a, b = sys.argv[1], sys.argv[2], sys.argv[3]
t = tf()

if mode == "logs":
    tok = t["observability-read"]
    evs, abr, t0 = [], set(), ms(a)
    while t0 < ms(b):
        t1 = min(t0 + 3600_000, ms(b))
        offset = None
        while True:
            body = {"queryId": "uk-r3-logs", "timeframe": {"from": t0, "to": t1}, "view": "events", "limit": 2000,
                    "parameters": {"filters": [{"key": "$metadata.service", "operation": "eq", "type": "string",
                                                "value": "blipscope-wx-ingest"}]}}
            if offset:
                body["offset"] = offset
            r = post(f"https://api.cloudflare.com/client/v4/accounts/{ACCOUNT}/workers/observability/telemetry/query", body, tok)["result"]
            abr.add(r.get("statistics", {}).get("abr_level"))
            page = r["events"]["events"]
            evs += page
            if len(page) < 2000:
                break
            offset = page[-1]["$metadata"]["id"]
        t0 = t1
    minutes = {}
    opuk = {"wx_ingest": 0, "wx_ingest_error": 0, "statuses": {}, "pruned_on": {}, "errors": []}
    for e in evs:
        s = e.get("source") or {}
        if s.get("product") == "cref" and s.get("check") in (":00", ":30") and isinstance(s.get("ms"), int):
            m = int((e["timestamp"] - s["ms"]) // 60000)
            minutes.setdefault(m, set()).add((s["check"], s.get("evt")))
        if s.get("product") == "opuk":
            opuk[s.get("evt", "?")] = opuk.get(s.get("evt", "?"), 0) + 1
            st = s.get("status", "error")
            opuk["statuses"][st] = opuk["statuses"].get(st, 0) + 1
            if "pruned" in s:
                opuk["pruned_on"][st] = opuk["pruned_on"].get(st, 0) + 1
            if s.get("evt") == "wx_ingest_error":
                opuk["errors"].append({"err": str(s.get("err"))[:80], "attempts": s.get("attempts")})
    span = range(ms(a) // 60000, ms(b) // 60000)
    both = sum(1 for m in span if {c for c, _ in minutes.get(m, set())} >= {":00", ":30"})
    errs = sum(1 for m in span if any(ev == "wx_ingest_error" for _, ev in minutes.get(m, set())))
    print(json.dumps({"window": [a, b], "abr_levels": sorted(x for x in abr if x is not None), "events": len(evs),
                      "cref_minutes": len(span), "cref_minutes_with_both_checks": both,
                      "cref_minutes_with_an_error_line": errs, "opuk": opuk}, indent=1))

elif mode == "cpu":
    tok = t["analytics-read"]
    q = """query($acct: String!, $a: Time!, $b: Time!) { viewer { accounts(filter: {accountTag: $acct}) {
      workersInvocationsAdaptive(limit: 100, filter: {datetime_geq: $a, datetime_lt: $b, scriptName: "blipscope-wx-ingest"}) {
        sum { requests errors cpuTimeUs } quantiles { cpuTimeP50 cpuTimeP99 } dimensions { status } } } } }"""
    r = post("https://api.cloudflare.com/client/v4/graphql", {"query": q, "variables": {"acct": ACCOUNT, "a": a, "b": b}}, tok)
    if r.get("errors"):
        raise SystemExit("GraphQL errors: " + json.dumps(r["errors"])[:300])
    groups = r["data"]["viewer"]["accounts"][0]["workersInvocationsAdaptive"]
    print(json.dumps({"window": [a, b], "by_status": [{"status": g["dimensions"]["status"], "invocations": g["sum"]["requests"],
                      "errors": g["sum"]["errors"], "cpu_ms_total": round(g["sum"]["cpuTimeUs"] / 1000),
                      "cpu_p50_ms": round(g["quantiles"]["cpuTimeP50"] / 1000, 1),
                      "cpu_p99_ms": round(g["quantiles"]["cpuTimeP99"] / 1000, 1)} for g in groups]}, indent=1))
