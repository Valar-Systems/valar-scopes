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
# EXPECT_SHA=<the full sha the deploy plan names> is required, and ALLOW_BEHIND_SHAS=<full sha>,... names any commit behind
# main that touches proxy/ (docs/deploy-sha-pin.md); a dirty proxy/ or tools/wx/ is refused.
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

# ---- 3b. the commit the plan names, a clean tree, and the behind check by name (docs/deploy-sha-pin.md) --------------
# The owner's standing rule (2026-11-17), the same as proxy/scripts/deploy.sh's: EXPECT_SHA must be HEAD; ALLOW_BEHIND=1
# is retired; a tree behind main deploys only past the commits ALLOW_BEHIND_SHAS names, exactly. The blocking set here is
# the interim one -- every commit behind that touches proxy/, this script or scripts/session-claim.py -- which over-blocks
# (any proxy change needs naming) until deploy-inputs.mjs has an ingest target. And a dirty tree is refused.
fatal() { printf '\n\033[31mREFUSING TO DEPLOY\033[0m -- %s\n' "$1" >&2; }
ROOT="$(git -C "$HERE/../.." rev-parse --show-toplevel 2>/dev/null)"
if [ -z "$ROOT" ]; then
  fatal "not inside a git checkout, so nothing here can be verified."; exit 1
fi
HEAD_SHA="$(git -C "$ROOT" rev-parse HEAD)"
if [ -z "${EXPECT_SHA:-}" ]; then
  fatal "EXPECT_SHA is not set: the deploy plan names the commit it ships (docs/deploy-sha-pin.md)."; exit 1
fi
if ! [[ "${EXPECT_SHA:-}" =~ ^[0-9a-f]{40}$ ]]; then
  fatal "EXPECT_SHA must be a full 40-hex sha (got: ${EXPECT_SHA:-})"; exit 1
fi
if [ "$HEAD_SHA" != "$EXPECT_SHA" ]; then
  fatal "HEAD is not EXPECT_SHA: HEAD $HEAD_SHA, the plan names $EXPECT_SHA."; exit 1
fi
printf 'pinned: HEAD is EXPECT_SHA (%s)\n' "$HEAD_SHA"
if [ "${ALLOW_BEHIND:-}" = "1" ]; then
  fatal "ALLOW_BEHIND=1 is retired: name the commits behind with ALLOW_BEHIND_SHAS=<full sha>,..."; exit 1
fi
NAMED=()
if [ -n "${ALLOW_BEHIND_SHAS:-}" ]; then
  IFS=',' read -r -a NAMED_RAW <<< "$ALLOW_BEHIND_SHAS"
  for c in "${NAMED_RAW[@]}"; do
    if ! [[ "$c" =~ ^[0-9a-f]{40}$ ]]; then
      fatal "ALLOW_BEHIND_SHAS must list full 40-hex shas (got: $c)"; exit 1
    fi
    NAMED+=("$c")
  done
fi
DIRTY="$(git -C "$ROOT" status --porcelain -- proxy/ tools/wx/)"
if [ -n "$DIRTY" ]; then
  fatal "uncommitted changes under proxy/ or tools/wx/, and wrangler bundles the working tree."; printf '\n%s\n\n' "$DIRTY" >&2; exit 1
fi
git -C "$ROOT" fetch -q origin main 2>/dev/null || true
BLOCKING="$(git -C "$ROOT" rev-list HEAD..origin/main -- proxy/ tools/wx/ingest_deploy.sh scripts/session-claim.py 2>/dev/null | tr '\n' ' ')"
inlist() { local x=$1; shift; local y; for y in "$@"; do [ "$x" = "$y" ] && return 0; done; return 1; }
for c in $BLOCKING; do
  if ! inlist "$c" "${NAMED[@]}"; then
    fatal "behind and not named: ${c:0:12} (a merge after the plan; name it in ALLOW_BEHIND_SHAS, or re-plan)"; exit 1
  fi
done
for c in "${NAMED[@]}"; do
  if ! inlist "$c" $BLOCKING; then
    fatal "named but not behind: ${c:0:12} (the plan is stale)"; exit 1
  fi
done
for c in $BLOCKING; do
  echo "named and behind: ${c:0:12} -- named in the plan's ALLOW_BEHIND_SHAS; it is not in this tree, so this deploy does not ship it"
done

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
