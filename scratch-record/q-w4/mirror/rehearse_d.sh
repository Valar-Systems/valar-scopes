#!/usr/bin/env bash
# SCRATCH: rehearsal of switch_d.py on a COPY of the cache's plan (mirror/rehearse-d/), with SYNTHETIC "mirror" zips, so every stage
# and every refusal is seen working before the real switch. The real cache is only READ (its plan's sha before and after), R0 runs the
# process guard on the COPY, where it must refuse (the retry is running). Predictions: mirror/rehearse_d.pred (written before this runs).
#   bash runlog.sh mirror-rehearse-d bash mirror/rehearse_d.sh      (from the scratchpad)
set -u
cd "$(dirname "$0")/.."
P="python -X utf8 -I mirror/switch_d.py"
R="mirror/rehearse-d"; RC="$R/cache"
real_plan="../bm-cache-qw4/plan.json"
step() { echo "--- $1"; shift; "$@"; echo "    exit $?"; }
echo "real plan before: $(sha256sum "$real_plan" | cut -c1-12)"
procs=$(powershell -NoProfile -Command "(Get-CimInstance Win32_Process | Where-Object { \$_.CommandLine -match 'ingest-basemap.ts' -and \$_.CommandLine -notmatch 'Get-CimInstance Win32_Process' } | Measure-Object).Count")
echo "ingest-basemap processes now: $procs"
rm -rf "$R"; mkdir -p "$RC"; cp "$real_plan" "$RC/plan.json"
if [ "$procs" -ge 1 ]; then step "R0 apply on the rehearsal copy WITHOUT the proc skip (must STOP on the process guard: the retry runs)" $P apply --cache "$RC"; else echo "R0 SKIPPED: no retry process seen, so the guard cannot be shown refusing"; fi
step "R8 a rehearsal flag on the REAL cache (must STOP)" $P final --rehearsal-skip-identity
step "R1 apply (rehearsal)" $P apply --cache "$RC" --rehearsal-skip-proc
step "R1b apply again (must STOP: already carries a mirror url)" $P apply --cache "$RC" --rehearsal-skip-proc
python -X utf8 -I - "$RC" <<'PYEOF'
# R2: synthetic "mirror" files and their retry records, as the ingest's retry would write them (not the real bytes, so identity fails)
import hashlib, json, os, sys
sys.path.insert(0, "mirror")
from common import eight, mirror_of
rc = sys.argv[1]; got = {}
for u in eight():
    m = mirror_of(u); p = os.path.join(rc, "files", *m.split("://")[1].split("/"))
    os.makedirs(os.path.dirname(p), exist_ok=True)
    b = bytes([0x50, 0x4B, 0x03, 0x04]) + ("SYNTHETIC " + m).encode()
    open(p, "wb").write(b)
    got[m] = {"bytes": len(b), "lastModified": None, "sha256": hashlib.sha256(b).hexdigest(), "vintage": "2024"}
open(os.path.join(rc, "retry-fetched.json"), "w", encoding="utf-8").write(json.dumps(got, indent=1, sort_keys=True) + "\n")
print(f"--- R2 synthetic mirror files written: {len(got)}")
PYEOF
step "R3 check (must report 8 DIFFERS, exit 1)" $P check --cache "$RC"
before="$(sha256sum "$RC/plan.json" "$RC/retry-fetched.json" | cut -c1-12 | tr '\n' ' ')"
step "R4 rekey without the skip (must STOP: identity did not pass)" $P rekey --cache "$RC"
echo "    plan + records unchanged by R4: $([ "$before" = "$(sha256sum "$RC/plan.json" "$RC/retry-fetched.json" | cut -c1-12 | tr '\n' ' ')" ] && echo yes || echo NO)"
step "R5 rekey (rehearsal, identity skipped)" $P rekey --cache "$RC" --rehearsal-skip-identity
step "R5b rekey again (must STOP: a record already exists under the primary url)" $P rekey --cache "$RC" --rehearsal-skip-identity
step "R6 final (rehearsal)" $P final --cache "$RC" --rehearsal-skip-identity
f="$RC/files/www2.census.gov/geo/tiger/TIGER2024/AREAWATER/tl_2024_44007_areawater.zip"
cp "$f" "$f.keep"; printf 'X' >> "$f"
step "R7 final with one primary copy tampered (must report 1 NOT OK, exit 1)" $P final --cache "$RC" --rehearsal-skip-identity
mv -f "$f.keep" "$f"
step "R7b final restored (must pass again)" $P final --cache "$RC" --rehearsal-skip-identity
echo "real plan after: $(sha256sum "$real_plan" | cut -c1-12)"
