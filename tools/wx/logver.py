"""Read-only: which script version wrote a Worker's product lines in a window (Workers Logs, ABR 1 checked).
Prints version-id prefixes, statuses and counts only. Tokens from the token file, never printed.
  python -I logver.py <service> <from ISO> <to ISO>"""
import json, os, sys, urllib.request
from datetime import datetime
ACCOUNT = "48822e896bb10c45aa6bfe139bcff3d1"
import importlib.util as _ilu   # the token-file helper, by path: python -I drops this folder from sys.path
_tfs = _ilu.spec_from_file_location("token_file", os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "token_file.py"))
TOKEN_FILE = _ilu.module_from_spec(_tfs); _tfs.loader.exec_module(TOKEN_FILE)   # docs/token-file-python.md
tok = TOKEN_FILE.token("observability-read")
ms = lambda s: int(datetime.fromisoformat(s.replace("Z", "+00:00")).timestamp() * 1000)
svc, a, b = sys.argv[1], ms(sys.argv[2]), ms(sys.argv[3])
evs, abr, off = [], set(), None
while True:
    body = {"queryId": "logver", "timeframe": {"from": a, "to": b}, "view": "events", "limit": 2000,
            "parameters": {"filters": [{"key": "$metadata.service", "operation": "eq", "type": "string", "value": svc}]}}
    if off: body["offset"] = off
    req = urllib.request.Request(f"https://api.cloudflare.com/client/v4/accounts/{ACCOUNT}/workers/observability/telemetry/query",
                                 data=json.dumps(body).encode(), method="POST", headers={"Authorization": "Bearer " + tok, "Content-Type": "application/json"})
    r = json.loads(urllib.request.urlopen(req, timeout=120).read())["result"]
    abr.add(r.get("statistics", {}).get("abr_level")); page = r["events"]["events"]; evs += page
    if len(page) < 2000: break
    off = page[-1]["$metadata"]["id"]
if sorted(x for x in abr if x is not None) != [1]:
    print(json.dumps({"refused": f"sampled (ABR {sorted(abr, key=str)})"})); sys.exit(2)
keys = sorted({k for e in evs for k in (e.get("$workers") or {})})
out = {}
for e in evs:
    w = e.get("$workers") or {}
    v = (w.get("scriptVersion") or {}).get("id", "none")[:8]
    s = e.get("source") or {}
    st = s.get("status") if s.get("product") else ("invocation" if "outcome" in w else "other")
    out[f"{v}:{st}"] = out.get(f"{v}:{st}", 0) + 1
print(json.dumps({"service": svc, "events": len(evs), "workers_keys": keys, "by_version_status": out}))
