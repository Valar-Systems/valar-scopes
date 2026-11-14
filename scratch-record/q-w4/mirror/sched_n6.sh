#!/usr/bin/env bash
# SCRATCH: the census MIRROR retry on the orchestrator's schedule (ruling (a), after 07:22:11Z 2026-11-14). One round at each of
# +60, +180, +360, +720 and +1440 min from this script's start. Each round is the ingest's own retry as its OWN process
# (--schedule-min 0, the 6 ftp2 urls, one a minute, with its UA), through runlog.sh, from sab-ingest/proxy (c3acc338, clean).
# Why a process per round rather than the ingest's in-process schedule: that schedule goes on to the next point after a partial
# round, and stopping it there would need a kill (the killed-driver shape). Each round's process ends by itself.
# Before each round: 0 ingest-basemap processes, and plan.json still step 1's 1cadb9712e03; else STOP (exit 3).
# The verdict after each round (sched_n6.pred), from m = ftp2 records in retry-fetched.json and the round's lines:
#   ALL        m 6, 6 fetched lines, 0 other, exit 0, stderr 0 B           -> exit 0: the schedule ends; resume at step 3
#   PARTIAL    m 1..5                                                       -> exit 4: STOP; n = 6 - m to the orchestrator
#   REFUSED    m 0, 6 refused lines, 0 fetched, 0 other, exit 1, stderr 0 B -> the next point; after +1440, exit 5: STOP, (c)
#   otherwise                                                                -> exit 3: STOP and decide
#   bash mirror/sched_n6.sh      (from the scratchpad; about 24 h; it ends itself)
set -u
S="$(cd "$(dirname "$0")/.." && pwd)"
CB="$S/../bm-cache-qw4"
C="$(cd "$CB" && pwd -W)"
U6="$(cat "$S/seastore/logs/n6-mirror-urls.txt")"
t0=$(date -u +%s)
say() { echo "$(date -u +%FT%TZ) $*"; }
say "sched_n6 armed; rounds at $(for p in 60 180 360 720 1440; do date -u -d @$((t0 + p * 60)) +%FT%H:%MZ; done | paste -sd' ' -)"
for p in 60 180 360 720 1440; do
  while [ "$(date -u +%s)" -lt $((t0 + p * 60)) ]; do sleep 60; done
  n=$(powershell -NoProfile -Command "(Get-CimInstance Win32_Process | Where-Object { \$_.CommandLine -match 'ingest-basemap' -and \$_.CommandLine -notmatch 'Get-CimInstance' } | Measure-Object).Count")
  [ "$n" = "0" ] || { say "STOP: $n ingest-basemap processes before round +$p"; exit 3; }
  ps=$(sha256sum "$CB/plan.json" | cut -c1-12)
  [ "$ps" = "1cadb9712e03" ] || { say "STOP: plan.json is $ps before round +$p, not step 1's 1cadb9712e03"; exit 3; }
  say "round +$p starts"
  (cd "$S/sab-ingest/proxy" && bash "$S/runlog.sh" "census-mirror-p$p" npx tsx scripts/ingest-basemap.ts retry --cache "$C" --schedule-min 0 --urls "$U6")
  rc=$?
  o=$(ls -t "$S"/run-logs/census-mirror-p$p-*.out | head -1); e="${o%.out}.err"
  read -r m f r x eb <<< "$(python -X utf8 -I "$S/mirror/sched_n6_counts.py" "$C" "$(cd "$(dirname "$o")" && pwd -W)/$(basename "$o")" "$(cd "$(dirname "$e")" && pwd -W)/$(basename "$e")")"
  say "round +$p: exit $rc; ftp2 records $m; fetched $f, refused $r, other $x; stderr $eb B"
  if [ "$m" = "6" ] && [ "$f" = "6" ] && [ "$x" = "0" ] && [ "$rc" = "0" ] && [ "$eb" = "0" ]; then say "VERDICT ALL: the 6 fetched at +$p; resume at step 3"; exit 0; fi
  if [ "$m" -ge 1 ] && [ "$m" -le 5 ]; then say "VERDICT PARTIAL: $m fetched at +$p, n = $((6 - m)); STOP"; exit 4; fi
  if [ "$m" = "0" ] && [ "$r" = "6" ] && [ "$f" = "0" ] && [ "$x" = "0" ] && [ "$rc" = "1" ] && [ "$eb" = "0" ]; then say "round +$p: all 6 refused; next point"; continue; fi
  say "VERDICT OUTSIDE: round +$p read none of ALL / PARTIAL / REFUSED; STOP and decide"; exit 3
done
say "VERDICT ALL REFUSED through +1440; STOP: (c) goes to the owner"
exit 5
