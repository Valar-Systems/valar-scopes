"""The proxy bill's KV model read and its grading (docs/proxy-bill.md, amendment (5)). Read-only: tokens come from
the token file (analytics-read, kv-read) and are never printed. Counts and sums only: no key, value, device id or hex
is printed or written.

  python -I kv_model_read.py read <from ISO> <to ISO> <out.json> <lb:dev count>,<lb:firsttype count>   one window [from, to)
  python -I kv_model_read.py grade <read.json>                      KM1', KM1'', KM2', KM2'', KM3, as registered

Exit 0 the read or the grading ran; 2 the rig (a token, a query or the schema could not be read).
"""
import json
import os
import sys
import urllib.parse
import urllib.request

ACCOUNT = "48822e896bb10c45aa6bfe139bcff3d1"          # proxy/wrangler.toml (public)
PROD_NS = "733bf90056104f56be9492e62450a0bf"          # production ENRICH_KV, proxy/wrangler.toml (public)
DATASET = "blipscope_proxy"                           # the METRICS binding's dataset

import importlib.util as _ilu   # the token-file helper, by path: python -I drops this folder from sys.path
_tfs = _ilu.spec_from_file_location("token_file", os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "token_file.py"))
TOKEN_FILE = _ilu.module_from_spec(_tfs); _tfs.loader.exec_module(TOKEN_FILE)   # docs/token-file-python.md

# The route groups of docs/proxy-bill.md's table: AE index1 -> group.
GROUPS = {
    "blips": ["/api/v1/blipscope/blips", "/v1/blips"],
    "config": ["/api/v1/blipscope/config", "/v1/config"],
    "airports": ["/api/v1/blipscope/airports", "/v1/airports"],
    "enrich": ["/api/v1/blipscope/enrich", "/v1/enrich"],
    "photo": ["/api/v1/blipscope/photo", "/v1/photo"],
    "wx": ["/api/v1/blipscope/wx/" + p for p in
           ("cref", "opuk", "opis", "opib", "opmt", "opwe", "opce", "opse", "opba", "opsw", "opsn", "shsr")],
    "weatherscope": ["/api/v1/weatherscope/alerts", "/api/v1/weatherscope/conditions", "/api/v1/weatherscope/forecast"],
    "lb_submit": ["/api/v1/blipscope/leaderboard"],
    "lb_pages": ["/blipscope/leaderboard", "/blipscope/leaderboard.json", "/leaderboard", "/leaderboard.json",
                 "/v1/leaderboard"],
    "enroll": ["/blipscope/enroll", "/enroll"],
    "pages": ["/", "/blipscope/support", "/other"],
}
ROUTE_GROUP = {r: g for g, rs in GROUPS.items() for r in rs}
DEVICE_GROUPS = ("blips", "config", "airports", "enrich", "photo", "wx", "weatherscope", "lb_submit")


def rig(msg):
    print("RIG: " + msg)
    sys.exit(2)


def post_json(url, body, tok, raw=False):
    data = body.encode() if raw else json.dumps(body).encode()
    req = urllib.request.Request(url, data=data, method="POST",
                                 headers={"Authorization": "Bearer " + tok,
                                          "Content-Type": "text/plain" if raw else "application/json"})
    try:
        with urllib.request.urlopen(req, timeout=120) as r:
            return json.loads(r.read())
    except Exception as e:   # the error's class only: its text can carry a URL
        rig("a query failed (%s)" % type(e).__name__)


def gql(query, variables, tok):
    out = post_json("https://api.cloudflare.com/client/v4/graphql", {"query": query, "variables": variables}, tok)
    if out.get("errors"):
        rig("GraphQL returned %d error(s)" % len(out["errors"]))
    return out["data"]


def ae(sql, tok):
    out = post_json("https://api.cloudflare.com/client/v4/accounts/%s/analytics_engine/sql" % ACCOUNT, sql, tok, raw=True)
    return out.get("data", [])


def kv_key_count(prefix, tok):
    n, cursor = 0, ""
    while True:
        q = {"prefix": prefix, "limit": "1000"}
        if cursor:
            q["cursor"] = cursor
        url = "https://api.cloudflare.com/client/v4/accounts/%s/storage/kv/namespaces/%s/keys?%s" % (
            ACCOUNT, PROD_NS, urllib.parse.urlencode(q))
        req = urllib.request.Request(url, headers={"Authorization": "Bearer " + tok})
        try:
            with urllib.request.urlopen(req, timeout=60) as r:
                out = json.loads(r.read())
        except Exception as e:
            rig("the key list failed (%s)" % type(e).__name__)
        n += len(out.get("result", []))
        cursor = (out.get("result_info") or {}).get("cursor") or ""
        if not cursor:
            return n


def read(a, b, path, lb_counts):
    # The leaderboard's key counts come in as numbers, counted by `wrangler kv key list --remote` piped into a counter
    # (tools/token_file.py knows no KV-read purpose), so no key name ever reaches this script.
    at = TOKEN_FILE.token("analytics-read")
    out = {"window": {"from": a, "to": b}}
    # (iv) the schema first, with no data: does the KV operations dataset have a key dimension?
    sch = gql('{ __type(name: "AccountKvOperationsAdaptiveGroupsDimensions") { fields { name } } }', {}, at)
    t = sch.get("__type")
    out["kv_dimensions"] = sorted(f["name"] for f in t["fields"]) if t else None
    # KV operations: production ENRICH_KV, and the account
    q = ("query($acc: String!, $a: Time!, $b: Time!%s) { viewer { accounts(filter: {accountTag: $acc}) { "
         "kvOperationsAdaptiveGroups(limit: 1000, filter: {datetime_geq: $a, datetime_lt: $b%s}) "
         "{ sum { requests } dimensions { actionType } } } } }")
    v = {"acc": ACCOUNT, "a": a, "b": b}
    prod = gql(q % (", $ns: String!", ", namespaceId: $ns"), dict(v, ns=PROD_NS), at)
    acct = gql(q % ("", ""), v, at)
    def by_action(d):
        tot = {}
        for g in d["viewer"]["accounts"][0]["kvOperationsAdaptiveGroups"]:
            k = g["dimensions"]["actionType"]
            tot[k] = tot.get(k, 0) + g["sum"]["requests"]
        return tot
    out["kv_prod"], out["kv_account"] = by_action(prod), by_action(acct)
    # Analytics Engine: request rows by route (sum(_sample_interval x double4)), one window
    w = "timestamp >= toDateTime('%s') AND timestamp < toDateTime('%s')" % (a.replace("T", " ").rstrip("Z"),
                                                                            b.replace("T", " ").rstrip("Z"))
    routes = ae("SELECT index1 AS r, SUM(_sample_interval * double4) AS n FROM %s WHERE %s GROUP BY r FORMAT JSON"
                % (DATASET, w), at)
    out["routes"] = {row["r"]: float(row["n"]) for row in routes if row["r"] in ROUTE_GROUP}
    out["routes_other_index1"] = sorted(row["r"] for row in routes if row["r"] not in ROUTE_GROUP)
    enr = ae("SELECT blob2 AS c, SUM(_sample_interval * double4) AS n FROM %s WHERE %s AND index1 IN ('%s') "
             "GROUP BY c FORMAT JSON" % (DATASET, w, "','".join(GROUPS["enrich"])), at)
    out["enrich_cache"] = {row["c"]: float(row["n"]) for row in enr}
    gaps = ae("SELECT blob2 AS g, SUM(_sample_interval) AS n FROM %s WHERE %s AND index1 = 'enrich_gap' "
              "GROUP BY g FORMAT JSON" % (DATASET, w), at)
    out["enrich_gaps"] = {row["g"]: float(row["n"]) for row in gaps}
    dev_routes = [r for g in DEVICE_GROUPS for r in GROUPS[g]]
    dv = ae("SELECT COUNT(DISTINCT blob5) AS d FROM %s WHERE %s AND blob5 != '' AND index1 IN ('%s') FORMAT JSON"
            % (DATASET, w, "','".join(dev_routes)), at)
    out["n_dev"] = int(dv[0]["d"]) if dv else None
    out["l_dev"], out["l_type"] = lb_counts
    json.dump(out, open(path, "w"), indent=1)
    print("read %s .. %s: kv dimensions %s; prod kv %s; account kv %s; %d route(s) known, %d other index1; "
          "enrich cache states %d; gap kinds %d; devices %s; lb rows %d / %d" % (
              a, b, out["kv_dimensions"], out["kv_prod"], out["kv_account"], len(out["routes"]),
              len(out["routes_other_index1"]), len(out["enrich_cache"]), len(out["enrich_gaps"]), out["n_dev"],
              out["l_dev"], out["l_type"]))


def grade(path):
    d = json.load(open(path))
    R = {g: sum(d["routes"].get(r, 0.0) for r in rs) for g, rs in GROUPS.items()}
    Ld, Lt = d["l_dev"], d["l_type"]
    # reads per request [low, high], docs/proxy-bill.md's table (lb_submit's T taken as [0, Lt])
    per = {"blips": (1, 2), "config": (2, 3), "airports": (1, 32), "enrich": (1, 16), "photo": (2, 3), "wx": (1, 2),
           "weatherscope": (0, 2), "lb_submit": (3, 3 + Lt + Ld + Lt), "lb_pages": (1, 1 + Ld + Lt),
           "enroll": (0, 7), "pages": (0, 2)}
    lo = sum(R[g] * per[g][0] for g in per)
    hi = sum(R[g] * per[g][1] for g in per)
    meas = d["kv_prod"].get("read", 0)
    acct = d["kv_account"].get("read", 0)
    mid = (lo + hi) / 2
    km1 = 0.8 * lo <= meas <= 1.2 * hi
    xmin = (1.2 * hi - meas) / R["blips"] if R["blips"] else float("nan")
    print("window %s .. %s; requests by group: %s" % (d["window"]["from"], d["window"]["to"],
          {g: round(v) for g, v in R.items()}))
    print("KM1'  measured reads %d (account %d); band [0.8 x %d, 1.2 x %d] = [%d, %d]; width %.0f%% of the midpoint; "
          "%s; x_min %.2f reads a /blips request" % (meas, acct, lo, hi, 0.8 * lo, 1.2 * hi, 100 * (1.2 * hi - 0.8 * lo) / mid,
          "PASS" if km1 else "FAIL", xmin))
    # KM1'': point rates where AE records the branch; range midpoints elsewhere
    ec = d["enrich_cache"]; hit, miss, skip = ec.get("HIT", 0), ec.get("MISS", 0), ec.get("SKIP", 0)
    looked = hit + miss
    gaps = d["enrich_gaps"]; g_type, g_name, g_photo = gaps.get("type", 0), gaps.get("name", 0), gaps.get("photo", 0)
    g_none = max(0.0, looked - g_type - g_name - g_photo)
    floor = 1 + 0.5                                       # fw: + revocation's [0, 1] midpoint
    photo = (g_type * 1 + g_photo * 2.5 + (g_name + g_none) * 2) / looked if looked else 2
    enrich_looked = floor + 1 + 1.5 + 1 + 1.5 + 0.5 + photo   # ac:, route+mirror mid, ap: mid, ovr/pa/mil mid, tn: mid
    enrich_reads = skip * floor + hit * enrich_looked + miss * (enrich_looked + 1)   # + tn: on a miss, [0, 2] mid
    other = sum(R[g] * (per[g][0] + per[g][1]) / 2 for g in per if g != "enrich")
    hyb = enrich_reads + other
    km1h = abs(hyb - meas) <= 0.2 * meas if meas else False
    print("KM1'' hybrid %d against measured %d: %+.0f%% (want within +/-20%%): %s; enrich: HIT %d, MISS %d, SKIP %d; "
          "gaps type %d, name %d, photo %d" % (hyb, meas, 100 * (hyb - meas) / meas if meas else 0,
          "PASS" if km1h else "FAIL", hit, miss, skip, g_type, g_name, g_photo))
    # KM2' and KM2'': writes
    H = 7 * 24
    wlo = R["lb_submit"] * 2
    whi = d["n_dev"] * H + R["enrich"] * 2 + R["lb_submit"] * (2 + Lt) + R["enroll"] * 3 + R["pages"] * 3
    wmeas = d["kv_prod"].get("write", 0)
    km2 = 0.8 * wlo <= wmeas <= 1.2 * whi
    print("KM2'  measured writes %d (account %d); band [%d, %d]; %s" % (wmeas, d["kv_account"].get("write", 0),
          0.8 * wlo, 1.2 * whi, "PASS" if km2 else "FAIL"))
    whyb = miss + R["enrich"] * 0.5 + d["n_dev"] * H + R["lb_submit"] * (2 + Lt / 2)
    km2h = abs(whyb - wmeas) <= 0.2 * wmeas if wmeas else False
    print("KM2'' hybrid writes %d against measured %d: %+.0f%%: %s (fw: at its bound, N_dev %d x %d h)" % (
          whyb, wmeas, 100 * (whyb - wmeas) / wmeas if wmeas else 0, "PASS" if km2h else "FAIL", d["n_dev"], H))
    # KM3: the model's family shares at both ends
    dev = sum(R[g] for g in DEVICE_GROUPS if g != "weatherscope")
    fw_reads = dev                                         # exactly 1 a device request at both ends
    print("KM3   fw: %.0f%% of the low end, %.0f%% of the high end: %s (a claim about the model; kv dimensions %s)" % (
          100 * fw_reads / lo if lo else 0, 100 * fw_reads / hi if hi else 0,
          "PASS" if lo and hi and fw_reads / lo > 0.5 and fw_reads / hi > 0.5 else "FAIL", d["kv_dimensions"]))


if __name__ == "__main__":
    a = sys.argv[1:]
    if a[:1] == ["read"] and len(a) == 5:
        dv, ty = (int(x) for x in a[4].split(","))
        read(a[1], a[2], a[3], (dv, ty))
    elif a[:1] == ["grade"] and len(a) == 2:
        grade(a[1])
    else:
        print(__doc__)
        sys.exit(2)
