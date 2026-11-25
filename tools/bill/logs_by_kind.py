#!/usr/bin/env python3
"""The proxy's Workers Logs events, by kind (docs/proxy-bill.md, step 3's events-by-kind read, E1-E4).

Read-only. For one whole hour: every Workers Logs event of the production proxy (`blipscope-proxy`), counted and sized
by KIND, plus the proxy's invocations in the same hour from the analytics API. Sizes are "API bytes" exactly as
tools/budget/logs_bytes.py defines them (the event's compact JSON as the telemetry API returns it), not necessarily
the billed bytes.

A kind is one of:
  invocation        $metadata.type == "cf-worker-event" (the invocation log, per Cloudflare's docs)
  evt:<name>        a console line whose JSON carries evt == <name>, for a <name> the proxy's source writes (KNOWN)
  evt:unnamed       a console line with an evt this list does not know (the value is never printed)
  text:<prefix>     a plain-text line the source writes (route_stale, enroll)
  unnamed           anything else

PRINTS ONLY kind names from the lists below, counts and byte totals, and the counts of $metadata.type values shaped
[a-z-]{1,30}. Never an event, a value, a message or a field's contents. Tokens from the token file
(observability-read, analytics-read), never printed. Refuses a sampled hour (exit 3), like logs_bytes.py.

  python3 -I tools/bill/logs_by_kind.py <hour start ISO, a whole hour>
"""
import json
import os
import re
import sys
import urllib.error
import urllib.request
from datetime import datetime, timedelta, timezone

ACCOUNT = "48822e896bb10c45aa6bfe139bcff3d1"   # proxy/wrangler.toml (public)
SERVICE = "blipscope-proxy"
# Every evt value the proxy's request path can write (grep 'evt: *"' proxy/src, 2026-11-13; the ingest Workers'
# wx_ingest* values are other scripts' and are listed so a misattributed event is named rather than hidden).
KNOWN = {"req", "upstream", "upstream_skip", "breaker", "enrich_gap", "tn_miss", "lb_clamp", "incoherent_row", "hold",
         "error", "nws", "missileer_unconfigured", "provision_mint", "activation_admin", "bm_build",
         "wx_ingest", "wx_ingest_error", "wx_ingest_off"}
TEXT = ("route_stale", "enroll")
TYPE_SHAPE = re.compile(r"[a-z-]{1,30}")

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


def evt_of(obj):
    if isinstance(obj, dict) and isinstance(obj.get("evt"), str):
        return obj["evt"]
    return None


def kind(e):
    md = e.get("$metadata") or {}
    if md.get("type") == "cf-worker-event":
        return "invocation"
    # A console line: the parsed JSON may sit in `source`; the raw text in $metadata.message.
    ev = evt_of(e.get("source"))
    msg = md.get("message")
    if ev is None and isinstance(msg, str) and msg.lstrip().startswith("{"):
        try:
            ev = evt_of(json.loads(msg))
        except ValueError:
            ev = None
    if ev is not None:
        return "evt:" + ev if ev in KNOWN else "evt:unnamed"
    text = msg if isinstance(msg, str) else (e.get("source") if isinstance(e.get("source"), str) else "")
    for p in TEXT:
        if text.startswith(p):
            return "text:" + p
    return "unnamed"


def main():
    h1 = datetime.fromisoformat(sys.argv[1].replace("Z", "+00:00")).astimezone(timezone.utc)
    if h1.minute or h1.second:
        raise SystemExit("the hour must start on a whole hour")
    h2 = h1 + timedelta(hours=1)
    n, by, types, abr, offset, pages, other_svc = {}, {}, {}, set(), None, 0, 0
    while True:
        body = {"queryId": "proxy-bill-logs-by-kind", "timeframe": {"from": int(h1.timestamp() * 1000), "to": int(h2.timestamp() * 1000)},
                "view": "events", "limit": 2000, "parameters": {}}
        if offset:
            body["offset"] = offset
        r = post(f"https://api.cloudflare.com/client/v4/accounts/{ACCOUNT}/workers/observability/telemetry/query", body,
                 token("observability-read"))["result"]
        abr.add(r.get("statistics", {}).get("abr_level"))
        page = r["events"]["events"]
        pages += 1
        for e in page:
            md = e.get("$metadata") or {}
            if md.get("service") != SERVICE:
                other_svc += 1
                continue
            t = md.get("type")
            tk = t if isinstance(t, str) and TYPE_SHAPE.fullmatch(t) else "(other shape)"
            types[tk] = types.get(tk, 0) + 1
            k = kind(e)
            n[k] = n.get(k, 0) + 1
            by[k] = by.get(k, 0) + len(json.dumps(e, separators=(",", ":")).encode())
        if len(page) < 2000:
            break
        offset = page[-1]["$metadata"]["id"]
    q = {"query": """query($acct: String!, $a: Time!, $b: Time!) { viewer { accounts(filter: {accountTag: $acct}) {
           workersInvocationsAdaptive(limit: 1000, filter: {datetime_geq: $a, datetime_lt: $b, scriptName: $s}) { sum { requests } } } } }""".replace("$s", '"' + SERVICE + '"'),
         "variables": {"acct": ACCOUNT, "a": h1.strftime("%Y-%m-%dT%H:%M:%SZ"), "b": h2.strftime("%Y-%m-%dT%H:%M:%SZ")}}
    d = post("https://api.cloudflare.com/client/v4/graphql", q, token("analytics-read"))
    if d.get("errors"):
        raise SystemExit("GraphQL errors: " + json.dumps(d["errors"])[:300])
    inv = sum(g["sum"]["requests"] for g in d["data"]["viewer"]["accounts"][0]["workersInvocationsAdaptive"])
    levels = sorted(x for x in abr if x is not None)
    print(f"hour {h1:%Y-%m-%dT%H}Z  pages {pages}  ABR {levels}  other services' events skipped {other_svc:,}")
    if levels not in ([1], []):
        print("REFUSED: the hour came back sampled; it is not graded")
        return 3
    ev, b = sum(n.values()), sum(by.values())
    print(f"{SERVICE}: events {ev:,}  invocations {inv:,}  events/inv {ev / inv if inv else float('nan'):.3f}  API bytes {b:,}")
    print(f"  {'kind':26s} {'events':>9s} {'per inv':>8s} {'bytes':>12s} {'byte share':>10s} {'B/event':>8s}")
    for k in sorted(n, key=lambda k: -by[k]):
        print(f"  {k:26s} {n[k]:>9,} {n[k] / inv if inv else float('nan'):>8.3f} {by[k]:>12,} {by[k] / b if b else 0:>10.1%} {by[k] / n[k]:>8.0f}")
    print("  $metadata.type counts: " + ", ".join(f"{t} {c:,}" for t, c in sorted(types.items())))
    return 0


def guarded():
    """Any failure prints its exception TYPE only (and an HTTP status), never its message or a traceback, so no event
    content can reach stderr through an error path."""
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
