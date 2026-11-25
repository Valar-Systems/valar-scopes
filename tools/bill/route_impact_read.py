#!/usr/bin/env python3
"""IMPACT of adsb.lol's routeset outage on /enrich, for one whole hour (docs/proxy-bill.md, "The route source's
outage", read (2): I1/I2).

Read-only. Two sources for the same hour:
  - Analytics Engine: /enrich requests, SUM(_sample_interval * double4) over the two enrich route templates;
  - Workers Logs (every proxy event of the hour, held in memory only): upstream_skip by (id, reason) for the two route
    sources, the routeset's upstream ok/fail, and route_stale lines by verdict (printed only from the source's two values).
Prints the impact table: counts and shares of /enrich. Never an event, a callsign, a route or another field. No wrangler.
Error paths print the exception type only. Tokens from the token file (observability-read, analytics-read).

  python3 -I tools/bill/route_impact_read.py <hour start ISO, a whole hour>
"""
import json
import os
import sys
import urllib.error
import urllib.request
from datetime import datetime, timedelta, timezone

ACCOUNT = "48822e896bb10c45aa6bfe139bcff3d1"   # proxy/wrangler.toml (public)
SERVICE = "blipscope-proxy"
DATASET = "blipscope_proxy"                    # the METRICS binding's dataset
ENRICH = ["/api/v1/blipscope/enrich", "/v1/enrich"]
ROUTE_IDS = {"adsb_lol_route", "adsbdb_route"}
REASONS = {"disabled_by_config", "breaker_open", "hold_4xx"}
VERDICTS = {"not_for_position", "reversed"}    # enrich.ts routeVerdict, the two non-ok values

import importlib.util as _ilu   # the token-file helper, by path: python -I drops this folder from sys.path
_tfs = _ilu.spec_from_file_location("token_file", os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "token_file.py"))
TOKEN_FILE = _ilu.module_from_spec(_tfs); _tfs.loader.exec_module(TOKEN_FILE)   # docs/token-file-python.md


def post(url, data, tok, ctype):
    req = urllib.request.Request(url, data=data, method="POST", headers={"Authorization": "Bearer " + tok, "Content-Type": ctype})
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


def text_of(e):
    msg = (e.get("$metadata") or {}).get("message")
    if isinstance(msg, str):
        return msg
    src = e.get("source")
    return src if isinstance(src, str) else ""


def main():
    h1 = datetime.fromisoformat(sys.argv[1].replace("Z", "+00:00")).astimezone(timezone.utc)
    if h1.minute or h1.second:
        raise SystemExit("the hour must start on a whole hour")
    h2 = h1 + timedelta(hours=1)
    # --- Analytics Engine: /enrich requests in the hour
    w = "timestamp >= toDateTime('%s') AND timestamp < toDateTime('%s')" % (h1.strftime("%Y-%m-%d %H:%M:%S"), h2.strftime("%Y-%m-%d %H:%M:%S"))
    sql = "SELECT SUM(_sample_interval * double4) AS n FROM %s WHERE %s AND index1 IN ('%s') FORMAT JSON" % (DATASET, w, "','".join(ENRICH))
    d = post(f"https://api.cloudflare.com/client/v4/accounts/{ACCOUNT}/analytics_engine/sql", sql.encode(), TOKEN_FILE.token("analytics-read"), "text/plain")
    rows = d.get("data", [])
    n_enrich = float(rows[0]["n"]) if rows and rows[0].get("n") is not None else float("nan")
    # --- Workers Logs: the hour's route lines
    skips, ups, stale, abr, offset, pages = {}, {}, {}, set(), None, 0
    while True:
        body = {"queryId": "proxy-bill-route-impact", "timeframe": {"from": int(h1.timestamp() * 1000), "to": int(h2.timestamp() * 1000)},
                "view": "events", "limit": 2000, "parameters": {}}
        if offset:
            body["offset"] = offset
        r = post(f"https://api.cloudflare.com/client/v4/accounts/{ACCOUNT}/workers/observability/telemetry/query",
                 json.dumps(body).encode(), TOKEN_FILE.token("observability-read"), "application/json")["result"]
        abr.add(r.get("statistics", {}).get("abr_level"))
        page = r["events"]["events"]
        pages += 1
        for e in page:
            if (e.get("$metadata") or {}).get("service") != SERVICE:
                continue
            o = line_of(e)
            if o is not None:
                if o["evt"] == "upstream_skip" and o.get("id") in ROUTE_IDS:
                    k = (o["id"], o.get("reason") if o.get("reason") in REASONS else "unknown")
                    skips[k] = skips.get(k, 0) + 1
                elif o["evt"] == "upstream" and o.get("id") == "adsb_lol_route":
                    k = "ok" if o.get("ok") is True else "fail" if o.get("ok") is False else "unknown"
                    ups[k] = ups.get(k, 0) + 1
                continue
            t = text_of(e)
            if t.startswith("route_stale"):
                v = t.rsplit(" ", 1)[-1].strip()
                v = v if v in VERDICTS else "unknown"
                stale[v] = stale.get(v, 0) + 1
        if len(page) < 2000:
            break
        offset = page[-1]["$metadata"]["id"]
    levels = sorted(x for x in abr if x is not None)
    print(f"hour {h1:%Y-%m-%dT%H}Z  pages {pages}  ABR {levels}")
    if levels not in ([1], []):
        print("REFUSED: the hour came back sampled; it is not graded")
        return 3
    miss = skips.get(("adsbdb_route", "disabled_by_config"), 0)
    withheld = sum(stale.values())
    ok = ups.get("ok", 0)

    def share(x):
        return f"{x / n_enrich:6.1%}" if n_enrich == n_enrich and n_enrich > 0 else "   n/a"

    print(f"/enrich requests (Analytics Engine): {n_enrich:,.0f}")
    print(f"  no route, KV miss (adsbdb_route disabled_by_config): {miss:>7,}  {share(miss)}")
    print(f"  no route, KV hit withheld (route_stale):               {withheld:>7,}  {share(withheld)}   "
          + ", ".join(f"{k} {v:,}" for k, v in sorted(stale.items())))
    print(f"  routeset answered (upstream ok:true; upper bound):    {ok:>7,}  {share(ok)}")
    served = n_enrich - miss - withheld - ok
    print(f"  route from KV (mirror or runtime cache), UPPER bound: {served:>7,.0f}  {share(served)}   (requests without a callsign are inside it)")
    print("  skips by (id, reason): " + ", ".join(f"{a}/{b} {v:,}" for (a, b), v in sorted(skips.items())))
    print("  routeset upstream lines: " + ", ".join(f"{k} {v:,}" for k, v in sorted(ups.items())))
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
