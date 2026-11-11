#!/usr/bin/env bash
# S3-2 (docs/wx-opera-stage3.md): poll Workers Logs (r3p.py lines, ABR 1, 15-minute pieces) for the product's first
# storing check after its deploy, then read latest (latest_check.py). Read-only. Prints counts, times, statuses only.
#   bash tools/wx/s32_wait.sh <product> <deploy ISO, e.g. 2026-11-11T15:00:15Z>
# Exit 0: a storing check timestamped within 15 min of the deploy AND latest names a listed frame.
# Exit 1: none by the deadline, or latest fails. Exit 2: a Logs read refused (sampled) twice in a row.
set -u
P="$1"; DEP="$2"
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
HW="$(cygpath -m "$HERE" 2>/dev/null || echo "$HERE")"   # the same folder, as a path Windows Python can open
WT="$(cd "$HERE/../.." && pwd)"
SC="${S3_OUT:-$(mktemp -d)}"; SC="$(cygpath -m "$SC" 2>/dev/null || echo "$SC")"
cd "$WT" || exit 1
dep_s=$(date -u -d "$DEP" +%s); limit_s=$((dep_s + 900)); give_up_s=$((dep_s + 1500))   # lines may land late: read to +25 min
sampled=0
while :; do
  now=$(date -u +%Y-%m-%dT%H:%M:%SZ); now_s=$(date -u +%s)
  R3P_CHUNK_S=900 python -I tools/wx/r3p.py lines "blipscope-wx-ingest-$P" "$DEP" "$now" > "$SC/s32-$P.json" 2> "$SC/s32-$P.err"; rc=$?
  if [ $rc -eq 2 ]; then sampled=$((sampled + 1)); echo "$now sampled read ($sampled)"; [ $sampled -ge 2 ] && exit 2
  elif [ $rc -ne 0 ]; then echo "$now r3p exit $rc: $(head -c 200 "$SC/s32-$P.err")"
  else
    sampled=0
    first=$(python -c "
import json,sys
d=json.load(open(sys.argv[1]))
f=d['first_storing_check']
print('NONE' if not f else f\"{f['at']} {f['status']} pruneKeys={f['pruneKeys']}\")
print('statuses', d['statuses'], 'errors', len(d['errors']), 'events', d['events'], 'abr', d['abr_levels'])
" "$SC/s32-$P.json")
    echo "$now $(echo "$first" | tr '\n' ' ')"
    fs=$(echo "$first" | head -1)
    if [ "$fs" != "NONE" ]; then
      at=${fs%% *}; at_s=$(date -u -d "${DEP%%T*}T$at" +%s)
      echo "first storing check $at: $((at_s - dep_s)) s after the deploy (limit 900)"
      python -I "$HW/latest_check.py" "$P"; lrc=$?
      [ $at_s -le $limit_s ] && [ $lrc -eq 0 ] && exit 0
      exit 1
    fi
  fi
  [ "$now_s" -ge "$give_up_s" ] && { echo "no storing check by $(date -u -d @$give_up_s +%H:%M:%SZ)"; exit 1; }
  sleep 60
done
