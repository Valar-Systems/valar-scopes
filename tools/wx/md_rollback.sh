#!/usr/bin/env bash
# opuk's rollback: the MD and RD rows' escape (docs/wx-opera-map-r2.md), moved to the opera toml by the code stage
# (docs/wx-opera-code-stage.md §7.2, ruled 2026-11-08 ~02:33Z).
#
# opuk's Worker, blipscope-wx-ingest-opuk, is [env.opuk] of proxy/wx-ingest-opera.wrangler.toml. That file's TOP LEVEL is
# another Worker, blipscope-wx-ingest-opera, which does not exist, so the env is named on every call: without it a rollback
# reaches nothing, and never opuk. -m and -y, and stdin closed, so it never stops at a prompt.
#
#   tools/wx/md_rollback.sh --dry             read-only: [env.opuk] names blipscope-wx-ingest-opuk; the Worker -e opuk
#                                             resolves to holds the target version; the top level resolves to no Worker
#                                             (the control, so the check can fail). Prints what opuk runs now.
#   tools/wx/md_rollback.sh "<the off row>"   rolls opuk back to $OPUK_ROLLBACK_TO (default: stage 1a's b7626571)
#
# A rollback is never refused (docs/session-claims.md). The claim hook cannot see one run inside a script, so this one
# writes its own line to rollbacks.log beside the claims, as the hook would. Announce it through Daniel.
set -u
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROXY="$(cd "$HERE/../../proxy" && pwd)"
TOML=wx-ingest-opera.wrangler.toml
ENV=opuk
WORKER=blipscope-wx-ingest-opuk
V="${OPUK_ROLLBACK_TO:-b7626571-e7dd-4f62-9afa-80f4e0e73d5b}"
T="$(mktemp -d)"
TW="$(cygpath -m "$T" 2>/dev/null || echo "$T")"   # the same folder, as a path Windows Python can open
cd "$PROXY" || exit 1

PY=""
for c in python python3; do "$c" -c "import tomllib" >/dev/null 2>&1 && { PY="$c"; break; }; done

if [ "${1:-}" = "--dry" ]; then
  fail=""
  # 1. The name, from the file wrangler reads, by a real TOML parser (the relative name: Windows Python cannot open an
  #    MSYS path).
  if [ -z "$PY" ]; then fail="$fail no-python-with-tomllib"; else
    names="$("$PY" -c "import tomllib,sys; c=tomllib.load(open(sys.argv[1],'rb')); print(c['env'][sys.argv[2]]['name'], c['name'])" "$TOML" "$ENV" 2>"$T/py.err")"
    envname="${names%% *}"; topname="${names##* }"
    echo "name: -e $ENV is [$envname]; the top level is [$topname]"
    [ "$envname" = "$WORKER" ] || fail="$fail env-name"
    [ "$topname" != "$WORKER" ] || fail="$fail top-level-is-opuk"
  fi
  # 2. The Worker -e opuk resolves to holds the rollback target (the API scopes a version to its script).
  npx wrangler versions view "$V" -c "$TOML" -e "$ENV" --json > "$T/vv.out" 2> "$T/vv.err" < /dev/null
  rc=$?; echo "target ${V:0:8} in the -e $ENV Worker: exit $rc"
  [ $rc -eq 0 ] || { fail="$fail target-not-found"; head -3 "$T/vv.err"; }
  npx wrangler deployments status -c "$TOML" -e "$ENV" --json > "$T/ds.out" 2> "$T/ds.err" < /dev/null
  rc=$?
  if [ $rc -eq 0 ] && [ -n "$PY" ]; then
    echo "opuk runs now: $("$PY" -c "import json,sys; d=json.load(open(sys.argv[1])); print([(v['version_id'][:8], v['percentage']) for v in d['versions']])" "$TW/ds.out")"
  else fail="$fail status-unreadable"; head -3 "$T/ds.err"; fi
  # 3. The control: the same read at the top level must reach no Worker.
  npx wrangler deployments status -c "$TOML" --json > "$T/top.out" 2> "$T/top.err" < /dev/null
  rc=$?; echo "control, the top level: exit $rc ($(grep -o '1000[0-9]' "$T/top.out" "$T/top.err" 2>/dev/null | head -1 | cut -d: -f2))"
  [ $rc -ne 0 ] || fail="$fail control-reached-a-worker"
  rm -rf "$T"
  if [ -z "$fail" ]; then echo "DRY PASS: the rollback names $WORKER"; exit 0; fi
  echo "DRY FAIL:$fail"; exit 1
fi

[ -n "${1:-}" ] || { echo "usage: md_rollback.sh --dry | md_rollback.sh \"<the off row>\""; exit 2; }
CMD="npx wrangler rollback $V -c $TOML -e $ENV -m \"opuk rollback: $1\" -y"
LOGDIR="${BLIPSCOPE_CLAIMS_DIR:-$HOME/.config/blipscope/claims}"
mkdir -p "$LOGDIR" && printf '%s %s %s\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ)" "${BLIPSCOPE_SESSION:-unnamed}" "${CMD:0:200}" >> "$LOGDIR/rollbacks.log"
echo "rollback start $(date -u +%H:%M:%S.%3NZ) (logged in rollbacks.log; announce it through Daniel)"
npx wrangler rollback "$V" -c "$TOML" -e "$ENV" -m "opuk rollback: $1" -y > "$T/rb.out" 2> "$T/rb.err" < /dev/null
rc=$?
echo "rollback end $(date -u +%H:%M:%S.%3NZ) exit $rc"
tail -6 "$T/rb.out"
echo "stderr bytes $(wc -c < "$T/rb.err")"; head -5 "$T/rb.err"
npx wrangler deployments status -c "$TOML" -e "$ENV" --json > "$T/ds.out" 2>/dev/null < /dev/null
[ -n "$PY" ] && "$PY" -c "import json,sys; d=json.load(open(sys.argv[1])); print([(v['version_id'][:8], v['percentage']) for v in d['versions']])" "$TW/ds.out"
rm -rf "$T"
exit $rc
