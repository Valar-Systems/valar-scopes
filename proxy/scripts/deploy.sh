#!/usr/bin/env bash
# deploy.sh -- the only supported way to put this Worker into an environment.
#
# WHY THIS EXISTS. `wrangler deploy` bundles the WORKING TREE, not a commit. That
# is a permanent hazard here, not a one-off: several sessions and a human all edit
# this checkout, so at any moment it can hold work that belongs to someone else's
# unmerged branch. A deploy from that state ships code that appears in no PR, no
# review and no commit -- and nothing afterwards can tell you it happened, because
# the deployment record only carries a timestamp.
#
# It nearly happened on 2026-08-07: an unrelated relay-partitioning change to
# src/upstreams/chain.ts sat uncommitted in this tree while the support page was
# being deployed. The only thing that kept it out of production was a 23-minute
# gap between the deploy and the edit. That is luck, and luck is not a control.
#
# So: this refuses to deploy a dirty proxy/ tree, refuses to deploy a commit that
# is not on the remote, and stamps the commit it did deploy into the bundle so
# /healthz can be asked afterwards instead of guessed at.
#
#   ./scripts/deploy.sh production
#   ./scripts/deploy.sh staging
#
# Escape hatches, both deliberately loud and neither silent:
#   ALLOW_DIRTY=1     deploy a dirty tree anyway; prints the diff first and stamps
#                     the commit as <sha>-dirty, because a stamp that claims a
#                     clean commit while shipping extra changes is worse than none
#   ALLOW_UNPUSHED=1  deploy a commit that exists only locally

set -uo pipefail

ENVIRONMENT="${1:-}"
case "$ENVIRONMENT" in
  production|staging) ;;
  *) echo "usage: $0 <production|staging>" >&2; exit 2 ;;
esac

ROOT="$(git rev-parse --show-toplevel 2>/dev/null)"
if [ -z "$ROOT" ]; then
  echo "FATAL: not inside a git checkout, so nothing here can be verified." >&2
  exit 1
fi
cd "$ROOT" || exit 1

fatal() { printf '\n\033[31mREFUSING TO DEPLOY\033[0m -- %s\n' "$1" >&2; }

# ---- 1. the bundle's own directory must be clean ----------------------------
# Scoped to proxy/ because that is what `main = src/index.ts` pulls in. Untracked
# files count: an untracked .ts under src/ is every bit as importable as a tracked
# one, and "it wasn't in git" is exactly the property that makes it dangerous.
DIRTY="$(git status --porcelain -- proxy/)"
if [ -n "$DIRTY" ]; then
  if [ "${ALLOW_DIRTY:-}" != "1" ]; then
    fatal "proxy/ has uncommitted changes, and wrangler bundles the working tree."
    printf '\n%s\n\n' "$DIRTY" >&2
    echo "Commit them, stash them, or deploy from a clean checkout." >&2
    echo "To ship them anyway, knowing they are in no PR:  ALLOW_DIRTY=1 $0 $ENVIRONMENT" >&2
    exit 1
  fi
  printf '\n\033[33mALLOW_DIRTY=1\033[0m -- deploying uncommitted changes:\n\n%s\n' "$DIRTY"
  printf '\n--- full diff of what is being shipped beyond the commit ---\n'
  git --no-pager diff -- proxy/
  printf -- '--- end diff ---\n\n'
fi

# Dirt outside proxy/ cannot reach the bundle, so it is worth saying and not worth
# blocking on -- a blocker that fires on unrelated work teaches people to bypass it.
OTHER="$(git status --porcelain -- . ':(exclude)proxy/')"
[ -n "$OTHER" ] && printf '\033[33mnote\033[0m: changes outside proxy/ (not bundled, not blocking):\n%s\n\n' "$OTHER"

# ---- 2. the commit must exist somewhere other than this laptop --------------
# "Production diverged from every PR" is unprovable after the fact if the commit
# was never pushed. Requiring it on a remote keeps the deployment record joinable
# to something reviewable.
if [ -z "$(git branch -r --contains HEAD 2>/dev/null)" ]; then
  if [ "${ALLOW_UNPUSHED:-}" != "1" ]; then
    fatal "HEAD is not on any remote branch, so production would not correspond to anything reviewable."
    echo "  push it first, or:  ALLOW_UNPUSHED=1 $0 $ENVIRONMENT" >&2
    exit 1
  fi
  printf '\033[33mALLOW_UNPUSHED=1\033[0m -- deploying a commit that exists only locally.\n\n'
fi

# ---- 3. stamp what is actually being shipped --------------------------------
SHA="$(git rev-parse --short HEAD)"
[ -n "$DIRTY" ] && SHA="${SHA}-dirty"
BRANCH="$(git rev-parse --abbrev-ref HEAD)"

printf 'deploying to \033[1m%s\033[0m\n' "$ENVIRONMENT"
printf '  commit  %s  (%s)\n' "$SHA" "$BRANCH"
printf '  subject %s\n\n' "$(git log -1 --format=%s)"

cd "$ROOT/proxy" || exit 1
npx wrangler deploy --env "$ENVIRONMENT" --define BUILD_COMMIT:"\"$SHA\"" || exit 1

# ---- 4. prove it, rather than assuming the upload implies it ----------------
# Edge isolates drain for a few minutes after a deploy, so an immediate check can
# still be answered by the old version. This confirms the stamp eventually, and
# says plainly when it has not settled yet rather than reporting success.
HOST="scopes.valarsystems.com"
[ "$ENVIRONMENT" = "staging" ] && HOST="scopes-staging.valarsystems.com"

printf '\nconfirming /healthz reports %s (isolates take a few minutes to drain)\n' "$SHA"
for i in $(seq 1 30); do
  LIVE="$(curl -s --max-time 10 "https://$HOST/healthz" | grep -oE '"commit":"[^"]*"' | cut -d'"' -f4)"
  if [ "$LIVE" = "$SHA" ]; then
    printf '  \033[32mconfirmed\033[0m: /healthz reports commit=%s\n' "$LIVE"
    printf '\nNow run the smoke test:  BLIP_KEY=... ./scripts/smoke-prod.sh\n'
    exit 0
  fi
done
printf '  \033[33mnot confirmed yet\033[0m: /healthz last reported commit=%s, expected %s\n' "${LIVE:-<none>}" "$SHA"
printf '  The upload succeeded. Re-check in a minute:  curl -s https://%s/healthz\n' "$HOST"
