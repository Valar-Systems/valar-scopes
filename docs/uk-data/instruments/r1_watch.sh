#!/usr/bin/env bash
# R1's disk watcher, re-armed 2026-11-04. Below 5 GB free it PAUSES the collection itself (creates PAUSE, which the
# loop checks before every run) and logs it in loop.log, then exits 2 so the session is told too. An unreadable df
# also pauses (exit 3): a watcher that cannot see the disk must not let the loop write blind. It stops watching once
# the loop has logged its end.
D=/c/Github/Blipscope/bench-logs/uk-opera
echo "watch RE-ARMED $(date -u +%Y-%m-%dT%H:%M:%SZ), free $(df --output=avail -B1G /c | tail -1 | tr -d ' ')G" >> $D/loop.log
while :; do
  g=$(df --output=avail -B1G /c 2>/dev/null | tail -1 | tr -d ' G')
  case "$g" in
    ''|*[!0-9]*) echo "df unreadable at $(date -u +%H:%M:%SZ)" > $D/PAUSE; echo "watch: df UNREADABLE at $(date -u +%H:%M:%SZ) -- PAUSE created" >> $D/loop.log; exit 3 ;;
  esac
  if [ "$g" -lt 5 ]; then
    echo "${g}G free at $(date -u +%Y-%m-%dT%H:%M:%SZ)" > $D/PAUSE
    echo "watch: LOW DISK ${g}G free at $(date -u +%H:%M:%SZ) -- PAUSE created, collection paused" >> $D/loop.log
    exit 2
  fi
  grep -q "^loop ended" $D/loop.log && { echo "watch ended $(date -u +%H:%M:%SZ): the loop has ended" >> $D/loop.log; exit 0; }
  sleep 300
done
