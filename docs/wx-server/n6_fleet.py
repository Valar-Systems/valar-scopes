"""N6 (docs/wx-server.md, frozen at f8c5751): the fleet's own traffic in a window, by S3′'s rules.

Fleet = per-request points carrying a device id, the smoke credential's device excluded; err5xx points are their own
family and excluded. Counts are sample-weighted (_sample_interval x double4, the app's hit-sampling weight), as P5.
The token and the excluded device id are read from the environment and never printed.
Usage: python -I n6_fleet.py <from ISO> <to ISO>      (env: AE_TOKEN, EXCL_DEV)
"""
import json
import os
import sys
import urllib.request

ACCT = "48822e896bb10c45aa6bfe139bcff3d1"
tok, excl = os.environ["AE_TOKEN"], os.environ["EXCL_DEV"]
t0, t1 = sys.argv[1], sys.argv[2]
if not all(c.isalnum() for c in excl):
    raise SystemExit("unexpected characters in the excluded device id")


def sql(q):
    req = urllib.request.Request(f"https://api.cloudflare.com/client/v4/accounts/{ACCT}/analytics_engine/sql",
                                 data=q.encode(), headers={"Authorization": f"Bearer {tok}"}, method="POST")
    with urllib.request.urlopen(req, timeout=120) as r:
        return json.loads(r.read())["data"]


W = (f"timestamp >= toDateTime('{t0}') AND timestamp < toDateTime('{t1}') AND index1 != 'err5xx' "
     f"AND blob5 != '' AND blob5 != '{excl}'")
rows = sql(f"SELECT blob1 AS route, double1 AS status, blob3 AS upstream, SUM(_sample_interval * double4) AS n "
           f"FROM blipscope_proxy WHERE {W} GROUP BY route, status, upstream FORMAT JSON")
devs = sql(f"SELECT count(DISTINCT blob5) AS d FROM blipscope_proxy WHERE {W} FORMAT JSON")
blips = "/api/v1/blipscope/blips"
tot = lambda f: round(sum(float(r["n"]) for r in rows if f(r)))  # noqa: E731
out = {
    "window": [t0, t1],
    "fleet_requests": tot(lambda r: True),
    "fleet_devices": int(devs[0]["d"]) if devs else None,
    "fleet_blips": tot(lambda r: r["route"] == blips),
    "fleet_4xx": tot(lambda r: 400 <= float(r["status"]) < 500),
    "fleet_5xx_blips_503_no_upstream": tot(lambda r: r["route"] == blips and float(r["status"]) == 503 and not r["upstream"]),
    "fleet_5xx_other": tot(lambda r: float(r["status"]) >= 500 and not (r["route"] == blips and float(r["status"]) == 503
                                                                        and not r["upstream"])),
}
out["no_upstream_503_share_of_blips"] = (round(out["fleet_5xx_blips_503_no_upstream"] / out["fleet_blips"], 5)
                                         if out["fleet_blips"] else None)
out["readable"] = bool(rows) and out["fleet_blips"] > 0
print(json.dumps(out))
