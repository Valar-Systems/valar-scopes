#!/usr/bin/env bash
# SCRATCH: R12 (d), the three drivers once, in the LF clone sab-lf at the PR head 9008c74dee8f (docs/basemap-dense-code.md, "R12", and
# "MB9 ruled in"; frozen at 771afd34 and 9008c74d). Each driver starts only when TIME_WAIT is under 2,000 AND no process on the PC has
# `vitest` or `sabotage-` in its command line (id, name and start time printed, never the command line), polled every 15 s, capped at
# 20 min (else STOP, exit 2). A driver runs only if the one before read EXACTLY as predicted:
#   1. sabotage-basemap.mjs, whole: exit 0; baseline[ingest] 11 cases in 1 file, baseline[mv, -t MV] 31 in 1, baseline[all] 190 in 18,
#      baseline[sea] 14 in 1; every sabotage AS PREDICTED (C-LOADFAIL AS PREDICTED, BLIND); no OFF-PREDICTION, BLIND, RIG or RESTORE line;
#   2. sabotage-smoke-basemap.mjs: exit 0; "CONTROL (unsabotaged): 1091 passed, 1 skipped, of 1092; 0 failed"; "ALL AS REGISTERED";
#      no "raw output kept";
#   3. sabotage-credits.mjs: exit 0; "ALL AS REGISTERED".
# Outside the set: stop, and every stop is an instrument question for the reviewer, never a red.
cd "$(dirname "$0")/sab-lf/proxy" || exit 2
S="$(cd ../.. && pwd)"
tw() { netstat -an | grep -c TIME_WAIT; }
foreign() {
  powershell -NoProfile -Command "Get-CimInstance Win32_Process | Where-Object { \$_.CommandLine -match 'vitest|sabotage-' -and \$_.Name -ne 'powershell.exe' } | ForEach-Object { '{0} {1} {2:u}' -f \$_.ProcessId, \$_.Name, \$_.CreationDate.ToUniversalTime() }" 2>/dev/null | tr -d '\r' | grep -v '^\s*$'
}
wait_clear() {
  local end=$(( $(date +%s) + 1200 )) n f
  while :; do
    n=$(tw); f="$(foreign)"
    if [ "$n" -lt 2000 ] && [ -z "$f" ]; then echo "$1: TIME_WAIT $n, no vitest/sabotage- process, at $(date -u +%H:%M:%SZ)"; return 0; fi
    if [ "$(date +%s)" -gt "$end" ]; then echo "$1: STOP after 20 min: TIME_WAIT $n; processes: ${f:-none}"; return 2; fi
    sleep 15
  done
}
echo "tree $(git rev-parse --short=12 HEAD), porcelain lines $(git status --porcelain | wc -l); now TIME_WAIT $(tw)"
[ "$(git rev-parse HEAD)" = "9008c74dee8f223ddee1b9b41ff8df83241f7ea6" ] || { echo "STOP: not the PR head"; exit 2; }

wait_clear "basemap wait" || exit 2
timeout 9000 node scripts/sabotage-basemap.mjs > "$S/r12d-sab-basemap.log" 2>&1; rc=$?
echo "basemap driver exit $rc at $(date -u +%H:%M:%SZ), TIME_WAIT after $(tw)"
cat "$S/r12d-sab-basemap.log"
B="$S/r12d-sab-basemap.log"
ok=1
[ "$rc" -eq 0 ] || ok=0
grep -qx "baseline\[ingest\]: 0 failing cases (11 cases seen in 1 files)\( \[TIME_WAIT [0-9]* at start\]\)\?" "$B" || { echo "PRED: baseline[ingest] not as predicted"; ok=0; }
grep -qx "baseline\[mv, -t MV\]: 0 failing cases (31 cases seen in 1 files)\( \[TIME_WAIT [0-9]* at start\]\)\?" "$B" || { echo "PRED: baseline[mv] not as predicted"; ok=0; }
grep -qx "baseline\[all\]: 0 failing cases (190 cases seen in 18 files)\( \[TIME_WAIT [0-9]* at start\]\)\?" "$B" || { echo "PRED: baseline[all] not as predicted"; ok=0; }
grep -qx "baseline\[sea\]: 0 failing cases (14 cases seen in 1 files)\( \[TIME_WAIT [0-9]* at start\]\)\?" "$B" || { echo "PRED: baseline[sea] not as predicted"; ok=0; }
np=$(grep -c ": AS PREDICTED" "$B"); echo "AS PREDICTED lines: $np (predicted 89)"; [ "$np" -eq 89 ] || ok=0
grep -E "OFF-PREDICTION|: BLIND \(|UNREADABLE|RIG|RESTORE FAILED|BASELINE\[" "$B" && ok=0
echo "porcelain lines after the basemap driver: $(git status --porcelain | wc -l)"
[ "$ok" -eq 1 ] || { echo "basemap driver NOT as predicted: STOP (smoke and credits NOT run)"; exit 1; }

wait_clear "smoke wait" || exit 2
timeout 3600 node scripts/sabotage-smoke-basemap.mjs > "$S/r12d-sab-smoke.log" 2>&1; rc=$?
echo "smoke driver exit $rc at $(date -u +%H:%M:%SZ), TIME_WAIT after $(tw)"; cat "$S/r12d-sab-smoke.log"
[ "$rc" -eq 0 ] && grep -q "^CONTROL (unsabotaged): 1091 passed, 1 skipped, of 1092; 0 failed" "$S/r12d-sab-smoke.log" \
  && grep -q "^ALL AS REGISTERED" "$S/r12d-sab-smoke.log" && ! grep -q "raw output kept" "$S/r12d-sab-smoke.log" \
  || { echo "smoke NOT as predicted: STOP (credits NOT run)"; echo "porcelain lines after: $(git status --porcelain | wc -l)"; exit 1; }

wait_clear "credits wait" || exit 2
timeout 1800 node scripts/sabotage-credits.mjs > "$S/r12d-sab-credits.log" 2>&1; rc=$?
echo "credits driver exit $rc at $(date -u +%H:%M:%SZ), TIME_WAIT after $(tw)"; cat "$S/r12d-sab-credits.log"
echo "porcelain lines after: $(git status --porcelain | wc -l)"
[ "$rc" -eq 0 ] && grep -q "^ALL AS REGISTERED" "$S/r12d-sab-credits.log" || { echo "credits NOT as predicted"; exit 1; }
echo "R12 (d): all three drivers read as predicted"
