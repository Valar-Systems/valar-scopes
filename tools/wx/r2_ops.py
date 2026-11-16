"""PKD-5 (docs/wx-opera-europe.md section 11): R2 operations on blipscope-wx by hour and action, from the R2
analytics dataset (adaptively sampled, so PRINTED, never graded). Read-only; the token file's analytics-read line,
never printed.

Usage: python -I r2_ops.py <from ISO> <to ISO> [actionType ...]   (default: ListObjects DeleteObject DeleteObjects
                                                                    PutObject)
"""
import json
import os
import sys
import urllib.request

ACCOUNT = "48822e896bb10c45aa6bfe139bcff3d1"
a, b = sys.argv[1], sys.argv[2]
actions = sys.argv[3:] or ["ListObjects", "DeleteObject", "DeleteObjects", "PutObject"]
import importlib.util as _ilu   # the token-file helper, by path: python -I drops this folder from sys.path
_tfs = _ilu.spec_from_file_location("token_file", os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "token_file.py"))
TOKEN_FILE = _ilu.module_from_spec(_tfs); _tfs.loader.exec_module(TOKEN_FILE)   # docs/token-file-python.md
tok = TOKEN_FILE.token("analytics-read")
q = """query($acct: String!, $a: Time!, $b: Time!, $act: [string!]) { viewer { accounts(filter: {accountTag: $acct}) {
  r2OperationsAdaptiveGroups(limit: 1000, filter: {datetime_geq: $a, datetime_lt: $b, bucketName: "blipscope-wx",
                                                   actionType_in: $act}) {
    sum { requests } dimensions { datetimeHour actionType objectName } } } } }"""
req = urllib.request.Request("https://api.cloudflare.com/client/v4/graphql",
                             data=json.dumps({"query": q, "variables": {"acct": ACCOUNT, "a": a, "b": b, "act": actions}}).encode(),
                             method="POST", headers={"Authorization": "Bearer " + tok, "Content-Type": "application/json"})
r = json.loads(urllib.request.urlopen(req, timeout=120).read())
if r.get("errors"):
    print(json.dumps({"errors": r["errors"]})[:600])
    sys.exit(2)
rows = r["data"]["viewer"]["accounts"][0]["r2OperationsAdaptiveGroups"]
by = {}
for g in rows:
    d = g["dimensions"]
    name = d.get("objectName") or ""
    prefix = "/".join(name.split("/")[:2]) if name else "(none)"      # wx/<product>, never a full key
    k = (d["datetimeHour"][11:16] + "Z", d["actionType"], prefix)
    by[k] = by.get(k, 0) + g["sum"]["requests"]
print(json.dumps({"window": [a, b], "bucket": "blipscope-wx", "note": "sampled dataset: printed, not graded",
                  "by_hour_action_prefix": {" ".join(k): v for k, v in sorted(by.items())}}, indent=1))
