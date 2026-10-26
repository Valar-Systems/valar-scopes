"""(e) driver: SeamlessHSR's 15 mi ring over change 1's 24 snowing hours at KMSP (addendum 6). Control first.
    python -I ring.py <scratchpad>
"""
import json, os, subprocess, sys
from datetime import datetime
SP = sys.argv[1]
DATA = os.path.join(SP, "wxdata2")
SPIKE = os.path.join(SP, "spike-a5db678")
HERE = os.path.dirname(os.path.abspath(__file__))
r2 = json.load(open(os.path.join(DATA, "results2.json")))
files = []
for c in r2["change1"]:
    t = datetime.fromisoformat(c["hsr_file"])
    files.append(os.path.join(DATA, "grib", "hsr", f"MRMS_SeamlessHSR_00.00_{t:%Y%m%d-%H%M%S}.grib2.gz"))
missing = [f for f in files if not os.path.exists(f)]
if missing:
    raise SystemExit(f"STOP: {len(missing)} of change 1's SeamlessHSR files are not on disk")
out = subprocess.run(["node", "--experimental-strip-types", os.path.join(HERE, "ring.ts"), SPIKE,
                      os.path.join(SPIKE, "tools", "wx", "airports-a2.txt"), "KMSP", *files], capture_output=True, text=True)
if out.returncode != 0:
    raise SystemExit(f"STOP: ring.ts failed\n{out.stderr[-600:]}")
rows = [json.loads(l) for l in out.stdout.splitlines() if l.strip()]
json.dump(rows, open(os.path.join(DATA, "ring.json"), "w"), indent=1)
assert len(rows) == 24, len(rows)
if not all(r["census_ok"] for r in rows):
    raise SystemExit("STOP: the census failed on a file (the -40 floor)")
ctl = [r for r in rows if r["validTimeUtc"].startswith("2024-12-02T06:52")]
print(f"CONTROL (F4's hour, 06:52Z file): ring max {ctl[0]['max'] if ctl else 'MISSING'} dBZ; frozen: >= 20 and < 30")
if not ctl or ctl[0]["max"] is None or not (20 <= ctl[0]["max"] < 30):
    raise SystemExit("STOP: the control failed -- the ring geometry or the decode disagrees with the screen")
print(f"ring cells per file: {sorted({r['ringCells'] for r in rows})}")
for r in rows:
    print(f"  {r['validTimeUtc'][:16]}Z  ring max {'no echo' if r['max'] is None else r['max']}")
for th, lo, hi in ((15, 40, 75), (10, 60, 90), (5, 75, 100)):
    n = sum(1 for r in rows if r["max"] is not None and r["max"] >= th)
    pct = 100 * n / 24
    print(f">= {th:2d} dBZ: the ring catches {n}/24 = {pct:.1f} % of snowing hours (predicted {lo}-{hi} %) -> {'as predicted' if lo <= pct <= hi else 'OFF-PREDICTION'}")
