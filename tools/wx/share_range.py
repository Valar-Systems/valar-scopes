"""The 1c redeploy's frame row (docs/wx-opera-map-r2.md section 7): coveredShare and bytes on opuk's stored and backfilled
lines over one or more windows, from Workers Logs, in 900 s pieces. Read-only; tokens never printed. A sampled piece is
refused: it is re-read once at half the width, then the read fails (the ~18:22Z amendment).

Usage: python -I share_range.py <from ISO> <to ISO> [<from ISO> <to ISO> ...]
"""
import json
import os
import sys
import urllib.request
from datetime import datetime

ACCOUNT = "48822e896bb10c45aa6bfe139bcff3d1"
import importlib.util as _ilu   # the token-file helper, by path: python -I drops this folder from sys.path
_tfs = _ilu.spec_from_file_location("token_file", os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "token_file.py"))
TOKEN_FILE = _ilu.module_from_spec(_tfs); _tfs.loader.exec_module(TOKEN_FILE)   # docs/token-file-python.md
tok = TOKEN_FILE.token("observability-read")
ms = lambda s: int(datetime.fromisoformat(s.replace("Z", "+00:00")).timestamp() * 1000)


def piece(a, b):
    evs, off = [], None
    lv = set()
    while True:
        body = {"queryId": "share-range", "timeframe": {"from": a, "to": b}, "view": "events", "limit": 2000,
                "parameters": {"filters": [{"key": "$metadata.service", "operation": "eq", "type": "string",
                                            "value": "blipscope-wx-ingest-opuk"}]}}
        if off:
            body["offset"] = off
        req = urllib.request.Request(f"https://api.cloudflare.com/client/v4/accounts/{ACCOUNT}/workers/observability/telemetry/query",
                                     data=json.dumps(body).encode(), method="POST",
                                     headers={"Authorization": "Bearer " + tok, "Content-Type": "application/json"})
        with urllib.request.urlopen(req, timeout=120) as r:
            res = json.loads(r.read())["result"]
        page = res["events"]["events"]
        evs += page
        lv.add(res.get("statistics", {}).get("abr_level"))
        if len(page) < 2000:
            return evs, lv
        off = page[-1]["$metadata"]["id"]


def window(a, b, step):
    out, lv, t = [], set(), a
    while t < b:
        e = min(t + step, b)
        p, l = piece(t, e)
        out += p
        lv |= l
        t = e
    return out, lv


rows, abrs, reread = [], set(), 0
args = sys.argv[1:]
for i in range(0, len(args), 2):
    a, b = ms(args[i]), ms(args[i + 1])
    evs, lv = window(a, b, 900_000)
    if lv != {1}:
        reread += 1
        evs, lv = window(a, b, 450_000)
        if lv != {1}:
            print(json.dumps({"verdict": "BLIND", "abr": sorted(lv, key=str), "window": [args[i], args[i + 1]]}))
            sys.exit(2)
    abrs |= lv
    rows += [(e["timestamp"], e["source"]) for e in evs
             if (e.get("source") or {}).get("status") in ("stored", "backfilled") and e["source"].get("product") == "opuk"]
sh = [s["coveredShare"] for _, s in rows if isinstance(s.get("coveredShare"), (int, float))]
print(json.dumps({"windows": args, "abr": sorted(abrs, key=str), "rereads": reread, "lines": len(rows), "with_share": len(sh),
                  "share_min": min(sh) if sh else None, "share_max": max(sh) if sh else None,
                  "bytes": sorted({s.get("bytes") for _, s in rows}, key=str),
                  "statuses": sorted({s.get("status") for _, s in rows})}))
