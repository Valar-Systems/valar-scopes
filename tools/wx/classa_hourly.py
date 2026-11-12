"""S3-7's attribution (docs/wx-opera-stage3.md section 8): one R2 bucket's Class A, by hour, over one 24-hour window.
Read-only; the same query, account and class set as tools/wx/classa_24h.py, with a bucket filter and the hour as a
dimension. Its control is that the hours sum to classa_24h.py's figure for the same bucket and window. Token from the
token file, never printed. Counts only.

  python -I classa_hourly.py <bucket> <window end ISO>     exit 0 rows read, 2 no rows (never read as "zero")
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
tok = next(v for k, _, v in (l.strip().partition("=") for l in open(os.path.expanduser("~/.config/blipscope/tokens"), encoding="utf-8"))
           if k == "analytics-read" and v)
bucket = sys.argv[1]
b = datetime.fromisoformat(sys.argv[2].replace("Z", "+00:00"))
a = b - timedelta(days=1)
iso = lambda t: t.isoformat().replace("+00:00", "Z")
q = """query($acct: String!, $a: Time!, $b: Time!, $bucket: String!) { viewer { accounts(filter: {accountTag: $acct}) {
  r2OperationsAdaptiveGroups(limit: 5000, filter: {datetime_geq: $a, datetime_lt: $b, bucketName: $bucket}) {
    sum { requests } dimensions { actionType datetimeHour } } } } }"""
req = urllib.request.Request("https://api.cloudflare.com/client/v4/graphql",
                             data=json.dumps({"query": q, "variables": {"acct": ACCOUNT, "a": iso(a), "b": iso(b), "bucket": bucket}}).encode(),
                             method="POST", headers={"Authorization": "Bearer " + tok, "Content-Type": "application/json"})
d = json.loads(urllib.request.urlopen(req, timeout=120).read())
if d.get("errors"):
    raise SystemExit("GraphQL errors: " + json.dumps(d["errors"])[:400])
groups = d["data"]["viewer"]["accounts"][0]["r2OperationsAdaptiveGroups"]
if not groups:
    print(f"NO ROWS for {bucket} in {iso(a)}..{iso(b)}: not a reading of zero (wrong bucket, or the query cannot see it)")
    sys.exit(2)
hours = {}
for g in groups:
    if g["dimensions"]["actionType"] in CLASS_A:
        h = g["dimensions"]["datetimeHour"][:13]
        hours[h] = hours.get(h, 0) + g["sum"]["requests"]
for h in sorted(hours):
    print(h, hours[h])
total = sum(hours.values())
print(json.dumps({"bucket": bucket, "window": [iso(a), iso(b)], "hours": len(hours), "class_a_24h": total, "class_a_x30": total * 30}))
