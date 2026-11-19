#!/usr/bin/env bash
# The ingest deploy path (docs/wx-ingest-deploy.md, ruled 2026-11-11 ~04:35Z). Every weather ingest deploy goes through
# here, and nothing runs unless the caller holds the deploy claim for the Worker the deploy reaches. The claim check is
# proxy/scripts/deploy.sh's, unchanged but for its target; scripts/session-claim.py is not touched.
#
#   BLIPSCOPE_SESSION=<name> bash tools/wx/ingest_deploy.sh wx-ingest-opera.wrangler.toml <env>   an OPERA product
#   BLIPSCOPE_SESSION=<name> bash tools/wx/ingest_deploy.sh wx-ingest.wrangler.toml                cref (it has no envs)
#   BLIPSCOPE_SESSION=<name> bash tools/wx/ingest_deploy.sh wx-ingest-dev.wrangler.toml <env>      the dev Worker
#
# The claim to hold is the Worker's name without "blipscope-", then ":production" (e.g. wx-ingest-opuk:production). Take
# it first, and release it after your own read-back.
# Exit 2 usage (nothing read, nothing run); 1 refused (no claim, or no Python to read it with); otherwise the deploy's own.
set -u
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROXY="$(cd "$HERE/../../proxy" && pwd)"
TOML="${1:-}"
ENV="${2:-}"
usage() {
  echo "usage: ingest_deploy.sh wx-ingest.wrangler.toml | wx-ingest-opera.wrangler.toml <env> | wx-ingest-dev.wrangler.toml <env>" >&2
  exit 2
}

# ---- 1. usage, before the claim is read --------------------------------------
# Only the three ingest tomls. The proxy deploys through proxy/scripts/deploy.sh.
case "$TOML" in
  wx-ingest.wrangler.toml|wx-ingest-opera.wrangler.toml|wx-ingest-dev.wrangler.toml) ;;
  *) echo "ingest_deploy.sh: [$TOML] is not an ingest toml" >&2; usage ;;
esac
# The opera toml's top level is a Worker that must not exist (blipscope-wx-ingest-opera): without an env, a deploy would
# create it. The dev toml's deploys have always named an env.
REQUIRE_ENV=""
case "$TOML" in wx-ingest-opera.wrangler.toml|wx-ingest-dev.wrangler.toml) REQUIRE_ENV=1 ;; esac
if [ -n "$REQUIRE_ENV" ] && [ -z "$ENV" ]; then
  echo "ingest_deploy.sh: $TOML needs an env" >&2; usage
fi

# ---- 2. the target: the Worker this deploy reaches, from the toml wrangler reads
# The first Python that actually runs (deploy.sh's rule: on Windows, `python3` can be the Store's stub).
claim_python() { local c; for c in python3 python; do command -v "$c" >/dev/null 2>&1 && "$c" -c 'import sys' >/dev/null 2>&1 && { echo "$c"; return 0; }; done; return 1; }
CLAIM_PY="$(claim_python)" || { printf '\n\033[31mREFUSING TO DEPLOY\033[0m -- no working Python to read the deploy claim with.\n' >&2; exit 1; }
"$CLAIM_PY" -c 'import tomllib' >/dev/null 2>&1 || { printf '\n\033[31mREFUSING TO DEPLOY\033[0m -- %s has no tomllib to read %s with.\n' "$CLAIM_PY" "$TOML" >&2; exit 1; }
cd "$PROXY" || exit 1
# The relative name: Windows Python cannot open an MSYS path. Exit 3 means the env is not in the toml.
NAME="$("$CLAIM_PY" -c "
import sys, tomllib
c = tomllib.load(open(sys.argv[1], 'rb'))
e = sys.argv[2]
if e and e not in c.get('env', {}):
    sys.exit(3)
print((c['env'][e] if e else c)['name'])
" "$TOML" "$ENV")"
nrc=$?
if [ $nrc -eq 3 ]; then echo "ingest_deploy.sh: $TOML has no [env.$ENV]" >&2; usage; fi
if [ $nrc -ne 0 ] || [ -z "$NAME" ]; then echo "ingest_deploy.sh: could not read the Worker's name from $TOML (exit $nrc)" >&2; exit 1; fi
CLAIM_TARGET="${NAME#blipscope-}:production"

# ---- 3. the deploy claim (docs/session-claims.md): deploy.sh's check, unchanged
CLAIM_TOOL="$(cd "$HERE/../.." && pwd)/scripts/session-claim.py"
if ! "$CLAIM_PY" "$CLAIM_TOOL" check deploy --covers "$CLAIM_TARGET"; then
  printf '\n\033[31mREFUSING TO DEPLOY\033[0m -- this session does not hold the deploy claim for %s.\n' "$CLAIM_TARGET" >&2
  "$CLAIM_PY" "$CLAIM_TOOL" show >&2
  echo "Take it first:  BLIPSCOPE_SESSION=<name> python scripts/session-claim.py take deploy --covers $CLAIM_TARGET --until +45m" >&2
  exit 1
fi

# ---- 4. the deploy, then 5. the read-back ------------------------------------
EARGS=()
[ -n "$ENV" ] && EARGS=(-e "$ENV")
T="$(mktemp -d)"
TW="$(cygpath -m "$T" 2>/dev/null || echo "$T")"   # the same folder, as a path Windows Python can open
echo "deploy start $(date -u +%H:%M:%S.%3NZ): $NAME (-c $TOML${ENV:+ -e $ENV}), the claim held for $CLAIM_TARGET"
node scripts/wrangler-run.mjs deploy -c "$TOML" ${EARGS[@]+"${EARGS[@]}"} > "$T/dep.out" 2> "$T/dep.err" < /dev/null   # docs/wrangler-run.md
rc=$?
echo "deploy end $(date -u +%H:%M:%S.%3NZ) exit $rc"
tail -8 "$T/dep.out"
echo "stderr bytes $(wc -c < "$T/dep.err")"; head -5 "$T/dep.err"
node scripts/wrangler-run.mjs deployments status -c "$TOML" ${EARGS[@]+"${EARGS[@]}"} --json > "$T/ds.out" 2> "$T/ds.err" < /dev/null
"$CLAIM_PY" -c "import json,sys; d=json.load(open(sys.argv[1])); print('$NAME runs now:', [(v['version_id'][:8], v['percentage']) for v in d['versions']])" "$TW/ds.out" \
  || { echo "the read-back is unreadable"; head -3 "$T/ds.err"; }
rm -rf "$T"
# 6. the deploy's own exit code; the caller releases the claim after their read-back.
exit $rc
