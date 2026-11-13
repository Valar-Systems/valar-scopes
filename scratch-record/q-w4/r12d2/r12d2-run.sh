#!/usr/bin/env bash
# SCRATCH: R12 (d)'s RE-RUN, as ruled after 05:0xZ, in the LF clone sab-lf2 at the PR head 4ac195bb6fa1 (docs/basemap-dense-code.md, "R12", and
# "MB9 ruled in"; frozen at 771afd34 and 9008c74d). Each driver starts only when TIME_WAIT is under 2,000 AND no process on the PC has
# `vitest` or `sabotage-` in its command line (id, name and start time printed, never the command line), polled every 15 s, capped at
# 20 min (else STOP, exit 2). A driver runs only if the one before read EXACTLY as predicted:
#   1. sabotage-basemap.mjs, whole: exit 0; baseline[ingest] 12 cases in 1 file, baseline[mv, -t MV] 31 in 1, baseline[all] 195 in 19,
#      baseline[sea] 15 in 1; all 93 sabotages AS PREDICTED (C-LOADFAIL AS PREDICTED, BLIND); no OFF-PREDICTION, BLIND, RIG or RESTORE line;
#   2. sabotage-smoke-basemap.mjs: exit 0; "CONTROL (unsabotaged): 1096 passed, 1 skipped, of 1097; 0 failed"; "ALL AS REGISTERED";
#      no "raw output kept";
#   (credits is not re-run: nothing it touches has changed, as ruled.)
# Outside the set: stop, and every stop is an instrument question for the reviewer, never a red.
cd "$(dirname "$0")/sab-lf2/proxy" || exit 2
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
[ "$(git rev-parse HEAD)" = "4ac195bb6fa139802b3392ceb71245b8e3adbf13" ] || { echo "STOP: not the PR head"; exit 2; }

wait_clear "basemap wait" || exit 2
timeout 9000 node scripts/sabotage-basemap.mjs > "$S/r12d2-sab-basemap.log" 2>&1; rc=$?
echo "basemap driver exit $rc at $(date -u +%H:%M:%SZ), TIME_WAIT after $(tw)"
cat "$S/r12d2-sab-basemap.log"
B="$S/r12d2-sab-basemap.log"
ok=1
[ "$rc" -eq 0 ] || ok=0
grep -qx "baseline\[ingest\]: 0 failing cases (12 cases seen in 1 files)\( \[TIME_WAIT [0-9]* at start\]\)\?" "$B" || { echo "PRED: baseline[ingest] not as predicted"; ok=0; }
grep -qx "baseline\[mv, -t MV\]: 0 failing cases (31 cases seen in 1 files)\( \[TIME_WAIT [0-9]* at start\]\)\?" "$B" || { echo "PRED: baseline[mv] not as predicted"; ok=0; }
grep -qx "baseline\[all\]: 0 failing cases (195 cases seen in 19 files)\( \[TIME_WAIT [0-9]* at start\]\)\?" "$B" || { echo "PRED: baseline[all] not as predicted"; ok=0; }
grep -qx "baseline\[sea\]: 0 failing cases (15 cases seen in 1 files)\( \[TIME_WAIT [0-9]* at start\]\)\?" "$B" || { echo "PRED: baseline[sea] not as predicted"; ok=0; }
np=$(grep -c ": AS PREDICTED" "$B"); echo "AS PREDICTED lines: $np (predicted 93)"; [ "$np" -eq 93 ] || ok=0
grep -E "OFF-PREDICTION|: BLIND \(|UNREADABLE|RIG|RESTORE FAILED|BASELINE\[" "$B" && ok=0
echo "porcelain lines after the basemap driver: $(git status --porcelain | wc -l)"
[ "$ok" -eq 1 ] || { echo "basemap driver NOT as predicted: STOP (smoke and credits NOT run)"; exit 1; }

wait_clear "smoke wait" || exit 2
timeout 3600 node scripts/sabotage-smoke-basemap.mjs > "$S/r12d2-sab-smoke.log" 2>&1; rc=$?
echo "smoke driver exit $rc at $(date -u +%H:%M:%SZ), TIME_WAIT after $(tw)"; cat "$S/r12d2-sab-smoke.log"
[ "$rc" -eq 0 ] && grep -q "^CONTROL (unsabotaged): 1096 passed, 1 skipped, of 1097; 0 failed" "$S/r12d2-sab-smoke.log" \
  && grep -q "^ALL AS REGISTERED" "$S/r12d2-sab-smoke.log" && ! grep -q "raw output kept" "$S/r12d2-sab-smoke.log" \
  || { echo "smoke NOT as predicted: STOP (credits NOT run)"; echo "porcelain lines after: $(git status --porcelain | wc -l)"; exit 1; }

echo "credits: not run (ruled: nothing it touches has changed)"
echo "R12 (d) re-run: the basemap and smoke drivers read as predicted"
