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

# --builder deploys the basemap builder Worker (basemap.wrangler.toml) instead of the proxy, through the SAME guards
# (docs/basemap-deploy.md, Q2 and Q3 as ruled). Three things differ, each because the builder needs it: the stamp is
# the FULL 40-hex commit (PROVENANCE pins it and --check checks it out), a dirty tree is refused with no ALLOW_DIRTY
# escape (a "-dirty" stamp is not a commit, so the builder would refuse every build), and there is no /healthz to
# confirm against (the builder has no route; D3's binding test is the proof).
#
# --screen deploys the screen service (screen.wrangler.toml; its phase-1 registration, R4, ruled 2026-11-25) through the
# SAME guards, with the builder's three differences (the FULL commit as its stamp, no ALLOW_DIRTY, no /healthz) and one
# env of its own: production-fulllogs, the full-logs day (docs/screen-logs-runbook.md), which takes --until <ISO UTC>.
# Before the upload it runs screen-deploy-check.mjs on the env's own dry-run bundle (the counter's write, the --until
# rules, the end's stamp). production-fulllogs IS the production script, so its deploy claim is screen:production.
BUILDER=""
SCREEN=""
UNTIL=""
UNTIL_GIVEN=""
usage() { echo "usage: $0 [--builder] <production|staging>   |   $0 --screen <staging|production|production-fulllogs> [--until <ISO UTC>]" >&2; exit 2; }
case "${1:-}" in
  --builder) BUILDER=1; shift ;;
  --screen) SCREEN=1; shift ;;
esac
ENVIRONMENT="${1:-}"
case "$ENVIRONMENT" in
  production|staging) ;;
  production-fulllogs) [ -n "$SCREEN" ] || usage ;;
  *) usage ;;
esac
# --until is the screen's alone; its value is passed through as given and judged in one place (screen-deploy-check.mjs, P-U).
case "${SCREEN}:${2:-}" in
  1:--until) UNTIL_GIVEN=1; UNTIL="${3:-}"; [ $# -le 3 ] || usage ;;
  1:) ;;
  1:*) usage ;;
esac
# ... and on every other path it is REFUSED, never silently dropped (map-server's F2 on #811): an ignored flag on a deploy
# is a statement the operator believes took effect. scripts/check-deploy-args.sh (DA1-DA4) and its sabotage hold this.
if [ -z "$SCREEN" ]; then for a in "$@"; do case "$a" in --until|--until=*) usage ;; esac; done; fi

# ---- 0. the deploy claim (docs/session-claims.md) -----------------------------
# Before any other work: one deployer at a time, across every session on this
# machine. The caller holds the deploy claim for this Worker and env, or nothing
# here runs. The claim lives outside every git tree, so no checkout can be behind
# on it; scripts/session-claim.py is deploy machinery all the same (Q-DB1).
CLAIM_TARGET="proxy:$ENVIRONMENT"; [ -n "$BUILDER" ] && CLAIM_TARGET="builder:$ENVIRONMENT"
[ -n "$SCREEN" ] && CLAIM_TARGET="screen:${ENVIRONMENT%-fulllogs}"
# The first Python that actually runs: on Windows, `python3` can be the Store's stub, which fails every call (found by CL10).
claim_python() { local c; for c in python3 python; do command -v "$c" >/dev/null 2>&1 && "$c" -c 'import sys' >/dev/null 2>&1 && { echo "$c"; return 0; }; done; return 1; }
CLAIM_PY="$(claim_python)" || { printf '\n\033[31mREFUSING TO DEPLOY\033[0m -- no working Python to read the deploy claim with.\n' >&2; exit 1; }
CLAIM_TOOL="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)/scripts/session-claim.py"
if ! "$CLAIM_PY" "$CLAIM_TOOL" check deploy --covers "$CLAIM_TARGET"; then
  printf '\n\033[31mREFUSING TO DEPLOY\033[0m -- this session does not hold the deploy claim for %s.\n' "$CLAIM_TARGET" >&2
  "$CLAIM_PY" "$CLAIM_TOOL" show >&2
  echo "Take it first:  BLIPSCOPE_SESSION=<name> python scripts/session-claim.py take deploy --covers $CLAIM_TARGET --until +45m" >&2
  exit 1
fi
# CL10's control only: stop once the claim has been read as held, before anything else runs.
if [ -n "${DEPLOY_CLAIM_CHECK_ONLY:-}" ]; then echo "deploy claim: held for $CLAIM_TARGET; DEPLOY_CLAIM_CHECK_ONLY, stopping here"; exit 0; fi

ROOT="$(git rev-parse --show-toplevel 2>/dev/null)"
if [ -z "$ROOT" ]; then
  echo "FATAL: not inside a git checkout, so nothing here can be verified." >&2
  exit 1
fi
cd "$ROOT" || exit 1

fatal() { printf '\n\033[31mREFUSING TO DEPLOY\033[0m -- %s\n' "$1" >&2; }

# ---- 0b. the commit the plan names (docs/deploy-sha-pin.md; the owner's standing rule, 2026-11-17) -----------------
# "Every deploy plan names the exact commit sha it ships. A merge that touches the deployed code after the plan is
# written forces a re-check, enforced by a check in the deploy script, not by memory." So EXPECT_SHA is required and must
# be HEAD, after the claim and before anything that deploys. ALLOW_BEHIND=1 is retired: a tree behind main may deploy
# only past the commits the plan NAMES, in ALLOW_BEHIND_SHAS, and the named set must equal the blocking set (section 3).
HEAD_SHA="$(git rev-parse HEAD)"
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

# ---- 1. the bundle's own directory must be clean ----------------------------
# Scoped to proxy/ because that is what `main = src/index.ts` pulls in. Untracked
# files count: an untracked .ts under src/ is every bit as importable as a tracked
# one, and "it wasn't in git" is exactly the property that makes it dangerous.
DIRTY="$(git status --porcelain -- proxy/)"
if [ -n "$DIRTY" ] && [ -n "$BUILDER" ]; then
  fatal "proxy/ has uncommitted changes, and the builder's stamp must be a commit (ALLOW_DIRTY does not apply to --builder)."
  printf '\n%s\n\n' "$DIRTY" >&2
  echo "A builder stamped <sha>-dirty refuses every build (builder_unstamped). Commit, push, and deploy from a clean tree." >&2
  exit 1
fi
if [ -n "$DIRTY" ] && [ -n "$SCREEN" ]; then
  fatal "proxy/ has uncommitted changes, and the screen service's stamp must be a commit (ALLOW_DIRTY does not apply to --screen)."
  printf '\n%s\n\n' "$DIRTY" >&2
  echo "Commit, push, and deploy from a clean tree." >&2
  exit 1
fi
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
# PER TARGET since docs/deploy-behind-per-target.md (Q-DB1..Q-DB3, ruled 2026-11-06): it blocks only on commits
# that touch what THIS target ships -- the bundler's own inputs (a dry-run's metafile), its wrangler config,
# tsconfig and the package files, and the deploy machinery (this script, deploy-inputs.mjs, session-claim.py;
# for the proxy also confirm-deploy.sh and healthz-commit.sh). deploy-inputs.mjs prints the set, the dry-run's
# time and every commit behind with its verdict. A guard that fires on unrelated work teaches people to bypass
# it, which costs more than it saves. If the bundler cannot answer, the script falls back to today's rule (all
# of proxy/, plus the machinery); if the script itself fails (any exit but 0 or 1), the rule below is applied
# here, so a broken probe costs a refusal, never a revert.
git fetch -q origin main 2>/dev/null || true
DI_TARGET=proxy; [ -n "$BUILDER" ] && DI_TARGET=builder
[ -n "$SCREEN" ] && DI_TARGET=screen
# deploy-inputs.mjs writes the FULL shas of the commits it blocks on to this file (docs/deploy-sha-pin.md), so the named
# set below is compared with its own decision, not with a reading of its printed prefixes.
DEPLOY_INPUTS_BLOCKING_OUT="$(mktemp)"; export DEPLOY_INPUTS_BLOCKING_OUT
node "$ROOT/proxy/scripts/deploy-inputs.mjs" check "$DI_TARGET" "$ENVIRONMENT"; DI_RC=$?
BLOCKING=""
if [ "$DI_RC" -eq 0 ] || [ "$DI_RC" -eq 1 ]; then
  BLOCKING="$(tr -s ' \n' '  ' < "$DEPLOY_INPUTS_BLOCKING_OUT")"
  if [ "$DI_RC" -eq 1 ] && [ -z "${BLOCKING// /}" ]; then
    fatal "deploy-inputs.mjs blocked but wrote no blocking commit, so the named set cannot be checked."; rm -f "$DEPLOY_INPUTS_BLOCKING_OUT"; exit 1
  fi
  if [ "$DI_RC" -eq 0 ] && [ -n "${BLOCKING// /}" ]; then
    fatal "deploy-inputs.mjs did not block but wrote blocking commits: the two disagree."; rm -f "$DEPLOY_INPUTS_BLOCKING_OUT"; exit 1
  fi
else
  # The probe itself failed: today's rule here (all of proxy/, plus scripts/session-claim.py), so a broken probe costs
  # a refusal unless the plan names exactly those commits, never a silent revert.
  echo "deploy-inputs.mjs exited $DI_RC: applying today's rule here (all of proxy/, plus scripts/session-claim.py)." >&2
  BLOCKING="$(git rev-list HEAD..origin/main -- proxy/ scripts/session-claim.py 2>/dev/null | tr '\n' ' ')"
  BEHIND_ALL="$(git rev-list --count HEAD..origin/main 2>/dev/null || echo 0)"
  [ -z "${BLOCKING// /}" ] && [ "${BEHIND_ALL:-0}" -gt 0 ] && echo "note: $BEHIND_ALL commit(s) behind origin/main, none touching proxy/ (not bundled, not blocking)."
fi
rm -f "$DEPLOY_INPUTS_BLOCKING_OUT"
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
[ -n "${BLOCKING// /}" ] && echo ""

# ---- 3. stamp what is actually being shipped --------------------------------
SHA="$(git rev-parse --short HEAD)"
[ -n "$DIRTY" ] && SHA="${SHA}-dirty"
# The builder's stamp is the FULL commit: its PROVENANCE names it, and --check checks it out (a dirty tree never reaches here)
[ -n "$BUILDER" ] && SHA="$(git rev-parse HEAD)"
[ -n "$SCREEN" ] && SHA="$(git rev-parse HEAD)"
BRANCH="$(git rev-parse --abbrev-ref HEAD)"

printf 'deploying to \033[1m%s\033[0m\n' "$ENVIRONMENT"
printf '  commit  %s  (%s)\n' "$SHA" "$BRANCH"
printf '  subject %s\n\n' "$(git log -1 --format=%s)"
[ -n "$BUILDER" ] && printf '  worker  the basemap BUILDER (basemap.wrangler.toml): no route, its R2 binding STORE\n\n'
[ -n "$SCREEN" ] && printf '  worker  the SCREEN service (screen.wrangler.toml, env %s)%s\n\n' "$ENVIRONMENT" "${UNTIL_GIVEN:+, --until $UNTIL}"

cd "$ROOT/proxy" || exit 1

# ---- the builder: deploy, and say plainly how little that output proves -----
# The proxy-only steps below do not apply: the builder holds no DEVICE_KEY_SECRET, and it has no route, so there is no
# /healthz of its own to confirm against. What this output shows -- the stamp above and the STORE binding wrangler
# lists -- is D2's ONLY confirmation, and it is the weak form. The proof is D3's binding test on staging: the proxy's
# /healthz `basemap.commit`, read after one /basemap build, must equal the stamp above (docs/basemap-deploy.md, Q3).
if [ -n "$BUILDER" ]; then
  node scripts/wrangler-run.mjs deploy --config basemap.wrangler.toml --env "$ENVIRONMENT" --define BUILD_COMMIT:"\"$SHA\"" || exit 1
  printf '\nbuilder deployed to %s, stamped %s.\n' "$ENVIRONMENT" "$SHA"
  printf 'NOT YET CONFIRMED: this output is the only evidence until a bound proxy reports this commit (D3).\n'
  exit 0
fi

# ---- the screen service: its own pre-deploy check, then deploy ---------------
# The proxy-only steps below do not apply: the screen holds no DEVICE_KEY_SECRET, and it has no route yet, so there is
# no /health to confirm against until its custom domain lands. The check builds the env's OWN dry-run bundle and refuses
# unless it holds the unsampled counter's write exactly once (P-M2), the --until rules hold (P-U: production-fulllogs
# needs an end within 24 h, no other env takes one), and the end it was given survives into that bundle (P-S). The same
# end is stamped below as FULL_LOGS_UNTIL, which /health reports and the logs watch grades (docs/screen-logs-runbook.md).
if [ -n "$SCREEN" ]; then
  CHECK_ARGS=("$ENVIRONMENT"); [ -n "$UNTIL_GIVEN" ] && CHECK_ARGS+=(--until "$UNTIL")
  node scripts/screen-deploy-check.mjs "${CHECK_ARGS[@]}" || { fatal "the screen's pre-deploy check refused (above); nothing was deployed."; exit 1; }
  STAMP=(--define BUILD_COMMIT:"\"$SHA\"")
  [ "$ENVIRONMENT" = production-fulllogs ] && STAMP+=(--define FULL_LOGS_UNTIL:"\"$UNTIL\"")
  node scripts/wrangler-run.mjs deploy --config screen.wrangler.toml --env "$ENVIRONMENT" "${STAMP[@]}" || exit 1
  printf '\nscreen service deployed to %s, stamped %s%s.\n' "$ENVIRONMENT" "$SHA" "${UNTIL_GIVEN:+, full logs until $UNTIL}"
  printf 'NOT YET CONFIRMED: read its /health (commit, logs.rate, logs.until) once it has a route.\n'
  exit 0
fi

# ---- 3b. DEVICE_KEY_SECRET must exist in the target environment -------------
# Since the shared BLIP_KEYS list was removed (2026-08-13), per-device keys are
# the only way in -- and verifyDeviceKey() returns false when the secret is
# unset. So a missing secret does not degrade the fleet, it REFUSES it: every
# device 401s until someone notices. Checked here, against the environment being
# deployed to, because the two environments hold separate secrets and the failure
# is total rather than partial.
#
# Run bare and inspected whole, per CLAUDE.md. This block ALSO serves as the
# scope-revealing preflight: it is the first call that needs Workers Scripts
# permission, and it happens before anything irreversible.
#
# BOTH STREAMS ARE PRINTED ON FAILURE, and that is a correction rather than
# belt-and-braces. This comment used to assert that wrangler "puts auth failures
# on stderr and a cheerful body on stdout". Measured 2026-09-01 against a real
# `Authentication error [code: 10000]`, it is the other way round:
#
#     stdout  20 lines  the ERROR, the failing endpoint, the account table
#     stderr   7 lines  a WARNING about `unsafe` wrangler.toml fields
#
# So the script captured stdout into a variable, discarded it on the failure
# path, printed the warning, and told the reader to "fix the error and re-run"
# without ever showing the error. That cost a session. A guard that knows which
# stream the reason arrives on has pre-decided the shape of the failure, which is
# the one thing this repo keeps learning not to do -- so it no longer decides.
SECRET_ERR="$(mktemp)"
SECRET_OUT_F="$(mktemp)"
node scripts/wrangler-run.mjs secret list --env "$ENVIRONMENT" >"$SECRET_OUT_F" 2>"$SECRET_ERR"
SECRET_RC=$?
SECRET_OUT="$(cat "$SECRET_OUT_F")"
if [ $SECRET_RC -ne 0 ]; then
  echo "FATAL: could not list secrets for $ENVIRONMENT (wrangler exit $SECRET_RC)." >&2
  echo "  This is NOT the same as the secret being absent." >&2
  echo "  --- wrangler stdout ---" >&2
  sed 's/^/  /' "$SECRET_OUT_F" >&2
  echo "  --- wrangler stderr ---" >&2
  sed 's/^/  /' "$SECRET_ERR" >&2
  # THE LIKELY CAUSE, NAMED. Four sessions have now been spent on this variable,
  # and every one of them started by checking whether it was SET. Presence was
  # never the question: wrangler PREFERS CLOUDFLARE_API_TOKEN over an existing
  # `wrangler login` session, so a token that is present and valid but scoped for
  # something else (this account's is the GHA KV-write token) SHADOWS a working
  # OAuth login and produces an auth error identical to having no credential at
  # all. Valid is not the same as correctly scoped, and set is not the same as
  # wanted.
  #
  # Boolean only -- never echo the value. A presence check that prints the secret
  # puts it in scrollback, in CI logs, and in whatever the screen was shared to.
  if [ -n "${CLOUDFLARE_API_TOKEN:-}" ]; then
    echo "" >&2
    echo "  LIKELY CAUSE: CLOUDFLARE_API_TOKEN is set in this environment (present;" >&2
    echo "  value not shown) and wrangler prefers it over your OAuth login" >&2
    echo "  session. If that token lacks Workers Scripts permission it fails exactly" >&2
    echo "  like a missing credential. Try:" >&2
    echo "" >&2
    echo "      unset CLOUDFLARE_API_TOKEN          # PowerShell: Remove-Item Env:CLOUDFLARE_API_TOKEN" >&2
    echo "      scripts/deploy.sh $ENVIRONMENT" >&2
    echo "" >&2
    echo "  If a fresh shell re-inherits it, it is set at user level, which CLAUDE.md" >&2
    echo "  forbids: no user-level CLOUDFLARE_API_TOKEN on this machine, ever. The" >&2
    echo "  by-hand scripts read their tokens from the file BLIPSCOPE_TOKEN_FILE names." >&2
  else
    echo "" >&2
    echo "  CLOUDFLARE_API_TOKEN is NOT set here, so wrangler is using your login" >&2
    echo "  session. Check \`node scripts/wrangler-run.mjs whoami\` (from proxy/) and that the account has Workers" >&2
    echo "  Scripts permission." >&2
  fi
  rm -f "$SECRET_ERR" "$SECRET_OUT_F"
  exit 1
fi
rm -f "$SECRET_ERR" "$SECRET_OUT_F"
if ! printf '%s' "$SECRET_OUT" | grep -q 'DEVICE_KEY_SECRET'; then
  echo "FATAL: DEVICE_KEY_SECRET is not set on $ENVIRONMENT." >&2
  echo "  Per-device keys are the only auth path, so deploying now 401s the whole fleet." >&2
  echo "  Set it first (from proxy/):  node scripts/wrangler-run.mjs secret put DEVICE_KEY_SECRET --env $ENVIRONMENT" >&2
  echo "  Secrets present:" >&2
  printf '%s\n' "$SECRET_OUT" | sed 's/^/    /' >&2
  exit 1
fi
printf '  secrets  DEVICE_KEY_SECRET present on %s\n\n' "$ENVIRONMENT"

node scripts/wrangler-run.mjs deploy --env "$ENVIRONMENT" --define BUILD_COMMIT:"\"$SHA\"" || exit 1   # docs/wrangler-run.md

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
#
# ANCHORED ON $ROOT, NOT ON $0. `dirname "$0"` is a RELATIVE path when the script
# was invoked by one, and this line runs after `cd "$ROOT/proxy"` -- so
# `bash proxy/scripts/deploy.sh production` from the repo root resolved it to
# $ROOT/proxy/proxy/scripts/ and died with "No such file or directory" (exit 127)
# on 2026-09-01, AFTER a successful upload. The deploy itself was fine; the step
# that PROVES a deploy landed was the one that did not run, which is the worst
# possible thing to leave un-run and the easiest to shrug at because production
# was in fact healthy. Invoking from inside proxy/ happens to work, which is why
# this survived: the canonical path masked it.
"$ROOT/proxy/scripts/confirm-deploy.sh" "$HOST" "$SHA"
