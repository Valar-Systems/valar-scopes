"""R1's T and B (docs/uk-data.md A1.3, with the details pinned in A3.12), from the collection's listings.

    python -I r1_tb.py selftest                         the controls and sabotages, on synthetic weeks
    python -I r1_tb.py offsets <out.json>               B's ingest offsets, from cref's Workers Logs (newest 24 h)
    python -I r1_tb.py read <collection_dir> <offsets.json|none> <out.json>

V is each key's validity time; P is the .tiff's LastModified. B's model: a check at t stores the newest frame with
P <= t, servable from t + b; the device polls at phi + 60 n, holds the frame served at the poll (from the reply), and
draws while now - V <= limit. Blanks are exact interval lengths. No coordinate is read or printed; the token is read
from the token file and never printed.
"""
import bisect
import glob
import json
import math
import os
import re
import sys
import time
import urllib.error
import urllib.request
from datetime import datetime, timezone

W0 = datetime(2026, 10, 28, tzinfo=timezone.utc).timestamp()
W1 = datetime(2026, 11, 4, 16, tzinfo=timezone.utc).timestamp()
S0 = W0 + 600                      # B's span starts 600 s in (A3.12)
DAYS = [W0 + 86400 * d for d in range(7)]   # the gap rule's 7 UTC days, 10-28 to 11-03
KEY_RE = re.compile(r"/OPERA@(\d{8})T(\d{4})@0@DBZH\.tiff$")
ACCOUNT = "48822e896bb10c45aa6bfe139bcff3d1"   # proxy/wrangler.toml (public)


def v_of(key):
    m = KEY_RE.search(key)
    if not m:
        return None
    d, t = m.groups()
    return datetime(int(d[:4]), int(d[4:6]), int(d[6:8]), int(t[:2]), int(t[2:]), tzinfo=timezone.utc).timestamp()


def iso(s):
    return datetime.fromisoformat(s.replace("Z", "+00:00")).timestamp()


# ---- the listings and their control ----------------------------------------------------------------------------------

def read_listings(path):
    """{key: LastModified seconds} for the DBZH .tiff keys, and the keys whose LastModified differed between listings."""
    seen, moved = {}, set()
    for line in open(path, encoding="utf-8"):
        rec = json.loads(line)
        for k, lm in rec["keys"].items():
            if v_of(k) is None:
                continue
            p = iso(lm)
            if k in seen and seen[k] != p:
                moved.add(k)
            seen.setdefault(k, p)
    return seen, moved


# ---- T --------------------------------------------------------------------------------------------------------------

def nearest_rank(xs, p):
    s = sorted(xs)
    return s[max(0, math.ceil(p / 100 * len(s)) - 1)]


def t_rows(frames):
    """frames: [(V, P)] within the week, any order."""
    vs = sorted(v for v, _ in frames)
    steps = [b - a for a, b in zip(vs, vs[1:])]
    lat = [p - v for v, p in frames]
    slots = int((W1 - W0) // 300) + 1
    have = {v for v in vs}
    missing = sum(1 for i in range(slots) if W0 + 300 * i not in have)
    return {
        "frames": len(vs), "steps": len(steps),
        "T1_share_300": sum(1 for s in steps if s == 300) / len(steps) if steps else float("nan"),
        "T2_p50": nearest_rank(lat, 50), "T2_p95": nearest_rank(lat, 95),
        "T2_share_over_450": sum(1 for x in lat if x > 450) / len(lat),
        "T3_slots": slots, "T3_missing": missing, "T3_share": missing / slots,
    }


def t_grade(r):
    return {"T1": r["T1_share_300"] >= 0.99,
            "T2": 240 <= r["T2_p50"] <= 265 and r["T2_p95"] <= 320 and r["T2_share_over_450"] <= 0.01,
            "T3": r["T3_share"] <= 0.005}


# ---- B --------------------------------------------------------------------------------------------------------------

def checks_for(frames, o00, o30, extra):
    """The ingest's check times: :00 and :30 every minute (at their offsets), plus, with `extra`, one every 10 s from
    V + 240 until one finds the frame listed. Returns (sorted times, the number of extra checks)."""
    t = []
    m0, m1 = int(W0 // 60) - 30, int(W1 // 60) + 2
    for m in range(m0, m1):
        t.append(m * 60 + o00)
        t.append(m * 60 + 30 + o30)
    n_extra = 0
    if extra:
        for v, p in frames:
            c = v + 240 + o00
            while True:
                t.append(c)
                n_extra += 1
                if c >= p:
                    break
                c += 10
    t.sort()
    return t, n_extra


def served_steps(frames, checks, b):
    """[(time servable, V)], both increasing: the newest stored frame from each time on."""
    fr = sorted(frames, key=lambda f: f[1])
    ps = [p for _, p in fr]
    pref, best = [], -math.inf
    for v, _ in fr:
        best = max(best, v)
        pref.append(best)
    out, cur = [], -math.inf
    for t in checks:
        i = bisect.bisect_right(ps, t)
        if i and pref[i - 1] > cur:
            cur = pref[i - 1]
            out.append((t + b, cur))
    return out


def blanks_one_phase(steps, phi, limit, reply, p_of, age_from="V"):
    """The blank intervals inside [S0, W1] for one poll phase. Returns (total s, count, longest s)."""
    times = [s for s, _ in steps]
    first = math.floor((W0 - 3600 - phi) / 60) * 60 + phi
    polls = []
    s = first
    while s <= W1 + 60:
        polls.append(s)
        s += 60
    segs = []
    for k in range(len(polls) - 1):
        a, c = polls[k] + reply, polls[k + 1] + reply
        i = bisect.bisect_right(times, polls[k])
        v = steps[i - 1][1] if i else None
        if v is None:
            lo = a
        else:
            ref = v if age_from == "V" else p_of[v]
            lo = max(a, ref + limit)
        if lo < c:
            lo, hi = max(lo, S0), min(c, W1)
            if lo < hi:
                segs.append((lo, hi))
    merged = []
    for lo, hi in segs:
        if merged and abs(merged[-1][1] - lo) < 1e-9:
            merged[-1][1] = hi
        else:
            merged.append([lo, hi])
    lens = [hi - lo for lo, hi in merged]
    return sum(lens), len(lens), max(lens) if lens else 0.0


def b_rows(frames, o00, o30, b=2.0, limit=600, reply=0.0, extra=False, age_from="V"):
    checks, n_extra = checks_for(frames, o00, o30, extra)
    steps = served_steps(frames, checks, b)
    p_of = {v: p for v, p in frames}
    tot = cnt = 0.0
    longest = 0.0
    for phi in range(60):
        t, n, mx = blanks_one_phase(steps, phi, limit, reply, p_of, age_from)
        tot += t
        cnt += n
        longest = max(longest, mx)
    span = W1 - S0
    days = span / 86400
    return {"B1_pct": 100 * tot / 60 / span, "B2_per_h": cnt / 60 / (span / 3600), "B3_longest_s": longest,
            "extra_checks_per_day": n_extra / days if extra else 0,
            "extra_cpu_s_per_day_at_8ms": 0.008 * n_extra / days if extra else 0}


def b_grade(today, extra):
    return {"today": {"B1": 2.0 <= today["B1_pct"] <= 5.0, "B2": 4 <= today["B2_per_h"] <= 8, "B3": today["B3_longest_s"] <= 600},
            "extra": {"B1": 0.5 <= extra["B1_pct"] <= 2.5, "B2": 2 <= extra["B2_per_h"] <= 5, "B3": extra["B3_longest_s"] <= 600},
            "bar_a_today": today["B1_pct"] <= 1 and today["B2_per_h"] <= 1 and today["B3_longest_s"] <= 300,
            "bar_a_extra": extra["B1_pct"] <= 1 and extra["B2_per_h"] <= 1 and extra["B3_longest_s"] <= 300}


# ---- the offsets, from Workers Logs -----------------------------------------------------------------------------------

def token(name):
    for line in open(os.path.expanduser("~/.config/blipscope/tokens"), encoding="utf-8"):
        k, _, v = line.strip().partition("=")
        if k == name and v:
            return v
    raise SystemExit(f"{name} absent from the token file")


def offsets_from_logs():
    now = int(time.time() * 1000)
    tok = token("observability-read")
    t0s = {":00": [], ":30": []}
    offset, pages = None, 0
    while True:
        body = {"queryId": "uk-r1-b-offsets", "timeframe": {"from": now - 86400 * 1000, "to": now}, "view": "events",
                "limit": 2000, "parameters": {"filters": [{"key": "$metadata.service", "operation": "eq", "type": "string",
                                                           "value": "blipscope-wx-ingest"}]}}
        if offset:
            body["offset"] = offset
        req = urllib.request.Request(
            f"https://api.cloudflare.com/client/v4/accounts/{ACCOUNT}/workers/observability/telemetry/query",
            data=json.dumps(body).encode(), method="POST",
            headers={"Authorization": "Bearer " + tok, "Content-Type": "application/json"})
        with urllib.request.urlopen(req, timeout=120) as r:
            d = json.loads(r.read())
        evs = d["result"]["events"]["events"]
        pages += 1
        for e in evs:
            s = e.get("source") or {}
            if s.get("evt") == "wx_ingest" and s.get("product") == "cref" and s.get("check") in t0s and isinstance(s.get("ms"), int):
                t0s[s["check"]].append(((e["timestamp"] - s["ms"]) / 1000.0) % 60)
        if len(evs) < 2000 or pages > 20:
            break
        offset = evs[-1]["$metadata"]["id"]
    med = lambda xs: sorted(xs)[len(xs) // 2] if xs else None
    return {"pages": pages, "n00": len(t0s[":00"]), "n30": len(t0s[":30"]),
            "o00": med(t0s[":00"]), "o30": (med(t0s[":30"]) - 30) if t0s[":30"] else None}


# ---- read the week ---------------------------------------------------------------------------------------------------

def read_week(coll, offsets_path):
    listed, moved = read_listings(os.path.join(coll, "listings.jsonl"))
    if moved:
        return {"STOP": f"listing control: {len(moved)} key(s) changed LastModified between listings"}
    week = [(v_of(k), p, k) for k, p in listed.items() if W0 <= v_of(k) <= W1]
    held = {os.path.basename(f)[:-5] for f in glob.glob(os.path.join(coll, "frames", "*.json"))
            if os.path.exists(f[:-5] + ".tiles")}
    days = []
    for d0 in DAYS:
        ks = [k for v, _, k in week if d0 <= v < d0 + 86400]
        stamps = {re.search(r"@(\d{8}T\d{4})@", k).group(1) for k in ks}
        days.append({"day": datetime.fromtimestamp(d0, timezone.utc).strftime("%Y-%m-%d"), "listed": len(ks),
                     "not_held": len(stamps - held)})
    whole = sum(1 for d in days if d["not_held"] == 0 and d["listed"] > 0)
    out = {"listed_keys": len(listed), "week_frames": len(week), "days": days, "days_wholly_held": whole}
    if whole < 6:
        out["BLIND"] = "fewer than 6 of the 7 days wholly held"
        return out
    frames = [(v, p) for v, p, _ in week]
    t = t_rows(frames)
    out["T"], out["T_grade"] = t, t_grade(t)
    offs = None if offsets_path == "none" else json.load(open(offsets_path))
    sets = [(offs["o00"], offs["o30"])] if offs and offs.get("o00") is not None and offs.get("o30") is not None else [(0, 0), (5, 5)]
    out["B"] = []
    for o00, o30 in sets:
        today, extra = b_rows(frames, o00, o30), b_rows(frames, o00, o30, extra=True)
        row = {"offsets": [o00, o30], "today": today, "extra": extra, "grade": b_grade(today, extra),
               "printed": {f"b={bb}": {"today": b_rows(frames, o00, o30, b=bb)["B1_pct"], "extra": b_rows(frames, o00, o30, b=bb, extra=True)["B1_pct"]}
                           for bb in (0, 5)}}
        row["printed"]["reply+1s"] = {"today": b_rows(frames, o00, o30, reply=1)["B1_pct"], "extra": b_rows(frames, o00, o30, reply=1, extra=True)["B1_pct"]}
        row["B5"] = {f"limit {L}": {"today": b_rows(frames, o00, o30, limit=L)["B1_pct"], "extra": b_rows(frames, o00, o30, limit=L, extra=True)["B1_pct"]}
                     for L in (720, 900)}
        out["B"].append(row)
    return out


# ---- the selftest: controls and sabotages on synthetic weeks ------------------------------------------------------------

def synth(latency=250, start=W0 - 3600):
    out, v = [], start
    while v <= W1:
        out.append((v, v + latency))
        v += 300
    return out


def selftest():
    fails = []
    def check(ok, what):
        print(("ok    " if ok else "FAIL  ") + what)
        if not ok:
            fails.append(what)

    wk = [f for f in synth() if W0 <= f[0] <= W1]
    t = t_rows(wk)
    check(t["T1_share_300"] == 1.0 and t["T2_p50"] == 250 and t["T3_missing"] == 0 and t["T3_slots"] == 2209,
          f"T on a perfect week: T1 {t['T1_share_300']:.4f}, p50 {t['T2_p50']}, missing {t['T3_missing']} of {t['T3_slots']}")
    sab = [f for i, f in enumerate(sorted(wk)) if i % 3 != 2]
    ts = t_rows(sab)
    check(not t_grade(ts)["T1"], f"SABOTAGE every third key dropped: T1 {ts['T1_share_300']:.4f} (red)")

    c1 = b_rows(synth(), 0, 0, b=0)
    check(abs(c1["B1_pct"] - 2.4167) <= 0.01 and abs(c1["B2_per_h"] - 5.80) <= 0.01,
          f"control (1): B1 {c1['B1_pct']:.4f} % (2.4167 +- 0.01), B2 {c1['B2_per_h']:.3f}/h (5.80 +- 0.01), B3 {c1['B3_longest_s']:.1f} s")
    c2 = b_rows(synth(), 0, 0, b=0, limit=3600)
    check(c2["B1_pct"] == 0, f"control (2): limit 3,600 s gives B1 {c2['B1_pct']:.4f} % (0)")
    # not frozen, derived before its first run: the extra checks find a 250 s frame at V + 250, so 9 of 60 phases
    # blank for 1-9 s: 45/60 s per 300 s = 0.25 %, and 9/60 x 12 = 1.80 an hour
    ce = b_rows(synth(), 0, 0, b=0, extra=True)
    check(abs(ce["B1_pct"] - 0.25) <= 0.01 and abs(ce["B2_per_h"] - 1.80) <= 0.01,
          f"instrument check, extra checks: B1 {ce['B1_pct']:.4f} % (0.25), B2 {ce['B2_per_h']:.3f}/h (1.80), "
          f"{ce['extra_checks_per_day']:.0f} extra checks a day")
    sb = b_rows(synth(), 0, 0, b=0, age_from="P")
    check(abs(sb["B1_pct"] - 2.4167) > 0.01, f"SABOTAGE age from P: control (1) reads B1 {sb['B1_pct']:.4f} % (red)")

    # the listing control can fire: one key with two LastModifieds
    import tempfile
    with tempfile.TemporaryDirectory() as td:
        p = os.path.join(td, "l.jsonl")
        k = "2026/10/28/OPERA/COMP/OPERA@20261028T0000@0@DBZH.tiff"
        with open(p, "w") as f:
            f.write(json.dumps({"at": "a", "keys": {k: "2026-10-28T00:04:10Z"}}) + "\n")
            f.write(json.dumps({"at": "b", "keys": {k: "2026-10-28T00:04:11Z"}}) + "\n")
        _, moved = read_listings(p)
        check(moved == {k}, "listing control: a key whose LastModified changed is caught")
    print("SELFTEST " + ("PASSED" if not fails else f"FAILED ({len(fails)})"))
    return 0 if not fails else 1


if __name__ == "__main__":
    if sys.argv[1] == "selftest":
        sys.exit(selftest())
    if sys.argv[1] == "offsets":
        r = offsets_from_logs()
        json.dump(r, open(sys.argv[2], "w"), indent=1)
        print(json.dumps(r))
    elif sys.argv[1] == "read":
        r = read_week(sys.argv[2], sys.argv[3])
        json.dump(r, open(sys.argv[4], "w"), indent=1)
        print(json.dumps({k: r[k] for k in r if k in ("STOP", "BLIND", "week_frames", "days_wholly_held", "T", "T_grade")}, indent=1))
