"""S3-2's second half (docs/wx-opera-stage3.md): does wx/<id>/latest exist, and does the frame it names appear in a
listing of wx/<id>/frames/? Read-only; tokens from the token file, never printed; prints counts, times and booleans
only. Reuses tools/wx/r3_5b.py's SigV4 GET, copied (that file runs its read at import).

  python -I latest_check.py <product>
Exit 0 = latest readable AND its frame listed; 1 = either fails (BLIND = FAIL); 2 = usage.
"""
import datetime
import hashlib
import hmac
import json
import os
import re
import sys
import time
import urllib.parse
import urllib.request

ACCOUNT = "48822e896bb10c45aa6bfe139bcff3d1"
BUCKET = "blipscope-wx"
HOST = f"{ACCOUNT}.r2.cloudflarestorage.com"


import importlib.util as _ilu   # the token-file helper, by path: python -I drops this folder from sys.path
_tfs = _ilu.spec_from_file_location("token_file", os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "token_file.py"))
TOKEN_FILE = _ilu.module_from_spec(_tfs); _tfs.loader.exec_module(TOKEN_FILE)   # docs/token-file-python.md
def tf():
    return TOKEN_FILE.tokens("r2-wx-read-id", "r2-wx-read-secret")


def s3_get(akid, secret, path, q=None):
    q = q or {}
    qs = "&".join(f"{urllib.parse.quote(k, safe='-_.~')}={urllib.parse.quote(v, safe='-_.~')}" for k, v in sorted(q.items()))
    uri = "/" + BUCKET + ("/" + urllib.parse.quote(path, safe="/-_.~") if path else "")
    now = datetime.datetime.now(datetime.timezone.utc)
    amz, day = now.strftime("%Y%m%dT%H%M%SZ"), now.strftime("%Y%m%d")
    ph = hashlib.sha256(b"").hexdigest()
    canon = f"GET\n{uri}\n{qs}\nhost:{HOST}\nx-amz-content-sha256:{ph}\nx-amz-date:{amz}\n\nhost;x-amz-content-sha256;x-amz-date\n{ph}"
    scope = f"{day}/auto/s3/aws4_request"
    sts = f"AWS4-HMAC-SHA256\n{amz}\n{scope}\n{hashlib.sha256(canon.encode()).hexdigest()}"
    sign = lambda key, msg: hmac.new(key, msg.encode(), hashlib.sha256).digest()
    k = sign(sign(sign(sign(("AWS4" + secret).encode(), day), "auto"), "s3"), "aws4_request")
    sig = hmac.new(k, sts.encode(), hashlib.sha256).hexdigest()
    auth = f"AWS4-HMAC-SHA256 Credential={akid}/{scope}, SignedHeaders=host;x-amz-content-sha256;x-amz-date, Signature={sig}"
    req = urllib.request.Request(f"https://{HOST}{uri}" + (f"?{qs}" if qs else ""),
                                 headers={"x-amz-date": amz, "x-amz-content-sha256": ph, "Authorization": auth})
    with urllib.request.urlopen(req, timeout=60) as r:
        return r.read().decode()


if len(sys.argv) != 2 or not re.fullmatch(r"[a-z]{4}", sys.argv[1]):
    print("usage: latest_check.py <product>", file=sys.stderr)
    sys.exit(2)
product = sys.argv[1]
t = tf()
akid, secret = t["r2-wx-read-id"], t["r2-wx-read-secret"]
prefix = f"wx/{product}/frames/"
keys, token = [], None
while True:
    q = {"list-type": "2", "prefix": prefix}
    if token:
        q["continuation-token"] = token
    x = s3_get(akid, secret, "", q)
    keys += re.findall(r"<Key>([^<]+)</Key>", x)
    m = re.search(r"<NextContinuationToken>([^<]+)</NextContinuationToken>", x)
    if "<IsTruncated>true</IsTruncated>" in x and m:
        token = m.group(1)
    else:
        break
epochs = sorted(int(k[len(prefix):-4]) for k in keys if re.fullmatch(re.escape(prefix) + r"\d+\.wxg", k))
try:
    latest_epoch = int(json.loads(s3_get(akid, secret, f"wx/{product}/latest"))["epoch"])
    latest_err = None
except Exception as err:  # noqa: BLE001 -- unreadable latest is BLIND, never a pass
    latest_epoch, latest_err = None, type(err).__name__
hms = lambda e: datetime.datetime.fromtimestamp(e, datetime.timezone.utc).strftime("%H:%M:%SZ")
listed = latest_epoch is not None and latest_epoch in epochs
print(json.dumps({"product": product, "read_at": hms(time.time()), "frames": len(epochs),
                  "newest": hms(epochs[-1]) if epochs else None, "oldest": hms(epochs[0]) if epochs else None,
                  "latest": hms(latest_epoch) if latest_epoch else None, "latest_err": latest_err,
                  "latest_listed": listed}))
sys.exit(0 if listed else 1)
