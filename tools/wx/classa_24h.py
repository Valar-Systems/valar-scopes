"""S3-7 (docs/wx-opera-stage3.md): the whole account's R2 Class A over one 24-hour window, x 30, by bucket.
Read-only; the same query and the same class set as tools/wx/account_cost_read.py (copied: that file is the 7-day
run rate, this is the frozen row's "last 24 hours x 30"). Token from the token file, never printed. Counts only.

  python -I classa_24h.py <window end ISO>        the 24 hours ending then
"""
import json
import os
import sys
import urllib.request
from datetime import datetime, timedelta

ACCOUNT = "48822e896bb10c45aa6bfe139bcff3d1"
CLASS_A = {"ListBuckets", "PutBucket", "ListObjects", "PutObject", "CopyObject", "CompleteMultipartUpload",
           "CreateMultipartUpload", "ListMultipartUploads", "UploadPart", "UploadPartCopy", "ListParts",
           "PutBucketEncryption", "PutBucketCors", "PutBucketLifecycleConfiguration", "LifecycleStorageTierTransition"}
import importlib.util as _ilu   # the token-file helper, by path: python -I drops this folder from sys.path
_tfs = _ilu.spec_from_file_location("token_file", os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "token_file.py"))
TOKEN_FILE = _ilu.module_from_spec(_tfs); _tfs.loader.exec_module(TOKEN_FILE)   # docs/token-file-python.md
tok = TOKEN_FILE.token("analytics-read")
b = datetime.fromisoformat(sys.argv[1].replace("Z", "+00:00"))
a = b - timedelta(days=1)
iso = lambda t: t.isoformat().replace("+00:00", "Z")
q = """query($acct: String!, $a: Time!, $b: Time!) { viewer { accounts(filter: {accountTag: $acct}) {
  r2OperationsAdaptiveGroups(limit: 1000, filter: {datetime_geq: $a, datetime_lt: $b}) { sum { requests } dimensions { actionType bucketName } } } } }"""
req = urllib.request.Request("https://api.cloudflare.com/client/v4/graphql",
                             data=json.dumps({"query": q, "variables": {"acct": ACCOUNT, "a": iso(a), "b": iso(b)}}).encode(),
                             method="POST", headers={"Authorization": "Bearer " + tok, "Content-Type": "application/json"})
d = json.loads(urllib.request.urlopen(req, timeout=120).read())
if d.get("errors"):
    raise SystemExit("GraphQL errors: " + json.dumps(d["errors"])[:400])
groups = d["data"]["viewer"]["accounts"][0]["r2OperationsAdaptiveGroups"]
by = {}
for g in groups:
    if g["dimensions"]["actionType"] in CLASS_A:
        by[g["dimensions"]["bucketName"]] = by.get(g["dimensions"]["bucketName"], 0) + g["sum"]["requests"]
tot = sum(by.values())
print(json.dumps({"window": [iso(a), iso(b)], "groups": len(groups), "class_a_24h": tot, "class_a_x30": tot * 30,
                  "x30_by_bucket": {k: v * 30 for k, v in sorted(by.items(), key=lambda kv: -kv[1])}}))
if len(groups) >= 1000:
    raise SystemExit("BLIND: 1000 groups, the read may be cut")
