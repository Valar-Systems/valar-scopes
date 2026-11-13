#!/usr/bin/env bash
# runlog.sh -- every selftest and driver run goes through here (the orchestrator, after 07:45Z 2026-11-13: the third filtered-output
# slip of the night was two selftests piped through `tail`, so the exit printed was tail's). Same control as push.sh:
#   - the command's stdout and stderr go each to its OWN log file, run-logs/<tag>-<UTC stamp>.out / .err -- never through a pipe;
#   - both are printed WHOLE after the run, each with its byte count;
#   - the exit status printed is the PROCESS's own ($? of the command itself), and runlog exits with it;
#   - the log paths are printed last.
#   bash runlog.sh <tag> <command> [args...]        (from anywhere; logs land beside this script)
set -u
S="$(cd "$(dirname "$0")" && pwd)"
if [ "$#" -lt 2 ]; then echo "usage: runlog.sh <tag> <command> [args...]"; exit 2; fi
tag="$1"; shift
case "$tag" in *[!A-Za-z0-9._-]*) echo "runlog: the tag must be [A-Za-z0-9._-]"; exit 2;; esac
stamp="$(date -u +%Y%m%dT%H%M%SZ)"
mkdir -p "$S/run-logs"
o="$S/run-logs/$tag-$stamp.out"; e="$S/run-logs/$tag-$stamp.err"
echo "=== runlog $tag at $(date -u +%FT%TZ) in $(pwd): $*"
"$@" > "$o" 2> "$e"
rc=$?
echo "=== stdout ($(wc -c < "$o") B, whole)"; cat "$o"
echo "=== stderr ($(wc -c < "$e") B, whole)"; cat "$e"
echo "=== exit $rc (the process's own) at $(date -u +%FT%TZ)"
echo "runlog: $o $e"
exit "$rc"
