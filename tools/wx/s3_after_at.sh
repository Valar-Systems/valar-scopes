#!/usr/bin/env bash
# Wait until max(deploy + 72 min, first storing check + 62 min), then grade S3-3..S3-5 (s3_after.py).
#   bash tools/wx/s3_after_at.sh <product> <deploy ISO> <first ISO>
set -u
P="$1"; DEP="$2"; FIRST="$3"
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
HW="$(cygpath -m "$HERE" 2>/dev/null || echo "$HERE")"   # the same folder, as a path Windows Python can open
WT="$(cd "$HERE/../.." && pwd)"
SC="${S3_OUT:-$(mktemp -d)}"
a=$(( $(date -u -d "$DEP" +%s) + 4320 )); b=$(( $(date -u -d "$FIRST" +%s) + 3720 )); t=$(( a > b ? a : b ))
echo "$P after-window read starts $(date -u -d @$t +%H:%M:%SZ)"
w=$(( t - $(date -u +%s) )); [ $w -gt 0 ] && sleep $w
python "$HW/s3_after.py" "$P" "$DEP" "$FIRST" > "$SC/s3-after-$P.log" 2>&1; rc=$?
cat "$SC/s3-after-$P.log"; echo "s3_after exit $rc"
