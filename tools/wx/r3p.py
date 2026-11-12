"""R3' (docs/wx-opuk-split.md §3, + R3'-pre): the per-Worker Logs reads. Read-only; tokens from the token file, never
printed. Services: blipscope-wx-ingest (cref) and blipscope-wx-ingest-opuk (opuk).

  python -I r3p.py lines <service> <from ISO> <to ISO> [--minutes <product>]
      product lines by product (R3'-pre, R3'-9); <product>'s :00/:30 per minute and minutes with an error line (R3'-2's
      log half), cref by default; opuk's statuses, errors and `pruned` by status, and the first storing check's `pruned`
      (R3'-5a, R3'-7). W4 (docs/weatherscope-stage4.md S4.12.3): `--minutes shsr` for blipscope-wx-ingest-shsr. With
      cref, the output is exactly what it was, keys and all; another product's keys are named `<product>_minutes_*`.
  python -I r3p.py cpu <service> <product> <from ISO> <to ISO>
      R3'-6 from Workers Logs invocation records: outcomes; single-build invocations (exactly one `stored` frame, no
      `backfilled`) median/max; the rest (no frame built) median; backfill/multi-build invocations printed beside;
      p99 of all; and R3-6's GraphQL by status with its sampleInterval (printed, never graded alone)
  python -I r3p.py first <service> <deploy ISO>
      R3'-10: the first invocation's scheduledTime after the deploy, and the seconds between
"""
import json
import os
import statistics
import sys
import urllib.request
from datetime import datetime, timezone

ACCOUNT = "48822e896bb10c45aa6bfe139bcff3d1"


import importlib.util as _ilu   # the token-file helper, by path: python -I drops this folder from sys.path
_tfs = _ilu.spec_from_file_location("token_file", os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "token_file.py"))
TOKEN_FILE = _ilu.module_from_spec(_tfs); _tfs.loader.exec_module(TOKEN_FILE)   # docs/token-file-python.md
def tf():
    return TOKEN_FILE.tokens("analytics-read", "observability-read")


def post(url, body, tok):
    req = urllib.request.Request(url, data=json.dumps(body).encode(), method="POST",
                                 headers={"Authorization": "Bearer " + tok, "Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=120) as r:
        return json.loads(r.read())


ms = lambda s: int(datetime.fromisoformat(s.replace("Z", "+00:00")).timestamp() * 1000)
hms = lambda t: datetime.fromtimestamp(t, timezone.utc).strftime("%H:%M:%SZ")
T = tf()


def abr1(abr):
    """A sampled Logs read is refused (R3' found one at ABR 10 that missed checks; docs/wx-opera-europe.md section 11):
    only ABR 1 on every page is a full read. R3P_FORCE_ABR is the sabotage seam: set, it replaces the levels the API
    reported, and the read must refuse (exit 2)."""
    if os.environ.get("R3P_FORCE_ABR"):
        abr = [int(os.environ["R3P_FORCE_ABR"])]
    if abr != [1]:
        print(json.dumps({"refused": f"sampled Logs read (ABR {abr}); re-read in shorter pieces"}))
        sys.exit(2)
    return abr


def events(service, a_ms, b_ms):
    # R3P_CHUNK_S: the width of each Logs query (default 1 h). A long read is cut into narrower pieces, and abr1()
    # refuses the read if ANY piece came back sampled; the one ruled re-read halves the width (2026-11-06 ~18:22Z).
    step = int(os.environ.get("R3P_CHUNK_S", "3600")) * 1000
    evs, abr, t0 = [], set(), a_ms
    while t0 < b_ms:
        t1 = min(t0 + step, b_ms)
        off = None
        while True:
            body = {"queryId": "r3p", "timeframe": {"from": t0, "to": t1}, "view": "events", "limit": 2000,
                    "parameters": {"filters": [{"key": "$metadata.service", "operation": "eq", "type": "string", "value": service}]}}
            if off:
                body["offset"] = off
            r = post(f"https://api.cloudflare.com/client/v4/accounts/{ACCOUNT}/workers/observability/telemetry/query", body,
                     T["observability-read"])["result"]
            abr.add(r.get("statistics", {}).get("abr_level"))
            page = r["events"]["events"]
            evs += page
            if len(page) < 2000:
                break
            off = page[-1]["$metadata"]["id"]
        t0 = t1
    return evs, sorted(x for x in abr if x is not None)


mode = sys.argv[1]
if mode == "lines":
    service, a, b = sys.argv[2], sys.argv[3], sys.argv[4]
    mp = "cref"
    if len(sys.argv) > 5:
        if len(sys.argv) != 7 or sys.argv[5] != "--minutes":
            sys.exit("usage: r3p.py lines <service> <from ISO> <to ISO> [--minutes <product>]")
        mp = sys.argv[6]
    evs, abr = events(service, ms(a), ms(b))
    abr = abr1(abr)
    by_product, minutes, statuses, pruned_on, errors, storing = {}, {}, {}, {}, [], []
    prunekeys_on, map_on, map_put_errors = {}, {}, 0
    for e in evs:
        s = e.get("source") or {}
        p = s.get("product")
        if not p:
            continue
        by_product[p] = by_product.get(p, 0) + 1
        if s.get("check") in (":00", ":30") and isinstance(s.get("ms"), int):
            minutes.setdefault((p, int((e["timestamp"] - s["ms"]) // 60000)), set()).add((s["check"], s.get("evt")))
        st = s.get("status", "error")
        statuses[f"{p}:{st}"] = statuses.get(f"{p}:{st}", 0) + 1
        if "pruned" in s:
            pruned_on[f"{p}:{st}"] = pruned_on.get(f"{p}:{st}", 0) + 1
        if "pruneKeys" in s:          # stage 1a: an OPERA prune by key reports the keys it sent
            prunekeys_on[f"{p}:{st}:{s['pruneKeys']}"] = prunekeys_on.get(f"{p}:{st}:{s['pruneKeys']}", 0) + 1
        if "map" in s or "mapRefused" in s:   # stage 1c: where a build's resample map came from
            mk = f"{p}:{st}:{s.get('map')}" + (":refused" if s.get("mapRefused") else "")
            map_on[mk] = map_on.get(mk, 0) + 1
        if "mapPutErrors" in s:               # #579: a map put that threw; the frame was kept
            map_put_errors += s["mapPutErrors"]
        if st in ("stored", "backfilled"):
            storing.append((e["timestamp"], p, st, s.get("pruned"), s.get("pruneKeys")))
        if s.get("evt") == "wx_ingest_error":
            errors.append({"product": p, "at": hms(e["timestamp"] / 1000), "err": str(s.get("err"))[:80], "attempts": s.get("attempts")})
    span = range(ms(a) // 60000, ms(b) // 60000)
    cref_both = sum(1 for m in span if {c for c, _ in minutes.get((mp, m), set())} >= {":00", ":30"})
    cref_one = sum(1 for m in span if len({c for c, _ in minutes.get((mp, m), set())}) == 1)
    cref_none = sum(1 for m in span if not minutes.get((mp, m)))
    cref_err = sum(1 for m in span if any(ev == "wx_ingest_error" for _, ev in minutes.get((mp, m), set())))
    storing.sort()
    row = lambda t, p, st, pr, pk: {"at": hms(t / 1000), "product": p, "status": st, "pruned": pr, "pruneKeys": pk}
    first_store = next((row(*s) for s in storing), None)
    print(json.dumps({"service": service, "window": [a, b], "abr_levels": abr, "events": len(evs), "lines_by_product": by_product,
                      "minutes": len(span), f"{mp}_minutes_both": cref_both, f"{mp}_minutes_missing_one": cref_one,
                      f"{mp}_minutes_missing_both": cref_none, f"{mp}_minutes_with_error": cref_err,
                      "statuses": statuses, "pruned_on": pruned_on, "pruneKeys_on": prunekeys_on, "map_on": map_on, "mapPutErrors": map_put_errors,
                      "first_storing_check": first_store, "storing_checks": [row(*s) for s in storing],
                      "errors": errors}, indent=1))

elif mode == "cpu":
    service, product, a, b = sys.argv[2], sys.argv[3], sys.argv[4], sys.argv[5]
    evs, abr = events(service, ms(a), ms(b) + 120_000)
    abr = abr1(abr)
    inv = {}
    for e in evs:
        w = e.get("$workers") or {}
        st = (w.get("event") or {}).get("scheduledTime")
        if st is None or not (ms(a) / 1000 <= st < ms(b) / 1000):
            continue
        d = inv.setdefault(st, {"cpu": None, "outcome": None, "builds": [], "maps": []})
        if "outcome" in w:
            d["cpu"], d["outcome"] = w.get("cpuTimeMs"), w.get("outcome")
        s = e.get("source") or {}
        if s.get("product") == product and s.get("status") in ("stored", "backfilled"):
            d["builds"].append(s["status"])
            d["maps"].append(s.get("map"))      # stage 1c: cache / r2 / built (absent before 1c)
    recs = [d for d in inv.values() if d["outcome"] is not None]
    single = [d["cpu"] for d in recs if d["builds"] == ["stored"]]
    by_map = {}
    for d in recs:
        if d["builds"] == ["stored"]:
            by_map.setdefault(str(d["maps"][0]), []).append(d["cpu"])
    rest = [d["cpu"] for d in recs if not d["builds"]]
    beside = [{"cpu": d["cpu"], "builds": d["builds"]} for d in recs if d["builds"] and d["builds"] != ["stored"]]
    allc = sorted(d["cpu"] for d in recs if isinstance(d["cpu"], (int, float)))
    outcomes = {}
    for d in recs:
        outcomes[d["outcome"]] = outcomes.get(d["outcome"], 0) + 1
    q = """query($acct: String!, $a: Time!, $b: Time!, $s: String!) { viewer { accounts(filter: {accountTag: $acct}) {
      workersInvocationsAdaptive(limit: 100, filter: {datetime_geq: $a, datetime_lt: $b, scriptName: $s}) {
        sum { requests errors } avg { sampleInterval } dimensions { status } } } } }"""
    g = post("https://api.cloudflare.com/client/v4/graphql", {"query": q, "variables": {"acct": ACCOUNT, "a": a, "b": b, "s": service}},
             T["analytics-read"])
    gq = [{"status": x["dimensions"]["status"], "requests": x["sum"]["requests"], "errors": x["sum"]["errors"],
           "avg_sampleInterval": x["avg"]["sampleInterval"]} for x in g["data"]["viewer"]["accounts"][0]["workersInvocationsAdaptive"]] if not g.get("errors") else g["errors"]
    med = lambda v: statistics.median(v) if v else None
    print(json.dumps({"service": service, "product": product, "window": [a, b], "abr_levels": abr, "records": len(recs),
                      "invocations_seen": len(inv), "outcomes": outcomes, "exceededCpu": outcomes.get("exceededCpu", 0),
                      "single_build": {"n": len(single), "median": med(single), "max": max(single) if single else None},
                      "single_build_by_map": {k: {"n": len(v), "median": med(v), "max": max(v)} for k, v in sorted(by_map.items())},
                      "rest": {"n": len(rest), "median": med(rest), "max": max(rest) if rest else None},
                      "beside_backfill_or_multi": beside,
                      "p99_all": allc[min(len(allc) - 1, int(0.99 * len(allc)))] if allc else None,
                      "graphql_by_status": gq}, indent=1))

elif mode == "first":
    service, dep = sys.argv[2], sys.argv[3]
    d = ms(dep) / 1000
    evs, abr = events(service, ms(dep) - 60_000, int(datetime.now(timezone.utc).timestamp() * 1000))
    abr = abr1(abr)
    sts = sorted({(e.get("$workers") or {}).get("event", {}).get("scheduledTime") for e in evs} - {None})
    after = [s for s in sts if s >= d]
    print(json.dumps({"service": service, "deploy": dep, "first": hms(after[0]) if after else None,
                      "seconds_after": round(after[0] - d, 1) if after else None, "invocations_since": len(after)}))
