#!/usr/bin/env python3
"""The continent's return check: when OPERA's radar is fresh for the nine continental products, every day.

The owner's ruling (2026-11-25): the continent goes live after 7 FRESH days from either OPERA store, and the check keeps
running after go-live and reports the first day that fails. Our own store keeps one hour of frames (RETAIN_S = 3600 in
proxy/src/wx/ingest.ts), so the check must READ at least hourly. It does that through production's public /healthz,
whose `wx` field gives each product's newest-frame age (`frameAgeS`), its `source` ("primary" or "backup") and `stale`.
Anonymous GETs only; no token, no write.

The grading, per product and UTC day:
  - a read is FRESH for a product when its frameAgeS is a number under 1,800 s, at EVERY read;
  - hour H holds a stored frame when a read taken in [H:30, H+1:00) is fresh (latest's epoch is then H:00 or later,
    and latest only moves forward);
  - the day is FRESH when all 24 hours hold a frame and every read is fresh; FAILED when a read is not fresh; BLIND
    (the instrument's, never a failure, never fresh) when an hour has no second-half read at all;
  - a read that failed, or that does not name all ten OPERA products, is NO read (K-READ).
The continent's day: BLIND if a control fails, or if the UK product (opuk, K-UK) is not FRESH on the same reads; else
FAILED if any of the nine is FAILED; else BLIND if any is BLIND; else FRESH. Controls (K-STALE): on COPIES of the day's
reads, a synthetic product at 7,200 s everywhere must grade FAILED, and a fresh synthetic one with hour 05's second
half removed must grade BLIND, so the grader is shown able to fail on the day's own reads.

    python3 tools/wx/opera_return.py read <out.json>      one read; a failed read is recorded, exit 0
    python3 tools/wx/opera_return.py grade-ci <dir> [day]  in Actions: fetch the day's reads (GITHUB_TOKEN, never printed),
                                                          grade, read the 6 prior daily grades for the count; exit 1 on a
                                                          FAILED continent day, a ::warning:: on BLIND
    python3 tools/wx/opera_return.py --selftest           synthetic days only
    python3 tools/wx/opera_return.py --sabotage           planted copies: each must red exactly its own row
"""
import datetime
import io
import json
import os
import re
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request
import zipfile

HEALTHZ = "https://scopes.valarsystems.com/healthz"
UA = "BlipscopeOperaReturn/1 (+https://github.com/Valar-Systems/valar-scopes)"
API = "https://api.github.com"
UK = "opuk"
NINE = ("opis", "opib", "opmt", "opwe", "opce", "opse", "opba", "opsw", "opsn")
OPERA = (UK,) + NINE
FRESH_S = 1800
STAMP = "%Y%m%dT%H%M%SZ"


def stamp(t):
    return time.strftime(STAMP, time.gmtime(t))


def epoch_of(s):
    return datetime.datetime.strptime(s, STAMP).replace(tzinfo=datetime.timezone.utc).timestamp()


def day_start(day):
    return datetime.datetime.strptime(day, "%Y-%m-%d").replace(tzinfo=datetime.timezone.utc).timestamp()


def _get(url, token=None, accept=None):
    req = urllib.request.Request(url, headers={"User-Agent": UA, **({"Accept": accept} if accept else {})})
    if token:
        req.add_unredirected_header("Authorization", "Bearer " + token)   # never sent on to a redirect's host
    last = None
    for _ in range(2):
        try:
            with urllib.request.urlopen(req, timeout=20) as r:
                return r.read()
        except urllib.error.HTTPError as e:
            if e.code < 500:
                raise
            last = e
        except (urllib.error.URLError, TimeoutError) as e:
            last = e
        time.sleep(2)
    raise last


# ---- one read ----------------------------------------------------------------------------------------------------

def read_healthz(get=_get):
    rec = {"readAt": stamp(time.time())}
    try:
        wx = json.loads(get(HEALTHZ)).get("wx")
        if not isinstance(wx, dict):
            raise ValueError("no wx field")
        rec["products"] = {p: {k: wx[p].get(k) for k in ("frameAgeS", "source", "stale")}
                           for p in OPERA if isinstance(wx.get(p), dict)}
    except Exception as e:                  # recorded, never raised: a failed read must still upload its record
        rec.pop("products", None)
        rec["error"] = type(e).__name__
    return rec


# ---- grading -----------------------------------------------------------------------------------------------------

def valid(r, need=OPERA):
    """K-READ: a read counts only when it names all ten OPERA products (and a control's synthetic one, on its copy)."""
    return isinstance(r.get("products"), dict) and all(isinstance(r["products"].get(p), dict) for p in need)


def is_fresh(e):
    a = e.get("frameAgeS")
    return isinstance(a, (int, float)) and not isinstance(a, bool) and a < FRESH_S


def grade_product(day_reads, p, ds, need=OPERA):
    held = [False] * 24
    stale = []
    for r in day_reads:
        off = r["t"] - ds
        h = int(off // 3600)
        if "error" in r:
            continue                       # a failed read is no read
        if not valid(r, need):
            continue                       # K-READ: a read missing a product is no read
        e = r["products"][p]
        if not is_fresh(e):
            stale.append((h, e.get("frameAgeS")))
            continue
        if off % 3600 >= 1800:
            held[h] = True
    if stale:
        return {"verdict": "FAILED", "first": list(stale[0]), "staleReads": len(stale)}
    missing = [h for h in range(24) if not held[h]]
    if missing:
        return {"verdict": "BLIND", "hoursWithoutSecondHalfRead": missing}
    return {"verdict": "FRESH"}


def controls(day_reads, ds):
    with_stale = [dict(r, products=dict(r["products"], zzstale={"frameAgeS": 7200})) for r in day_reads if valid(r) and "error" not in r]
    hole = [dict(r, products=dict(r["products"], zzfresh={"frameAgeS": 60})) for r in day_reads
            if valid(r) and "error" not in r and not (5 * 3600 + 1800 <= r["t"] - ds < 6 * 3600)]
    k_stale = grade_product(with_stale, "zzstale", ds, OPERA + ("zzstale",))["verdict"] == "FAILED"
    k_hole = grade_product(hole, "zzfresh", ds, OPERA + ("zzfresh",))["verdict"] == "BLIND"
    return k_stale, k_hole


def grade_day(records, day):
    ds = day_start(day)
    day_reads = []
    for r in records:
        try:
            t = epoch_of(r["readAt"])
        except (KeyError, ValueError, TypeError):
            continue
        if ds <= t < ds + 86400:
            day_reads.append(dict(r, t=t))
    k_stale, k_hole = controls(day_reads, ds)
    products = {p: grade_product(day_reads, p, ds) for p in OPERA}
    k_uk = products[UK]["verdict"] == "FRESH"
    nine = [products[p]["verdict"] for p in NINE]
    if not (k_stale and k_hole):
        cont, why = "BLIND", "a control failed: the grader could not be shown able to fail on this day's reads"
    elif not k_uk:
        cont, why = "BLIND", f"K-UK: opuk is {products[UK]['verdict']}, not FRESH, on the same reads"
    elif "FAILED" in nine:
        cont, why = "FAILED", "FAILED: " + ", ".join(p for p in NINE if products[p]["verdict"] == "FAILED")
    elif "BLIND" in nine:
        cont, why = "BLIND", "an hour with no second-half read"
    else:
        cont, why = "FRESH", "all nine FRESH"
    return {"day": day, "reads": len(day_reads), "failedReads": sum(1 for r in day_reads if "error" in r),
            "incompleteReads": sum(1 for r in day_reads if "error" not in r and not valid(r)),
            "controls": {"K-STALE": k_stale, "K-HOLE": k_hole, "K-UK": k_uk}, "products": products,
            "continent": cont, "why": why}


def count_fresh(verdicts, end_day):
    """Consecutive FRESH days ending at end_day, at most 7. A day with no grade is not FRESH."""
    k, d = 0, datetime.date.fromisoformat(end_day)
    for _ in range(7):
        if verdicts.get(d.isoformat()) != "FRESH":
            break
        k += 1
        d -= datetime.timedelta(days=1)
    return k


# ---- in Actions --------------------------------------------------------------------------------------------------

def _artifacts_named(repo, token, name):
    """Every unexpired artifact with exactly this name, across runs (the REST API's exact `name` filter): a day's reads
    are one name, so this is 1 or 2 requests, never a walk through the repo's other workflows' artifacts."""
    out, page = [], 1
    while True:
        j = json.loads(_get(f"{API}/repos/{repo}/actions/artifacts?name={name}&per_page=100&page={page}", token,
                            "application/vnd.github+json"))
        arts = j.get("artifacts", [])
        out += [a for a in arts if not a.get("expired") and a.get("name") == name]
        if len(arts) < 100:
            return out
        if page >= 20:                    # a day holds about 96: 2,000 of one name is a broken listing, said aloud
            raise RuntimeError(f"more than 2,000 artifacts named {name}")
        page += 1


def _zip_json(repo, token, art, name):
    z = zipfile.ZipFile(io.BytesIO(_get(f"{API}/repos/{repo}/actions/artifacts/{art['id']}/zip", token)))
    return json.loads(z.read(name))


def grade_ci(outdir, day):
    token, repo = os.environ["GITHUB_TOKEN"], os.environ["GITHUB_REPOSITORY"]
    if not day:
        day = (datetime.datetime.now(datetime.timezone.utc).date() - datetime.timedelta(days=1)).isoformat()
    if not re.fullmatch(r"\d{4}-\d{2}-\d{2}", day):
        raise ValueError("day must be YYYY-MM-DD")
    arts = _artifacts_named(repo, token, "opera-read-" + day.replace("-", ""))
    reads = [_zip_json(repo, token, a, "read.json") for a in arts]
    g = grade_day(reads, day)
    g["artifacts"] = len(arts)
    verdicts = {day: g["continent"]}
    for i in range(1, 7):
        d = (datetime.date.fromisoformat(day) - datetime.timedelta(days=i)).isoformat()
        cands = sorted(_artifacts_named(repo, token, "opera-grade-" + d), key=lambda a: a.get("created_at", ""))
        if cands:
            verdicts[d] = _zip_json(repo, token, cands[-1], "grade.json").get("continent")
    g["count"] = count_fresh(verdicts, day)
    os.makedirs(outdir, exist_ok=True)
    with open(os.path.join(outdir, "grade.json"), "w", encoding="utf-8") as fh:
        json.dump(g, fh, indent=1)
    if os.environ.get("GITHUB_OUTPUT"):
        with open(os.environ["GITHUB_OUTPUT"], "a", encoding="utf-8") as fh:
            fh.write(f"day={day}\n")
    print(f"{day}: {g['artifacts']} read artifacts, {g['reads']} reads in the day ({g['failedReads']} failed, "
          f"{g['incompleteReads']} incomplete); controls {g['controls']}")
    for p in OPERA:
        print(f"  {p}: {g['products'][p]}")
    print(f"CONTINENT {day}: {g['continent']} ({g['why']}); count {g['count']} of 7")
    if g["continent"] == "FAILED":
        print(f"::error title=opera-return::{day} FAILED: {g['why']}")
        return 1
    if g["continent"] == "BLIND":
        print(f"::warning title=opera-return::{day} BLIND (the instrument's, not OPERA's): {g['why']}")
    return 0


# ---- selftest and sabotage ---------------------------------------------------------------------------------------

def _synth(day, per_hour=(5, 20, 35, 50), edit=None):
    ds, recs = day_start(day), []
    for h in range(24):
        for m in per_hour:
            r = {"readAt": stamp(ds + h * 3600 + m * 60 + 1),
                 "products": {p: {"frameAgeS": 300, "source": "backup", "stale": False} for p in OPERA}}
            r = edit(h, m, r) if edit else r
            if r is not None:
                recs.append(r)
    return recs


def selftest():
    D, rows = "2026-11-26", []

    def row(name, ok):
        rows.append(ok)
        print(("PASS " if ok else "FAIL ") + name)

    def setp(p, age, when):
        def e(h, m, r):
            if when(h, m):
                r["products"][p] = dict(r["products"][p], frameAgeS=age)
            return r
        return e

    g = grade_day(_synth(D), D)
    row("R1: all ten fresh all day -> FRESH, count 1", g["continent"] == "FRESH" and count_fresh({D: g["continent"]}, D) == 1)
    g = grade_day(_synth(D, edit=setp("opce", 2400, lambda h, m: (h, m) == (13, 35))), D)
    row("R2: one stale opce read -> FAILED, named with its hour and age",
        g["continent"] == "FAILED" and g["products"]["opce"].get("first") == [13, 2400])
    g = grade_day(_synth(D, edit=lambda h, m, r: None if h == 7 else r), D)
    row("R3: no read at all in hour 07 -> BLIND, not FAILED",
        g["continent"] == "BLIND" and g["products"]["opib"].get("hoursWithoutSecondHalfRead") == [7])
    g = grade_day(_synth(D, edit=setp(UK, 3600, lambda h, m: True)), D)
    row("R4: opuk stale all day, the nine fresh -> BLIND by K-UK", g["continent"] == "BLIND" and g["why"].startswith("K-UK"))

    def only_35(hour, fn):
        def e(h, m, r):
            if h != hour:
                return r
            return fn(r) if m == 35 else None
        return e
    g = grade_day(_synth(D, edit=only_35(10, lambda r: dict(r, products={p: v for p, v in r["products"].items() if p != "opsn"}))), D)
    row("R5: hour 10's only second-half read lacks opsn -> no read -> BLIND", g["continent"] == "BLIND" and g["incompleteReads"] == 1)
    g = grade_day(_synth(D, edit=only_35(11, lambda r: {"readAt": r["readAt"], "error": "URLError"})), D)
    row("R6: hour 11's only second-half read failed -> BLIND, never FRESH", g["continent"] == "BLIND" and g["failedReads"] == 1)
    g = grade_day(_synth(D, edit=lambda h, m, r: None if (h == 12 and m >= 30) else r), D)
    row("R7: hour 12 read only at :05 and :20 -> BLIND, opib's own hour list [12]",   # Correction 1: not masked by a control
        g["continent"] == "BLIND" and g["products"]["opib"].get("hoursWithoutSecondHalfRead") == [12])
    g = grade_day(_synth(D, edit=setp("opce", 1800, lambda h, m: (h, m) == (14, 35))), D)
    row("R8: frameAgeS exactly 1,800 -> FAILED (fresh is < 1,800)", g["continent"] == "FAILED")
    days = [(datetime.date.fromisoformat(D) - datetime.timedelta(days=i)).isoformat() for i in range(7)]
    v7 = {d: "FRESH" for d in days}
    v3 = dict(v7, **{days[2]: "FAILED"})
    vmiss = {d: v for d, v in v7.items() if d != days[0]}
    row("R9: the count: 7 FRESH -> 7; FAILED 3 days back -> 2; a missing grade -> 0",
        count_fresh(v7, D) == 7 and count_fresh(v3, D) == 2 and count_fresh(vmiss, D) == 0)
    ds = day_start(D)
    k_stale, k_hole = controls([dict(r, t=epoch_of(r["readAt"])) for r in _synth(D)], ds)
    row("R10: the controls can fail: a 7,200 s product grades FAILED, a fresh one without hour 05's second half grades BLIND",
        k_stale and k_hole)
    fails = rows.count(False)
    print("SELFTEST PASSED" if not fails else f"SELFTEST FAILED: {fails}")
    return 0 if not fails else 1


PLANTS = (
    ("P1: a failed read counted fresh", '        if "error" in r:\n            continue                       # a failed read is no read\n',
     '        if "error" in r:\n            held[h] = held[h] or off % 3600 >= 1800\n            continue\n', {"R6"}),
    # P2 also fails R10's hole control on every day, so every continent reads BLIND (predict-pr.md Correction 1)
    ("P2: first-half reads counted for the hour", "        if off % 3600 >= 1800:\n", "        if off % 3600 >= 0:\n",
     {"R1", "R2", "R4", "R7", "R8", "R10"}),
    ("P3: <= 1,800 counted fresh", "and a < FRESH_S\n", "and a <= FRESH_S\n", {"R8"}),
    ("P4: K-UK skipped", "    elif not k_uk:\n", "    elif False and not k_uk:\n", {"R4"}),
)


def sabotage():
    src = open(os.path.abspath(__file__), encoding="utf-8").read()
    bad = 0
    with tempfile.TemporaryDirectory() as d:
        for i, (name, needle, repl, want) in enumerate((("unplanted", None, None, set()),) + PLANTS):
            body = src if needle is None else src.replace(needle, repl)
            if needle is not None and (src.count(needle) != 1 or body == src):
                print(f"BLIND {name}: the needle occurs {src.count(needle)} times (want 1)")
                bad += 1
                continue
            p = os.path.join(d, f"copy{i}.py")
            with open(p, "w", encoding="utf-8") as fh:
                fh.write(body)
            r = subprocess.run([sys.executable, "-I", p, "--selftest"], capture_output=True, text=True)
            red = {line.split(":", 1)[0][5:] for line in r.stdout.splitlines() if line.startswith("FAIL ") and ":" in line}
            ok = red == want and (r.returncode == (1 if want else 0))
            print(("PASS " if ok else "FAIL ") + f"{name}: red {sorted(red)} (want {sorted(want)}), exit {r.returncode}")
            bad += 0 if ok else 1
    print("SABOTAGE PASSED" if not bad else f"SABOTAGE FAILED: {bad}")
    return 0 if not bad else 1


def main(argv):
    if "--selftest" in argv:
        return selftest()
    if "--sabotage" in argv:
        return sabotage()
    if len(argv) >= 2 and argv[0] == "read":
        rec = read_healthz()
        os.makedirs(os.path.dirname(os.path.abspath(argv[1])), exist_ok=True)
        with open(argv[1], "w", encoding="utf-8") as fh:
            json.dump(rec, fh, indent=1)
        n = len(rec.get("products", {}))
        print(f"read {rec['readAt']}: {n} of {len(OPERA)} OPERA products" + (f"; error {rec['error']}" if "error" in rec else ""))
        if os.environ.get("GITHUB_OUTPUT"):
            with open(os.environ["GITHUB_OUTPUT"], "a", encoding="utf-8") as fh:
                fh.write(f"stamp={rec['readAt']}\nday={rec['readAt'][:8]}\n")   # the artifact is named by this day
        return 0
    if len(argv) >= 2 and argv[0] == "grade-ci":
        return grade_ci(argv[1], argv[2] if len(argv) >= 3 else "")
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
