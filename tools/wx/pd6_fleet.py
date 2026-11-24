"""PD6 (docs/wx-opuk.md), adapted from N6 (docs/wx-server.md, f8c5751): the fleet's own traffic in a window, by S3′'s rules.
Change from N6: TWO devices excluded, E2:08 (records-device) and the test identity (test-device), both read from
the token file and never printed; the token is the token file's analytics-read.

Fleet = per-request ROUTE points (blob1 starts with '/') carrying a device id, the excluded devices left out. The rule
is POSITIVE: every other family any Worker writes into this dataset (ota, usage, boot, enrich_gap, provision, sitefeed,
err5xx) is excluded by not being a route, not by name. The earlier filter excluded only err5xx and an empty blob5, so it
admitted usage points (blob5 = their arity, "u8" or "u9", counted as a device), boot points (blob5 = a device id) and
ota points with a reset reason (blob5 = the reason; double4 = a byte count, counted as requests): docs/wx-opuk.md's
record, the 2026-11-24 re-grade. Counts are sample-weighted (_sample_interval x double4, the app's hit-sampling weight),
as P5.
The token and the excluded device id are read from the environment and never printed.
Usage: python -I pd6_fleet.py <from> <to>     times as 'YYYY-MM-DD HH:MM:SS' (UTC)
       python -I pd6_fleet.py --selftest      offline: the real queries run against planted rows in SQLite
"""
import json
import os
import sys

ACCT = "48822e896bb10c45aa6bfe139bcff3d1"
BLIPS = "/api/v1/blipscope/blips"


def where(t0, t1, excl):
    # substring, not startsWith: Analytics Engine's SQL reference lists substring (1-based) and not startsWith.
    return (f"timestamp >= toDateTime('{t0}') AND timestamp < toDateTime('{t1}') AND substring(blob1, 1, 1) = '/' "
            f"AND index1 != 'err5xx' AND blob5 != '' AND blob5 NOT IN ('{excl[0]}', '{excl[1]}')")


def queries(t0, t1, excl):
    W = where(t0, t1, excl)
    return (f"SELECT blob1 AS route, double1 AS status, blob3 AS upstream, SUM(_sample_interval * double4) AS n "
            f"FROM blipscope_proxy WHERE {W} GROUP BY route, status, upstream FORMAT JSON",
            f"SELECT count(DISTINCT blob5) AS d FROM blipscope_proxy WHERE {W} FORMAT JSON")


def summarize(t0, t1, rows, devs):
    tot = lambda f: round(sum(float(r["n"]) for r in rows if f(r)))  # noqa: E731
    out = {
        "window": [t0, t1],
        "fleet_requests": tot(lambda r: True),
        "fleet_devices": int(devs[0]["d"]) if devs else None,
        "fleet_blips": tot(lambda r: r["route"] == BLIPS),
        "fleet_4xx": tot(lambda r: 400 <= float(r["status"]) < 500),
        "fleet_5xx_blips_503_no_upstream": tot(lambda r: r["route"] == BLIPS and float(r["status"]) == 503 and not r["upstream"]),
        "fleet_5xx_other": tot(lambda r: float(r["status"]) >= 500 and not (r["route"] == BLIPS and float(r["status"]) == 503
                                                                            and not r["upstream"])),
    }
    out["no_upstream_503_share_of_blips"] = (round(out["fleet_5xx_blips_503_no_upstream"] / out["fleet_blips"], 5)
                                             if out["fleet_blips"] else None)
    out["readable"] = bool(rows) and out["fleet_blips"] > 0
    return out


def selftest():
    """The real queries, run in SQLite over planted rows: the fleet's route rows plus one point of EVERY family any
    Worker writes into this dataset (every writeDataPoint in proxy/src: ota, usage, boot, enrich_gap, provision,
    sitefeed, err5xx). Only route rows (blob1 starting with '/') are fleet traffic, so adding the seven family points
    must leave EVERY count unchanged. The expected counts are first checked on the route rows alone (the control)."""
    import sqlite3
    t0, t1, excl = "2026-01-01 00:00:00", "2026-01-01 00:30:00", ["excla", "exclb"]
    cols = "timestamp, index1, blob1, blob2, blob3, blob4, blob5, double1, double2, double3, double4, _sample_interval"
    inside, outside = "2026-01-01 00:10:00", "2026-01-01 00:40:00"
    route = [  # timestamp, index1, blob1..blob5, double1..double4, _sample_interval
        (inside, BLIPS, BLIPS, "hit", "adsb", "s3-128", "dev1", 200, 40, 30, 1, 1),
        (inside, BLIPS, BLIPS, "hit", "adsb", "s3-128", "dev2", 200, 40, 30, 10, 1),
        (inside, BLIPS, BLIPS, "miss", "", "s3-128", "dev3", 503, 5, 0, 1, 1),
        (inside, "/api/v1/enrich/:hex", "/api/v1/enrich/:hex", "", "kv", "s3-128", "dev2", 404, 3, 0, 1, 1),
        (inside, BLIPS, BLIPS, "hit", "adsb", "s3-128", "excla", 200, 40, 30, 1, 1),   # an excluded device
        (outside, BLIPS, BLIPS, "hit", "adsb", "s3-128", "dev1", 200, 40, 30, 1, 1),  # outside the window
    ]
    families = [  # one point of each family, shaped as proxy/src writes it (absent blobs are '', absent doubles 0)
        (inside, "ota", "ota", "ok", "s3-128", "dev1", "POWERON", 18, 19, 120000, 64000, 1),
        (inside, "usage", "usage", "s3-128", "19", "dev1", "u9", 3, 2, 1, 5, 1),
        (inside, "boot", "boot", "SW", "s3-128", "19", "dev9", 0, 0, 0, 0, 1),
        (inside, "enrich_gap", "enrich_gap", "no_type", "B739", "a4b420", "", 1, 0, 0, 0, 1),
        (inside, "provision", "provision", "machash", "operator", "dev7", "", 2, 0, 0, 0, 1),
        (inside, "sitefeed", "sitefeed", "poll", "", "", "", 1, 0, 0, 0, 1),
        (inside, "err5xx", "err5xx", BLIPS, "", "LHR", "", 503, 0, 0, 0, 1),
    ]
    want = {"window": [t0, t1], "fleet_requests": 13, "fleet_devices": 3, "fleet_blips": 12, "fleet_4xx": 1,
            "fleet_5xx_blips_503_no_upstream": 1, "fleet_5xx_other": 0, "no_upstream_503_share_of_blips": round(1 / 12, 5),
            "readable": True}

    def run(points):
        db = sqlite3.connect(":memory:")
        db.create_function("toDateTime", 1, lambda s: s)
        db.execute(f"CREATE TABLE blipscope_proxy ({cols})")
        db.executemany(f"INSERT INTO blipscope_proxy VALUES ({', '.join('?' * 12)})", points)
        db.row_factory = sqlite3.Row
        q_rows, q_devs = (q.replace(" FORMAT JSON", "") for q in queries(t0, t1, excl))
        rows = [dict(r) for r in db.execute(q_rows)]
        devs = [dict(r) for r in db.execute(q_devs)]
        return summarize(t0, t1, rows, devs)

    fails = 0
    got = run(route)
    ok = got == want
    print(("PASS" if ok else "FAIL") + " T0 CONTROL: the route rows alone give the expected counts")
    if not ok:
        print("  got  " + json.dumps(got)); print("  want " + json.dumps(want)); fails += 1
    for fam in families:
        got = run(route + [fam])
        ok = got == want
        print(("PASS" if ok else "FAIL") + f" T1 a {fam[1]} point leaves every count unchanged")
        if not ok:
            print("  got  " + json.dumps(got)); fails += 1
    got = run(route + families)
    ok = got == want
    print(("PASS" if ok else "FAIL") + " T2 all seven family points together leave every count unchanged")
    if not ok:
        print("  got  " + json.dumps(got)); fails += 1
    print(f"selftest: {'all 9 passed' if fails == 0 else str(fails) + ' of 9 FAILED'}")
    return 0 if fails == 0 else 1


def main(t0, t1):
    import importlib.util as _ilu   # the token-file helper, by path: python -I drops this folder from sys.path
    import urllib.request
    _tfs = _ilu.spec_from_file_location("token_file", os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "token_file.py"))
    TOKEN_FILE = _ilu.module_from_spec(_tfs); _tfs.loader.exec_module(TOKEN_FILE)   # docs/token-file-python.md
    _t = TOKEN_FILE.tokens("analytics-read", "records-device", "test-device")
    tok = _t["analytics-read"]
    excl = [_t["records-device"], _t["test-device"]]
    if not all(all(c.isalnum() for c in e) for e in excl):
        raise SystemExit("unexpected characters in an excluded device id")

    def sql(q):
        req = urllib.request.Request(f"https://api.cloudflare.com/client/v4/accounts/{ACCT}/analytics_engine/sql",
                                     data=q.encode(), headers={"Authorization": f"Bearer {tok}"}, method="POST")
        with urllib.request.urlopen(req, timeout=120) as r:
            return json.loads(r.read())["data"]

    q_rows, q_devs = queries(t0, t1, excl)
    print(json.dumps(summarize(t0, t1, sql(q_rows), sql(q_devs))))
    return 0


if __name__ == "__main__":
    if sys.argv[1:] == ["--selftest"]:
        sys.exit(selftest())
    sys.exit(main(sys.argv[1], sys.argv[2]))
