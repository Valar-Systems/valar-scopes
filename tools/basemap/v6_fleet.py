#!/usr/bin/env python3
"""v6_fleet.py -- D4's V6: the fleet's own traffic before and after the map's production switch (registered before any code).

Analytics Engine, dataset blipscope_proxy. COUNTS ONLY: no token, device id or query value is ever printed.

A FLEET REQUEST ROW is a point whose route (blob1) starts with "/" -- every per-request route the proxy writes does, "/other"
included -- and whose device (blob5) is non-empty and is neither of the two excluded ids (the token file's records-device and
test-device). Every other family the proxy writes into the dataset (err5xx, enrich_gap, ota, boot, usage, provision, sitefeed)
carries its own name in blob1, so it is excluded by WHAT IT IS, not by a list of what it is not. Counts are sample-weighted
(_sample_interval x double4), as P5 and PD6.

  python -I tools/basemap/v6_fleet.py read <T>        T = the proxy deploy's exit (the switch), YYYY-MM-DDTHH:MM:SSZ
  python -I tools/basemap/v6_fleet.py compare <read1.json> <read2.json>
  python -I tools/basemap/v6_fleet.py --selftest     planted rows only; the network is blocked

Windows: B [T-60m, T); A [T+5m, T+65m); FF [T, T+15m); C3 [T-24h, T) over ALL points; EMPTY [T-365d-60m, T-365d).
Controls: C1 B requests > 0 and B devices >= 4; C2 |B \\ EMPTY| == |B|; C3 the 24 h 4xx over all points > 0.
Rows: V6-1 A 5xx 0; V6-2 A 4xx 0 (B's 4xx above 0: OUTSIDE); V6-3 |B \\ A| 0; V6-4 A /blips over B /blips in [0.4, 2.5];
V6-5 A /basemap 0 (A only; B's basemap requests were recorded as "/other" before the route was listed). FF: any 5xx or 401/403
sets "rollback": "R-1 at once"; so does any 5xx or 401/403 in A.
Exit: read 0 as stated, 1 a row off or OUTSIDE, 2 BLIND, 3 usage or a query error; compare 0 AGREE, 1 OUTSIDE; selftest 0 or 1.
"""
import importlib.util
import json
import math
import os
import re
import sys
import urllib.request
from datetime import datetime, timedelta, timezone

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.environ.get("V6_FLEET_REPO") or os.path.dirname(os.path.dirname(HERE))
DATASET = "blipscope_proxy"
BLIPS = "/api/v1/blipscope/blips"
BASEMAP = "/api/v1/blipscope/basemap"
BASEMAP_ROUTES = ("/api/v1/blipscope/basemap", "/v1/basemap")
BAND = (0.4, 2.5)
KEYS = ("T", "windows", "B", "A", "FF", "b_not_a", "c3_4xx_all_points", "controls", "rows", "outside", "rollback", "verdict")


def is_request_route(route):
    return isinstance(route, str) and route.startswith("/")


def windows(T):
    m = timedelta(minutes=1)
    year = timedelta(days=365)
    return {"B": (T - 60 * m, T), "A": (T + 5 * m, T + 65 * m), "FF": (T, T + 15 * m), "C3": (T - 24 * 60 * m, T),
            "EMPTY": (T - year - 60 * m, T - year)}


def ts(t):
    return t.strftime("%Y-%m-%d %H:%M:%S")


def where_window(w):
    return f"timestamp >= toDateTime('{ts(w[0])}') AND timestamp < toDateTime('{ts(w[1])}')"


def where_fleet(w, excl):
    return where_window(w) + f" AND blob5 != '' AND blob5 NOT IN ('{excl[0]}', '{excl[1]}')"


def q_rows(where):
    return (f"SELECT blob1 AS route, double1 AS status, SUM(_sample_interval * double4) AS n FROM {DATASET} "
            f"WHERE {where} GROUP BY route, status FORMAT JSON")


def q_devs(where):
    return f"SELECT blob1 AS route, blob5 AS d FROM {DATASET} WHERE {where} GROUP BY route, d FORMAT JSON"


def num(x):
    try:
        v = float(x)
    except (TypeError, ValueError):
        return None
    return v if math.isfinite(v) else None


def tally(rows):
    """Counts over fleet request rows only. A count any non-value touches is None, never a number."""
    keep = [r for r in rows if is_request_route(r.get("route"))]

    def tot(f):
        s = 0.0
        for r in keep:
            n, st = num(r.get("n")), num(r.get("status"))
            if n is None or st is None:
                return None
            if f(r["route"], st):
                s += n
        return round(s)
    return {"requests": tot(lambda rt, s: True), "blips": tot(lambda rt, s: rt == BLIPS),
            "basemap": tot(lambda rt, s: rt == BASEMAP), "x4xx": tot(lambda rt, s: 400 <= s < 500),
            "x5xx": tot(lambda rt, s: s >= 500), "x401_403": tot(lambda rt, s: s in (401.0, 403.0))}


def device_set(rows):
    return {r["d"] for r in rows if is_request_route(r.get("route")) and r.get("d")}


def diff(b, a):
    """How many devices are in b and not in a. Used by V6-3 AND by C2, so C2 shows this function working on live data."""
    return len(b - a)


def read(T, sql, excl):
    W = windows(T)
    out = {"T": ts(T) + "Z", "windows": {k: [ts(a) + "Z", ts(b) + "Z"] for k, (a, b) in W.items()}}
    sets = {}
    for k in ("B", "A", "FF"):
        c = tally(sql(q_rows(where_fleet(W[k], excl))))
        sets[k] = device_set(sql(q_devs(where_fleet(W[k], excl))))
        c["devices"] = len(sets[k])
        out[k] = c
    sets["EMPTY"] = device_set(sql(q_devs(where_fleet(W["EMPTY"], excl))))
    out["c3_4xx_all_points"] = tally(sql(q_rows(where_window(W["C3"]))))["x4xx"]
    out["b_not_a"] = diff(sets["B"], sets["A"])
    B, A, FF = out["B"], out["A"], out["FF"]
    nulls = [f"{k}.{f}" for k in ("B", "A", "FF") for f, v in out[k].items() if v is None]
    out["controls"] = {"C1": B["requests"] is not None and B["requests"] > 0 and B["devices"] >= 4,
                       "C2": len(sets["B"]) > 0 and diff(sets["B"], sets["EMPTY"]) == len(sets["B"]),
                       "C3": out["c3_4xx_all_points"] is not None and out["c3_4xx_all_points"] > 0}
    blind = (not all(out["controls"].values())) or bool(nulls) or not B["blips"]
    out["rows"] = {} if blind else {
        "V6-1": A["x5xx"] == 0, "V6-2": A["x4xx"] == 0, "V6-3": out["b_not_a"] == 0,
        "V6-4": BAND[0] <= A["blips"] / B["blips"] <= BAND[1], "V6-5": A["basemap"] == 0}
    out["outside"] = ["B's fleet 4xx above 0: V6-2 cannot separate D4 from background"] if (B["x4xx"] or 0) > 0 else []
    out["rollback"] = ("R-1 at once" if any((x or 0) > 0 for x in (FF["x5xx"], FF["x401_403"], A["x5xx"], A["x401_403"]))
                       else None)
    out["verdict"] = ("BLIND" if blind else "OUTSIDE" if out["outside"] else
                      "OFF" if not all(out["rows"].values()) else "AS STATED")
    if nulls:
        out["not_a_value"] = nulls
    return out


def exit_code(out):
    return {"AS STATED": 0, "OFF": 1, "OUTSIDE": 1, "BLIND": 2}[out["verdict"]]


def compare(r1, r2):
    moved = [k for k in KEYS if r1.get(k) != r2.get(k)]
    return {"compare": "AGREE" if not moved else "OUTSIDE", "moved": moved}


def live():
    spec = importlib.util.spec_from_file_location("token_file", os.path.join(REPO, "tools", "token_file.py"))
    tf = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(tf)
    t = tf.tokens("analytics-read", "records-device", "test-device")
    excl = [t["records-device"], t["test-device"]]
    if not all(e.isalnum() for e in excl):
        raise SystemExit("v6_fleet: an excluded device id has unexpected characters (not printed)")
    toml = open(os.path.join(REPO, "proxy", "basemap.wrangler.toml"), encoding="utf-8").read()
    m = re.search(r'^account_id\s*=\s*"([0-9a-f]{32})"', toml, re.M)
    if not m:
        raise SystemExit("v6_fleet: no account_id in proxy/basemap.wrangler.toml")
    url = f"https://api.cloudflare.com/client/v4/accounts/{m.group(1)}/analytics_engine/sql"

    def sql(q):
        req = urllib.request.Request(url, data=q.encode(), headers={"Authorization": f"Bearer {t['analytics-read']}"},
                                     method="POST")
        with urllib.request.urlopen(req, timeout=120) as r:
            return json.loads(r.read())["data"]
    return sql, excl


# ---------------------------------------------------------------------------------------------------------------------------
# The selftest: planted rows only. The route and family lists are READ from the proxy's source on this tree.
def routes_from_source(text):
    s = re.search(r"KNOWN_ROUTES\s*=\s*new Set\(\[(.*?)\]\);", text, re.S)
    lits = re.findall(r'^\s*"(/[^"]*)"', s.group(1), re.M) if s else []
    body = re.search(r"export function routeTemplate\(.*?\n}\n", text, re.S)
    tmpl = re.findall(r'return "(/[^"]+)"', body.group(0)) if body else []
    other = ["/other"] if body and '"/other"' in body.group(0) else []
    return sorted(set(lits + tmpl + other))


def families_from_source(root):
    """Each writeDataPoint call's first blob, when it is a string literal: the family's name. Comments may sit between the
    call and its blobs (the ota call has three lines of them), so the call is read up to its own first `blobs:`."""
    fams = set()
    for dp, _, fs in os.walk(os.path.join(root, "proxy", "src")):
        for f in fs:
            if not f.endswith(".ts"):
                continue
            text = open(os.path.join(dp, f), encoding="utf-8").read()
            for m in re.finditer(r"writeDataPoint\(", text):
                b = re.search(r'blobs:\s*\[\s*(")?(\w*)', text[m.end():m.end() + 800])
                if b and b.group(1):
                    fams.add(b.group(2))
    return sorted(fams)


class FakeSql:
    """Answers the tool's queries from planted points, applying the query's own WHERE clauses. A planted window is found by its
    END time and by whether the query carries the fleet filter (C3 has none; B has it; both end at T)."""
    def __init__(self, T, plant):
        self.W, self.plant = windows(T), plant

    def __call__(self, q):
        fleet = "blob5 != ''" in q
        end = re.findall(r"timestamp < toDateTime\('([^']+)'\)", q)[0]
        key = next((k for k, (a, b) in self.W.items() if ts(b) == end and (k == "C3") != fleet), None)
        pts = list(self.plant.get(key, []))
        if fleet:
            excl = re.search(r"NOT IN \('([^']*)', '([^']*)'\)", q).groups()
            pts = [p for p in pts if p.get("d") and p["d"] not in excl]
        if "SUM(" in q:
            agg = {}
            for p in pts:
                k = (p["route"], p["status"])
                agg[k] = None if (p["n"] is None or agg.get(k, 0) is None) else agg.get(k, 0) + p["n"]
            return [{"route": r, "status": s, "n": n} for (r, s), n in agg.items()]
        return [{"route": r, "d": d} for r, d in sorted({(p["route"], p.get("d", "")) for p in pts})]


def selftest():
    T = datetime(2026, 11, 25, 14, 0, 0, tzinfo=timezone.utc)
    EXCL = ("excl-x1", "excl-x2")
    DEVS = ["dev-a", "dev-b", "dev-c", "dev-d", "dev-e", "dev-f"]
    attempts = {"n": 0}

    def blocked(*a, **k):
        attempts["n"] += 1
        raise RuntimeError("network blocked in the selftest")
    urllib.request.urlopen = blocked
    try:
        urllib.request.urlopen("https://example.com/")
    except RuntimeError:
        pass
    ctl_attempts = attempts["n"]
    results = []

    def row(name, ok, detail=""):
        results.append(ok)
        print(f"{'PASS' if ok else 'FAIL'} {name}{': ' + detail if detail else ''}")

    def pt(route, d, n=1, status=200):
        return {"route": route, "status": status, "n": n, "d": d}

    def clean(**over):
        p = {"B": [pt(BLIPS, d, 10) for d in DEVS], "A": [pt(BLIPS, d, 10) for d in DEVS],
             "FF": [pt(BLIPS, d, 2) for d in DEVS], "C3": [pt(BLIPS, EXCL[0], 1, 404)], "EMPTY": []}
        for k, v in over.items():
            p[k] = v
        return p

    def run(plant):
        out = read(T, FakeSql(T, plant), EXCL)
        return out, exit_code(out)

    row("ST0 the network block fires on its own control", ctl_attempts == 1, f"attempts {ctl_attempts}")
    W = windows(T)
    m = timedelta(minutes=1)
    row("ST1 the windows", W["B"] == (T - 60 * m, T) and W["A"] == (T + 5 * m, T + 65 * m) and W["FF"] == (T, T + 15 * m)
        and W["C3"] == (T - 1440 * m, T) and W["EMPTY"] == (T - timedelta(days=365) - 60 * m, T - timedelta(days=365)))

    src = open(os.path.join(REPO, "proxy", "src", "metrics.ts"), encoding="utf-8").read()
    routes = routes_from_source(src)
    base = tally([pt(r, "dev-a") for r in routes])
    row("ST2 every route the proxy writes is counted", len(routes) >= 10 and base["requests"] == len(routes),
        f"routes parsed {len(routes)}, counted {base['requests']}")
    row("ST2b /blips and both basemap routes are in the parsed routes",
        all(r in routes for r in (BLIPS,) + BASEMAP_ROUTES), f"missing {[r for r in (BLIPS,) + BASEMAP_ROUTES if r not in routes]}")

    fams = families_from_source(REPO)
    planted = [pt(r, "dev-a") for r in routes] + [{"route": f, "status": 18, "n": 150000, "d": "POWERON"} for f in fams]
    with_fams = tally(planted)
    row("ST3 every other family is excluded, counts and devices unchanged",
        len(fams) >= 7 and with_fams == base and device_set(planted) == device_set([pt(r, "dev-a") for r in routes]),
        f"families parsed {len(fams)}")

    out, rc = run(clean())
    text = json.dumps(out)
    row("ST4 no device id and no excluded id in the output", not any(x in text for x in DEVS + list(EXCL)))

    row("ST5 |B \\ A|", diff(set(DEVS[:5]), set(DEVS[:4])) == 1 and diff(set(DEVS[:4]), set(DEVS)) == 0)
    row("ST6 C2 against an empty window", out["controls"]["C2"] is True and diff(set(DEVS), set()) == len(DEVS))

    st7 = [rc == 0 and out["verdict"] == "AS STATED"]
    o, r = run(clean(A=clean()["A"] + [pt(BLIPS, "dev-a", 1, 503)]))
    st7.append(r == 1 and o["rows"]["V6-1"] is False and o["rollback"] == "R-1 at once")
    o, r = run(clean(A=clean()["A"] + [pt(BLIPS, "dev-a", 1, 404)]))
    st7.append(r == 1 and o["rows"]["V6-2"] is False)
    o, r = run(clean(A=[pt(BLIPS, d, 10) for d in DEVS[:5]] + [pt(BLIPS, "dev-a", 10)]))
    st7.append(r == 1 and o["rows"]["V6-3"] is False and o["b_not_a"] == 1)
    for a_blips, ok in ((39, False), (40, True), (250, True), (251, False)):
        o, r = run(clean(B=[pt(BLIPS, d, 0) for d in DEVS] + [pt(BLIPS, "dev-a", 100)],
                         A=[pt(BLIPS, d, 0) for d in DEVS] + [pt(BLIPS, "dev-a", a_blips)]))
        st7.append(o["rows"].get("V6-4") is ok and r == (0 if ok else 1))
    o, r = run(clean(A=clean()["A"] + [pt(BASEMAP, "dev-a", 1)]))
    st7.append(r == 1 and o["rows"]["V6-5"] is False)
    o, r = run(clean(B=clean()["B"] + [pt(BLIPS, "dev-a", 1, 404)]))
    st7.append(r == 1 and o["verdict"] == "OUTSIDE")
    o, r = run(clean(FF=clean()["FF"] + [pt(BLIPS, "dev-a", 1, 401)]))
    st7.append(o["rollback"] == "R-1 at once")
    row("ST7 grading", all(st7), f"{sum(st7)} of {len(st7)} cases")

    st8 = []
    o, r = run(clean(B=[pt(BLIPS, d, 10) for d in DEVS[:3]]))
    st8.append(r == 2 and o["verdict"] == "BLIND")
    o, r = run(clean(C3=[]))
    st8.append(r == 2)
    o, r = run(clean(A=clean()["A"] + [{"route": BLIPS, "status": 200, "n": None, "d": "dev-a"}]))
    st8.append(r == 2 and "A.requests" in o.get("not_a_value", []))
    o, r = run(clean(B=[pt("/api/v1/blipscope/config", d, 10) for d in DEVS]))
    st8.append(r == 2)
    row("ST8 BLIND", all(st8), f"{sum(st8)} of {len(st8)} cases")

    o1, _ = run(clean())
    o2, _ = run(clean())
    o3, _ = run(clean(A=clean()["A"] + [pt(BLIPS, "dev-a", 3)]))
    row("ST9 compare", compare(o1, o2)["compare"] == "AGREE" and compare(o1, o3) == {"compare": "OUTSIDE", "moved": ["A", "rows"]}
        or (compare(o1, o2)["compare"] == "AGREE" and compare(o1, o3)["compare"] == "OUTSIDE" and "A" in compare(o1, o3)["moved"]))
    row("ST10 no network attempt after the control", attempts["n"] == ctl_attempts, f"attempts {attempts['n'] - ctl_attempts}")
    ok = all(results)
    print(f"SELFTEST {'PASSED' if ok else 'FAILED'}: {sum(results)} of {len(results)} rows")
    return 0 if ok else 1


def main(argv):
    if argv[1:2] == ["--selftest"]:
        return selftest()
    if len(argv) == 3 and argv[1] == "read":
        T = datetime.strptime(argv[2], "%Y-%m-%dT%H:%M:%SZ").replace(tzinfo=timezone.utc)
        try:
            sql, excl = live()
            out = read(T, sql, excl)
        except Exception as e:  # the message, never a value
            print(json.dumps({"verdict": "ERROR", "error": type(e).__name__}))
            return 3
        print(json.dumps(out, sort_keys=True))
        return exit_code(out)
    if len(argv) == 4 and argv[1] == "compare":
        r1, r2 = (json.load(open(p, encoding="utf-8")) for p in argv[2:4])
        res = compare(r1, r2)
        print(json.dumps(res))
        return 0 if res["compare"] == "AGREE" else 1
    print(__doc__)
    return 3


if __name__ == "__main__":
    sys.exit(main(sys.argv))
