"""3.3c's frames (docs/weatherscope-stage3.md A8.10): fetch the 15 SeamlessHSR GRIB2 files for the loop.

    python -I tools/weatherscope/loop/fetch_loop.py <empty-download-dir>

THE RULE, as frozen: the objects in CONUS/SeamlessHSR_00.00/20240521/ whose valid time (from the file name) is at or
before 22:00:59Z; the latest of those first, then every second object before it; 15 in all, written oldest first.
The files go to a directory OUTSIDE every repo and are never committed. Prints each key and its sha256 -- no
coordinate. Writes <dir>/fetched.json (key, valid time, bytes, sha256) for make_loop.ts and the manifest.
"""
import hashlib
import json
import os
import re
import sys
import urllib.request

BUCKET = "https://noaa-mrms-pds.s3.amazonaws.com"
PREFIX = "CONUS/SeamlessHSR_00.00/20240521/MRMS_SeamlessHSR_00.00_20240521-"
CUTOFF = "220059"
COUNT = 15
NAME = re.compile(r"MRMS_SeamlessHSR_00\.00_20240521-(\d{6})\.grib2\.gz$")


def list_keys():
    keys, token = [], None
    while True:
        url = f"{BUCKET}/?list-type=2&prefix={PREFIX}&max-keys=1000" + (f"&continuation-token={token}" if token else "")
        body = urllib.request.urlopen(url, timeout=60).read().decode()
        keys += re.findall(r"<Key>([^<]+)</Key>", body)
        m = re.search(r"<NextContinuationToken>([^<]+)</NextContinuationToken>", body)
        if not m:
            return keys
        token = urllib.parse.quote(m.group(1), safe="")


def main(argv):
    if len(argv) != 2:
        print(__doc__)
        return 2
    out = argv[1]
    if os.path.exists(out) and os.listdir(out):
        sys.exit(f"REFUSED: {out} is not empty")
    os.makedirs(out, exist_ok=True)
    timed = sorted((m.group(1), k) for k in list_keys() if (m := NAME.search(k)) and m.group(1) <= CUTOFF)
    print(f"objects at or before {CUTOFF}Z that day: {len(timed)}; the latest: {timed[-1][0] if timed else None}")
    picked = timed[::-1][::2][:COUNT][::-1]                 # the latest, then every second before it; oldest first
    if len(picked) != COUNT:
        sys.exit(f"STOP: the rule found {len(picked)} frames, not {COUNT}")
    rows = []
    for hhmmss, key in picked:
        data = None
        for attempt in range(3):                            # a network error retries; anything else stops
            try:
                data = urllib.request.urlopen(f"{BUCKET}/{key}", timeout=180).read()
                break
            except OSError as e:
                print(f"  {hhmmss}Z  attempt {attempt + 1} failed: {type(e).__name__}")
        if data is None:
            sys.exit(f"STOP: {key} could not be fetched in 3 attempts")
        name = os.path.basename(key)
        open(os.path.join(out, name), "wb").write(data)
        sha = hashlib.sha256(data).hexdigest()
        rows.append({"key": key, "valid": f"2024-05-21T{hhmmss[:2]}:{hhmmss[2:4]}:{hhmmss[4:]}Z", "file": name,
                     "bytes": len(data), "sha256": sha})
        print(f"  {hhmmss}Z  {len(data):9d} B  {sha[:16]}")
    json.dump(rows, open(os.path.join(out, "fetched.json"), "w"), indent=1)
    print(f"wrote {len(rows)} files and fetched.json to {out}")
    return 0


if __name__ == "__main__":
    import urllib.parse
    sys.exit(main(sys.argv))
