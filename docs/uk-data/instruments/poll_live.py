"""Instrument check for M2/O2: is LastModified the moment a frame becomes listable?

Polls both listings every 10 s for <minutes>, and for every frame key that appears after the first poll prints
V (from the key), LastModified, the first poll that saw it, and first_seen - LastModified. Printed, not graded.
Usage: python -I poll_live.py <minutes> <out.json>
"""
import json
import re
import sys
import time
import urllib.request
from datetime import datetime, timezone

MO = "https://met-office-radar-obs-data.s3.eu-west-2.amazonaws.com/?list-type=2&prefix=radar/2026/10/28/"
OP = "https://s3.waw3-1.cloudferro.com/openradar-24h/?list-type=2&prefix=2026/10/28/OPERA/COMP/OPERA@20261028T"


def listing(url):
    out, token = {}, None
    while True:
        u = url + (f"&continuation-token={urllib.request.quote(token)}" if token else "")
        x = urllib.request.urlopen(urllib.request.Request(u, headers={"User-Agent": "uk-data-plan-measurement/1"}), timeout=30).read().decode()
        for m in re.finditer(r"<Key>([^<]+)</Key><LastModified>([^<]+)</LastModified>", x):
            if m.group(1).endswith(".h5"):
                out[m.group(1)] = m.group(2)
        if "<IsTruncated>true</IsTruncated>" not in x:
            return out
        token = re.search(r"<NextContinuationToken>([^<]+)</NextContinuationToken>", x).group(1)


def ts(s):
    return datetime.fromisoformat(s.replace("Z", "+00:00")).timestamp()


minutes, path = float(sys.argv[1]), sys.argv[2]
seen = {}
first = True
errors = 0
end = time.time() + minutes * 60
while time.time() < end:
    for name, url in (("mo", MO), ("op", OP)):
        try:
            got = listing(url)
        except Exception as e:  # recorded, never hidden
            errors += 1
            print(f"poll error {name}: {e}", file=sys.stderr)
            continue
        now = time.time()
        for k, lm in got.items():
            if k not in seen:
                seen[k] = {"src": name, "lm": lm, "first_seen": None if first else now}
    first = False
    time.sleep(10)
new = [{"key": k.rsplit("/", 1)[-1], "src": v["src"], "lm": v["lm"],
        "first_seen": datetime.fromtimestamp(v["first_seen"], timezone.utc).strftime("%H:%M:%SZ"),
        "seen_minus_lm_s": round(v["first_seen"] - ts(v["lm"]), 1)}
       for k, v in sorted(seen.items()) if v["first_seen"] is not None]
json.dump({"errors": errors, "new": new}, open(path, "w"), indent=1)
print(json.dumps({"errors": errors, "new": new}, indent=1))
