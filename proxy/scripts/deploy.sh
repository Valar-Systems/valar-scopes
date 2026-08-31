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

# ---- 3. the tree must not be BEHIND origin/main -----------------------------
#
# wrangler bundles the WORKING TREE, so deploying from a branch that is behind
# main silently REVERTS whatever main has and the branch does not. Nothing in the
# deploy output says so: the upload succeeds, /healthz reports a real commit, and
# production quietly loses features that were merged days ago.
#
# Caught by hand on 2026-08-26, one command before a production deploy: the route
# mirror branch was three commits behind and would have reverted the printed-card
# 302 (#258), the splash change (#251) and the nautical-miles unit refactor
# (#259). All three were already live. The deploy would have reported success.
#
# So it is a check in the tool, not a line in a runbook: "which branch am I on"
# is a PRODUCTION question here, not bookkeeping.
#
# Only blocks on commits that touch proxy/ -- firmware-only commits on main
# cannot reach this bundle, and a guard that fires on unrelated work teaches
# people to bypass it, which costs more than it saves.
git fetch -q origin main 2>/dev/null || true
BEHIND_ALL="$(git rev-list --count HEAD..origin/main 2>/dev/null || echo 0)"
BEHIND_PROXY="$(git rev-list --count HEAD..origin/main -- proxy/ 2>/dev/null || echo 0)"
if [ "${BEHIND_PROXY:-0}" -gt 0 ]; then
  if [ "${ALLOW_BEHIND:-}" != "1" ]; then
    fatal "this tree is $BEHIND_PROXY commit(s) behind origin/main in proxy/, and wrangler ships the WORKING TREE."
    echo "" >&2
    echo "Deploying now would REVERT these from production:" >&2
    git --no-pager log --oneline HEAD..origin/main -- proxy/ >&2
    echo "" >&2
    echo "  git merge origin/main    (then re-run)" >&2
    echo "  or, knowing exactly what you are reverting:  ALLOW_BEHIND=1 $0 $ENVIRONMENT" >&2
    exit 1
  fi
  echo "ALLOW_BEHIND=1 -- deploying a tree behind origin/main; these are being REVERTED:"
  git --no-pager log --oneline HEAD..origin/main -- proxy/
  echo ""
elif [ "${BEHIND_ALL:-0}" -gt 0 ]; then
  # Behind main only OUTSIDE proxy/, so it cannot reach the bundle: say it, do not block.
  echo "note: $BEHIND_ALL commit(s) behind origin/main, none touching proxy/ (not bundled, not blocking)."
  echo ""
fi

# ---- 3. stamp what is actually being shipped --------------------------------
SHA="$(git rev-parse --short HEAD)"
[ -n "$DIRTY" ] && SHA="${SHA}-dirty"
BRANCH="$(git rev-parse --abbrev-ref HEAD)"

printf 'deploying to \033[1m%s\033[0m\n' "$ENVIRONMENT"
printf '  commit  %s  (%s)\n' "$SHA" "$BRANCH"
printf '  subject %s\n\n' "$(git log -1 --format=%s)"

cd "$ROOT/proxy" || exit 1

# ---- 3b. DEVICE_KEY_SECRET must exist in the target environment -------------
# Since the shared BLIP_KEYS list was removed (2026-08-13), per-device keys are
# the only way in -- and verifyDeviceKey() returns false when the secret is
# unset. So a missing secret does not degrade the fleet, it REFUSES it: every
# device 401s until someone notices. Checked here, against the environment being
# deployed to, because the two environments hold separate secrets and the failure
# is total rather than partial.
#
# Run bare and inspected whole, per CLAUDE.md: `wrangler secret list` puts auth
# failures on stderr and a cheerful body on stdout, so a filtered read of a
# failed call looks like "secret absent" when it actually means "not logged in".
# An error is therefore reported as an error, never as a missing secret.
SECRET_ERR="$(mktemp)"
SECRET_OUT="$(npx wrangler secret list --env "$ENVIRONMENT" 2>"$SECRET_ERR")"
SECRET_RC=$?
if [ $SECRET_RC -ne 0 ]; then
  echo "FATAL: could not list secrets for $ENVIRONMENT (wrangler exit $SECRET_RC)." >&2
  echo "  This is NOT the same as the secret being absent -- fix the error and re-run." >&2
  sed 's/^/  /' "$SECRET_ERR" >&2
  rm -f "$SECRET_ERR"
  exit 1
fi
rm -f "$SECRET_ERR"
if ! printf '%s' "$SECRET_OUT" | grep -q 'DEVICE_KEY_SECRET'; then
  echo "FATAL: DEVICE_KEY_SECRET is not set on $ENVIRONMENT." >&2
  echo "  Per-device keys are the only auth path, so deploying now 401s the whole fleet." >&2
  echo "  Set it first:  npx wrangler secret put DEVICE_KEY_SECRET --env $ENVIRONMENT" >&2
  echo "  Secrets present:" >&2
  printf '%s\n' "$SECRET_OUT" | sed 's/^/    /' >&2
  exit 1
fi
printf '  secrets  DEVICE_KEY_SECRET present on %s\n\n' "$ENVIRONMENT"

npx wrangler deploy --env "$ENVIRONMENT" --define BUILD_COMMIT:"\"$SHA\"" || exit 1

# ---- 4. prove it, rather than assuming the upload implies it ----------------
# Edge isolates drain for a few minutes after a deploy, so an immediate check can
# still be answered by the old version. This confirms the stamp eventually, and
# says plainly when it has not settled yet rather than reporting success.
HOST="scopes.valarsystems.com"
[ "$ENVIRONMENT" = "staging" ] && HOST="scopes-staging.valarsystems.com"

printf '
confirming /healthz reports %s (isolates take a few minutes to drain)
' "$SHA"
# The loop lives in its own script so it can be exercised against a stub without
# deploying -- see scripts/test-confirm-deploy.sh. Two bugs hid in it precisely
# because reaching them required a real production deploy.
"$(dirname "$0")/confirm-deploy.sh" "$HOST" "$SHA"
