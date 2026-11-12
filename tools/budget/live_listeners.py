#!/usr/bin/env python3
"""Point 4 (docs/account-budget.md section 7.1): how often anyone listens to missileer.watch's "Listen live", as far
as R2's analytics can see. Read-only; counts only; the analytics token from the token file, never printed.

  python3 -I tools/budget/live_listeners.py <end ISO, a whole hour> [days, default 7]

Per hour, missileer-live's GetObject requests (those that reach R2; the edge's cache hits never do) by object class.
Only `.ts` audio segments are fetched by a playing listener alone, once per data centre (immutable, a year), so
`.ts` GETs / 360 is the number of data centres with a listener that hour. Each class, "other" included, is counted by
its OWN filtered query, and the classes must sum to the bucket's GetObject count from a separate unfiltered query,
within 1%, every hour. Otherwise the tool exits 2 (outcome L3: it cannot be trusted to see per-object counts).
"""
import json
import os
import sys
import urllib.request
from datetime import datetime, timedelta, timezone

ACCOUNT = "48822e896bb10c45aa6bfe139bcff3d1"   # proxy/wrangler.toml (public)
BUCKET = "missileer-live"
CLASSES = [("ts", {"objectName_like": "%.ts"}), ("wf", {"objectName_like": "%.wf"}),     # case-sensitive, so
           ("playlist", {"objectName_like": "%.m3u8"}), ("voice", {"objectName_like": "%voice.json"}),   # "other" (the
           ("state", {"objectName_like": "%state.json"})]                                   # notlike AND) is their exact complement


def token():
    for line in open(os.path.expanduser("~/.config/blipscope/tokens"), encoding="utf-8"):
        k, _, v = line.strip().partition("=")
        if k == "analytics-read" and v:
            return v
    raise SystemExit("analytics-read absent from the token file")


def by_hour(a, b, extra, dims="datetimeHour"):
    flt = {"datetime_geq": a, "datetime_lt": b, "bucketName": BUCKET, "actionType": "GetObject", **extra}
    q = {"query": """query($acct: String!, $f: AccountR2OperationsAdaptiveGroupsFilter_InputObject!) { viewer { accounts(filter: {accountTag: $acct}) {
           r2OperationsAdaptiveGroups(limit: 10000, filter: $f) { sum { requests } dimensions { %s } } } } }""" % dims,
         "variables": {"acct": ACCOUNT, "f": flt}}
    req = urllib.request.Request("https://api.cloudflare.com/client/v4/graphql", data=json.dumps(q).encode(), method="POST",
                                 headers={"Authorization": "Bearer " + token(), "Content-Type": "application/json"})
    d = json.loads(urllib.request.urlopen(req, timeout=120).read())
    if d.get("errors"):
        raise SystemExit("GraphQL errors: " + json.dumps(d["errors"])[:400])
    return d["data"]["viewer"]["accounts"][0]["r2OperationsAdaptiveGroups"]


def main():
    end = datetime.fromisoformat(sys.argv[1].replace("Z", "+00:00")).astimezone(timezone.utc)
    days = int(sys.argv[2]) if len(sys.argv) > 2 else 7
    iso = lambda t: t.strftime("%Y-%m-%dT%H:%M:%SZ")
    hours = [end - timedelta(hours=h) for h in range(days * 24, 0, -1)]
    tab = {h: {} for h in hours}
    key = lambda s: datetime.fromisoformat(s.replace("Z", "+00:00"))
    for d in range(days):                       # one day per query keeps every query well under its group limit
        a, b = end - timedelta(days=days - d), end - timedelta(days=days - d - 1)
        for g in by_hour(iso(a), iso(b), {}):
            tab[key(g["dimensions"]["datetimeHour"])]["total"] = g["sum"]["requests"]
        for name, f in CLASSES:
            for g in by_hour(iso(a), iso(b), f):
                tab[key(g["dimensions"]["datetimeHour"])][name] = g["sum"]["requests"]
        other = {"AND": [{"objectName_notlike": f["objectName_like"]} for _, f in CLASSES]}
        for g in by_hour(iso(a), iso(b), other):
            tab[key(g["dimensions"]["datetimeHour"])]["other"] = g["sum"]["requests"]
        for g in by_hour(iso(a), iso(b), {"objectName_like": "%.ts"}, "datetimeHour eyeballRegion"):
            r = tab[key(g["dimensions"]["datetimeHour"])]
            r.setdefault("regions", set()).add(g["dimensions"]["eyeballRegion"])
    bad, listened, dc_hours = [], 0, 0.0
    names = [n for n, _ in CLASSES] + ["other"]
    print("hour (UTC)        total     ts     wf  playlist  voice  state  other  ts/360  regions")
    for h in hours:
        r = tab[h]
        s = sum(r.get(n, 0) for n in names)
        t = r.get("total", 0)
        if abs(s - t) > 0.01 * max(t, 1):
            bad.append(f"{h:%m-%d %H}Z classes {s} vs total {t}")
        ts = r.get("ts", 0)
        listened += ts > 0
        dc_hours += ts / 360
        print(f"{h:%Y-%m-%d %H}Z  {t:7d} {ts:6d} {r.get('wf', 0):6d} {r.get('playlist', 0):9d} {r.get('voice', 0):6d} "
              f"{r.get('state', 0):6d} {r.get('other', 0):6d}  {ts / 360:6.2f}  {len(r.get('regions', ()))}")
    n = len(hours)
    share = listened / n
    outcome = ("L3" if bad else "L0" if (n - listened) / n >= 0.8 else "L1" if share >= 0.8 else "L2")
    print(f"SUMMARY: {n} hours; .ts GETs above 0 in {listened} ({share:.0%}); listener data-centre-hours {dc_hours:.1f} "
          f"({dc_hours / days:.1f} a day); control {'FAILED: ' + '; '.join(bad[:5]) if bad else 'held every hour'}; outcome {outcome}")
    return 2 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
