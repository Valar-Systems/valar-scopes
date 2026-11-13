#!/usr/bin/env python3
"""The proxy's `breaker` and `upstream_skip` lines, broken down (docs/proxy-bill.md, "The breaker and skip breakdown",
B1-B3 and S1-S2).

Read-only. For one whole hour: every Workers Logs event of the production proxy (`blipscope-proxy`) whose JSON carries
evt == "breaker" or "upstream_skip", grouped by (id, state) and (id, reason), with each group's count, its count in each
10-minute slice of the hour, and (breakers) the maximum consecutiveFailures. Events are held in memory only, as
tools/budget/logs_bytes.py and tools/bill/logs_by_kind.py do.

PRINTS ONLY ids, states and reasons from the lists below (anything else as "unknown"), counts, and numbers. Never an
event, a message or another field. Error paths print the exception type only. Tokens from the token file
(observability-read), never printed. No wrangler. Refuses a sampled hour (exit 3).

  python3 -I tools/bill/breaker_skip_read.py <hour start ISO, a whole hour>
"""
import json
import os
import sys
import urllib.error
import urllib.request
from datetime import datetime, timedelta, timezone

ACCOUNT = "48822e896bb10c45aa6bfe139bcff3d1"   # proxy/wrangler.toml (public)
SERVICE = "blipscope-proxy"
# Every breaker / skip id the proxy's source passes (proxy/src/upstreams/*.ts, proxy/src/weatherscope/*.ts, 2026-11-13).
IDS = {"adsb_lol", "adsb_lol_b", "adsb_fi", "adsb_fi_b", "airplanes_live", "bend", "adsbdb_ac", "adsb_lol_route",
       "adsbdb_route", "nws", "nws_points", "nws_stations", "nws_obs", "nws_forecast", "nws_alerts"}
STATES = {"open", "reopened", "closed"}                               # types.ts logBreaker
REASONS = {"disabled_by_config", "breaker_open", "hold_4xx"}          # chain.ts / weatherscope/fetch.ts logSkip
SLICES = 6                                                            # 10-minute slices of the hour

import importlib.util as _ilu   # the token-file helper, by path: python -I drops this folder from sys.path
_tfs = _ilu.spec_from_file_location("token_file", os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "token_file.py"))
TOKEN_FILE = _ilu.module_from_spec(_tfs); _tfs.loader.exec_module(TOKEN_FILE)   # docs/token-file-python.md


def post(url, body, tok):
    req = urllib.request.Request(url, data=json.dumps(body).encode(), method="POST",
                                 headers={"Authorization": "Bearer " + tok, "Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=120) as r:
        return json.loads(r.read())


def line_of(e):
    """The console line's parsed JSON, or None."""
    src = e.get("source")
    if isinstance(src, dict) and isinstance(src.get("evt"), str):
        return src
    msg = (e.get("$metadata") or {}).get("message")
    if isinstance(msg, str) and msg.lstrip().startswith("{"):
        try:
            o = json.loads(msg)
        except ValueError:
            return None
        if isinstance(o, dict) and isinstance(o.get("evt"), str):
            return o
    return None


def event_ms(e):
    for k in ("timestamp", "$timestamp"):
        v = e.get(k)
        if isinstance(v, (int, float)):
            return v
    v = (e.get("$metadata") or {}).get("timestamp") if isinstance(e.get("$metadata"), dict) else None
    return v if isinstance(v, (int, float)) else None


def main():
    h1 = datetime.fromisoformat(sys.argv[1].replace("Z", "+00:00")).astimezone(timezone.utc)
    if h1.minute or h1.second:
        raise SystemExit("the hour must start on a whole hour")
    h2 = h1 + timedelta(hours=1)
    t0 = h1.timestamp() * 1000
    brk, skp, abr, offset, pages, no_ts = {}, {}, set(), None, 0, 0
    while True:
        body = {"queryId": "proxy-bill-breaker-skip", "timeframe": {"from": int(t0), "to": int(h2.timestamp() * 1000)},
                "view": "events", "limit": 2000, "parameters": {}}
        if offset:
            body["offset"] = offset
        r = post(f"https://api.cloudflare.com/client/v4/accounts/{ACCOUNT}/workers/observability/telemetry/query", body,
                 TOKEN_FILE.token("observability-read"))["result"]
        abr.add(r.get("statistics", {}).get("abr_level"))
        page = r["events"]["events"]
        pages += 1
        for e in page:
            if (e.get("$metadata") or {}).get("service") != SERVICE:
                continue
            o = line_of(e)
            if o is None or o["evt"] not in ("breaker", "upstream_skip"):
                continue
            i = o.get("id") if o.get("id") in IDS else "unknown"
            ms = event_ms(e)
            sl = min(SLICES - 1, max(0, int((ms - t0) // 600_000))) if ms is not None else None
            if sl is None:
                no_ts += 1
            if o["evt"] == "breaker":
                st = o.get("state") if o.get("state") in STATES else "unknown"
                g = brk.setdefault((i, st), {"n": 0, "max": 0, "sl": [0] * SLICES})
                cf = o.get("consecutiveFailures")
                if isinstance(cf, int):
                    g["max"] = max(g["max"], cf)
            else:
                rs = o.get("reason") if o.get("reason") in REASONS else "unknown"
                g = skp.setdefault((i, rs), {"n": 0, "sl": [0] * SLICES})
            g["n"] += 1
            if sl is not None:
                g["sl"][sl] += 1
        if len(page) < 2000:
            break
        offset = page[-1]["$metadata"]["id"]
    levels = sorted(x for x in abr if x is not None)
    print(f"hour {h1:%Y-%m-%dT%H}Z  pages {pages}  ABR {levels}  events without a timestamp {no_ts}")
    if levels not in ([1], []):
        print("REFUSED: the hour came back sampled; it is not graded")
        return 3
    nb, ns = sum(g["n"] for g in brk.values()), sum(g["n"] for g in skp.values())
    print(f"breaker events {nb:,}   (id, state): count, max consecutiveFailures, per 10-minute slice")
    for (i, st), g in sorted(brk.items(), key=lambda kv: -kv[1]["n"]):
        print(f"  {i:16s} {st:9s} {g['n']:>6,}  max {g['max']:>5}  {' '.join(f'{x:>4}' for x in g['sl'])}")
    print(f"upstream_skip events {ns:,}   (id, reason): count, per 10-minute slice")
    for (i, rs), g in sorted(skp.items(), key=lambda kv: -kv[1]["n"]):
        print(f"  {i:16s} {rs:18s} {g['n']:>6,}  {' '.join(f'{x:>4}' for x in g['sl'])}")
    return 0


def guarded():
    """Any failure prints its exception TYPE only (and an HTTP status), never its message or a traceback."""
    try:
        return main()
    except SystemExit:
        raise
    except urllib.error.HTTPError as ex:
        print(f"FAILED: HTTPError {ex.code}")
        return 4
    except Exception as ex:   # noqa: BLE001 -- the point is to swallow the message
        print(f"FAILED: {type(ex).__name__}")
        return 4


if __name__ == "__main__":
    sys.exit(guarded())
