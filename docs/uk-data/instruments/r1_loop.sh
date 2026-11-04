#!/usr/bin/env bash
# R1's collection loop, re-armed 2026-11-04 by Daniel's "1" (resume, watcher re-armed). Same cycle and end as frozen
# (every 3 h to 2026-11-04T16:00Z; the last run lands just after the end, as the original loop's did). New: a run is
# skipped while $OUT/PAUSE exists -- the disk watcher (r1_watch.sh) creates it below 5 GB, so the pause no longer
# waits for anyone to read a notification.
SP="/c/Users/DANIEL~1/AppData/Local/Temp/claude/c--Github-Blipscope/fb8462e7-ee3a-4757-8b09-2d0165eb9254/scratchpad"
OUT="C:/Github/Blipscope/bench-logs/uk-opera"
LOG=/c/Github/Blipscope/bench-logs/uk-opera/loop.log
END=$(date -u -d "2026-11-04T16:00:00Z" +%s)
echo "loop RE-ARMED $(date -u +%Y-%m-%dT%H:%M:%SZ), ends $(date -u -d @$END +%Y-%m-%dT%H:%MZ); runs skip while PAUSE exists" >> "$LOG"
while [ "$(date -u +%s)" -lt "$END" ]; do
  sleep 10800
  if [ -e /c/Github/Blipscope/bench-logs/uk-opera/PAUSE ]; then
    echo "run SKIPPED at $(date -u +%H:%M:%SZ): PAUSE present ($(cat /c/Github/Blipscope/bench-logs/uk-opera/PAUSE))" >> "$LOG"
    continue
  fi
  "$SP/venv/Scripts/python.exe" -I "$OUT/collect_opera.py" "$OUT" "$OUT/ref-20261028T0355-DBZH.h5" >> "$LOG" 2>> /c/Github/Blipscope/bench-logs/uk-opera/loop.err
  echo "run exit $? at $(date -u +%H:%M:%SZ)" >> "$LOG"
done
echo "loop ended $(date -u +%Y-%m-%dT%H:%M:%SZ)" >> "$LOG"
