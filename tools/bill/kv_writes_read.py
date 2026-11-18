#!/usr/bin/env python3
"""What production's KV writes are: docs/proxy-bill.md step 4c (KW1, KW1b, KW2, KR1, KW3, the anchors, the escape).

Read-only, analytics only: no wrangler, no Workers Logs, no KV key list. The analytics-read token comes from the token
file and is never printed. Counts per day only: no key, value, device id or hex is printed or written.

  python -I tools/bill/kv_writes_read.py read <out.json>          the frozen window, every figure the grade needs
  python -I tools/bill/kv_writes_read.py grade <read1> <read2>    the two reads must agree; then the rows, as registered

Exit: read 0 done; grade 0 graded (whatever the grades), 3 ESCAPE (stop and report, no reading); 2 the rig (a token, a
query or a file could not be read). An error prints its exception's TYPE only, never its message.
"""
import importlib.util as _ilu
import json
import os
import statistics
import sys
import urllib.request
from datetime import datetime, timezone

ACCOUNT = "48822e896bb10c45aa6bfe139bcff3d1"          # proxy/wrangler.toml (public)
NS = {"733bf90056104f56be9492e62450a0bf": "prod", "8b6e92ed802a4192b6b6c1399fdf96aa": "staging"}   # wrangler.toml (public)
DATASET = "blipscope_proxy"                           # the METRICS binding's dataset

# Frozen by the registration (docs/proxy-bill.md step 4c). Nothing below is chosen after a read.
WINDOW = ("2026-11-05T00:00:00Z", "2026-11-18T00:00:00Z")
EVENTS = {"2026-11-09": "refresh-data", "2026-11-13": "route refresh (A)", "2026-11-14": "route refresh (A) + photos",
          "2026-11-16": "refresh-data"}
BACKGROUND = ["2026-11-05", "2026-11-06", "2026-11-07", "2026-11-08", "2026-11-10", "2026-11-11", "2026-11-12",
              "2026-11-15", "2026-11-17"]
# The other point families, from every writeDataPoint in proxy/src (amendment 4c-1): five in metrics.ts, plus
# `provision` (provision.ts:84) and `sitefeed` (proxy/src/sitefeed/). Neither of the two carries blob5 or a double4,
# so neither can enter a model sum. An index1 outside routes and these seven is still the escape.
NONROUTE = ("err5xx", "enrich_gap", "ota", "boot", "usage", "provision", "sitefeed")
INFO_FAMILIES = ("provision", "sitefeed")    # their row counts printed separately, as information (the ruling)
ENRICH = ("/api/v1/blipscope/enrich", "/v1/enrich")
SUBMIT = ("/api/v1/blipscope/leaderboard", "/v1/leaderboard")
PAGES = ("/blipscope/leaderboard", "/blipscope/leaderboard.json", "/blipscope/leaderboard/:id",
         "/leaderboard", "/leaderboard.json", "/leaderboard/:id")
ENROLL = ("/blipscope/enroll", "/enroll")
# The anchors: kv-days' whole-day writes (docs/account-budget.json, read kv-days-a-*), step 2b's AE figures.
KV_ANCHORS = [("prod", "2026-11-13", 1899), ("prod", "2026-11-14", 570544), ("staging", "2026-11-13", 628713)]
AE_ANCHOR_WINDOW = ("2026-11-06T00:00:00Z", "2026-11-13T00:00:00Z")
AE_ANCHOR_MISS, AE_ANCHOR_DEVICES = 7239, 22
# Step 2b's device routes, exactly as kv_model_read.py listed them, so the device anchor repeats ITS query.
DEV_ROUTES_2B = (["/api/v1/blipscope/blips", "/v1/blips", "/api/v1/blipscope/config", "/v1/config",
                  "/api/v1/blipscope/airports", "/v1/airports", "/api/v1/blipscope/enrich", "/v1/enrich",
                  "/api/v1/blipscope/photo", "/v1/photo"]
                 + ["/api/v1/blipscope/wx/" + p for p in ("cref", "opuk", "opis", "opib", "opmt", "opwe", "opce", "opse",
                                                         "opba", "opsw", "opsn", "shsr")]
                 + ["/api/v1/weatherscope/alerts", "/api/v1/weatherscope/conditions", "/api/v1/weatherscope/forecast",
                    "/api/v1/blipscope/leaderboard"])
# The rows' bounds, as registered.
KW2_PROD, KW2_STG = (59108, 72244), (59106, 72241)
KW1B, KW3_WB, KW3_DOLLARS = (0.8, 1.3), (1000, 4000), 10.0
SECTION1_LINE = 53.83     # section 1's 600-board KV-writes line, printed beside KW3, never replaced by it

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


def ae(sql, tok):
    out = post("https://api.cloudflare.com/client/v4/accounts/%s/analytics_engine/sql" % ACCOUNT, sql, tok, raw=True)
    if "data" not in out:
        raise Rig("an Analytics Engine query returned no data field")
    return out["data"]


def sqlt(t):
    return "toDateTime('%s')" % t.replace("T", " ").rstrip("Z")


def where(a, b):
    return "timestamp >= %s AND timestamp < %s" % (sqlt(a), sqlt(b))


def inlist(xs):
    return "('" + "','".join(xs) + "')"


def per_day(rows, key="d", val="n"):
    out = {}
    for r in rows:
        day = str(r[key])[:10]
        out[day] = out.get(day, 0.0) + float(r[val])
    return out


def read(path):
    tok = TOKEN_FILE.token("analytics-read")
    a, b = WINDOW
    da = datetime.fromisoformat(a.replace("Z", "+00:00"))
    db = datetime.fromisoformat(b.replace("Z", "+00:00"))
    if db > datetime.now(timezone.utc):
        raise Rig("the window has not ended")
    out = {"window": [a, b], "read_at": datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")}
    # KV operations per UTC day, by action and namespace.
    q = ("query($acct: String!, $a: Time!, $b: Time!) { viewer { accounts(filter: {accountTag: $acct}) { "
         "kvOperationsAdaptiveGroups(limit: 5000, filter: {datetime_geq: $a, datetime_lt: $b}) "
         "{ sum { requests } dimensions { date actionType namespaceId } } } } }")
    d = post("https://api.cloudflare.com/client/v4/graphql", {"query": q, "variables": {"acct": ACCOUNT, "a": a, "b": b}}, tok)
    if d.get("errors"):
        raise Rig("GraphQL returned %d error(s)" % len(d["errors"]))
    groups = d["data"]["viewer"]["accounts"][0]["kvOperationsAdaptiveGroups"]
    if len(groups) >= 5000:
        raise Rig("the KV query hit its 5000-group limit: a truncated read is refused")
    kv, other_ns = {}, set()
    for g in groups:
        dm = g["dimensions"]
        ns = NS.get(dm["namespaceId"])
        if ns is None:
            other_ns.add(dm["namespaceId"])
            ns = "other"
        cell = kv.setdefault(dm["date"], {}).setdefault(str(dm["actionType"]), {})
        cell[ns] = cell.get(ns, 0) + g["sum"]["requests"]
    out["kv"] = dict(sorted(kv.items()))
    out["other_namespaces"] = len(other_ns)          # a count only
    w = where(a, b)
    req = "index1 NOT IN %s" % inlist(NONROUTE)
    # The index inventory: every index1 that is not a route must be one of the five families.
    inv = ae("SELECT index1 AS r, SUM(_sample_interval) AS n FROM %s WHERE %s GROUP BY r FORMAT JSON" % (DATASET, w), tok)
    nonroute = {str(x["r"]): float(x["n"]) for x in inv if not str(x["r"]).startswith("/")}
    out["nonroute_known"] = {k: v for k, v in nonroute.items() if k in NONROUTE}
    out["nonroute_unknown_count"] = sum(1 for k in nonroute if k not in NONROUTE)   # a count; the values are not kept
    out["A"] = per_day(ae("SELECT toStartOfDay(timestamp) AS d, SUM(_sample_interval * double4) AS n FROM %s WHERE %s "
                          "AND index1 IN %s AND blob2 = 'MISS' GROUP BY d FORMAT JSON" % (DATASET, w, inlist(ENRICH)), tok))
    out["F"] = per_day(ae("SELECT toStartOfHour(timestamp) AS d, COUNT(DISTINCT blob5) AS n FROM %s WHERE %s AND %s "
                          "AND blob5 != '' AND blob6 != '' GROUP BY d FORMAT JSON" % (DATASET, w, req), tok))
    out["B"] = per_day(ae("SELECT toStartOfDay(timestamp) AS d, COUNT(DISTINCT blob5) AS n FROM %s WHERE %s AND %s "
                          "AND blob5 != '' GROUP BY d FORMAT JSON" % (DATASET, w, req), tok))
    out["L"] = per_day(ae("SELECT toStartOfDay(timestamp) AS d, SUM(_sample_interval * double4) AS n FROM %s WHERE %s "
                          "AND index1 IN %s AND double1 < 300 GROUP BY d FORMAT JSON" % (DATASET, w, inlist(SUBMIT)), tok))
    out["P"] = per_day(ae("SELECT toStartOfDay(timestamp) AS d, SUM(_sample_interval * double4) AS n FROM %s WHERE %s "
                          "AND index1 IN %s GROUP BY d FORMAT JSON" % (DATASET, w, inlist(PAGES)), tok))
    out["E"] = per_day(ae("SELECT toStartOfDay(timestamp) AS d, SUM(_sample_interval * double4) AS n FROM %s WHERE %s "
                          "AND index1 IN %s GROUP BY d FORMAT JSON" % (DATASET, w, inlist(ENROLL)), tok))
    out["rows"] = per_day(ae("SELECT toStartOfDay(timestamp) AS d, SUM(_sample_interval * double4) AS n FROM %s WHERE %s "
                             "AND %s GROUP BY d FORMAT JSON" % (DATASET, w, req), tok))
    # The AE anchor: step 2b's window and step 2b's own queries.
    w2 = where(*AE_ANCHOR_WINDOW)
    m = ae("SELECT SUM(_sample_interval * double4) AS n FROM %s WHERE %s AND index1 IN %s AND blob2 = 'MISS' FORMAT JSON"
           % (DATASET, w2, inlist(ENRICH)), tok)
    dv = ae("SELECT COUNT(DISTINCT blob5) AS n FROM %s WHERE %s AND blob5 != '' AND index1 IN %s FORMAT JSON"
            % (DATASET, w2, inlist(DEV_ROUTES_2B)), tok)
    out["anchor_ae"] = {"miss": float(m[0]["n"]) if m else 0.0, "devices": float(dv[0]["n"]) if dv else 0.0}
    with open(path, "w", encoding="utf-8", newline="\n") as fh:
        json.dump(out, fh, indent=1, sort_keys=True)
        fh.write("\n")
    days = sorted(out["kv"])
    print("read %s: %d KV groups, %d day(s) of %d; other namespaces %d; non-route index1: known %s, unknown %d" % (
        out["read_at"], len(groups), len(days), (db - da).days, out["other_namespaces"],
        {k: int(v) for k, v in sorted(out["nonroute_known"].items())}, out["nonroute_unknown_count"]))
    print("information (amendment 4c-1): rows of %s" % ", ".join(
        "%s %d" % (k, int(out["nonroute_known"].get(k, 0))) for k in INFO_FAMILIES))
    return 0


def kvn(d, day, action, ns):
    return d["kv"].get(day, {}).get(action, {}).get(ns, 0)


def comparable(d):
    return {k: v for k, v in d.items() if k != "read_at"}


def grade(p1, p2):
    with open(p1, encoding="utf-8") as fh:
        d = json.load(fh)
    with open(p2, encoding="utf-8") as fh:
        d2 = json.load(fh)
    print("reads %s and %s" % (d["read_at"], d2["read_at"]))
    escape = []
    if comparable(d) != comparable(d2):
        diff = [k for k in sorted(set(comparable(d)) | set(comparable(d2))) if comparable(d).get(k) != comparable(d2).get(k)]
        escape.append("the two reads differ in %s" % diff)
    # The anchors.
    for ns, day, want in KV_ANCHORS:
        got = kvn(d, day, "write", ns)
        ok = abs(got - want) <= 0.005 * want
        print("anchor KV %s writes %s: %s against %s -> %s" % (ns, day, f"{got:,}", f"{want:,}", "ok" if ok else "OFF"))
        if not ok:
            escape.append("KV anchor %s %s" % (ns, day))
    am, ad = d["anchor_ae"]["miss"], d["anchor_ae"]["devices"]
    ok_m, ok_d = abs(am - AE_ANCHOR_MISS) <= 0.01 * AE_ANCHOR_MISS, ad == AE_ANCHOR_DEVICES
    print("anchor AE /enrich MISS [11-06, 11-13): %s against %s -> %s; devices %d against %d -> %s" % (
        f"{am:,.0f}", f"{AE_ANCHOR_MISS:,}", "ok" if ok_m else "OFF", ad, AE_ANCHOR_DEVICES, "ok" if ok_d else "OFF"))
    if not ok_m:
        escape.append("AE anchor MISS")
    if not ok_d:
        escape.append("AE anchor devices")
    print("information (amendment 4c-1): rows of %s" % ", ".join(
        "%s %d" % (k, int(d["nonroute_known"].get(k, 0))) for k in INFO_FAMILIES))
    if d["nonroute_unknown_count"]:
        escape.append("%d non-route index1 value(s) outside the seven families" % d["nonroute_unknown_count"])
    g = lambda k, day: float(d[k].get(day, 0.0))
    for day in BACKGROUND:
        if kvn(d, day, "write", "prod") == 0 or g("rows", day) == 0:
            escape.append("background day %s has no writes or no request rows" % day)
    # EVERY escape is decided above this line: on an escape, no per-day figure is printed (no reading).
    if escape:
        print("ESCAPE: " + "; ".join(escape))
        print("the instrument is unreliable at this scale: stop and report, with no reading")
        return 3
    # The table.
    print("%-10s %-26s %9s %9s %7s %10s %7s %6s %5s %5s %4s %4s %8s %8s %8s %s" % (
        "day", "kind", "W prod", "W stg", "W oth", "R prod", "A", "F", "L", "P", "E", "B", "rows", "Mlow", "Mhigh", "band"))
    inband, implied = 0, []
    for day in sorted(set(BACKGROUND) | set(EVENTS)):
        W = kvn(d, day, "write", "prod")
        A, F, L, P, E = (g(k, day) for k in "AFLPE")
        mlo, mhi = A + 0.9 * F + 2 * L, A + 2.0 * F + 3 * L + P + 4 * E
        bg = day in BACKGROUND
        band = ""
        if bg:
            hit = 0.8 * mlo <= W <= 1.2 * mhi
            inband += hit
            band = "in" if hit else ("HIGH" if W > 1.2 * mhi else "LOW")
            if F > 0:
                implied.append((W - A - 2 * L - 2 * E) / F)
        print("%-10s %-26s %9s %9s %7s %10s %7.0f %6.0f %5.0f %5.0f %4.0f %4.0f %8.0f %8.0f %8.0f %s" % (
            day, "background" if bg else EVENTS[day], f"{W:,}", f"{kvn(d, day, 'write', 'staging'):,}",
            f"{kvn(d, day, 'write', 'other'):,}", f"{kvn(d, day, 'read', 'prod'):,}", A, F, L, P, E, g("B", day),
            g("rows", day), mlo, mhi, band))
    acts = sorted({act for v in d["kv"].values() for act in v})
    print("KV actions seen: %s; deletes and lists, production, per day: %s" % (acts, {
        day: (kvn(d, day, "delete", "prod"), kvn(d, day, "list", "prod")) for day in sorted(d["kv"])}))
    med = lambda xs: statistics.median(xs)
    bgW = [kvn(d, x, "write", "prod") for x in BACKGROUND]
    bgS = [kvn(d, x, "write", "staging") for x in BACKGROUND]
    bgO = [kvn(d, x, "write", "other") for x in BACKGROUND]
    bgR = [kvn(d, x, "read", "prod") for x in BACKGROUND]
    bgB = [g("B", x) for x in BACKGROUND]
    mW, mS, mO = med(bgW), med(bgS), med(bgO)
    print("\nKW1: %d of %d background days in band -> %s (needs 8)" % (inband, len(BACKGROUND), "HOLDS" if inband >= 8 else "FAILS"))
    f = med(implied) if implied else float("nan")
    print("KW1b: implied fw: writes per device-hour, median %.3f (per day %s) -> %s [%.1f, %.1f]" % (
        f, ["%.2f" % x for x in implied], "HOLDS" if KW1B[0] <= f <= KW1B[1] else "FAILS", *KW1B))
    j16p, j16s = kvn(d, "2026-11-16", "write", "prod") - mW, kvn(d, "2026-11-16", "write", "staging") - mS
    ok2 = KW2_PROD[0] <= j16p <= KW2_PROD[1] and KW2_STG[0] <= j16s <= KW2_STG[1]
    print("KW2: 11-16 less the background median: prod %s in %s, staging %s in %s -> %s" % (
        f"{j16p:,.0f}", KW2_PROD, f"{j16s:,.0f}", KW2_STG, "HOLDS" if ok2 else "FAILS"))
    lo, hi = 0.9 * min(bgR), 1.1 * max(bgR)
    mon = {x: kvn(d, x, "read", "prod") for x in ("2026-11-09", "2026-11-16")}
    ok3 = all(lo <= v <= hi for v in mon.values())
    print("KR1: production reads on the Mondays %s against [%s, %s] -> %s" % (
        {k: f"{v:,}" for k, v in mon.items()}, f"{lo:,.0f}", f"{hi:,.0f}", "HOLDS" if ok3 else "FAILS"))
    jp = statistics.mean(kvn(d, x, "write", "prod") - mW for x in ("2026-11-09", "2026-11-16"))
    js = statistics.mean(kvn(d, x, "write", "staging") - mS for x in ("2026-11-09", "2026-11-16"))
    mB = med(bgB)
    wb = 30 * mW / mB if mB else float("nan")
    line = 600 * wb + 4 * (jp + js) + 30 * (mS + mO)
    dollars = max(0.0, line - 1e6) * 5 / 1e6
    ok4 = KW3_WB[0] <= wb <= KW3_WB[1] and dollars < KW3_DOLLARS
    print("KW3: background median W %s, B %s -> w_b %s a board a month in %s; J_prod %s, J_stg %s; staging background "
          "median %s, other %s" % (f"{mW:,.0f}", f"{mB:.0f}", f"{wb:,.0f}", KW3_WB, f"{jp:,.0f}", f"{js:,.0f}",
                                   f"{mS:,.0f}", f"{mO:,.0f}"))
    print("     December at 600 boards: %s writes -> $%.2f a month (under $%.0f: %s); section 1's line, beside it: $%.2f" % (
        f"{line:,.0f}", dollars, KW3_DOLLARS, "HOLDS" if ok4 else "FAILS", SECTION1_LINE))
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
    except Exception as e:   # the type only: a message can carry a URL or a response body
        print("RIG: %s" % type(e).__name__)
        return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
