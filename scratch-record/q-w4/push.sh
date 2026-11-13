#!/usr/bin/env bash
# map-server's ONLY push path (the orchestrator, 2026-11-12 18:25:31Z, after two pushes piped through `tail` lost a failure's cause).
# There is no pipe at the call site because the call site is this script: every output is written to a file and printed WHOLE.
#   bash push.sh <worktree dir> <refspec>        e.g.  bash push.sh wt-q4 HEAD:refs/heads/map-server/kb8a-registration
# 1. the three guards (check_device_ids, check_wifi, check_macs), each printed whole, any failure stops before the push;
# 2. git push origin <refspec> > push.log 2>&1, then the WHOLE log and exit=<rc>;
# 3. the remote ref re-read (ls-remote), printed whole.
# SINCE 2026-11-13 ~05:15Z (the orchestrator, after four calls of this script were piped through `tail`): EVERY line also goes to a log file
# of its own, push-logs/push-<sha12>-<UTC stamp>.log beside this script, whatever the console does with it (`tee -p` keeps writing the
# file when a console pipe closes early), and the log's path is the LAST line printed. A filtered console can no longer lose the record.
set -u
DIR="${1:?worktree dir}"; REFSPEC="${2:?refspec}"
SCR="$(cd "$(dirname "$0")" && pwd)"
cd "$DIR" || { echo "push.sh: no such dir $DIR"; exit 2; }
mkdir -p "$SCR/push-logs"
LOG="$SCR/push-logs/push-$(git rev-parse --short=12 HEAD)-$(date -u +%Y%m%dT%H%M%SZ).log"
out() { tee -p -a "$LOG"; }
finish() { echo "push.sh log: $LOG" | out; exit "$1"; }
echo "=== push.sh $DIR $REFSPEC at $(date -u +%FT%TZ)" | out
LOGDIR="$(mktemp -d)"
for s in check_device_ids check_wifi check_macs; do
  timeout 300 python "scripts/$s.py" > "$LOGDIR/$s.log" 2>&1; rc=$?
  { echo "=== $s exit=$rc"; cat "$LOGDIR/$s.log"; } | out
  if [ "$rc" -ne 0 ]; then echo "push.sh: $s failed; NOT pushing" | out; finish 1; fi
done
git push origin "$REFSPEC" > "$LOGDIR/push.log" 2>&1; PRC=$?
{ echo "=== git push origin $REFSPEC (whole log)"; cat "$LOGDIR/push.log"; echo "=== push exit=$PRC"; } | out
REF="${REFSPEC##*:}"; case "$REF" in refs/*) ;; *) REF="refs/heads/$REF" ;; esac
git ls-remote origin "$REF" > "$LOGDIR/ls.log" 2>&1; LRC=$?
{ echo "=== ls-remote origin $REF exit=$LRC (whole)"; cat "$LOGDIR/ls.log"; echo "=== local HEAD $(git rev-parse HEAD)"; date -u +%FT%TZ; } | out
finish "$PRC"
