"""B (docs/uk-data.md A3.20-A3.22): fetch OPERA's DBZH composite GeoTIFFs for the coverage survey. Read-only, from the
public source the ingest reads (openradar-24h), one request at a time: the day's listing (paged), then each requested
hour's frame. Writes only into <out_dir>; skips a frame already there with its recorded sha256; stops before 1 GB.
Prints, per frame: key, HTTP status, bytes, sha256. Never writes anywhere else.

  python -I b_fetch.py <out_dir> <YYYY-MM-DD> <first hour> <last hour>
"""
import hashlib
import json
import os
import re
import sys
import time
import urllib.parse
import urllib.request

BUCKET = "https://s3.waw3-1.cloudferro.com/openradar-24h"   # proxy/src/wx/ingest.ts OPERA_BUCKET
UA = "BlipscopeUkSurvey/1 (read-only coverage survey; +https://github.com/Valar-Systems/valar-scopes)"
CAP = 1_000_000_000

out, day, h0, h1 = sys.argv[1], sys.argv[2], int(sys.argv[3]), int(sys.argv[4])
os.makedirs(out, exist_ok=True)
manifest_path = os.path.join(out, "manifest.json")
manifest = json.load(open(manifest_path, encoding="utf-8")) if os.path.exists(manifest_path) else {}


def get(url, timeout=120):
    req = urllib.request.Request(url, headers={"User-Agent": UA})
    with urllib.request.urlopen(req, timeout=timeout) as r:
        return r.status, r.read()


# the day's listing, paged (one request at a time)
y, m, d = day.split("-")
prefix = f"{y}/{m}/{d}/OPERA/COMP/"
keys, token, pages = [], None, 0
while True:
    q = {"list-type": "2", "prefix": prefix}
    if token:
        q["continuation-token"] = token
    st, body = get(f"{BUCKET}/?{urllib.parse.urlencode(q)}")
    pages += 1
    x = body.decode("utf-8", "replace")
    keys += re.findall(r"<Key>([^<]+)</Key>", x)
    t = re.search(r"<NextContinuationToken>([^<]+)</NextContinuationToken>", x)
    if "<IsTruncated>true</IsTruncated>" in x and t:
        token = t.group(1)
    else:
        break
want = {f"{y}{m}{d}T{h:02d}00": h for h in range(h0, h1 + 1)}
found = {}
for k in keys:
    mm = re.search(r"/OPERA@(\d{8}T\d{4})@0@DBZH\.tiff$", k)
    if mm and mm.group(1) in want:
        found[mm.group(1)] = k
print(json.dumps({"listing_pages": pages, "keys_listed": len(keys), "hours_wanted": len(want), "hours_listed": len(found),
                  "missing": sorted(set(want) - set(found))}))
total = sum(v["bytes"] for v in manifest.values())
for stamp in sorted(found):
    key = found[stamp]
    path = os.path.join(out, stamp + ".tiff")
    if stamp in manifest and os.path.exists(path) and hashlib.sha256(open(path, "rb").read()).hexdigest() == manifest[stamp]["sha256"]:
        print(f"{stamp} already here, {manifest[stamp]['bytes']} B")
        continue
    if total > CAP:
        print(f"STOP: {total} B fetched, over the 1 GB cap")
        break
    t0 = time.time()
    try:
        st, data = get(f"{BUCKET}/{urllib.parse.quote(key)}", timeout=300)
    except Exception as err:   # noqa: BLE001 -- reported, never retried in a loop
        print(f"{stamp} FAILED {type(err).__name__}: {str(err)[:120]}")
        continue
    sha = hashlib.sha256(data).hexdigest()
    open(path, "wb").write(data)
    total += len(data)
    manifest[stamp] = {"key": key, "status": st, "bytes": len(data), "sha256": sha, "fetched": time.strftime("%H:%M:%SZ", time.gmtime())}
    json.dump(manifest, open(manifest_path, "w", encoding="utf-8"), indent=1)
    print(f"{stamp} HTTP {st} {len(data)} B sha256 {sha[:16]}... in {time.time() - t0:.1f} s (total {total} B)")
print(json.dumps({"frames_here": len(manifest), "bytes_total": total}))
