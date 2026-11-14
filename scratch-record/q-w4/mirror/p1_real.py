# SCRATCH, read-only: switch_n6.pred step 6 (P1 on the REAL cache). It prints the 2 primary-fetched files' and records' sha256,
# computed exactly as rh_n6.py's `p1` (file sha12; record sha12 of json.dumps(record, sort_keys=True)). Expected at every print:
# 27081 file 1ea420092897 record fd7cb404eb9d; 54093 file 502372153bb1 record 12b67b948a01 (the rehearsal's byte copies of them).
#   python -X utf8 -I mirror/p1_real.py <label>
import hashlib, json, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import C, eight

sha = lambda b: hashlib.sha256(b).hexdigest()
local = lambda u: os.path.join(C, "files", *u.split("://")[1].split("/"))
r = json.load(open(os.path.join(C, "retry-fetched.json"), encoding="utf-8"))
out = []
for u in [u for u in eight() if u.endswith(("27081_linearwater.zip", "54093_linearwater.zip"))]:
    f = sha(open(local(u), "rb").read())[:12] if os.path.exists(local(u)) else "absent"
    out.append(f"{u.rsplit('/', 1)[1]} file {f} record {sha(json.dumps(r.get(u), sort_keys=True).encode())[:12]}")
print(f"P1 real {sys.argv[1]}: " + "; ".join(out))
