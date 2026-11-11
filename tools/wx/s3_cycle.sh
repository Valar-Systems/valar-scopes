#!/usr/bin/env bash
# One product of stage 3 (docs/wx-opera-stage3.md §2), exactly as frozen: claim, ingest_deploy.sh, S3-1, S3-2, release.
# Any reading outside the prediction STOPS with the claim still held, so the escape (§4) runs under it.
#   BLIPSCOPE_SESSION is set below (uk-data); S3_OUT=<folder> keeps the outputs (default: a temp folder).
#   bash tools/wx/s3_cycle.sh <product>
# Exit 0 = S3-1 and S3-2 as predicted, claim released. Anything else = stop, claim held (unless the take failed).
set -u
P="$1"
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
HW="$(cygpath -m "$HERE" 2>/dev/null || echo "$HERE")"   # the same folder, as a path Windows Python can open
WT="$(cd "$HERE/../.." && pwd)"
SCB="${S3_OUT:-$(mktemp -d)}"; export S3_OUT="$SCB"   # where outputs land: $S3_OUT, or a fresh temp folder
SCW="$(cygpath -m "$SCB" 2>/dev/null || echo "$SCB")"
export BLIPSCOPE_SESSION=uk-data
cd "$WT" || exit 1
git fetch -q origin
echo "== $P  $(date -u +%H:%M:%SZ)  checkout $(git rev-parse --short=12 HEAD), porcelain $(git status --porcelain | wc -l), proxy/ files differing from origin/main: $(git diff --name-only HEAD origin/main -- proxy/ | wc -l)"
[ "$(git status --porcelain | wc -l)" -eq 0 ] && [ "$(git diff --name-only HEAD origin/main -- proxy/ | wc -l)" -eq 0 ] || { echo "STOP: the checkout is not main's proxy/"; exit 1; }

python scripts/session-claim.py take deploy --covers "wx-ingest-$P:production" --until +20m || { echo "STOP: claim not taken"; exit 1; }

bash tools/wx/ingest_deploy.sh wx-ingest-opera.wrangler.toml "$P" > "$SCB/dep-$P.out" 2>&1; rc=$?
cat "$SCB/dep-$P.out"
[ $rc -eq 0 ] || { echo "STOP: ingest_deploy.sh exit $rc (claim held)"; exit 1; }
END=$(grep -o -E 'deploy end [0-9:]+' "$SCB/dep-$P.out" | awk '{print $3}')
VID=$(grep -o -E 'Current Version ID: [0-9a-f-]{36}' "$SCB/dep-$P.out" | awk '{print $4}')
grep -q -E '^ *schedule: \* \* \* \* \*' "$SCB/dep-$P.out" && cron=ok || cron=MISSING
[ -n "$END" ] && [ -n "$VID" ] || { echo "STOP: no deploy end or version id in the output (claim held)"; exit 1; }
DEP="$(date -u +%Y-%m-%d)T${END}Z"

# S3-1: the version record and the deployment
cd proxy
npx wrangler versions view "$VID" -c wx-ingest-opera.wrangler.toml -e "$P" --json > "$SCB/v-$P.json" 2> "$SCB/v-$P.err" < /dev/null; v=$?
npx wrangler deployments status -c wx-ingest-opera.wrangler.toml -e "$P" --json > "$SCB/d-$P.json" 2> "$SCB/d-$P.err" < /dev/null; d=$?
cd ..
python "$HW/s31_check.py" "$SCW/v-$P.json" "$SCW/d-$P.json" "$P" "$VID" "$cron" "$v" "$d" || { echo "STOP: S3-1 off (claim held)"; exit 1; }

# S3-2
echo "S3-2 from $DEP"
bash "$HERE/s32_wait.sh" "$P" "$DEP"; s=$?
[ $s -eq 0 ] || { echo "STOP: S3-2 exit $s (claim held)"; exit 1; }
python scripts/session-claim.py release deploy
python scripts/session-claim.py show
echo "== $P done $(date -u +%H:%M:%SZ)"
