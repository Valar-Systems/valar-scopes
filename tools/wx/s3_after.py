"""S3-3, S3-4, S3-5 for one product (docs/wx-opera-stage3.md §3), graded against the frozen predictions.
Read-only. Runs tools/wx/r3p.py and tools/wx/r3_list.py from the deploy checkout; prints counts, times, statuses and the
error strings (never a coordinate). Every Logs read is in 15-minute pieces at ABR 1; a sampled read is re-read once in
7.5-minute pieces, and a second sampled read is BLIND (= FAIL).

  python tools/wx/s3_after.py <product> <deploy ISO> <first storing check ISO>
Call it no earlier than max(deploy + 72 min, first + 62 min); it sleeps 600 s between S3-5's two listings.
Exit 0 every row as predicted; 1 a row off; 3 BLIND.
"""
import json
import os
import subprocess
import sys
import time
from datetime import datetime, timedelta, timezone

WT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))   # the checkout
BOUND = {"opmt": 152, "opsw": 411, "opce": 425, "opib": 443, "opis": 465, "opse": 477, "opba": 484, "opwe": 503,
         "opsn": 648, "opuk": 583}
p, dep, first = sys.argv[1], sys.argv[2], sys.argv[3]
iso = lambda t: t.strftime("%Y-%m-%dT%H:%M:%SZ")
pt = lambda s: datetime.fromisoformat(s.replace("Z", "+00:00"))
D, F = pt(dep), pt(first)
svc = f"blipscope-wx-ingest-{p}"
blind, off, notes = [], [], []


def r3p(args):
    for chunk in ("900", "450"):
        r = subprocess.run([sys.executable, "-I", "tools/wx/r3p.py", *args], cwd=WT, capture_output=True, text=True,
                           env={**os.environ, "R3P_CHUNK_S": chunk})
        if r.returncode == 0:
            return json.loads(r.stdout)
        if r.returncode != 2:
            blind.append(f"r3p {args[0]} exit {r.returncode}: {r.stderr[-200:]}")
            return None
        notes.append(f"r3p {args[0]} sampled at chunk {chunk}")
    blind.append(f"r3p {args[0]} sampled twice")
    return None


# S3-3: CPU over the 70 minutes from the deploy
c = r3p(["cpu", svc, p, iso(D), iso(D + timedelta(minutes=70))])
if c:
    outs = c["outcomes"]
    allmax = max([x for x in [c["single_build"]["max"], c["rest"]["max"]] + [b["cpu"] for b in c["beside_backfill_or_multi"]]
                  if isinstance(x, (int, float))], default=None)
    med = c["single_build"]["median"]
    s33 = []
    bound = int(os.environ.get("S3A_BOUND", BOUND[p]))   # seam: a plant must read OFF
    if med is None or med > bound: s33.append(f"single-build median {med} vs bound {bound}")
    if allmax is None or allmax > 6000: s33.append(f"max {allmax} vs 6000")
    if outs.get("exceededCpu", 0) or outs.get("exceededMemory", 0): s33.append(f"outcomes {outs}")
    if set(outs) - {"ok"}: notes.append(f"non-ok outcomes {outs}")
    print(f"S3-3 {p}: records {c['records']}, outcomes {outs}; single-build n {c['single_build']['n']} median {med} "
          f"(bound {BOUND[p]}) max {c['single_build']['max']}; rest n {c['rest']['n']} median {c['rest']['median']}; "
          f"beside {c['beside_backfill_or_multi']}; max of all {allmax}; p99 {c['p99_all']}; by map {c['single_build_by_map']}",
          "-> as predicted" if not s33 else f"-> OFF {s33}")
    off += [f"S3-3 {x}" for x in s33]

# S3-4: frames over the first full hour after the first storing check; errors by kind
h = r3p(["lines", svc, iso(F), iso(F + timedelta(minutes=60))])
if h:
    stored = [s for s in h["storing_checks"]]
    kinds = {}
    for e in h["errors"]:
        t = e["err"]
        k = "r2_10001" if "(10001)" in t else ("opera_list_timeout" if ("imeout" in t or "abort" in t) and e.get("attempts") == 3 else "OTHER")
        kinds.setdefault(k, []).append(f"{e['at']} a={e.get('attempts')} {t}")
    s34 = []
    if len(stored) < 10: s34.append(f"frames {len(stored)} < 10")
    if "OTHER" in kinds: s34.append(f"other errors {kinds['OTHER']}")
    print(f"S3-4 {p}: hour {iso(F)[11:]}-{iso(F + timedelta(minutes=60))[11:]}: frames stored {len(stored)} "
          f"({h['statuses']}); errors {len(h['errors'])} by kind {{{', '.join(f'{k}: {len(v)}' for k, v in kinds.items())}}}",
          "-> as predicted" if not s34 else f"-> OFF {s34}")
    for k, v in kinds.items():
        for line in v:
            print(f"   {k}: {line}")
    off += [f"S3-4 {x}" for x in s34]

# S3-5: pruneKeys on every storing line in the 70 minutes, then two listings 10 minutes apart
w = r3p(["lines", svc, iso(D), iso(D + timedelta(minutes=70))])
lists = []
if w:
    pk = sorted({s["pruneKeys"] for s in w["storing_checks"]}, key=str)
    s35 = [] if pk and set(pk) <= {900, 899} else [f"pruneKeys {pk}"]
for i in range(2):
    if i:
        time.sleep(int(os.environ.get("S3A_GAP_S", "600")))   # seam: controls only
    r = subprocess.run([sys.executable, "-I", "tools/wx/r3_list.py", f"wx/{p}/frames/"], cwd=WT, capture_output=True, text=True)
    if r.returncode != 0:
        blind.append(f"r3_list exit {r.returncode}: {r.stderr[-200:]}")
        break
    lists.append(json.loads(r.stdout))
if w and len(lists) == 2:
    s35 += [f"listing {x['read_at']} frames {x['frames']} > 13" for x in lists if x["frames"] > int(os.environ.get("S3A_MAXFRAMES", "13"))]
    if any(x["frames"] == 0 for x in lists): blind.append("an empty listing")
    print(f"S3-5 {p}: storing lines {len(w['storing_checks'])} pruneKeys {pk}; listings "
          + "; ".join(f"{x['read_at']} frames {x['frames']} (newest age {x['newest_age_s']} s, oldest {x['oldest_age_s']} s)" for x in lists),
          "-> as predicted" if not s35 else f"-> OFF {s35}")
    off += [f"S3-5 {x}" for x in s35]

for n in notes:
    print("note:", n)
if blind:
    print(f"BLIND {p}: {blind}")
    sys.exit(3)
print(f"== {p} after-window: " + ("every row as predicted" if not off else f"OFF: {off}"))
sys.exit(1 if off else 0)
