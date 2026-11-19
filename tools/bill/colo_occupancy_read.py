#!/usr/bin/env python3
"""/wx's F1: how many boards share a colo, and what a per-colo hold of `latest` would hit (docs/proxy-bill.md step 5).

Read-only. One closed hour of the proxy's Workers Logs `evt:"req"` lines (the Workers Observability API, as step 3's
readers): each line's `colo` and `devl` (the device LABEL). Held IN MEMORY ONLY, and only a histogram leaves the
process: how many colos hold k boards, for each k. Never a colo code, a label, an id or a line. A line that still
carries `dev` (the device id) means the label-only line is not deployed yet: the read REFUSES (exit 5) and keeps
nothing (the reviewer, 2026-11-19: wait for the devl line).

  python -X utf8 -I tools/bill/colo_occupancy_read.py ctl                       F1-CTL: the model's own control, no data
  python -X utf8 -I tools/bill/colo_occupancy_read.py read <hour start ISO> <out.json>
  python -X utf8 -I tools/bill/colo_occupancy_read.py grade <read1> <read2>      the reads agree, then F1-K/H55/H115

The model (frozen in the registration): every active board polls /wx every P = 60 s at a random phase, each poll
jittered +/-2 s; a per-colo hold of `latest` lasts T s from the miss that filled it; 200 seeded runs of one hour per
colo size. T = 55 s and 115 s, off the poll boundary on purpose.
Exit: 0 done/graded; 3 ESCAPE; 4 REFUSED (reads under 60 s apart); 5 REFUSED (a `dev` line); 2 the rig.
"""
import importlib.util as _ilu
import json
import os
import random
import sys
import urllib.error
import urllib.request
from datetime import datetime, timedelta, timezone

ACCOUNT = "48822e896bb10c45aa6bfe139bcff3d1"   # proxy/wrangler.toml (public)
SERVICE = "blipscope-proxy"
P, JITTER, HOUR, RUNS = 60.0, 2.0, 3600.0, 200
TS = (55.0, 115.0)
MIN_APART_S = 60

_tfs = _ilu.spec_from_file_location("token_file", os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "token_file.py"))
TOKEN_FILE = _ilu.module_from_spec(_tfs); _tfs.loader.exec_module(TOKEN_FILE)   # docs/token-file-python.md


class Rig(Exception):
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


def req_fields(e):
    """The req line's object, from `source` or the raw message; None if this event is not a req line."""
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
    if isinstance(obj, dict) and obj.get("evt") == "req":
        return obj
    return None


def read(hour, path):
    h1 = datetime.fromisoformat(hour.replace("Z", "+00:00")).astimezone(timezone.utc)
    if h1.minute or h1.second:
        raise Rig("the hour must start on a whole hour")
    h2 = h1 + timedelta(hours=1)
    if h2 > datetime.now(timezone.utc):
        raise Rig("the hour has not closed")
    tok = TOKEN_FILE.token("observability-read")
    colos, lines, no_colo, no_label, abr, offset, pages = {}, 0, 0, 0, set(), None, 0
    while True:
        body = {"queryId": "proxy-bill-colo-occupancy", "timeframe": {"from": int(h1.timestamp() * 1000), "to": int(h2.timestamp() * 1000)},
                "view": "events", "limit": 2000, "parameters": {}}
        if offset:
            body["offset"] = offset
        r = post(f"https://api.cloudflare.com/client/v4/accounts/{ACCOUNT}/workers/observability/telemetry/query", body, tok)["result"]
        abr.add(r.get("statistics", {}).get("abr_level"))
        page = r["events"]["events"]
        pages += 1
        for e in page:
            if (e.get("$metadata") or {}).get("service") != SERVICE:
                continue
            o = req_fields(e)
            if o is None:
                continue
            if "dev" in o:
                print("REFUSED: a req line still carries the device id; the label-only line is not deployed. Nothing kept.")
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
    out = {"hour": h1.strftime("%Y-%m-%dT%H:00:00Z"), "read_at": datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
           "pages": pages, "abr": sorted(x for x in abr if x is not None), "req_lines": lines,
           "no_colo": no_colo, "no_label": no_label, "histogram": {str(k): v for k, v in sorted(hist.items())}}
    colos.clear()          # nothing but the histogram survives
    with open(path, "w", encoding="utf-8", newline="\n") as fh:
        json.dump(out, fh, indent=1, sort_keys=True)
        fh.write("\n")
    print("read %s: %d page(s), ABR %s, req lines %d, no colo %d, no label %d, histogram %s" % (
        out["hour"], pages, out["abr"], lines, no_colo, no_label, out["histogram"]))
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
    if ctl() != 0:
        return 3
    strip = lambda x: {k: v for k, v in x.items() if k != "read_at"}
    esc = []
    if strip(d) != strip(d2):
        esc.append("the two reads differ")
    if d["abr"] not in ([1], []):
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
