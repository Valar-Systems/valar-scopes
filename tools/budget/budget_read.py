#!/usr/bin/env python3
"""The account budget's read (docs/account-budget.md section 1): one window, every billed line, attributed to the
budget's products, scaled to a 30-day month. Read-only; tokens come from the token file (analytics-read,
observability-read) and are never printed; counts and sums only.

  python3 -I tools/budget/budget_read.py <read id> <start ISO> <end ISO> <state of opera-ten> <take> <out.json> [--observation]

<take> (docs/account-budget.md section 5.1): "rest" keeps every group except the OPERA-dependent ones (the ten's
scripts, and blipscope-wx's Class A and B), which it writes in state "excluded"; "opera" keeps only those, and writes
the rest as "excluded"; "all" excludes nothing. Either way every group is a row, so the read anchors on its own totals.
--observation writes the read's totals only, marked "observation": true, with no rows: the checker prints it beside the
graded figure and never sums it.

The output is {"read": {...the account's own totals...}, "rows": [...]}, ready to merge into docs/account-budget.json.
Every script, bucket and namespace the account reports gets a row: one this file does not map is written as product
"unattributed" and printed, never dropped, so the checker's anchor cannot be passed by leaving something out.
"""
import json
import os
import sys
import urllib.request
from datetime import datetime

ACCOUNT = "48822e896bb10c45aa6bfe139bcff3d1"   # proxy/wrangler.toml (public)
CLASS_A = {"ListBuckets", "PutBucket", "ListObjects", "PutObject", "CopyObject", "CompleteMultipartUpload",
           "CreateMultipartUpload", "ListMultipartUploads", "UploadPart", "UploadPartCopy", "ListParts",
           "PutBucketEncryption", "PutBucketCors", "PutBucketLifecycleConfiguration", "LifecycleStorageTierTransition"}
CLASS_B = {"HeadBucket", "HeadObject", "GetObject", "UsageSummary", "GetBucketEncryption", "GetBucketLocation",
           "GetBucketCors", "GetBucketLifecycleConfiguration"}
SCRIPT = {"blipscope-proxy": "proxy", "blipscope-proxy-staging": "proxy-staging", "blipscope-wx-ingest": "cref",
          "blipscope-wx-ingest-dev": "wx-dev", "blipscope-basemap-staging": "basemap-staging",
          "valarscopes-app": "kiosk", "valarscopes-site": "kiosk"}
BUCKET = {"missileer-live": "missileer-live", "missileer-clips": "missileer-clips", "blipscope-wx-dev": "wx-dev",
          "blipscope-basemap-pkg-staging": "basemap-staging", "blipscope-basemap": "basemap-bucket", "": "account-level"}
KV_NS = {"733bf90056104f56be9492e62450a0bf": "proxy", "8b6e92ed802a4192b6b6c1399fdf96aa": "proxy-staging"}   # wrangler.toml (public)


def token(name):
    for line in open(os.path.expanduser("~/.config/blipscope/tokens"), encoding="utf-8"):
        k, _, v = line.strip().partition("=")
        if k == name and v:
            return v
    raise SystemExit(f"{name} absent from the token file")


def gql(q, a, b):
    body = {"query": "query($acct: String!, $a: Time!, $b: Time!) { viewer { accounts(filter: {accountTag: $acct}) { %s } } }" % q,
            "variables": {"acct": ACCOUNT, "a": a, "b": b}}
    req = urllib.request.Request("https://api.cloudflare.com/client/v4/graphql", data=json.dumps(body).encode(), method="POST",
                                 headers={"Authorization": "Bearer " + token("analytics-read"), "Content-Type": "application/json"})
    d = json.loads(urllib.request.urlopen(req, timeout=120).read())
    if d.get("errors"):
        raise SystemExit("GraphQL errors: " + json.dumps(d["errors"])[:400])
    return d["data"]["viewer"]["accounts"][0]


def script_product(s, opera_state):
    if s.startswith("blipscope-wx-ingest-op"):
        return "opera-ten", opera_state
    return SCRIPT.get(s, "unattributed"), "now"


def main():
    rid, a, b, opera_state, take, out_path = sys.argv[1:7]
    observation = "--observation" in sys.argv[7:]
    if take not in ("rest", "opera", "all"):
        raise SystemExit("take must be rest, opera or all")
    hours = (datetime.fromisoformat(b.replace("Z", "+00:00")) - datetime.fromisoformat(a.replace("Z", "+00:00"))).total_seconds() / 3600
    k = 30 * 24 / hours
    acc, unknown = {}, []   # (product, state, allowance) -> value in the window

    def add(p, st, al, v, name, opera_dependent=False):
        if p == "unattributed":
            unknown.append(f"{al}: {name}")
        if (take == "rest" and opera_dependent) or (take == "opera" and not opera_dependent):
            st = "excluded"
        acc[(p, st, al)] = acc.get((p, st, al), 0) + (v or 0)

    w = gql("workersInvocationsAdaptive(limit: 1000, filter: {datetime_geq: $a, datetime_lt: $b}) { sum { cpuTimeUs requests } dimensions { scriptName } }", a, b)
    for g in w["workersInvocationsAdaptive"]:
        s = g["dimensions"]["scriptName"]
        p, st = script_product(s, opera_state)
        dep = s.startswith("blipscope-wx-ingest-op")
        add(p, st, "cpu_ms", g["sum"]["cpuTimeUs"] / 1000.0, s, dep)
        add(p, st, "requests", g["sum"]["requests"], s, dep)
    r = gql("r2OperationsAdaptiveGroups(limit: 1000, filter: {datetime_geq: $a, datetime_lt: $b}) { sum { requests } dimensions { actionType bucketName } }", a, b)
    for g in r["r2OperationsAdaptiveGroups"]:
        act, bk = g["dimensions"]["actionType"], g["dimensions"]["bucketName"]
        cls = "r2_class_a" if act in CLASS_A else "r2_class_b" if act in CLASS_B else None
        if cls is None:
            continue   # free operations (deletes, aborts)
        if bk == "blipscope-wx":
            # Shared (docs/account-budget.md section 2). Class A: the proxy never writes it, so it is the ingests'. In a
            # silent read that is cref alone; in any other read it is cref and the ten together, which this tool does not
            # split by guess. Class B: the ingests plus the proxy's /wx serving, which R2 cannot separate.
            if cls == "r2_class_a":
                p = "cref" if opera_state == "silent" else "cref+opera-ten"
            else:
                p = "blipscope-wx-reads"
            add(p, "now", cls, g["sum"]["requests"], bk, True)
        else:
            add(BUCKET.get(bk, "unattributed"), "now", cls, g["sum"]["requests"], bk)
    kv = gql("kvOperationsAdaptiveGroups(limit: 1000, filter: {datetime_geq: $a, datetime_lt: $b}) { sum { requests } dimensions { actionType namespaceId } }", a, b)
    for g in kv["kvOperationsAdaptiveGroups"]:
        al = "kv_" + str(g["dimensions"]["actionType"])
        add(KV_NS.get(g["dimensions"]["namespaceId"], "unattributed"), "now", al, g["sum"]["requests"], g["dimensions"]["namespaceId"])
    do = gql("""durableObjectsInvocationsAdaptiveGroups(limit: 1000, filter: {datetime_geq: $a, datetime_lt: $b}) { sum { requests } dimensions { scriptName } }
                durableObjectsPeriodicGroups(limit: 1000, filter: {datetime_geq: $a, datetime_lt: $b}) { sum { activeTime } dimensions { namespaceId } }""", a, b)
    for g in do["durableObjectsInvocationsAdaptiveGroups"]:
        p, st = script_product(g["dimensions"]["scriptName"], opera_state)
        add(p, st, "do_requests", g["sum"]["requests"], g["dimensions"]["scriptName"])
    for g in do["durableObjectsPeriodicGroups"]:
        add("proxy", "now", "do_gb_s", (g["sum"]["activeTime"] or 0) / 1e6 * 0.128, g["dimensions"]["namespaceId"])   # the proxy's BendSnapshotDO
    st = gql("r2StorageAdaptiveGroups(limit: 1000, filter: {datetime_geq: $a, datetime_lt: $b}) { max { payloadSize metadataSize } dimensions { bucketName } }", a, b)
    store = {}
    for g in st["r2StorageAdaptiveGroups"]:
        bk = g["dimensions"]["bucketName"]
        store[bk] = max(store.get(bk, 0), (g["max"]["payloadSize"] or 0) + (g["max"]["metadataSize"] or 0))

    # The account's totals from SEPARATE, coarser queries (no script, bucket or namespace dimension), so the checker's
    # anchor compares the attributed rows against something they were not summed from: a truncated grouped query
    # (limit 1000) or a group the mapping skipped shows as a gap.
    totals = {}
    tw = gql("workersInvocationsAdaptive(limit: 10, filter: {datetime_geq: $a, datetime_lt: $b}) { sum { cpuTimeUs requests } }", a, b)
    totals["cpu_ms"] = round(sum(g["sum"]["cpuTimeUs"] for g in tw["workersInvocationsAdaptive"]) / 1000.0 * k)
    totals["requests"] = round(sum(g["sum"]["requests"] for g in tw["workersInvocationsAdaptive"]) * k)
    tr = gql("r2OperationsAdaptiveGroups(limit: 100, filter: {datetime_geq: $a, datetime_lt: $b}) { sum { requests } dimensions { actionType } }", a, b)
    totals["r2_class_a"] = round(sum(g["sum"]["requests"] for g in tr["r2OperationsAdaptiveGroups"] if g["dimensions"]["actionType"] in CLASS_A) * k)
    totals["r2_class_b"] = round(sum(g["sum"]["requests"] for g in tr["r2OperationsAdaptiveGroups"] if g["dimensions"]["actionType"] in CLASS_B) * k)
    tk = gql("kvOperationsAdaptiveGroups(limit: 100, filter: {datetime_geq: $a, datetime_lt: $b}) { sum { requests } dimensions { actionType } }", a, b)
    for g in tk["kvOperationsAdaptiveGroups"]:
        totals["kv_" + str(g["dimensions"]["actionType"])] = round(g["sum"]["requests"] * k)

    rows = []
    for (p, s, al), v in sorted(acc.items()):
        if al in ("kv_read", "kv_write", "kv_list", "kv_delete", "cpu_ms", "requests", "r2_class_a", "r2_class_b", "do_requests", "do_gb_s"):
            month = round(v * k)
            rows.append({"product": p, "state": s, "allowance": al, "value": month, "read": rid, "basis": "measured",
                         "how": f"{a}..{b} ({hours:g} h) x {k:g}"})
    for bk, by in sorted(store.items()):
        p = "blipscope-wx-reads" if bk == "blipscope-wx" else BUCKET.get(bk, "unattributed")
        if p == "blipscope-wx-reads":
            p = "cref+opera-ten" if opera_state != "silent" else "cref"   # storage is written by the ingests
        if p == "unattributed":
            unknown.append(f"r2_storage_gb: {bk}")
        gb = round(by / 1e9, 3)
        rows.append({"product": p, "state": "excluded" if take == "opera" else "now", "allowance": "r2_storage_gb", "value": gb,
                     "read": rid, "basis": "measured",
                     "how": f"the window's largest stored bytes, {bk} (not anchored: storage has no coarser total)"})
    read = {"tool": "tools/budget/budget_read.py", "window": [a, b], "totals": totals}
    if observation:
        read["observation"] = True
        rows = []   # totals only: printed beside the graded figure, never summed (section 5.1)
    out = {"read": {rid: read}, "rows": rows}
    json.dump(out, open(out_path, "w"), indent=1)
    for row in rows:
        print(f"  {row['product']:20s} {row['state']:8s} {row['allowance']:14s} {row['value']:>14,}")
    print("TOTALS:", json.dumps(totals))
    print("UNATTRIBUTED:", unknown or "none")
    return 0


if __name__ == "__main__":
    sys.exit(main())
