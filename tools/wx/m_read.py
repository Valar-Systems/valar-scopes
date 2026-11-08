"""M1-M5 for one dev product (docs/wx-opera-dev-measure.md): its storing checks, the counted builds (the `stored` builds
after the last backfill, up to 12), the CPU over those, and the CPU over the whole run for M3 and M4. All through r3p.py in
900 s pieces, and a refused read is re-read once at 450 s (the ~18:22Z amendment); a second refusal is BLIND.

Usage: python -I m_read.py <product> <deployed ISO> <parked ISO>
"""
import json
import os
import subprocess
import sys
from datetime import datetime, timedelta, timezone

here = os.path.dirname(os.path.abspath(__file__))
SVC = "blipscope-wx-ingest-dev"
product, a, b = sys.argv[1], sys.argv[2], sys.argv[3]


def r3p(*args):
    for chunk in ("900", "450"):
        r = subprocess.run([sys.executable, "-I", os.path.join(here, "r3p.py"), *args], capture_output=True, text=True,
                           encoding="utf-8", errors="replace", env={**os.environ, "R3P_CHUNK_S": chunk})
        if r.returncode != 2:
            return json.loads(r.stdout)
        print(f"refused at {chunk} s: {r.stdout.strip()[-120:]}")
    print("BLIND: the re-read was sampled too -> the row FAILS")
    sys.exit(2)


L = r3p("lines", SVC, a, b)
checks = [s for s in L["storing_checks"] if s["product"] == product]
last_bf = max((i for i, s in enumerate(checks) if s["status"] == "backfilled"), default=-1)
counted = [s for s in checks[last_bf + 1:] if s["status"] == "stored"][:12]
print(json.dumps({k: L[k] for k in ("abr_levels", "lines_by_product", "statuses", "map_on", "mapPutErrors", "errors")}))
print(f"storing checks {len(checks)}, counted {len(counted)}: {counted[0]['at'] if counted else None} .. "
      f"{counted[-1]['at'] if counted else None}")
day = a[:10]
t = lambda h: datetime.fromisoformat(f"{day}T{h[:8]}+00:00")
iso = lambda d: d.astimezone(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
keys = ("abr_levels", "records", "outcomes", "exceededCpu", "single_build", "single_build_by_map", "rest",
        "beside_backfill_or_multi", "p99_all")
C = r3p("cpu", SVC, product, iso(t(counted[0]["at"]) - timedelta(seconds=56)), iso(t(counted[-1]["at"]) + timedelta(seconds=25)))
print("counted:", json.dumps({k: C.get(k) for k in keys}))
W = r3p("cpu", SVC, product, a, b)
print("whole run:", json.dumps({k: W.get(k) for k in keys if k != "rest"}))
