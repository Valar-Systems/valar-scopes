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
  python -X utf8 -I tools/bill/colo_occupancy_read.py lc <out.json>             LC-1..LC-4 + LC-SUM, count only, on LC_HOUR
  python -X utf8 -I tools/bill/colo_occupancy_read.py scan <out.json> [<last hour ISO>]   when `devl` first appears; clean?
  python -X utf8 -I tools/bill/colo_occupancy_read.py read <hour start ISO> <out.json>
  python -X utf8 -I tools/bill/colo_occupancy_read.py grade <read1> <read2>      the reads agree, then F1-K/H55/H115
  python -X utf8 -I tools/bill/colo_occupancy_read.py classify <hour start ISO> <out.json>   F1 CLASSIFY: a DIAGNOSTIC
      (registration 627dd8c67b72): the hour's no_label lines by family / status / model / authPath / five-minute bucket,
      counts only. It walks the hour by `read`'s own path (walk_hour), and every line it prints is labelled
      "[diagnostic after F1's no-reading; changes no reading]". The family rules' constants are PARSED from
      proxy/src/index.ts and missileer.ts in this tree; a stale one is a RIG error before any query. Exit 3 = a BLIND row.
Every command prints one `attempts <query>:` line per query on every graded exit (0, 3, 5, 6); lc and scan also write
every attempt to <out.json> on those exits, and read writes them into its own result file (F1-R-1's recording clause;
ruling (4)). A rig error (exit 2) prints its exception type only, and nothing else.

The model (frozen in the registration): every active board polls /wx every P = 60 s at a random phase, each poll
jittered +/-2 s; a per-colo hold of `latest` lasts T s from the miss that filled it; 200 seeded runs of one hour per
colo size. T = 55 s and 115 s, off the poll boundary on purpose.
Exit: 0 done/graded; 3 ESCAPE (or: no reading possible now); 4 REFUSED (reads under 60 s apart); 5 REFUSED (a `dev`
line: pre-flight or backstop); 6 BLIND (a query never reached ABR 1); 2 the rig.
"""
import contextlib
import importlib.util as _ilu
import io
import json
import os
import random
import re
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


def print_attempts(log):
    """One line per query, every attempt on it (F1-R-1): status, abr_level, and the count only on a value."""
    order = []
    for a in log:
        if a["query"] not in order:
            order.append(a["query"])
    for q in order:
        parts = []
        for a in log:
            if a["query"] != q:
                continue
            abr = (a["statistics"] or {}).get("abr_level") if a["abr_present"] else "absent"
            s = "%d: status %s abr %s" % (a["attempt"], a["status"], abr)
            if a.get("error"):
                s += " error %s" % a["error"]
            if a["value"]:
                s += (" count %d" % a["count"]) if "count" in a else ""
                s += " (value)"
            else:
                s += " (not a value)"
            parts.append(s)
        print("  attempts %s: %s" % (q, "; ".join(parts)))


def write_record(path, kind, extra, log):
    """lc's and scan's record, on EVERY exit path: counts and statistics only, never an id or a line."""
    rec = dict(extra, kind=kind, at=datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"), attempts=log)
    with open(path, "w", encoding="utf-8", newline="\n") as fh:
        json.dump(rec, fh, indent=1, sort_keys=True)
        fh.write("\n")


def done(rc, log, path=None, kind=None, extra=None):
    print_attempts(log)
    if path:
        write_record(path, kind, extra or {}, log)
    return rc


def walk_hour(h1, tok, log, on_req):
    """`read`'s and `classify`'s ONE path over an hour (F1-R), so a guard here cannot be skipped by either: the count-only
    PRE-FLIGHT, then the `dev DOES_NOT_EXIST` events query, paged, every fetched event counted before any local test, and
    the BACKSTOP on any fetched req line holding `dev`. on_req(o, e) is called once per req line of this service.
    Returns (0, info) after a full walk; otherwise (rc, None) for a refusal (5) or a BLIND (6), having printed exactly the
    lines `read` has always printed for it. On a nonzero rc the caller discards everything on_req gathered."""
    try:
        pre = count_query(h1, params(DEV_EXISTS), tok, log, "preflight")
    except Blind:
        print("fetched 0 event(s), 0 with dev; no events request was made")
        return blind("preflight", log), None
    print("preflight dev count %d" % pre)
    if pre:
        print("fetched 0 event(s), 0 with dev; no events request was made")
        print("REFUSED (pre-flight): %d req line(s) in the hour carry the device id. Nothing fetched, nothing kept." % pre)
        return 5, None
    abr, offset, pages, fetched, fetched_dev = set(), None, 0, 0, 0
    while True:
        body = {"queryId": "proxy-bill-colo-occupancy", "timeframe": hour_bounds(h1), "view": "events", "limit": 2000,
                "parameters": params(DEV_ABSENT)}
        if offset:
            body["offset"] = offset
        label = "events page %d" % (pages + 1)
        res = ask(body, tok, False, log, label)
        if res is None:
            print("fetched %d event(s), %d with dev" % (fetched, fetched_dev))
            return blind(label, log), None
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
                print("fetched %d event(s), %d with dev" % (fetched, fetched_dev))
                print("REFUSED (backstop): a fetched req line carries the device id. Nothing kept.")
                return 5, None
            on_req(o, e)
        if len(page) < 2000:
            break
        offset = page[-1]["$metadata"]["id"]
    return 0, {"pre": pre, "pages": pages, "abr": sorted(abr), "fetched": fetched, "fetched_dev": fetched_dev}


def read(hour, path):
    h1 = parse_hour(hour)
    if h1 + timedelta(hours=1) > datetime.now(timezone.utc):
        raise Rig("the hour has not closed")
    tok = TOKEN_FILE.token("observability-read")
    log = []
    colos, n = {}, {"lines": 0, "no_colo": 0, "no_label": 0}

    def on_req(o, e):
        n["lines"] += 1
        c, d = o.get("colo"), o.get("devl")
        if not isinstance(c, str) or not c:
            n["no_colo"] += 1
            return
        if not isinstance(d, str) or not d:
            n["no_label"] += 1      # an unattributed request (a page, a refusal): not a board
            return
        colos.setdefault(c, set()).add(d)

    rc, w = walk_hour(h1, tok, log, on_req)
    if rc:
        colos.clear()        # a partial histogram is discarded, never written
        return done(rc, log)
    hist = {}
    for s in colos.values():
        hist[len(s)] = hist.get(len(s), 0) + 1
    colos.clear()          # nothing but the histogram survives
    out = {"hour": h1.strftime("%Y-%m-%dT%H:00:00Z"), "read_at": datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
           "pages": w["pages"], "abr": w["abr"], "req_lines": n["lines"], "no_colo": n["no_colo"], "no_label": n["no_label"],
           "preflight_dev": w["pre"], "fetched": w["fetched"], "fetched_dev": w["fetched_dev"],
           "histogram": {str(k): v for k, v in sorted(hist.items())}, "attempts": log}
    with open(path, "w", encoding="utf-8", newline="\n") as fh:
        json.dump(out, fh, indent=1, sort_keys=True)
        fh.write("\n")
    print("fetched %d event(s), %d with dev" % (w["fetched"], w["fetched_dev"]))
    print("read %s: %d page(s), ABR %s, req lines %d, no colo %d, no label %d, histogram %s" % (
        out["hour"], w["pages"], out["abr"], n["lines"], n["no_colo"], n["no_label"], out["histogram"]))
    return done(0, log)


# F1 CLASSIFY (registration 627dd8c67b72): a DIAGNOSTIC after F1's no-reading. Counts only; never an `ep`, a colo, a
# `devl`, a `model` value or a line leaves the process.
DIAG = "[diagnostic after F1's no-reading; changes no reading]"
FAMILIES = ("missileer", "healthz", "enroll", "api", "notfound", "page")
STATUS_CLASSES = ("2xx/3xx", "401", "403", "404", "429", "other4xx", "5xx")
RULE_SHAPES = (("index.ts", 'url.pathname === "/healthz"'), ("index.ts", 'url.pathname === "/enroll"'),
               ("index.ts", "url.pathname === `${PAGE_PREFIX}/enroll`"), ("index.ts", "pathname.startsWith(API_PREFIX)"),
               ("index.ts", "pathname.startsWith(WEATHERSCOPE_API_PREFIX)"),
               ("index.ts", "pathname.startsWith(API_LEGACY_PREFIX)"), ("index.ts", "if (isMissileerPath(url.pathname))"),
               ("missileer.ts", "pathname === PAGE_PREFIX ||"), ("missileer.ts", "pathname.startsWith(`${PAGE_PREFIX}/`) ||"),
               ("missileer.ts", "pathname.startsWith(API_PREFIX)"))


def _const(src, name, env, where):
    """`const NAME = "..."` or a template using only already-parsed names; found exactly once, or a RIG error."""
    m = re.findall(r'^const %s = ("[^"\n]*"|`[^`\n]*`);$' % re.escape(name), src, re.M)
    if len(m) != 1:
        raise Rig("%s: const %s found %d time(s), not exactly once; the family rule cannot be derived" % (where, name, len(m)))
    v = m[0][1:-1]
    if m[0][0] == "`":
        def sub(mm):
            if mm.group(1) not in env:
                raise Rig("%s: ${%s} in %s is not a parsed constant" % (where, mm.group(1), name))
            return env[mm.group(1)]
        v = re.sub(r"\$\{([A-Za-z_]+)\}", sub, v)
    return v


def route_rules():
    """The families' constants, PARSED from proxy/src in this tree (index.ts and missileer.ts), never transcribed; and
    the rule shapes classify depends on, each required to still be in the source. Anything else is a RIG error."""
    base = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "proxy", "src")
    src = {}
    for f in ("index.ts", "missileer.ts"):
        try:
            src[f] = open(os.path.join(base, f), encoding="utf-8").read()
        except OSError:
            raise Rig("proxy/src/%s is not beside this reader; the family rule cannot be derived" % f)
    env = {}
    env["EDITION"] = _const(src["index.ts"], "EDITION", env, "index.ts")
    r = {"api": tuple(_const(src["index.ts"], n, env, "index.ts")
                      for n in ("API_PREFIX", "WEATHERSCOPE_API_PREFIX", "API_LEGACY_PREFIX")),
         "page_prefix": _const(src["index.ts"], "PAGE_PREFIX", env, "index.ts"),
         "m_page": _const(src["missileer.ts"], "PAGE_PREFIX", {}, "missileer.ts"),
         "m_api": _const(src["missileer.ts"], "API_PREFIX", {}, "missileer.ts")}
    for f, shape in RULE_SHAPES:
        if shape not in src[f]:
            raise Rig("%s no longer contains %s; the family rule is stale" % (f, shape))
    return r


def family(ep, status, r):
    """index.ts's own order: Missileer first (:190), then /healthz, the enrolment page (above the auth gate), the API
    (behind it), and anything else is the 404 page or a page."""
    if ep == r["m_page"] or ep.startswith(r["m_page"] + "/") or ep.startswith(r["m_api"]): return "missileer"
    if ep == "/healthz":
        return "healthz"
    if ep in (r["page_prefix"] + "/enroll", "/enroll"):
        return "enroll"
    if ep.startswith(r["api"]):
        return "api"
    return "notfound" if status == 404 else "page"


def status_class(s):
    """A known status class, or None (BLIND): never a default."""
    if isinstance(s, bool) or not isinstance(s, int):
        return None
    if s in (401, 403, 404, 429):
        return str(s)
    if 200 <= s < 400:
        return "2xx/3xx"
    if 400 <= s < 500:
        return "other4xx"
    if 500 <= s < 600:
        return "5xx"
    return None


def classify(hour, path):
    h1 = parse_hour(hour)
    r = route_rules()                    # before any query: a stale rule is a RIG error with nothing asked
    if h1 + timedelta(hours=1) > datetime.now(timezone.utc):
        raise Rig("the hour has not closed")
    tok = TOKEN_FILE.token("observability-read")
    log, h1ms, blind_rows = [], int(h1.timestamp() * 1000), set()
    n = {"lines": 0, "no_colo": 0, "no_label": 0, "authpath": 0}
    t = {f: {"status": {s: 0 for s in STATUS_CLASSES}, "model": 0, "total": 0, "buckets": [0] * 12} for f in FAMILIES}

    def on_req(o, e):
        n["lines"] += 1
        c, d = o.get("colo"), o.get("devl")
        if not isinstance(c, str) or not c:
            n["no_colo"] += 1
            return
        if isinstance(d, str) and d:
            return                        # a labelled line: a board, not classified here
        n["no_label"] += 1
        if "authPath" in o:
            n["authpath"] += 1
        ep, s = o.get("ep"), o.get("status")
        if not isinstance(ep, str):
            blind_rows.add("family")      # cannot be placed: the family and time tables go BLIND, never a guess
            return
        row = t[family(ep, s, r)]
        row["total"] += 1
        sc = status_class(s)
        if sc is None:
            blind_rows.add("status")
        else:
            row["status"][sc] += 1
        m = o.get("model")
        if isinstance(m, str) and m:
            row["model"] += 1
        ts = e.get("timestamp")
        b = None
        if isinstance(ts, (int, float)) and not isinstance(ts, bool):
            b = int((ts - h1ms) // 300000)
        if b is None or not 0 <= b <= 11:
            blind_rows.add("time")        # an event outside its own hour is impossible: BLIND, never clamped
        else:
            row["buckets"][b] += 1

    rc, w = walk_hour(h1, tok, log, on_req)
    if rc:
        t.clear()
        return done(rc, log)
    fam_blind = bool(blind_rows & {"family", "status"})
    time_blind = bool(blind_rows & {"family", "time"})
    print("fetched %d event(s), %d with dev" % (w["fetched"], w["fetched_dev"]))
    print("anchor: req lines %d, no colo %d, no label %d" % (n["lines"], n["no_colo"], n["no_label"]))
    for f in FAMILIES:
        x = t[f]
        print("family %s BLIND" % f if fam_blind else "family %s 2xx/3xx %d 401 %d 403 %d 404 %d 429 %d other4xx %d 5xx %d | model %d | total %d" % (
            (f,) + tuple(x["status"][s] for s in STATUS_CLASSES) + (x["model"], x["total"])))
    print("authPath present %d" % n["authpath"])
    for f in FAMILIES:
        print("buckets %s BLIND" % f if time_blind else "buckets %s %s" % (f, " ".join(str(c) for c in t[f]["buckets"])))
    rc = 3 if blind_rows else 0
    if blind_rows:
        print("BLIND: %s -- no diagnostic" % ", ".join(sorted(blind_rows)))
    out = {"label": DIAG, "kind": "classify", "exit": rc, "hour": h1.strftime("%Y-%m-%dT%H:00:00Z"),
           "read_at": datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"), "pages": w["pages"], "abr": w["abr"],
           "req_lines": n["lines"], "no_colo": n["no_colo"], "no_label": n["no_label"], "authpath_present": n["authpath"],
           "blind": sorted(blind_rows),
           "families": {f: "BLIND" if fam_blind else {"status": t[f]["status"], "model": t[f]["model"], "total": t[f]["total"]}
                        for f in FAMILIES},
           "buckets": {f: "BLIND" if time_blind else t[f]["buckets"] for f in FAMILIES}, "attempts": log}
    t.clear()
    with open(path, "w", encoding="utf-8", newline="\n") as fh:
        json.dump(out, fh, indent=1, sort_keys=True)
        fh.write("\n")
    return done(rc, log)


def labelled(fn):
    """F1 CLASSIFY: every line the diagnostic prints carries DIAG, including RIG errors and the attempts lines."""
    buf = io.StringIO()
    with contextlib.redirect_stdout(buf):
        rc = fn()
    for l in buf.getvalue().splitlines():
        print(DIAG + " " + l)
    return rc


def scan(path, last=None):
    """F1-R: when `devl` first appears, measured. Count only, newest hour first, CALL_GAP_S between calls."""
    h = parse_hour(last) if last else last_closed_hour()
    if h + timedelta(hours=1) > datetime.now(timezone.utc):
        raise Rig("the hour has not closed")
    tok = TOKEN_FILE.token("observability-read")
    log, rows, calls = [], [], 0
    fin = lambda rc: done(rc, log, path, "scan", {"exit": rc, "calls": calls, "rows": [
        [t.strftime("%Y-%m-%dT%H:00Z"), dv, dl] for t, dv, dl in rows]})
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
                return fin(blind(label, log))
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
        return fin(3)
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
        return fin(3)
    newest = rows[0]
    if newest[1] == 0 and newest[2] > 0:
        print("last closed hour %s: clean" % newest[0].strftime("%Y-%m-%dT%H:00Z"))
        return fin(0)
    print("last closed hour %s: not clean; wait for the next closed hour" % newest[0].strftime("%Y-%m-%dT%H:00Z"))
    return fin(3)


def lc(path):
    """F1-R's live controls, count only, on LC_HOUR; each graded only as a value (ABR 1)."""
    h1 = parse_hour(LC_HOUR)
    tok = TOKEN_FILE.token("observability-read")
    log, v = [], {}
    fin = lambda rc, grades=None: done(rc, log, path, "lc", {"exit": rc, "hour": LC_HOUR, "values": v, "grades": grades})
    rows = (("LC-1", params()), ("LC-2", params(DEV_EXISTS)), ("LC-3", params(DEV_ABSENT)),
            ("LC-4", params(IMPOSSIBLE, scope=False)))
    for i, (name, prm) in enumerate(rows):
        if i:
            time.sleep(CALL_GAP_S)
        try:
            v[name] = count_query(h1, prm, tok, log, name)
        except Blind:
            return fin(blind(name, log))
    g = lambda ok: "HOLDS" if ok else "OFF"
    res = [("LC-1 scope > 0", v["LC-1"] > 0),
           ("LC-2 dev exists > 0 and <= LC-1", 0 < v["LC-2"] <= v["LC-1"]),
           ("LC-3 dev DOES_NOT_EXIST < LC-1", v["LC-3"] < v["LC-1"]),
           ("LC-4 impossible AND service = 0", v["LC-4"] == 0),
           ("LC-SUM LC-2 + LC-3 = LC-1", v["LC-2"] + v["LC-3"] == v["LC-1"])]
    print("LC hour %s, all at ABR 1: LC-1 %d, LC-2 %d, LC-3 %d, LC-4 %d" % (LC_HOUR, v["LC-1"], v["LC-2"], v["LC-3"], v["LC-4"]))
    for name, ok in res:
        print("  %s -> %s" % (name, g(ok)))
    return fin(0 if all(ok for _, ok in res) else 3, {name: g(ok) for name, ok in res})


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
    if d["req_lines"] and (d["no_colo"] + d["no_label"]) > 0.05 * d["req_lines"]:   # F1 NL-FIX: both halves of :1413
        esc.append("over 5% of req lines without a colo or a board key")
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
    if argv[:1] == ["classify"]:
        return labelled(lambda: dispatch(argv))
    return dispatch(argv)


def dispatch(argv):
    try:
        if len(argv) == 3 and argv[0] == "classify":
            return classify(argv[1], argv[2])
        if argv == ["ctl"]:
            return ctl()
        if len(argv) == 2 and argv[0] == "lc":
            return lc(argv[1])
        if argv[:1] == ["scan"] and len(argv) in (2, 3):
            return scan(argv[1], argv[2] if len(argv) == 3 else None)
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
