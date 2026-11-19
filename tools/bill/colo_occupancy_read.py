#!/usr/bin/env python3
"""/wx's F1: how many boards share a colo, and what a per-colo hold of `latest` would hit (docs/proxy-bill.md step 5,
with amendments F1-R and F1-R-1).

Read-only. One closed hour of the proxy's Workers Logs `evt:"req"` lines (the Workers Observability API): each line's
`colo` and `devl` (the device LABEL). Held IN MEMORY ONLY; only a histogram leaves the process: how many colos hold k
boards, for each k. Never a colo code, a label, an id or a line.

A `dev` line (the device id) is UNREACHABLE (F1-R), not refused after the fact:
  - every query is scoped on the server: `$metadata.service eq blipscope-proxy` AND `evt eq req`, filterCombination "and";
  - a count-only PRE-FLIGHT (scope AND `dev exists`) refuses the hour BEFORE any events request if it is nonzero;
  - the events query adds `dev DOES_NOT_EXIST`, so the server excludes such a line even if the count missed it;
  - the backstop (a fetched req line holding `dev` refuses) stays, and every fetched event is counted first.
A query is a VALUE only at ABR 1 (F1-R-1, map-server's rule (i)): HTTP 200, a statistics block, `abr_level` PRESENT and
== 1, and for a count every aggregates[].sampleInterval 0 or 1. Anything else is BLIND, re-asked up to 2 more times
REASK_S apart (chosen on resolution only, never on the count); still not a value -> exit 6, and F1 does not run on it.

  python -X utf8 -I tools/bill/colo_occupancy_read.py ctl                       F1-CTL: the model's own control, no data
  python -X utf8 -I tools/bill/colo_occupancy_read.py lc                        LC-1..LC-4 + LC-SUM, count only, on LC_HOUR
  python -X utf8 -I tools/bill/colo_occupancy_read.py scan [<last hour ISO>]    when `devl` first appears; is the last closed hour clean
  python -X utf8 -I tools/bill/colo_occupancy_read.py read <hour start ISO> <out.json>
  python -X utf8 -I tools/bill/colo_occupancy_read.py grade <read1> <read2>      the reads agree, then F1-K/H55/H115

The model (frozen in the registration): every active board polls /wx every P = 60 s at a random phase, each poll
jittered +/-2 s; a per-colo hold of `latest` lasts T s from the miss that filled it; 200 seeded runs of one hour per
colo size. T = 55 s and 115 s, off the poll boundary on purpose.
Exit: 0 done/graded; 3 ESCAPE (or: no reading possible now); 4 REFUSED (reads under 60 s apart); 5 REFUSED (a `dev`
line: pre-flight or backstop); 6 BLIND (a query never reached ABR 1); 2 the rig.
"""
import importlib.util as _ilu
import json
import os
import random
import sys
import time
import urllib.error
import urllib.request
from datetime import datetime, timedelta, timezone

ACCOUNT = "48822e896bb10c45aa6bfe139bcff3d1"   # proxy/wrangler.toml (public)
SERVICE = "blipscope-proxy"
URL = f"https://api.cloudflare.com/client/v4/accounts/{ACCOUNT}/workers/observability/telemetry/query"
P, JITTER, HOUR, RUNS = 60.0, 2.0, 3600.0, 200
TS = (55.0, 115.0)
MIN_APART_S = 60
ATTEMPTS = 3           # F1-R-1: a query that is not a value is re-asked up to 2 more times ...
REASK_S = 30           # ... this far apart. Only the plant harness sets it to 0.
CALL_GAP_S = 60        # F1-R: scan and LC calls at least this far apart. Only the plant harness sets it to 0.
SCAN_MAX_H = 6
LC_HOUR = "2026-11-19T12:00:00Z"   # F1-R: closed, before the logs deploy, production writing `dev`

LEAF_SERVICE = {"key": "$metadata.service", "operation": "eq", "type": "string", "value": SERVICE}
LEAF_REQ = {"key": "evt", "operation": "eq", "type": "string", "value": "req"}
DEV_EXISTS = {"key": "dev", "operation": "exists", "type": "string"}
DEV_ABSENT = {"key": "dev", "operation": "DOES_NOT_EXIST", "type": "string"}
DEVL_EXISTS = {"key": "devl", "operation": "exists", "type": "string"}
IMPOSSIBLE = {"key": "evt", "operation": "eq", "type": "string", "value": "__f1r_impossible__"}

_tfs = _ilu.spec_from_file_location("token_file", os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "token_file.py"))
TOKEN_FILE = _ilu.module_from_spec(_tfs); _tfs.loader.exec_module(TOKEN_FILE)   # docs/token-file-python.md


class Rig(Exception):
    pass


class Blind(Exception):
    pass


def hit_rate(k, T, runs=RUNS, seed=1):
    """Polls that find a live entry, over `runs` seeded hours, for k boards in one colo."""
    hits = total = 0
    for r in range(runs):
        rng = random.Random(seed * 100003 + k * 1009 + int(T) * 7 + r)
        polls = []
        for _ in range(k):
            t = rng.uniform(0, P)
            while t < HOUR + P:          # one warm-up period, then one hour counted
                polls.append(t + rng.uniform(-JITTER, JITTER))
                t += P
        polls.sort()
        expiry = -1.0
        for t in polls:
            live = t < expiry
            if not live:
                expiry = t + T
            if P <= t < HOUR + P:
                total += 1
                hits += live
    return hits / total if total else 0.0


def ctl():
    a, b, c = hit_rate(1, 55.0), hit_rate(1, 115.0), hit_rate(10, 55.0)
    ok = a == 0.0 and abs(b - 0.5) <= 0.02 and c > 0.75
    print("F1-CTL: k=1 T=55 %.3f (want 0); k=1 T=115 %.3f (want 0.50 +/-0.02); k=10 T=55 %.3f (want > 0.75) -> %s" % (
        a, b, c, "HOLDS" if ok else "FAILS: the simulation is wrong; stop before any data"))
    return 0 if ok else 3


def post(url, body, tok):
    req = urllib.request.Request(url, data=json.dumps(body).encode(), method="POST",
                                 headers={"Authorization": "Bearer " + tok, "Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=120) as r:
        return json.loads(r.read())


def params(*extra, scope=True):
    """Every filter set this reader sends (F1-R): the read and the live controls share this one form."""
    leaves = ([LEAF_SERVICE, LEAF_REQ] if scope else [LEAF_SERVICE]) + list(extra)
    return {"filters": [dict(x) for x in leaves], "filterCombination": "and"}


def _num(x):
    return isinstance(x, (int, float)) and not isinstance(x, bool)


def is_value(status, res, count):
    """F1-R-1 (map-server's rule (i)): a query is a VALUE only at ABR 1, the level PRESENT."""
    if status != 200 or not isinstance(res, dict):
        return False
    st = res.get("statistics")
    if not (isinstance(st, dict) and "abr_level" in st and _num(st["abr_level"]) and st["abr_level"] == 1):
        return False
    if count:
        calcs = res.get("calculations")
        if not (isinstance(calcs, list) and len(calcs) == 1 and isinstance(calcs[0], dict)):
            return False
        aggs = calcs[0].get("aggregates")
        if not isinstance(aggs, list):
            return False
        for a in aggs:
            if not (isinstance(a, dict) and a.get("sampleInterval") in (0, 1) and _num(a.get("value"))):
                return False
    return True


def count_of(res):
    return sum(a["value"] for a in res["calculations"][0]["aggregates"])


def ask(body, tok, count, log, label):
    """One query, re-asked on RESOLUTION only (F1-R-1); every attempt recorded. The result when a value, else None."""
    for i in range(ATTEMPTS):
        if i:
            time.sleep(REASK_S)
        status, res, err = 0, None, None
        try:
            res = post(URL, body, tok).get("result")
            status = 200
        except urllib.error.HTTPError as e:
            status, err = e.code, "HTTPError"
        except Exception as e:        # the type only
            err = type(e).__name__
        st = res.get("statistics") if isinstance(res, dict) else None
        rec = {"query": label, "attempt": i + 1, "status": status, "error": err,
               "abr_present": isinstance(st, dict) and "abr_level" in st,
               "statistics": {k: st.get(k) for k in ("abr_level", "elapsed", "rows_read", "bytes_read")} if isinstance(st, dict) else None}
        if count and isinstance(res, dict) and isinstance(res.get("calculations"), list):
            rec["sampleIntervals"] = sorted({str(a.get("sampleInterval")) for c in res["calculations"] if isinstance(c, dict)
                                             for a in (c.get("aggregates") or []) if isinstance(a, dict)})
        ok = is_value(status, res, count)
        rec["value"] = ok
        if ok and count:
            rec["count"] = count_of(res)    # the count only on an attempt that is a value
        log.append(rec)
        if ok:
            return res
    return None


def hour_bounds(h1):
    return {"from": int(h1.timestamp() * 1000), "to": int((h1 + timedelta(hours=1)).timestamp() * 1000)}


def count_query(h1, prm, tok, log, label):
    body = {"queryId": "proxy-bill-colo-occupancy", "timeframe": hour_bounds(h1), "view": "calculations",
            "parameters": dict(prm, calculations=[{"operator": "count"}])}
    res = ask(body, tok, True, log, label)
    if res is None:
        raise Blind(label)
    return count_of(res)


def event_obj(e):
    """An event's object, from `source` or the raw message; None if it has none."""
    md = e.get("$metadata") or {}
    src = e.get("source")
    obj = src if isinstance(src, dict) else None
    if obj is None:
        msg = md.get("message")
        if isinstance(msg, str) and msg.lstrip().startswith("{"):
            try:
                obj = json.loads(msg)
            except ValueError:
                obj = None
    return obj if isinstance(obj, dict) else None


def req_fields(e):
    """The req line's object; None if this event is not a req line."""
    obj = event_obj(e)
    return obj if obj is not None and obj.get("evt") == "req" else None


def parse_hour(s):
    h = datetime.fromisoformat(s.replace("Z", "+00:00")).astimezone(timezone.utc)
    if h.minute or h.second or h.microsecond:
        raise Rig("the hour must start on a whole hour")
    return h


def last_closed_hour():
    return datetime.now(timezone.utc).replace(minute=0, second=0, microsecond=0) - timedelta(hours=1)


def blind(label, log):
    n = sum(1 for a in log if a["query"] == label)
    print("BLIND: %s never reached ABR 1 (%d attempt(s)); F1 does not run on it." % (label, n))
    return 6


def read(hour, path):
    h1 = parse_hour(hour)
    if h1 + timedelta(hours=1) > datetime.now(timezone.utc):
        raise Rig("the hour has not closed")
    tok = TOKEN_FILE.token("observability-read")
    log = []
    try:
        pre = count_query(h1, params(DEV_EXISTS), tok, log, "preflight")
    except Blind:
        print("fetched 0 event(s), 0 with dev; no events request was made")
        return blind("preflight", log)
    print("preflight dev count %d" % pre)
    if pre:
        print("fetched 0 event(s), 0 with dev; no events request was made")
        print("REFUSED (pre-flight): %d req line(s) in the hour carry the device id. Nothing fetched, nothing kept." % pre)
        return 5
    colos, lines, no_colo, no_label, abr, offset, pages, fetched, fetched_dev = {}, 0, 0, 0, set(), None, 0, 0, 0
    while True:
        body = {"queryId": "proxy-bill-colo-occupancy", "timeframe": hour_bounds(h1), "view": "events", "limit": 2000,
                "parameters": params(DEV_ABSENT)}
        if offset:
            body["offset"] = offset
        label = "events page %d" % (pages + 1)
        res = ask(body, tok, False, log, label)
        if res is None:
            colos.clear()        # the partial histogram is discarded, never written
            print("fetched %d event(s), %d with dev" % (fetched, fetched_dev))
            return blind(label, log)
        abr.add(res["statistics"]["abr_level"])
        page = res["events"]["events"]
        pages += 1
        fetched += len(page)                                                   # counted before any local test
        fetched_dev += sum(1 for e in page if "dev" in (event_obj(e) or {}))
        for e in page:
            if (e.get("$metadata") or {}).get("service") != SERVICE:
                continue
            o = req_fields(e)
            if o is None:
                continue
            if "dev" in o:
                colos.clear()
                print("fetched %d event(s), %d with dev" % (fetched, fetched_dev))
                print("REFUSED (backstop): a fetched req line carries the device id. Nothing kept.")
                return 5
            lines += 1
            c, d = o.get("colo"), o.get("devl")
            if not isinstance(c, str) or not c:
                no_colo += 1
                continue
            if not isinstance(d, str) or not d:
                no_label += 1      # an unattributed request (a page, a refusal): not a board
                continue
            colos.setdefault(c, set()).add(d)
        if len(page) < 2000:
            break
        offset = page[-1]["$metadata"]["id"]
    hist = {}
    for s in colos.values():
        hist[len(s)] = hist.get(len(s), 0) + 1
    colos.clear()          # nothing but the histogram survives
    out = {"hour": h1.strftime("%Y-%m-%dT%H:00:00Z"), "read_at": datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
           "pages": pages, "abr": sorted(abr), "req_lines": lines, "no_colo": no_colo, "no_label": no_label,
           "preflight_dev": pre, "fetched": fetched, "fetched_dev": fetched_dev,
           "histogram": {str(k): v for k, v in sorted(hist.items())}, "attempts": log}
    with open(path, "w", encoding="utf-8", newline="\n") as fh:
        json.dump(out, fh, indent=1, sort_keys=True)
        fh.write("\n")
    print("fetched %d event(s), %d with dev" % (fetched, fetched_dev))
    print("read %s: %d page(s), ABR %s, req lines %d, no colo %d, no label %d, histogram %s" % (
        out["hour"], pages, out["abr"], lines, no_colo, no_label, out["histogram"]))
    return 0


def scan(last=None):
    """F1-R: when `devl` first appears, measured. Count only, newest hour first, CALL_GAP_S between calls."""
    h = parse_hour(last) if last else last_closed_hour()
    if h + timedelta(hours=1) > datetime.now(timezone.utc):
        raise Rig("the hour has not closed")
    tok = TOKEN_FILE.token("observability-read")
    log, rows, calls = [], [], 0
    stopped = False
    for _ in range(SCAN_MAX_H):
        vals = []
        for name, leaf in (("dev", DEV_EXISTS), ("devl", DEVL_EXISTS)):
            if calls:
                time.sleep(CALL_GAP_S)
            calls += 1
            label = "scan %s %s" % (h.strftime("%Y-%m-%dT%H:00Z"), name)
            try:
                vals.append(count_query(h, params(leaf), tok, log, label))
            except Blind:
                return blind(label, log)
        rows.append((h, vals[0], vals[1]))
        print("  %s dev %d devl %d" % (h.strftime("%Y-%m-%dT%H:00Z"), vals[0], vals[1]))
        if vals[1] == 0:
            stopped = True
            break
        h -= timedelta(hours=1)
    print("scan: %d count call(s)" % calls)
    fwd = list(reversed(rows))                       # oldest first
    seen = [r for r in fwd if r[2] > 0]
    if not seen:
        print("devl not seen in the last closed hour: no clean hour; no reading possible now")
        return 3
    first = seen[0][0].strftime("%Y-%m-%dT%H:00Z")
    print(("devl first appears in the hour starting %s" if stopped else "devl first appears before %s") % first)
    clean_at, rollback = None, False
    for t, dv, dl in fwd:
        if clean_at is None and dv == 0 and dl > 0:
            clean_at = t
        elif clean_at is not None and dv > 0:
            rollback = True
    print("first clean hour %s" % (clean_at.strftime("%Y-%m-%dT%H:00Z") if clean_at else "none"))
    if rollback:
        print("ROLLBACK: an hour with dev after a clean hour; stop and report")
        return 3
    newest = rows[0]
    if newest[1] == 0 and newest[2] > 0:
        print("last closed hour %s: clean" % newest[0].strftime("%Y-%m-%dT%H:00Z"))
        return 0
    print("last closed hour %s: not clean; wait for the next closed hour" % newest[0].strftime("%Y-%m-%dT%H:00Z"))
    return 3


def lc():
    """F1-R's live controls, count only, on LC_HOUR; each graded only as a value (ABR 1)."""
    h1 = parse_hour(LC_HOUR)
    tok = TOKEN_FILE.token("observability-read")
    log, v = [], {}
    rows = (("LC-1", params()), ("LC-2", params(DEV_EXISTS)), ("LC-3", params(DEV_ABSENT)),
            ("LC-4", params(IMPOSSIBLE, scope=False)))
    for i, (name, prm) in enumerate(rows):
        if i:
            time.sleep(CALL_GAP_S)
        try:
            v[name] = count_query(h1, prm, tok, log, name)
        except Blind:
            return blind(name, log)
    g = lambda ok: "HOLDS" if ok else "OFF"
    res = [("LC-1 scope > 0", v["LC-1"] > 0),
           ("LC-2 dev exists > 0 and <= LC-1", 0 < v["LC-2"] <= v["LC-1"]),
           ("LC-3 dev DOES_NOT_EXIST < LC-1", v["LC-3"] < v["LC-1"]),
           ("LC-4 impossible AND service = 0", v["LC-4"] == 0),
           ("LC-SUM LC-2 + LC-3 = LC-1", v["LC-2"] + v["LC-3"] == v["LC-1"])]
    print("LC hour %s, all at ABR 1: LC-1 %d, LC-2 %d, LC-3 %d, LC-4 %d" % (LC_HOUR, v["LC-1"], v["LC-2"], v["LC-3"], v["LC-4"]))
    for name, ok in res:
        print("  %s -> %s" % (name, g(ok)))
    return 0 if all(ok for _, ok in res) else 3


def stamp(s):
    return datetime.strptime(s, "%Y-%m-%dT%H:%M:%SZ").replace(tzinfo=timezone.utc)


def grade(p1, p2):
    d, d2 = (json.load(open(p, encoding="utf-8")) for p in (p1, p2))
    apart = abs((stamp(d2["read_at"]) - stamp(d["read_at"])).total_seconds())
    print("reads %s and %s, %d s apart" % (d["read_at"], d2["read_at"], apart))
    if apart < MIN_APART_S:
        print("REFUSED: the two reads are under %d s apart; take another" % MIN_APART_S)
        return 4
    if ctl() != 0:
        return 3
    strip = lambda x: {k: v for k, v in x.items() if k not in ("read_at", "attempts")}
    esc = []
    if strip(d) != strip(d2):
        esc.append("the two reads differ")
    if d["abr"] != [1] or d2["abr"] != [1]:       # F1-R-1: an absent level is no longer accepted
        esc.append("a sampled hour")
    if d["req_lines"] and (d["no_colo"]) > 0.05 * d["req_lines"]:
        esc.append("over 5% of req lines without a colo")
    hist = {int(k): v for k, v in d["histogram"].items()}
    boards = sum(k * v for k, v in hist.items())
    if not boards:
        esc.append("no board in the hour")
    if esc:
        print("ESCAPE: " + "; ".join(esc) + " -- stop and report, with no reading")
        return 3
    alone = hist.get(1, 0)
    print("boards %d in %d colos; alone %d (%.0f%%); histogram k:colos %s" % (
        boards, sum(hist.values()), alone, 100 * alone / boards, dict(sorted(hist.items()))))
    g = lambda ok: "HOLDS" if ok else "FAILS"
    print("F1-K: at least half alone -> %s" % g(alone >= boards / 2))
    rates = {}
    for T in TS:
        reqs = hits = 0.0
        for k, v in hist.items():
            r = hit_rate(k, T)
            reqs += v * k * 60
            hits += v * k * 60 * r
        rates[T] = hits / reqs
    print("F1-H55: fleet hit rate at T=55 s %.1f%%, at most 25%% -> %s" % (100 * rates[55.0], g(rates[55.0] <= 0.25)))
    print("F1-H115: at T=115 s %.1f%%, in [50%%, 75%%] -> %s" % (100 * rates[115.0], g(0.50 <= rates[115.0] <= 0.75)))
    return 0


def main(argv):
    try:
        if argv == ["ctl"]:
            return ctl()
        if argv == ["lc"]:
            return lc()
        if argv[:1] == ["scan"] and len(argv) <= 2:
            return scan(argv[1] if len(argv) == 2 else None)
        if len(argv) == 3 and argv[0] == "read":
            return read(argv[1], argv[2])
        if len(argv) == 3 and argv[0] == "grade":
            return grade(argv[1], argv[2])
        print(__doc__)
        return 2
    except Rig as e:
        print("RIG: %s" % e)
        return 2
    except urllib.error.HTTPError as e:
        print("RIG: HTTPError %d" % e.code)
        return 2
    except Exception as e:   # the type only
        print("RIG: %s" % type(e).__name__)
        return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
