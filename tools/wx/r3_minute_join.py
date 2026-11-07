"""Scratch, read-only: per cron invocation in a window, its cpuTimeMs/wallTimeMs/outcome (Workers Logs invocation
record) joined to what each product's two checks did in it (stored / backfilled / already-stored / error).
Groups by $workers.event.scheduledTime. Usage: python -I r3_minute_join.py <from ISO> <to ISO>
"""
import json
import os
import statistics
import sys
import urllib.request
from datetime import datetime, timezone

ACCOUNT = "48822e896bb10c45aa6bfe139bcff3d1"
tok = next(l.strip().partition("=")[2] for l in open(os.path.expanduser("~/.config/blipscope/tokens"), encoding="utf-8")
           if l.strip().partition("=")[0] == "observability-read")
ms = lambda s: int(datetime.fromisoformat(s.replace("Z", "+00:00")).timestamp() * 1000)
a, b = ms(sys.argv[1]), ms(sys.argv[2])
evs, t0 = [], a
while t0 < b:
    t1 = min(t0 + 3600_000, b)
    off = None
    while True:
        body = {"queryId": "uk-r3-join", "timeframe": {"from": t0, "to": t1}, "view": "events", "limit": 2000,
                "parameters": {"filters": [{"key": "$metadata.service", "operation": "eq", "type": "string",
                                            "value": "blipscope-wx-ingest"}]}}
        if off:
            body["offset"] = off
        req = urllib.request.Request(f"https://api.cloudflare.com/client/v4/accounts/{ACCOUNT}/workers/observability/telemetry/query",
                                     data=json.dumps(body).encode(), method="POST",
                                     headers={"Authorization": "Bearer " + tok, "Content-Type": "application/json"})
        with urllib.request.urlopen(req, timeout=120) as r:
            page = json.loads(r.read())["result"]["events"]["events"]
        evs += page
        if len(page) < 2000:
            break
        off = page[-1]["$metadata"]["id"]
    t0 = t1
inv = {}
for e in evs:
    w = e.get("$workers") or {}
    st = (w.get("event") or {}).get("scheduledTime")
    if st is None:
        continue
    d = inv.setdefault(st, {"cpu": None, "wall": None, "outcome": None, "cref": [], "opuk": []})
    if "cpuTimeMs" in w:
        d["cpu"], d["wall"], d["outcome"] = w["cpuTimeMs"], w.get("wallTimeMs"), w.get("outcome")
    s = e.get("source") or {}
    if s.get("product") in ("cref", "opuk"):
        d[s["product"]].append(s.get("status") or s.get("evt"))
W = lambda L: sum(1 for x in L if x in ("stored", "backfilled"))
rows = sorted((st, d) for st, d in inv.items() if d["cpu"] is not None)
groups = {}
for st, d in rows:
    groups.setdefault((W(d["cref"]), W(d["opuk"])), []).append(d["cpu"])
print("invocations with a record:", len(rows), " outcomes:", sorted({d["outcome"] for _, d in rows}))
print("by (cref frames built, opuk frames built) in the invocation:")
for k in sorted(groups):
    v = sorted(groups[k])
    print(f"  cref {k[0]} opuk {k[1]}: n={len(v):3d}  median {statistics.median(v):7.0f}  max {v[-1]:6d}  min {v[0]:6d}")
print("the 12 heaviest:")
for st, d in sorted(rows, key=lambda r: -r[1]["cpu"])[:12]:
    print(" ", datetime.fromtimestamp(st, timezone.utc).strftime("%H:%M"), "cpu", d["cpu"], "wall", d["wall"], d["outcome"],
          "cref", d["cref"], "opuk", d["opuk"])
