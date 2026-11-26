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
import re
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
          "blipscope-basemap": "basemap-production", "valarscopes-app": "kiosk", "valarscopes-site": "kiosk",
          "blipscope-screen": "screen", "blipscope-screen-staging": "screen-staging"}
BUCKET = {"missileer-live": "missileer-live", "missileer-clips": "missileer-clips", "blipscope-wx-dev": "wx-dev",
          "blipscope-basemap-pkg-staging": "basemap-staging", "blipscope-basemap": "basemap-bucket",
          "blipscope-basemap-pkg": "basemap-production", "": "account-level"}
# Buckets attributed by their own rule below, not by BUCKET (docs/account-budget.md section 2): blipscope-wx is shared by the
# ingests and the proxy's /wx serving. scripts/check_budget_names.py reads BUCKET and this set, so both rules are checked.
SHARED_BUCKETS = {"blipscope-wx"}
KV_NS = {"733bf90056104f56be9492e62450a0bf": "proxy", "8b6e92ed802a4192b6b6c1399fdf96aa": "proxy-staging"}   # wrangler.toml (public)


import importlib.util as _ilu   # the token-file helper, by path: python -I drops this folder from sys.path
_TOKEN_FILE = None
def token(name):
    # Loaded on first use, not at import: --selftest (and the sabotage driver's copy of this file in a temp directory) reads
    # no token and has no ../token_file.py beside it. docs/token-file-python.md
    global _TOKEN_FILE
    if _TOKEN_FILE is None:
        spec = _ilu.spec_from_file_location("token_file", os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "token_file.py"))
        _TOKEN_FILE = _ilu.module_from_spec(spec); spec.loader.exec_module(_TOKEN_FILE)
    return _TOKEN_FILE.token(name)


def gql(q, a, b):
    body = {"query": "query($acct: String!, $a: Time!, $b: Time!) { viewer { accounts(filter: {accountTag: $acct}) { %s } } }" % q,
            "variables": {"acct": ACCOUNT, "a": a, "b": b}}
    req = urllib.request.Request("https://api.cloudflare.com/client/v4/graphql", data=json.dumps(body).encode(), method="POST",
                                 headers={"Authorization": "Bearer " + token("analytics-read"), "Content-Type": "application/json"})
    d = json.loads(urllib.request.urlopen(req, timeout=120).read())
    if d.get("errors"):
        raise SystemExit("GraphQL errors: " + json.dumps(d["errors"])[:400])
    return d["data"]["viewer"]["accounts"][0]


# Workers that are NOT part of Blipscope (docs/account-budget.md section 14): named only in an OPTIONAL local file outside any
# git tree, whose path BLIPSCOPE_BUDGET_EXCLUDE gives per shell, so no such name is ever committed. A listed Worker is written as
# product "non-blipscope", state "excluded": still a row, so a read's totals still anchor, and summed into no product's budget.
EXCLUDE_ENV = "BLIPSCOPE_BUDGET_EXCLUDE"
_EXCLUDED = frozenset()


def load_excluded(path):
    """The listed Worker names: one per line, "#" comments and blank lines ignored. A file inside a git work tree is REFUSED,
    so the list cannot be committed by accident."""
    ap = os.path.abspath(path)
    d = os.path.dirname(ap)
    while True:
        if os.path.exists(os.path.join(d, ".git")):
            raise SystemExit(f"{EXCLUDE_ENV} names a file inside a git work tree; keep the list outside every repository")
        up = os.path.dirname(d)
        if up == d:
            break
        d = up
    with open(ap, encoding="utf-8") as fh:
        return frozenset(n for n in (line.split("#", 1)[0].strip() for line in fh) if n)


def script_product(s, opera_state):
    if s in _EXCLUDED:
        return "non-blipscope", "excluded"
    if s.startswith("blipscope-wx-ingest-op"):
        return "opera-ten", opera_state
    if s in SCRIPT_STATE:
        return SCRIPT_STATE[s]
    return SCRIPT.get(s, "unattributed"), "now"


SCRIPT_STATE = {"blipscope-wx-ingest-shsr": ("shsr", "built")}   # 9.4: its product's live state, so a read attributes it

# Durable Object duration (docs/account-budget.md section 13). The periodic dataset reports duration per NAMESPACE, and the
# invocations dataset names the CALLERS of each namespace, not its owner. So a namespace is attributed per window, by its
# callers in that same window: exactly one -> that caller's product; none, or several -> "unattributed", printed, never
# smoothed onto a guess. No namespace id is written anywhere but the console, and there only as a 6-hex prefix.
DO_BY_CALLER = "attributed by its only caller in this window; the dataset names callers, not owners"


def do_gb_s_by_caller(inv, per, opera_state):
    """One (product, state, GB-s, label) per namespace with a duration row. inv: invocation groups (dimensions scriptName,
    namespaceId); per: periodic groups (sum activeTime in microseconds, dimensions namespaceId). GB-s = s x 0.128 GB."""
    callers = {}
    for g in inv:
        callers.setdefault(g["dimensions"]["namespaceId"], set()).add(g["dimensions"]["scriptName"])
    out = []
    for g in per:
        ns = g["dimensions"]["namespaceId"]
        c = callers.get(ns, set())
        if len(c) == 1:
            p, st = script_product(next(iter(c)), opera_state)
        else:
            p, st = "unattributed", "now"
        out.append((p, st, (g["sum"]["activeTime"] or 0) / 1e6 * 0.128, f"{ns[:6]}... ({len(c)} caller{'' if len(c) == 1 else 's'})"))
    return out


def main():
    rid, a, b, opera_state, take, out_path = sys.argv[1:7]
    observation = "--observation" in sys.argv[7:]
    if take not in ("rest", "opera", "all"):
        raise SystemExit("take must be rest, opera or all")
    hours = (datetime.fromisoformat(b.replace("Z", "+00:00")) - datetime.fromisoformat(a.replace("Z", "+00:00"))).total_seconds() / 3600
    k = 30 * 24 / hours
    global _EXCLUDED
    _EXCLUDED = load_excluded(os.environ[EXCLUDE_ENV]) if os.environ.get(EXCLUDE_ENV) else frozenset()
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
        if bk in SHARED_BUCKETS:
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
    do = gql("""durableObjectsInvocationsAdaptiveGroups(limit: 1000, filter: {datetime_geq: $a, datetime_lt: $b}) { sum { requests } dimensions { scriptName namespaceId } }
                durableObjectsPeriodicGroups(limit: 1000, filter: {datetime_geq: $a, datetime_lt: $b}) { sum { activeTime } dimensions { namespaceId } }""", a, b)
    for g in do["durableObjectsInvocationsAdaptiveGroups"]:
        p, st = script_product(g["dimensions"]["scriptName"], opera_state)
        add(p, st, "do_requests", g["sum"]["requests"], g["dimensions"]["scriptName"])
    # The account's DO namespaces, each attributed per window by its callers (section 13), never all to one product.
    for p, st, gb_s, label in do_gb_s_by_caller(do["durableObjectsInvocationsAdaptiveGroups"], do["durableObjectsPeriodicGroups"], opera_state):
        add(p, st, "do_gb_s", gb_s, label)
    st = gql("r2StorageAdaptiveGroups(limit: 1000, filter: {datetime_geq: $a, datetime_lt: $b}) { max { payloadSize metadataSize } dimensions { bucketName } }", a, b)
    store = {}
    for g in st["r2StorageAdaptiveGroups"]:
        bk = g["dimensions"]["bucketName"]
        store[bk] = max(store.get(bk, 0), (g["max"]["payloadSize"] or 0) + (g["max"]["metadataSize"] or 0))

    # The account's totals from SEPARATE, coarser queries (no script, bucket or namespace dimension), so the checker's
    # anchor compares the attributed rows against something they were not summed from: a truncated grouped query
    # (limit 1000) or a group the mapping skipped shows as a gap.
    # PREMISE the checker depends on (scripts/account_budget.py anchor, 9.12): each total below is rounded ONCE, to the
    # unit, on its raw sum x k, and each row further down is rounded once on its own. So a scaled read's rows and total
    # can differ by up to 0.5 x (rows + 1) from rounding alone, which is the allowance the anchor grants where k != 1.
    # Round anything here differently and that bound changes with it.
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
            month = round(v * k)   # once per row, to the unit: the anchor's rounding allowance (9.12) assumes it
            rows.append({"product": p, "state": s, "allowance": al, "value": month, "read": rid, "basis": "measured",
                         "how": f"{a}..{b} ({hours:g} h) x {k:g}" + (f"; {DO_BY_CALLER}" if al == "do_gb_s" else "")})
    for bk, by in sorted(store.items()):
        p = "blipscope-wx-reads" if bk in SHARED_BUCKETS else BUCKET.get(bk, "unattributed")
        if p == "blipscope-wx-reads":
            p = "cref+opera-ten" if opera_state != "silent" else "cref"   # storage is written by the ingests
        if p == "unattributed":
            unknown.append(f"r2_storage_gb: {bk}")
        gb = round(by / 1e9, 3)
        rows.append({"product": p, "state": "excluded" if take == "opera" else "now", "allowance": "r2_storage_gb", "value": gb,
                     "read": rid, "basis": "measured",
                     "how": f"the window's largest stored bytes, {bk} (not anchored: storage has no coarser total)"})
    # k: the checker's anchor reads it, and only asks whether it is 1. Six places: a full float repr (720/318 written in
    # full has 16 digits after the point) can carry 16 digits in a row, which the device-id guard rightly reads as an id.
    read = {"tool": "tools/budget/budget_read.py", "window": [a, b], "k": round(k, 6), "totals": totals}
    if observation:
        read["observation"] = True
        rows = []   # totals only: printed beside the graded figure, never summed (section 5.1)
    out = {"read": {rid: read}, "rows": rows}
    json.dump(out, open(out_path, "w"), indent=1)
    for row in rows:
        print(f"  {row['product']:20s} {row['state']:8s} {row['allowance']:14s} {row['value']:>14,}")
    print("TOTALS:", json.dumps(totals))
    print("UNATTRIBUTED:", unknown or "none")
    print(f"EXCLUDED (non-blipscope): {len(_EXCLUDED)} Worker(s), from the local list")   # a count, never a name
    return 0


def selftest():
    """Offline, on canned groups: the per-window caller attribution (section 13) and the local exclusion list (section 14).
    Synthetic ids and names, never real ones. D1 and E1 are the controls; the rest are the rows the sabotage driver must
    turn red. Exit 0 only if every row passes."""
    A, B, C, D = ("a" * 32), ("b" * 32), ("c" * 32), ("d" * 32)
    inv = [{"dimensions": {"scriptName": "blipscope-proxy", "namespaceId": A}},
           {"dimensions": {"scriptName": "blipscope-proxy", "namespaceId": B}},
           {"dimensions": {"scriptName": "valarscopes-app", "namespaceId": B}},
           {"dimensions": {"scriptName": "blipscope-screen", "namespaceId": C}}]
    per = [{"sum": {"activeTime": 1_000_000_000}, "dimensions": {"namespaceId": A}},   # 1,000 s = 128 GB-s
           {"sum": {"activeTime": 500_000_000}, "dimensions": {"namespaceId": B}},     # 64 GB-s, two callers
           {"sum": {"activeTime": 250_000_000}, "dimensions": {"namespaceId": C}},     # 32 GB-s, the screen alone
           {"sum": {"activeTime": 125_000_000}, "dimensions": {"namespaceId": D}}]     # 16 GB-s, no caller this window
    got = do_gb_s_by_caller(inv, per, "silent")
    by_ns = {lab[:6]: (p, st, gb) for p, st, gb, lab in got}
    want = [("proxy", "now", 128.0, "aaaaaa... (1 caller)"), ("unattributed", "now", 64.0, "bbbbbb... (2 callers)"),
            ("screen", "now", 32.0, "cccccc... (1 caller)"), ("unattributed", "now", 16.0, "dddddd... (0 callers)")]
    rows = [
        ("D1", by_ns.get("aaaaaa", ("?",))[0] == "proxy", "CONTROL: a namespace with one caller (blipscope-proxy) lands on proxy"),
        ("D2", by_ns.get("bbbbbb", ("?",))[0] == "unattributed", "a namespace with two callers lands on unattributed, never on a product"),
        ("D3", by_ns.get("dddddd", ("?",))[0] == "unattributed", "a namespace with no caller in the window lands on unattributed"),
        ("D4", by_ns.get("cccccc", ("?",))[0] == "screen", "a namespace whose only caller is blipscope-screen lands on screen"),
        ("D5", not any(re.search(r"[0-9a-f]{7,}", lab) for _, _, _, lab in got), "every label is a 6-hex prefix, never a whole id"),
        ("D-ALL", got == want, "the whole per-namespace list equals the expected list"),
    ]
    # Section 14: the owner's non-Blipscope Workers, from a local list. Synthetic names only.
    import tempfile
    global _EXCLUDED
    saved = _EXCLUDED
    try:
        _EXCLUDED = frozenset()
        e1 = script_product("example-site", "silent") == ("unattributed", "now")
        with tempfile.TemporaryDirectory() as t:
            lst = os.path.join(t, "exclude.txt")
            with open(lst, "w", encoding="utf-8") as fh:
                fh.write("# the owner's other sites\n\nexample-site   # a note\n")
            names = load_excluded(lst)
            _EXCLUDED = names
            e2 = script_product("example-site", "silent") == ("non-blipscope", "excluded")
            os.mkdir(os.path.join(t, ".git"))
            try:
                load_excluded(lst)
                refused = False
            except SystemExit:
                refused = True
    finally:
        _EXCLUDED = saved
    rows += [
        ("E1", e1, "CONTROL: with no list, a Worker the tables do not know is unattributed (as before)"),
        ("E2", e2, "a listed Worker is product non-blipscope, state excluded"),
        ("E3", refused and names == frozenset({"example-site"}), "a list inside a git work tree is refused; outside it, it loads"),
        ("E4", names == frozenset({"example-site"}), "comments and blank lines are ignored: the list loads exactly its name"),
    ]
    for rid, ok, what in rows:
        print(f"{'ok  ' if ok else 'FAIL'} {rid}: {what}" + ("" if ok else f" -- got {got}"))
    bad = sum(1 for _, ok, _ in rows if not ok)
    print(f"budget_read selftest: {len(rows) - bad} of {len(rows)}")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(selftest() if sys.argv[1:2] == ["--selftest"] else main())
