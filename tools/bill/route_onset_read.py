#!/usr/bin/env python3
"""ONSET of adsb.lol's routeset outage (docs/proxy-bill.md, "The route source's outage", read (1): O-a/O-b/O-c).

Read-only. For each whole hour in [from, to): the production proxy's Workers Logs events whose JSON `id` is
`adsb_lol_route`, selected SERVER-SIDE by a telemetry filter, counted per hour: breaker open/reopened/closed, upstream
ok:true/ok:false, upstream_skip by reason. Each hour's sampling level is printed; a sampled hour is marked, not graded.

CONTROLS run first and gate everything else (exit 5 if either fails, before any other hour is read):
  positive: the filtered counts for the control hour equal the unfiltered read's (open 25, reopened 324, closed 0,
            breaker_open skips 304 in [2026-11-13T00:00Z, 01:00Z));
  negative: the same filter on an id the source never writes returns 0 events.

PRINTS ONLY counts, hours, and states/reasons from the source's own lists. Events are held in memory only. No wrangler.
Error paths print the exception type only. Token from the token file (observability-read), never printed.

  python3 -I tools/bill/route_onset_read.py <from hour ISO> <to hour ISO>
"""
import json
import os
import sys
import urllib.error
import urllib.request
from datetime import datetime, timedelta, timezone

ACCOUNT = "48822e896bb10c45aa6bfe139bcff3d1"   # proxy/wrangler.toml (public)
SERVICE = "blipscope-proxy"
TARGET = "adsb_lol_route"
NEGATIVE = "zz_no_such_route_source"           # never written by the source
CONTROL_HOUR = "2026-11-13T00:00:00Z"
CONTROL = {("breaker", "open"): 25, ("breaker", "reopened"): 324, ("breaker", "closed"): 0, ("upstream_skip", "breaker_open"): 304}
STATES = {"open", "reopened", "closed"}
REASONS = {"disabled_by_config", "breaker_open", "hold_4xx"}

import importlib.util as _ilu   # the token-file helper, by path: python -I drops this folder from sys.path
_tfs = _ilu.spec_from_file_location("token_file", os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "token_file.py"))
TOKEN_FILE = _ilu.module_from_spec(_tfs); _tfs.loader.exec_module(TOKEN_FILE)   # docs/token-file-python.md


def post(url, body, tok):
    req = urllib.request.Request(url, data=json.dumps(body).encode(), method="POST",
                                 headers={"Authorization": "Bearer " + tok, "Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=120) as r:
        return json.loads(r.read())


def line_of(e):
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


def hour(h1, value, tok):
    """Counts for one hour of events with JSON id == value, filtered server-side. Returns (counts, abr, n, other)."""
    h2 = h1 + timedelta(hours=1)
    c, abr, offset, n, other = {}, set(), None, 0, 0
    while True:
        body = {"queryId": "proxy-bill-route-onset", "timeframe": {"from": int(h1.timestamp() * 1000), "to": int(h2.timestamp() * 1000)},
                "view": "events", "limit": 2000,
                "parameters": {"filters": [{"key": "id", "operation": "eq", "type": "string", "value": value}],
                               "filterCombination": "and"}}
        if offset:
            body["offset"] = offset
        r = post(f"https://api.cloudflare.com/client/v4/accounts/{ACCOUNT}/workers/observability/telemetry/query", body, tok)["result"]
        abr.add(r.get("statistics", {}).get("abr_level"))
        page = r["events"]["events"]
        for e in page:
            n += 1
            if (e.get("$metadata") or {}).get("service") != SERVICE:
                other += 1
                continue
            o = line_of(e)
            if o is None or o.get("id") != value:
                other += 1
                continue
            if o["evt"] == "breaker":
                k = ("breaker", o.get("state") if o.get("state") in STATES else "unknown")
            elif o["evt"] == "upstream":
                k = ("upstream", "ok" if o.get("ok") is True else "fail" if o.get("ok") is False else "unknown")
            elif o["evt"] == "upstream_skip":
                k = ("upstream_skip", o.get("reason") if o.get("reason") in REASONS else "unknown")
            else:
                k = ("other_evt", "-")
            c[k] = c.get(k, 0) + 1
        if len(page) < 2000:
            break
        offset = page[-1]["$metadata"]["id"]
    return c, sorted(x for x in abr if x is not None), n, other


def fmt(c):
    keys = [("breaker", "open"), ("breaker", "reopened"), ("breaker", "closed"), ("upstream", "ok"), ("upstream", "fail"),
            ("upstream_skip", "breaker_open")]
    rest = sum(v for k, v in c.items() if k not in keys)
    return " ".join(f"{c.get(k, 0):>5}" for k in keys) + f" {rest:>5}"


def main():
    tok = TOKEN_FILE.token("observability-read")
    ch = datetime.fromisoformat(CONTROL_HOUR.replace("Z", "+00:00"))
    pos, pabr, pn, poth = hour(ch, TARGET, tok)
    neg, nabr, nn, noth = hour(ch, NEGATIVE, tok)
    pos_ok = all(pos.get(k, 0) == v for k, v in CONTROL.items())
    print(f"CONTROL positive ({CONTROL_HOUR}): " + ", ".join(f"{k[0]}:{k[1]} {pos.get(k, 0)} (want {v})" for k, v in CONTROL.items())
          + f"; events {pn}, not-target {poth}, ABR {pabr} -> {'PASS' if pos_ok else 'FAIL'}")
    print(f"CONTROL negative: events {nn} (want 0) -> {'PASS' if nn == 0 else 'FAIL'}")
    if not pos_ok or nn != 0 or poth:
        print("CONTROLS FAILED: the filter is not trusted; no other hour is read")
        return 5
    h1 = datetime.fromisoformat(sys.argv[1].replace("Z", "+00:00")).astimezone(timezone.utc)
    h2 = datetime.fromisoformat(sys.argv[2].replace("Z", "+00:00")).astimezone(timezone.utc)
    print("hour (UTC)       ABR   open reopn close ok_up fl_up sk_bo  rest   (breaker open/reopened/closed; upstream ok/fail; skip breaker_open)")
    t = h1
    while t < h2:
        c, abr, n, oth = hour(t, TARGET, tok)
        mark = "" if abr in ([1], []) else "  SAMPLED (not graded)"
        print(f"{t:%Y-%m-%dT%HZ}  {str(abr):5s} {fmt(c)}" + (f"  not-target {oth}" if oth else "") + mark)
        t += timedelta(hours=1)
    return 0


def guarded():
    try:
        return main()
    except SystemExit:
        raise
    except urllib.error.HTTPError as ex:
        print(f"FAILED: HTTPError {ex.code}")
        return 4
    except Exception as ex:   # noqa: BLE001
        print(f"FAILED: {type(ex).__name__}")
        return 4


if __name__ == "__main__":
    sys.exit(guarded())
