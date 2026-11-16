#!/usr/bin/env python3
"""December's Workers Logs: bytes per event (docs/account-budget.md section 5.2). Read-only. For one whole hour: every
Workers Logs event, counted and sized by service (its JSON as the telemetry API returns it -- "API bytes", not
necessarily the billed bytes), and each script's invocations in the same hour from the analytics API. Never prints an
event, a key or a value; tokens from the token file (observability-read, analytics-read), never printed.

  python3 -I tools/budget/logs_bytes.py <hour start ISO, a whole hour>      exit 0 read, 3 refused (sampled hour)
"""
import json
import os
import sys
import urllib.request
from datetime import datetime, timedelta, timezone

ACCOUNT = "48822e896bb10c45aa6bfe139bcff3d1"   # proxy/wrangler.toml (public)


import importlib.util as _ilu   # the token-file helper, by path: python -I drops this folder from sys.path
_tfs = _ilu.spec_from_file_location("token_file", os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "token_file.py"))
TOKEN_FILE = _ilu.module_from_spec(_tfs); _tfs.loader.exec_module(TOKEN_FILE)   # docs/token-file-python.md
def token(name):
    return TOKEN_FILE.token(name)


def post(url, body, tok):
    req = urllib.request.Request(url, data=json.dumps(body).encode(), method="POST",
                                 headers={"Authorization": "Bearer " + tok, "Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=120) as r:
        return json.loads(r.read())


def main():
    h1 = datetime.fromisoformat(sys.argv[1].replace("Z", "+00:00")).astimezone(timezone.utc)
    if h1.minute or h1.second:
        raise SystemExit("the hour must start on a whole hour")
    h2 = h1 + timedelta(hours=1)
    n, by, abr, offset, pages = {}, {}, set(), None, 0
    while True:
        body = {"queryId": "uk-budget-logs-bytes", "timeframe": {"from": int(h1.timestamp() * 1000), "to": int(h2.timestamp() * 1000)},
                "view": "events", "limit": 2000, "parameters": {}}
        if offset:
            body["offset"] = offset
        r = post(f"https://api.cloudflare.com/client/v4/accounts/{ACCOUNT}/workers/observability/telemetry/query", body,
                 token("observability-read"))["result"]
        abr.add(r.get("statistics", {}).get("abr_level"))
        page = r["events"]["events"]
        pages += 1
        for e in page:
            svc = (e.get("$metadata") or {}).get("service", "?")
            n[svc] = n.get(svc, 0) + 1
            by[svc] = by.get(svc, 0) + len(json.dumps(e, separators=(",", ":")).encode())
        if len(page) < 2000:
            break
        offset = page[-1]["$metadata"]["id"]
    q = {"query": """query($acct: String!, $a: Time!, $b: Time!) { viewer { accounts(filter: {accountTag: $acct}) {
           workersInvocationsAdaptive(limit: 1000, filter: {datetime_geq: $a, datetime_lt: $b}) { sum { requests } dimensions { scriptName } } } } }""",
         "variables": {"acct": ACCOUNT, "a": h1.strftime("%Y-%m-%dT%H:%M:%SZ"), "b": h2.strftime("%Y-%m-%dT%H:%M:%SZ")}}
    d = post("https://api.cloudflare.com/client/v4/graphql", q, token("analytics-read"))
    if d.get("errors"):
        raise SystemExit("GraphQL errors: " + json.dumps(d["errors"])[:300])
    inv = {}
    for g in d["data"]["viewer"]["accounts"][0]["workersInvocationsAdaptive"]:
        inv[g["dimensions"]["scriptName"]] = inv.get(g["dimensions"]["scriptName"], 0) + g["sum"]["requests"]
    levels = sorted(x for x in abr if x is not None)
    print(f"hour {h1:%Y-%m-%dT%H}Z  pages {pages}  ABR {levels}")
    if levels not in ([1], []):
        print("REFUSED: the hour came back sampled; it is not graded (section 5.2)")
        return 3
    groups = {"proxy": ["blipscope-proxy"], "ingests": [s for s in set(n) | set(inv) if s.startswith("blipscope-wx-ingest")]}
    groups["all"] = sorted(set(n) | set(inv))
    for g, svcs in groups.items():
        ev = sum(n.get(s, 0) for s in svcs)
        b = sum(by.get(s, 0) for s in svcs)
        iv = sum(inv.get(s, 0) for s in svcs)
        print(f"  {g:8s} events {ev:>7,}  invocations {iv:>7,}  events/inv {ev / iv if iv else float('nan'):5.2f}  API bytes/event {b / ev if ev else float('nan'):7.1f}")
    unknown = sorted(s for s in n if s not in inv)
    print("services with events but no invocations in the analytics:", unknown or "none")
    return 0


if __name__ == "__main__":
    sys.exit(main())
