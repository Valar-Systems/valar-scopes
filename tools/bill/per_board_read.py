#!/usr/bin/env python3
"""KV reads and writes per board per month, and the read-overage trigger: docs/proxy-bill.md step 4d (PB1-PB3).

Read-only, analytics only: no wrangler, no Workers Logs, no KV key list. The analytics-read token comes from the token
file and is never printed. Counts only: no key, value, device id or hex is printed or written.

  python -I tools/bill/per_board_read.py read <out.json>
  python -I tools/bill/per_board_read.py grade <read1> <read2>

It reads exactly two whole UTC days: 2026-11-17 (the anchor: step 4c's figures for that day) and 2026-11-18 (graded).
Exit: read 0; grade 0 graded (whatever the grades), 3 ESCAPE, 4 REFUSED (reads under 60 s apart); 2 the rig.
"""
import importlib.util as _ilu
import json
import os
import sys
import urllib.request
from datetime import datetime, timezone

ACCOUNT = "48822e896bb10c45aa6bfe139bcff3d1"          # proxy/wrangler.toml (public)
PROD_NS = "733bf90056104f56be9492e62450a0bf"          # production ENRICH_KV (public)
DATASET = "blipscope_proxy"

# Frozen by the registration (docs/proxy-bill.md step 4d).
WINDOW = ("2026-11-17T00:00:00Z", "2026-11-19T00:00:00Z")
ANCHOR_DAY, DAY = "2026-11-17", "2026-11-18"
ANCHOR = {"reads": 416175, "writes": 2196, "B": 19}      # step 4c, read A, 2026-11-17
NONROUTE = ("err5xx", "enrich_gap", "ota", "boot", "usage", "provision", "sitefeed")   # amendment 4c-1
PB1, PB2, PB3 = (480000, 750000), (2500, 4500), (0.25, 0.40)
READ_PRICE, WRITE_PRICE, FREE_READS = 0.50, 5.00, 10_000_000      # $ per million; the account's free reads a month
TRIGGER = 5.00
MIN_APART_S = 60

_tfs = _ilu.spec_from_file_location("token_file", os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "token_file.py"))
TOKEN_FILE = _ilu.module_from_spec(_tfs); _tfs.loader.exec_module(TOKEN_FILE)   # docs/token-file-python.md


class Rig(Exception):
    pass


def post(url, body, tok, raw=False):
    data = body.encode() if raw else json.dumps(body).encode()
    req = urllib.request.Request(url, data=data, method="POST",
                                 headers={"Authorization": "Bearer " + tok,
                                          "Content-Type": "text/plain" if raw else "application/json"})
    with urllib.request.urlopen(req, timeout=120) as r:
        return json.loads(r.read())


def read(path):
    tok = TOKEN_FILE.token("analytics-read")
    a, b = WINDOW
    if datetime.fromisoformat(b.replace("Z", "+00:00")) > datetime.now(timezone.utc):
        raise Rig("2026-11-18 has not ended")
    out = {"window": [a, b], "read_at": datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")}
    q = ("query($acct: String!, $a: Time!, $b: Time!) { viewer { accounts(filter: {accountTag: $acct}) { "
         "kvOperationsAdaptiveGroups(limit: 5000, filter: {datetime_geq: $a, datetime_lt: $b}) "
         "{ sum { requests } dimensions { date actionType namespaceId } } } } }")
    d = post("https://api.cloudflare.com/client/v4/graphql", {"query": q, "variables": {"acct": ACCOUNT, "a": a, "b": b}}, tok)
    if d.get("errors"):
        raise Rig("GraphQL returned %d error(s)" % len(d["errors"]))
    groups = d["data"]["viewer"]["accounts"][0]["kvOperationsAdaptiveGroups"]
    if len(groups) >= 5000:
        raise Rig("the KV query hit its 5000-group limit")
    kv = {}
    for g in groups:
        dm = g["dimensions"]
        cell = kv.setdefault(dm["date"], {}).setdefault(str(dm["actionType"]), {"prod": 0, "account": 0})
        cell["account"] += g["sum"]["requests"]
        if dm["namespaceId"] == PROD_NS:
            cell["prod"] += g["sum"]["requests"]
    out["kv"] = dict(sorted(kv.items()))
    w = "timestamp >= toDateTime('%s') AND timestamp < toDateTime('%s')" % (a.replace("T", " ").rstrip("Z"),
                                                                            b.replace("T", " ").rstrip("Z"))
    rows = post("https://api.cloudflare.com/client/v4/accounts/%s/analytics_engine/sql" % ACCOUNT,
                "SELECT toStartOfDay(timestamp) AS d, COUNT(DISTINCT blob5) AS n FROM %s WHERE %s AND index1 NOT IN "
                "('%s') AND blob5 != '' GROUP BY d FORMAT JSON" % (DATASET, w, "','".join(NONROUTE)), tok, raw=True)
    if "data" not in rows:
        raise Rig("an Analytics Engine query returned no data field")
    out["B"] = {str(r["d"])[:10]: float(r["n"]) for r in rows["data"]}
    with open(path, "w", encoding="utf-8", newline="\n") as fh:
        json.dump(out, fh, indent=1, sort_keys=True)
        fh.write("\n")
    print("read %s: %d KV groups; days %s" % (out["read_at"], len(groups), sorted(out["kv"])))
    return 0


def stamp(s):
    return datetime.strptime(s, "%Y-%m-%dT%H:%M:%SZ").replace(tzinfo=timezone.utc)


def grade(p1, p2):
    d, d2 = (json.load(open(p, encoding="utf-8")) for p in (p1, p2))
    apart = abs((stamp(d2["read_at"]) - stamp(d["read_at"])).total_seconds())
    print("reads %s and %s, %d s apart" % (d["read_at"], d2["read_at"], apart))
    if apart < MIN_APART_S:
        print("REFUSED: the two reads are under %d s apart; take another" % MIN_APART_S)
        return 4
    kvv = lambda day, act, who: d["kv"].get(day, {}).get(act, {}).get(who, 0)
    escape = []
    strip = lambda x: {k: v for k, v in x.items() if k != "read_at"}
    if strip(d) != strip(d2):
        escape.append("the two reads differ in %s" % [k for k in sorted(strip(d)) if strip(d).get(k) != strip(d2).get(k)])
    got = {"reads": kvv(ANCHOR_DAY, "read", "prod"), "writes": kvv(ANCHOR_DAY, "write", "prod"),
           "B": int(d["B"].get(ANCHOR_DAY, 0))}
    ok = got == ANCHOR
    print("anchor %s: %s against %s -> %s" % (ANCHOR_DAY, got, ANCHOR, "ok" if ok else "OFF"))
    if not ok:
        escape.append("anchor off")
    B = d["B"].get(DAY, 0.0)
    if B <= 0:
        escape.append("B(%s) = 0" % DAY)
    if escape:
        print("ESCAPE: " + "; ".join(escape))
        print("stop and report, with no reading")
        return 3
    R, W, Racct = kvv(DAY, "read", "prod"), kvv(DAY, "write", "prod"), kvv(DAY, "read", "account")
    rb, wb = 30 * R / B, 30 * W / B
    price = rb * READ_PRICE / 1e6 + wb * WRITE_PRICE / 1e6
    over = max(0.0, Racct * 30 - FREE_READS) * READ_PRICE / 1e6
    print("%s: production reads %s, writes %s, boards %d; account reads %s" % (DAY, f"{R:,}", f"{W:,}", B, f"{Racct:,}"))
    g = lambda ok: "HOLDS" if ok else "FAILS"
    print("PB1: R_b = 30 x %s / %d = %s reads a board a month, in %s -> %s" % (f"{R:,}", B, f"{rb:,.0f}", PB1, g(PB1[0] <= rb <= PB1[1])))
    print("PB2: W_b = 30 x %s / %d = %s writes a board a month, in %s -> %s" % (f"{W:,}", B, f"{wb:,.0f}", PB2, g(PB2[0] <= wb <= PB2[1])))
    print("PB3: a new board, past the free tiers: $%.3f a month (reads $%.3f + writes $%.4f), in $%.2f-$%.2f -> %s" % (
        price, rb * READ_PRICE / 1e6, wb * WRITE_PRICE / 1e6, PB3[0], PB3[1], g(PB3[0] <= price <= PB3[1])))
    print("TRIGGER: account reads on %s x 30 = %s; projected read overage $%.2f a month (comes to the reviewer above $%.0f: %s)" % (
        DAY, f"{Racct * 30:,}", over, TRIGGER, "PASSED -- SEND NOW" if over > TRIGGER else "not passed"))
    return 0


def main(argv):
    try:
        if len(argv) == 2 and argv[0] == "read":
            return read(argv[1])
        if len(argv) == 3 and argv[0] == "grade":
            return grade(argv[1], argv[2])
        print(__doc__)
        return 2
    except Rig as e:
        print("RIG: %s" % e)
        return 2
    except Exception as e:   # the type only
        print("RIG: %s" % type(e).__name__)
        return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
