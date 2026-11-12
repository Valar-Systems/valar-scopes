"""The 1c redeploy's frame row (docs/wx-opera-map-r2.md section 7): copy every opuk frame in the production bucket to a local
folder, for frame_same.mts to compare. Read-only: the S3 API with the token file's r2-wx-read pair, never printed. Frames only;
prints counts and epochs. Usage: python -I frames_get.py <out dir>
"""
import datetime
import hashlib
import hmac
import json
import os
import re
import sys
import urllib.parse
import urllib.request

ACCOUNT = "48822e896bb10c45aa6bfe139bcff3d1"
BUCKET = "blipscope-wx"
HOST = f"{ACCOUNT}.r2.cloudflarestorage.com"
PREFIX = "wx/opuk/frames/"
import importlib.util as _ilu   # the token-file helper, by path: python -I drops this folder from sys.path
_tfs = _ilu.spec_from_file_location("token_file", os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "token_file.py"))
TOKEN_FILE = _ilu.module_from_spec(_tfs); _tfs.loader.exec_module(TOKEN_FILE)   # docs/token-file-python.md
T = TOKEN_FILE.tokens("r2-wx-read-id", "r2-wx-read-secret")


def s3_get(path, q=None):
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
    k = sign(sign(sign(sign(("AWS4" + T["r2-wx-read-secret"]).encode(), day), "auto"), "s3"), "aws4_request")
    sig = hmac.new(k, sts.encode(), hashlib.sha256).hexdigest()
    auth = (f"AWS4-HMAC-SHA256 Credential={T['r2-wx-read-id']}/{scope}, "
            f"SignedHeaders=host;x-amz-content-sha256;x-amz-date, Signature={sig}")
    req = urllib.request.Request(f"https://{HOST}{uri}" + (f"?{qs}" if qs else ""),
                                 headers={"x-amz-date": amz, "x-amz-content-sha256": ph, "Authorization": auth})
    with urllib.request.urlopen(req, timeout=60) as r:
        return r.read()


out = sys.argv[1]
os.makedirs(out, exist_ok=True)
for f in os.listdir(out):
    if f.endswith(".wxg"):
        os.remove(os.path.join(out, f))
x = s3_get("", {"list-type": "2", "prefix": PREFIX}).decode()
keys = re.findall(r"<Key>([^<]+)</Key>", x)
epochs = []
for k in keys:
    m = re.search(r"/(\d+)\.wxg$", k)
    if not m:
        continue
    open(os.path.join(out, m.group(1) + ".wxg"), "wb").write(s3_get(k))
    epochs.append(int(m.group(1)))
print(json.dumps({"frames": len(epochs), "oldest": min(epochs, default=None), "newest": max(epochs, default=None)}))
