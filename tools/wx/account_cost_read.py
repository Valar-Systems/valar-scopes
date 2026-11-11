"""The whole account's monthly cost (docs/wx-account-cost.md). Read-only: tokens come from the token file
(analytics-read, observability-read) and are never printed. No event, key or value is printed: counts, sums and byte
totals only.

  python -I account_cost_read.py read <out.json>                 the read, rows A1-A9 (30 days; run rate = last 7 x 30/7)
  python -I account_cost_read.py model <read.json> <out.json>    section 4's model: the scenario table, Read P and Read A

cost_read.py's queries are kept as they are (Workers by script, R2 by action and bucket) and the classes are its own.
"""
import json
import math
import os
import sys
import urllib.request
from datetime import datetime, timedelta, timezone

ACCOUNT = "48822e896bb10c45aa6bfe139bcff3d1"   # proxy/wrangler.toml (public)
CLASS_A = {"ListBuckets", "PutBucket", "ListObjects", "PutObject", "CopyObject", "CompleteMultipartUpload",
           "CreateMultipartUpload", "ListMultipartUploads", "UploadPart", "UploadPartCopy", "ListParts",
           "PutBucketEncryption", "PutBucketCors", "PutBucketLifecycleConfiguration", "LifecycleStorageTierTransition"}
CLASS_B = {"HeadBucket", "HeadObject", "GetObject", "UsageSummary", "GetBucketEncryption", "GetBucketLocation",
           "GetBucketCors", "GetBucketLifecycleConfiguration"}
FREE = {"DeleteObject", "DeleteObjects", "DeleteBucket", "AbortMultipartUpload"}


def token(name):
    for line in open(os.path.expanduser("~/.config/blipscope/tokens"), encoding="utf-8"):
        k, _, v = line.strip().partition("=")
        if k == name and v:
            return v
    raise SystemExit(f"{name} absent from the token file")


def post(url, body, tok):
    req = urllib.request.Request(url, data=json.dumps(body).encode(), method="POST",
                                 headers={"Authorization": "Bearer " + tok, "Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=120) as r:
        return json.loads(r.read())


def gql(q, v):
    d = post("https://api.cloudflare.com/client/v4/graphql", {"query": q, "variables": v}, token("analytics-read"))
    if d.get("errors"):
        raise SystemExit("GraphQL errors: " + json.dumps(d["errors"])[:400])
    return d["data"]["viewer"]["accounts"][0]


iso = lambda t: t.isoformat().replace("+00:00", "Z")
Q = 'query($acct: String!, $a: Time!, $b: Time!) { viewer { accounts(filter: {accountTag: $acct}) { %s } } }'


def read(out_path):
    now = datetime.now(timezone.utc).replace(microsecond=0)
    start = now - timedelta(days=30)
    days = [(start + timedelta(days=d), start + timedelta(days=d + 1)) for d in range(30)]
    last7 = range(23, 30)
    acc = {}   # (line, key, day) -> value

    def add(line, key, d, v):
        acc[(line, key, d)] = acc.get((line, key, d), 0) + (v or 0)

    for d, (a, b) in enumerate(days):
        v = {"acct": ACCOUNT, "a": iso(a), "b": iso(b)}
        w = gql(Q % """workersInvocationsAdaptive(limit: 1000, filter: {datetime_geq: $a, datetime_lt: $b}) {
                 sum { cpuTimeUs requests } dimensions { scriptName } }""", v)
        for g in w["workersInvocationsAdaptive"]:
            add("requests", g["dimensions"]["scriptName"], d, g["sum"]["requests"])
            add("cpu_ms", g["dimensions"]["scriptName"], d, g["sum"]["cpuTimeUs"] / 1000.0)
        r = gql(Q % """r2OperationsAdaptiveGroups(limit: 1000, filter: {datetime_geq: $a, datetime_lt: $b}) {
                 sum { requests } dimensions { actionType bucketName } }""", v)
        for g in r["r2OperationsAdaptiveGroups"]:
            act = g["dimensions"]["actionType"]
            c = "class_a" if act in CLASS_A else "class_b" if act in CLASS_B else "r2_free" if act in FREE else "r2_unclassed:" + act
            add(c, g["dimensions"]["bucketName"], d, g["sum"]["requests"])
        k = gql(Q % """kvOperationsAdaptiveGroups(limit: 1000, filter: {datetime_geq: $a, datetime_lt: $b}) {
                 sum { requests } dimensions { actionType namespaceId } }""", v)
        for g in k["kvOperationsAdaptiveGroups"]:
            add("kv_" + str(g["dimensions"]["actionType"]), g["dimensions"]["namespaceId"], d, g["sum"]["requests"])
        o = gql(Q % """durableObjectsInvocationsAdaptiveGroups(limit: 1000, filter: {datetime_geq: $a, datetime_lt: $b}) {
                 sum { requests } dimensions { scriptName } }
               durableObjectsPeriodicGroups(limit: 1000, filter: {datetime_geq: $a, datetime_lt: $b}) {
                 sum { activeTime duration rowsRead rowsWritten storageReadUnits storageWriteUnits storageDeletes } dimensions { namespaceId } }""", v)
        for g in o["durableObjectsInvocationsAdaptiveGroups"]:
            add("do_requests", g["dimensions"]["scriptName"], d, g["sum"]["requests"])
        for g in o["durableObjectsPeriodicGroups"]:
            for f in ("activeTime", "duration", "rowsRead", "rowsWritten", "storageReadUnits", "storageWriteUnits", "storageDeletes"):
                add("do_" + f, g["dimensions"]["namespaceId"], d, g["sum"][f])
        try:
            e = gql(Q % """workersAnalyticsEngineAdaptiveGroups(limit: 100, filter: {datetime_geq: $a, datetime_lt: $b}) {
                     count dimensions { dataset } }""", v)
            for g in e["workersAnalyticsEngineAdaptiveGroups"]:
                add("ae_points", g["dimensions"]["dataset"], d, g["count"])
        except SystemExit as err:
            acc[("ae_unreadable", str(err)[:120], d)] = 1

    def month(line, keys=None):
        """The run rate a month: the last 7 slices x 30/7."""
        return sum(v for (ln, k, d), v in acc.items() if ln == line and d in last7 and (keys is None or k in keys)) * 30 / 7

    def by_key(line):
        out = {}
        for (ln, k, d), v in acc.items():
            if ln == line and d in last7:
                out[k] = out.get(k, 0) + v * 30 / 7
        return {k: round(v) for k, v in sorted(out.items(), key=lambda x: -x[1])}

    # storage: the largest reading of the last day
    a, b = now - timedelta(days=1), now
    v = {"acct": ACCOUNT, "a": iso(a), "b": iso(b)}
    s = gql(Q % """r2StorageAdaptiveGroups(limit: 1000, filter: {datetime_geq: $a, datetime_lt: $b}) {
               max { payloadSize metadataSize objectCount } dimensions { bucketName storageClass } }
             kvStorageAdaptiveGroups(limit: 1000, filter: {datetime_geq: $a, datetime_lt: $b}) {
               max { byteCount keyCount } dimensions { namespaceId } }
             durableObjectsStorageGroups(limit: 100, filter: {datetime_geq: $a, datetime_lt: $b}) { max { storedBytes } }
             durableObjectsSqlStorageGroups(limit: 100, filter: {datetime_geq: $a, datetime_lt: $b}) { max { storedBytes } dimensions { namespaceId } }""", v)
    r2_store = {}
    for g in s["r2StorageAdaptiveGroups"]:
        key = g["dimensions"]["bucketName"]
        r2_store[key] = max(r2_store.get(key, 0), (g["max"]["payloadSize"] or 0) + (g["max"]["metadataSize"] or 0))
    kv_store = {}
    for g in s["kvStorageAdaptiveGroups"]:
        kv_store[g["dimensions"]["namespaceId"]] = max(kv_store.get(g["dimensions"]["namespaceId"], 0), g["max"]["byteCount"] or 0)
    do_store = max([g["max"]["storedBytes"] or 0 for g in s["durableObjectsStorageGroups"]] or [0]) + \
        sum(g["max"]["storedBytes"] or 0 for g in s["durableObjectsSqlStorageGroups"])

    # Workers Logs: one full hour, unsampled or refused; events counted and sized, never printed
    h1 = now.replace(minute=0, second=0) - timedelta(hours=1)
    h2 = h1 + timedelta(hours=1)
    ev_n, ev_bytes, abr, offset = {}, {}, set(), None
    while True:
        body = {"queryId": "uk-account-cost-logs", "timeframe": {"from": int(h1.timestamp() * 1000), "to": int(h2.timestamp() * 1000)},
                "view": "events", "limit": 2000, "parameters": {}}
        if offset:
            body["offset"] = offset
        r = post(f"https://api.cloudflare.com/client/v4/accounts/{ACCOUNT}/workers/observability/telemetry/query", body,
                 token("observability-read"))["result"]
        abr.add(r.get("statistics", {}).get("abr_level"))
        page = r["events"]["events"]
        for e in page:
            svc = (e.get("$metadata") or {}).get("service", "?")
            ev_n[svc] = ev_n.get(svc, 0) + 1
            ev_bytes[svc] = ev_bytes.get(svc, 0) + len(json.dumps(e, separators=(",", ":")).encode())
        if len(page) < 2000:
            break
        offset = page[-1]["$metadata"]["id"]
    hv = {"acct": ACCOUNT, "a": iso(h1), "b": iso(h2)}
    hw = gql(Q % """workersInvocationsAdaptive(limit: 1000, filter: {datetime_geq: $a, datetime_lt: $b}) {
               sum { requests } dimensions { scriptName } }""", hv)
    inv_hour = {}
    for g in hw["workersInvocationsAdaptive"]:
        inv_hour[g["dimensions"]["scriptName"]] = inv_hour.get(g["dimensions"]["scriptName"], 0) + g["sum"]["requests"]

    req_by = by_key("requests")
    out = {
        "window": [iso(start), iso(now)], "run_rate": "last 7 slices x 30/7",
        "A1_requests": round(month("requests")), "requests_by_script": req_by,
        "A2_cpu_ms": round(month("cpu_ms")), "cpu_ms_by_script": by_key("cpu_ms"),
        "A3_class_a": round(month("class_a")), "class_a_by_bucket": by_key("class_a"),
        "A4_class_b": round(month("class_b")), "class_b_by_bucket": by_key("class_b"),
        "r2_unclassed": {ln: round(month(ln)) for ln in {k[0] for k in acc if k[0].startswith("r2_unclassed:")}},
        "A5_r2_storage_bytes": sum(r2_store.values()), "r2_storage_by_bucket": r2_store,
        "A6_kv": {ln: round(month(ln)) for ln in sorted({k[0] for k in acc if k[0].startswith("kv_")})},
        "A6_kv_storage_bytes": sum(kv_store.values()), "kv_storage_by_namespace": kv_store,
        # added after the first read (2026-11-11 05:15Z), to see whose KV operations they are; no graded value changes
        "kv_ops_by_namespace": {ln: by_key(ln) for ln in sorted({k[0] for k in acc if k[0].startswith("kv_")})},
        "A7_do": {ln: round(month(ln)) for ln in sorted({k[0] for k in acc if k[0].startswith("do_")})},
        "A7_do_gb_s_from_active_time": round(month("do_activeTime") / 1e6 * 0.128),
        "A7_do_storage_bytes": do_store,
        "A8_logs_hour": {"window": [iso(h1), iso(h2)], "abr_levels": sorted(x for x in abr if x is not None),
                         "events_by_service": ev_n, "bytes_by_service": ev_bytes, "invocations_by_script": inv_hour},
        "A9_ae_points": round(month("ae_points")), "ae_by_dataset": by_key("ae_points"),
        "ae_unreadable": sorted({k[1] for k in acc if k[0] == "ae_unreadable"}),
    }
    if out["A8_logs_hour"]["abr_levels"] not in ([1], []):
        out["A8_refused"] = "the hour came back sampled: not graded (R3's rule)"
    ev_total, inv_total = sum(ev_n.values()), sum(inv_hour.values())
    out["A8_events_per_invocation"] = round(ev_total / inv_total, 3) if inv_total else None
    out["A8_bytes_per_event"] = round(sum(ev_bytes.values()) / ev_total) if ev_total else None
    out["A8_logs_events_month"] = round(out["A1_requests"] * out["A8_events_per_invocation"]) if out["A8_events_per_invocation"] else None
    json.dump(out, open(out_path, "w"), indent=1)
    keep = ("window", "A1_requests", "A2_cpu_ms", "A3_class_a", "A4_class_b", "A5_r2_storage_bytes", "A6_kv", "A6_kv_storage_bytes",
            "A7_do", "A7_do_gb_s_from_active_time", "A7_do_storage_bytes", "A8_events_per_invocation", "A8_bytes_per_event",
            "A8_logs_events_month", "A9_ae_points", "ae_unreadable", "r2_unclassed")
    print(json.dumps({k: out.get(k) for k in keep}, indent=1))
    print(json.dumps({"A8_abr_levels": out["A8_logs_hour"]["abr_levels"], "A8_refused": out.get("A8_refused")}))


# ---- section 4's model, frozen in docs/wx-account-cost.md before the read --------------------------------------------
PROD_KV = "733bf90056104f56be9492e62450a0bf"   # proxy/wrangler.toml [env.production] ENRICH_KV (public)
BOARDS_NOW, WX_NOW = 17, 2          # wx-cost.md: active boards read 2026-11-06, 2 of them with weather on
WX = {"req": 43_200, "cpu": (0.31e6, 1.16e6), "b": (86_400, 129_600)}            # docs/wx-cost.md, one board a month
WS = {"req": 69_840, "cpu": (0.56e6, 1.28e6), "b": (86_400, 129_600)}            # docs/weatherscope-stage4.md
Q113 = (0.47, 0.57)                  # its radar product, fixed, once any Weatherscope ships
TEN = {"cpu": 36.52e6, "a": 0.181e6, "b": 4.30e6, "req": 9 * 43_200, "r2_gb": 0.4}   # M6, as frozen; opuk's cron is in the read
PRICE = {   # (included, $ per unit, unit) -- Cloudflare's docs, read 2026-11-11 (docs/wx-account-cost.md section 1)
    "requests": (10e6, 0.30, 1e6), "cpu": (30e6, 0.02, 1e6), "logs_nov": (20e6, 0.60, 1e6),
    "class_a": (1e6, 4.50, 1e6), "class_b": (10e6, 0.36, 1e6), "r2_gb": (10, 0.015, 1),
    "kv_read": (10e6, 0.50, 1e6), "kv_write": (1e6, 5.0, 1e6), "kv_delete": (1e6, 5.0, 1e6), "kv_list": (1e6, 5.0, 1e6),
    "kv_gb": (1, 0.50, 1), "do_req": (1e6, 0.15, 1e6), "do_gb_s": (400e3, 12.50, 1e6),
    "logs_dec_ingest_gb": (50, 0.25, 1), "logs_dec_store_gb": (10, 0.10, 1),
}
ROUND_UP = {"class_a", "class_b", "r2_gb", "do_req", "do_gb_s"}   # Read A: R2 and DO bill a whole unit (their docs say so)


def line_cost(name, amount, read_a):
    inc, price, unit = PRICE[name]
    over = max(0.0, amount - inc) / unit
    if read_a and name in ROUND_UP:
        over = math.ceil(over - 1e-12)
    return over * price


def model(in_path, out_path):
    r = json.load(open(in_path))
    proxy = "blipscope-proxy"
    lh = r["A8_logs_hour"]
    ev_inv = lambda s: lh["events_by_service"].get(s, 0) / lh["invocations_by_script"][s] if lh["invocations_by_script"].get(s) else None
    ev_proxy = ev_inv(proxy) or r["A8_events_per_invocation"]
    ev_ingest = ev_inv("blipscope-wx-ingest-opuk") or r["A8_events_per_invocation"]
    bpe = r["A8_bytes_per_event"]
    per = {"req": r["requests_by_script"].get(proxy, 0) / BOARDS_NOW, "cpu": r["cpu_ms_by_script"].get(proxy, 0) / BOARDS_NOW}
    kv = r["A6_kv"]
    rows = []
    for n in (17, 50, 200, 600):
        for w in (0, 50, 200):
            for ten in (0, 1):
                for end in (0, 1):   # the low and high ends of the per-board ranges
                    q = {}
                    q["requests"] = r["A1_requests"] + (n - BOARDS_NOW) * per["req"] + (n - WX_NOW) * WX["req"] + w * WS["req"] + ten * TEN["req"]
                    q["cpu"] = (r["A2_cpu_ms"] + (n - BOARDS_NOW) * per["cpu"] + (n - WX_NOW) * WX["cpu"][end] + w * WS["cpu"][end]
                                + ten * TEN["cpu"])
                    q["class_a"] = r["A3_class_a"] + ten * TEN["a"]
                    q["class_b"] = r["A4_class_b"] + (n - WX_NOW) * WX["b"][end] + w * WS["b"][end] + ten * TEN["b"]
                    q["r2_gb"] = r["A5_r2_storage_bytes"] / 1e9 + ten * TEN["r2_gb"]
                    for op in ("read", "write", "delete", "list"):
                        # Section 4 scales "the proxy's run rate": production's namespace grows with the boards, and
                        # every other namespace (staging's) is held flat. A read made before kv_ops_by_namespace existed
                        # (the first, 05:14Z) has no split, so it scales the total: the as-run figure, kept for the record.
                        by_ns = r.get("kv_ops_by_namespace", {}).get("kv_" + op)
                        if by_ns is None:
                            q["kv_" + op] = kv.get("kv_" + op, 0) * n / BOARDS_NOW
                        else:
                            prod = by_ns.get(PROD_KV, 0)
                            q["kv_" + op] = prod * n / BOARDS_NOW + sum(v for k, v in by_ns.items() if k != PROD_KV)
                    q["kv_gb"] = r["A6_kv_storage_bytes"] / 1e9
                    q["do_req"] = r["A7_do"].get("do_requests", 0) * n / BOARDS_NOW
                    q["do_gb_s"] = r["A7_do_gb_s_from_active_time"] * n / BOARDS_NOW
                    ev = (r["A8_logs_events_month"] + ((n - BOARDS_NOW) * per["req"] + (n - WX_NOW) * WX["req"] + w * WS["req"]) * ev_proxy
                          + ten * TEN["req"] * ev_ingest)
                    q["logs_nov"] = ev
                    q["logs_dec_ingest_gb"] = ev * bpe / 1e9
                    q["logs_dec_store_gb"] = q["logs_dec_ingest_gb"] * 7 / 30   # 7-day retention
                    ae = r["A9_ae_points"] * n / BOARDS_NOW + w * WS["req"]
                    for read_a in (0, 1):
                        for dec in (0, 1):
                            names = [k for k in q if not k.startswith("logs")] + (["logs_dec_ingest_gb", "logs_dec_store_gb"] if dec else ["logs_nov"])
                            costs = {k: line_cost(k, q[k], read_a) for k in names}
                            extra = Q113[end] if w else 0.0
                            total = 5.0 + sum(costs.values()) + extra
                            top = sorted(costs.items(), key=lambda x: -x[1])[:2]
                            rows.append({"boards": n, "weatherscopes": w, "ten": ten, "end": "low" if end == 0 else "high",
                                         "read": "A" if read_a else "P", "logs": "dec" if dec else "nov",
                                         "total_usd": round(total, 2), "overage_usd": round(total - 5.0, 2),
                                         "largest": [[k, round(v, 2)] for k, v in top], "ae_points_unpriced": round(ae),
                                         "amounts": {k: round(v, 3) for k, v in q.items()}})
    out = {"inputs": {"per_board_other": {k: round(v) for k, v in per.items()}, "events_per_invocation_proxy": ev_proxy,
                      "events_per_invocation_ingest": ev_ingest, "bytes_per_event": bpe}, "rows": rows}
    json.dump(out, open(out_path, "w"), indent=1)
    pick = lambda n, w, ten, rd, lg: [x["total_usd"] for x in rows if (x["boards"], x["weatherscopes"], x["ten"], x["read"], x["logs"]) == (n, w, ten, rd, lg)]
    print(json.dumps(out["inputs"]))
    print("boards ws ten | P nov lo-hi | A nov lo-hi | P dec lo-hi | A dec lo-hi | largest two (P nov, high end)")
    for n in (17, 50, 200, 600):
        for w in (0, 50, 200):
            for ten in (0, 1):
                cells = [pick(n, w, ten, rd, lg) for rd, lg in (("P", "nov"), ("A", "nov"), ("P", "dec"), ("A", "dec"))]
                big = [x["largest"] for x in rows if (x["boards"], x["weatherscopes"], x["ten"], x["read"], x["logs"], x["end"]) == (n, w, ten, "P", "nov", "high")][0]
                print(f"{n:4d} {w:3d} {ten:2d} | " + " | ".join(f"{c[0]:7.2f}-{c[1]:7.2f}" for c in cells) + f" | {big}")


if __name__ == "__main__":
    if len(sys.argv) >= 3 and sys.argv[1] == "read":
        read(sys.argv[2])
    elif len(sys.argv) >= 4 and sys.argv[1] == "model":
        model(sys.argv[2], sys.argv[3])
    else:
        raise SystemExit(__doc__)
