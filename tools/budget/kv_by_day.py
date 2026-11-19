#!/usr/bin/env python3
"""The account's KV operations per UTC day, by action and namespace (docs/account-budget.md 9.4): the reader behind the
budget's KV rows by event. Read-only; the analytics-read token comes from the token file and is never printed; counts
only.

  python3 -I tools/budget/kv_by_day.py <start ISO> <end ISO> <out.json>

The window must be whole UTC days that have ended (start and end at 00:00:00Z, end not in the future): a day still in
progress is a partial count that reads exactly like a quiet day. Logic unchanged from the 2026-11-14 read
(kvOperationsAdaptiveGroups by date, actionType and namespaceId). A namespace the mapping does not name is counted as
"other" and printed, never dropped.

Output: {"window": [a, b], "days": {"YYYY-MM-DD": {"<action>": {"<namespace>": count}}}}.
"""
import importlib.util as _ilu
import json
import os
import sys
import urllib.request
from datetime import datetime, timezone

ACCOUNT = "48822e896bb10c45aa6bfe139bcff3d1"   # proxy/wrangler.toml (public)
KV_NS = {"733bf90056104f56be9492e62450a0bf": "proxy", "8b6e92ed802a4192b6b6c1399fdf96aa": "proxy-staging"}   # wrangler.toml (public)

_tfs = _ilu.spec_from_file_location("token_file", os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "token_file.py"))
TOKEN_FILE = _ilu.module_from_spec(_tfs); _tfs.loader.exec_module(TOKEN_FILE)   # docs/token-file-python.md


def whole_day(t):
    d = datetime.fromisoformat(t.replace("Z", "+00:00"))
    if d.tzinfo is None or (d.hour, d.minute, d.second, d.microsecond) != (0, 0, 0, 0):
        raise SystemExit(f"{t}: not a whole UTC day boundary (00:00:00Z)")
    return d


def main():
    a, b, out_path = sys.argv[1:4]
    da, db = whole_day(a), whole_day(b)
    if not da < db:
        raise SystemExit("the window is empty")
    if db > datetime.now(timezone.utc):
        raise SystemExit(f"{b} has not happened yet: the last day would be a partial count")
    q = ("query($acct: String!, $a: Time!, $b: Time!) { viewer { accounts(filter: {accountTag: $acct}) { "
         "kvOperationsAdaptiveGroups(limit: 5000, filter: {datetime_geq: $a, datetime_lt: $b}) "
         "{ sum { requests } dimensions { date actionType namespaceId } } } } }")
    req = urllib.request.Request("https://api.cloudflare.com/client/v4/graphql", method="POST",
                                 data=json.dumps({"query": q, "variables": {"acct": ACCOUNT, "a": a, "b": b}}).encode(),
                                 headers={"Authorization": "Bearer " + TOKEN_FILE.token("analytics-read"),
                                          "Content-Type": "application/json"})
    d = json.load(urllib.request.urlopen(req, timeout=120))
    if d.get("errors"):
        raise SystemExit("GraphQL errors: " + json.dumps(d["errors"])[:300])
    groups = d["data"]["viewer"]["accounts"][0]["kvOperationsAdaptiveGroups"]
    if len(groups) >= 5000:
        raise SystemExit("the grouped query hit its limit (5000): a truncated read is refused")
    days = {}
    for g in groups:
        dm = g["dimensions"]
        ns = KV_NS.get(dm["namespaceId"], "other")
        cell = days.setdefault(dm["date"], {}).setdefault(str(dm["actionType"]), {})
        cell[ns] = cell.get(ns, 0) + g["sum"]["requests"]
    n_days = (db - da).days
    with open(out_path, "w", encoding="utf-8", newline="\n") as fh:
        json.dump({"window": [a, b], "days": dict(sorted(days.items()))}, fh, indent=1)
        fh.write("\n")
    acts = sorted({act for v in days.values() for act in v})
    print(f"groups {len(groups)}, days {len(days)} of {n_days}, actions {acts}")
    for day, v in sorted(days.items()):
        print(day, " | ".join(f"{act} {sum(v.get(act, {}).values()):>9,} (" +
                              " ".join(f"{n}={c:,}" for n, c in sorted(v.get(act, {}).items())) + ")" for act in acts))
    other = sum(c for v in days.values() for cell in v.values() for n, c in cell.items() if n == "other")
    print("OTHER NAMESPACE:", other)
    return 0


if __name__ == "__main__":
    sys.exit(main())
