"""R3-5 (docs/wx-opuk.md): a direct S3-API listing of one prefix of blipscope-wx, read-only, with the token file's
r2-wx-read-id / r2-wx-read-secret (never printed). SigV4 by hand (no SDK). Prints counts and ages, never a key's value.

Usage: python -I r3_list.py <prefix>      e.g. wx/opuk/frames/
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


def tf():
    d = {}
    for line in open(os.path.expanduser("~/.config/blipscope/tokens"), encoding="utf-8"):
        k, _, v = line.strip().partition("=")
        if k and v:
            d[k] = v
    return d


def sign(key, msg):
    return hmac.new(key, msg.encode(), hashlib.sha256).digest()


def list_page(akid, secret, prefix, token=None):
    q = {"list-type": "2", "prefix": prefix}
    if token:
        q["continuation-token"] = token
    qs = "&".join(f"{urllib.parse.quote(k, safe='-_.~')}={urllib.parse.quote(v, safe='-_.~')}" for k, v in sorted(q.items()))
    now = datetime.datetime.now(datetime.timezone.utc)
    amz, day = now.strftime("%Y%m%dT%H%M%SZ"), now.strftime("%Y%m%d")
    ph = hashlib.sha256(b"").hexdigest()
    canon = f"GET\n/{BUCKET}\n{qs}\nhost:{HOST}\nx-amz-content-sha256:{ph}\nx-amz-date:{amz}\n\nhost;x-amz-content-sha256;x-amz-date\n{ph}"
    scope = f"{day}/auto/s3/aws4_request"
    sts = f"AWS4-HMAC-SHA256\n{amz}\n{scope}\n{hashlib.sha256(canon.encode()).hexdigest()}"
    k = sign(sign(sign(sign(("AWS4" + secret).encode(), day), "auto"), "s3"), "aws4_request")
    sig = hmac.new(k, sts.encode(), hashlib.sha256).hexdigest()
    auth = f"AWS4-HMAC-SHA256 Credential={akid}/{scope}, SignedHeaders=host;x-amz-content-sha256;x-amz-date, Signature={sig}"
    req = urllib.request.Request(f"https://{HOST}/{BUCKET}?{qs}",
                                 headers={"x-amz-date": amz, "x-amz-content-sha256": ph, "Authorization": auth})
    with urllib.request.urlopen(req, timeout=60) as r:
        return r.read().decode()


t = tf()
akid, secret = t["r2-wx-read-id"], t["r2-wx-read-secret"]
prefix = sys.argv[1]
keys, token = [], None
while True:
    x = list_page(akid, secret, prefix, token)
    keys += re.findall(r"<Key>([^<]+)</Key>", x)
    m = re.search(r"<NextContinuationToken>([^<]+)</NextContinuationToken>", x)
    if "<IsTruncated>true</IsTruncated>" in x and m:
        token = m.group(1)
    else:
        break
now = time.time()
ages = sorted(now - int(k.rsplit("/", 1)[1].split(".")[0]) for k in keys if re.search(r"/\d+\.wxg$", k))
print(json.dumps({"prefix": prefix, "read_at": datetime.datetime.now(datetime.timezone.utc).strftime("%H:%M:%SZ"),
                  "objects": len(keys), "frames": len(ages),
                  "newest_age_s": round(ages[0]) if ages else None, "oldest_age_s": round(ages[-1]) if ages else None}))
