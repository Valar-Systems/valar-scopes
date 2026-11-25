#!/usr/bin/env bash
# DA1-DA13 for proxy/scripts/deploy.sh's argument rules (map-server's F2, F3 and F4 on #811, ruled 2026-11-25). On every
# path but --screen, NOTHING may follow the environment: any further argument is REFUSED as usage, never silently
# dropped, because an ignored flag on a deploy is a statement the operator believes took effect (`staging --dry-run`
# would otherwise deploy for real; `production --screen` would deploy the proxy). --screen alone takes --until, and the
# refusal must not reach it. Frozen before the code (the screen service's phase-1 predictions, L2 and L3).
#
# Every row runs deploy.sh with an EMPTY claims folder, BLIPSCOPE_SESSION unset and DEPLOY_CLAIM_CHECK_ONLY=1, so
# nothing past the claim check can run. A refusal at usage exits 2 and prints "usage:"; a row that passes usage stops at
# the claim (exit 1, "does not hold the deploy claim for <target>"). No git, no network, no wrangler.
#
#   bash proxy/scripts/check-deploy-args.sh             the rows, each against its prediction: exit 0 all, 1 off, 2 rig
#   bash proxy/scripts/check-deploy-args.sh --sabotage  copies of deploy.sh, in a temp tree beside a copy of
#        scripts/session-claim.py so the claim tool resolves; an unmodified copy must read every row as predicted, then
#        S-EXTRA (the general refusal removed) must turn exactly DA1-DA4 and DA8-DA10 red, and S-OVERREACH (the refusal
#        applied to --screen too) exactly DA11 and DA12
set -u
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
T="$(mktemp -d)"
trap 'rm -rf "$T"' EXIT
mkdir -p "$T/claims"
wpath() { cygpath -m "$1" 2>/dev/null || echo "$1"; }   # python on Windows needs a Windows path
CLAIMS="$(wpath "$T/claims")"
U=2026-12-02T13:00:00Z
# The refusal line in deploy.sh. Each sabotage replaces it, so it must be found there exactly once, as a whole line.
ANCHOR='[ -n "$SCREEN" ] || [ $# -le 1 ] || usage'

# name | deploy.sh's arguments | predicted exit | a token the output must hold
ROWS=(
  "DA1|staging --until $U|2|usage:"
  "DA2|production --until $U|2|usage:"
  "DA3|--builder staging --until $U|2|usage:"
  "DA4|staging --until=$U|2|usage:"
  "DA5|--screen staging|1|does not hold the deploy claim for screen:staging"
  "DA6|staging|1|does not hold the deploy claim for proxy:staging"
  "DA7|--screen nonsense|2|usage:"
  "DA8|staging --builder|2|usage:"
  "DA9|production --screen|2|usage:"
  "DA10|--builder staging --dry-run|2|usage:"
  "DA11|--screen staging --until $U|1|does not hold the deploy claim for screen:staging"
  "DA12|--screen production-fulllogs --until $U|1|does not hold the deploy claim for screen:production"
  "DA13|--builder staging|1|does not hold the deploy claim for builder:staging"
)

RED=""
run_all() {   # run_all <deploy.sh path> [loud]
  local script=$1 loud=${2:-} r name args want token rc tk
  RED=""
  for r in "${ROWS[@]}"; do
    IFS='|' read -r name args want token <<< "$r"
    # shellcheck disable=SC2086 -- the arguments split on spaces on purpose; none holds one
    ( cd "$REPO" && env -u BLIPSCOPE_SESSION BLIPSCOPE_CLAIMS_DIR="$CLAIMS" DEPLOY_CLAIM_CHECK_ONLY=1 bash "$script" $args ) \
      > "$T/out" 2>&1 < /dev/null
    rc=$?
    tk=0; grep -qF -- "$token" "$T/out" && tk=1
    [ "$(ls -A "$T/claims")" = "" ] || { echo "RIG: a claim appeared in the empty claims folder"; exit 2; }
    if [ "$rc" = "$want" ] && [ "$tk" = 1 ]; then
      [ -n "$loud" ] && echo "$name  AS PREDICTED  (exit $rc; \"$token\")"
    else
      RED="$RED $name"
      [ -n "$loud" ] && echo "$name  OFF: exit $rc, token $([ "$tk" = 1 ] && echo present || echo ABSENT) (predicted exit $want, \"$token\")"
    fi
  done
  RED="${RED# }"
}

if [ "${1:-}" != "--sabotage" ]; then
  run_all "$HERE/deploy.sh" loud
  [ -z "$RED" ] && { echo "ALL AS PREDICTED (${#ROWS[@]} rows)"; exit 0; }
  echo "OFF: $RED"; exit 1
fi

# --sabotage: the same rows against copies in a temp tree laid out like the repo.
mkdir -p "$T/tree/proxy/scripts" "$T/tree/scripts"
cp "$REPO/scripts/session-claim.py" "$T/tree/scripts/session-claim.py" || { echo "RIG: session-claim.py did not copy"; exit 2; }
COPY="$T/tree/proxy/scripts/deploy.sh"
cp "$HERE/deploy.sh" "$COPY" && cmp -s "$HERE/deploy.sh" "$COPY" || { echo "RIG: deploy.sh did not copy byte-identical"; exit 2; }
run_all "$COPY"
[ -z "$RED" ] || { echo "RIG: the unmodified copy is not all as predicted (red: $RED)"; exit 2; }
echo "CONTROL (an unmodified copy in the temp tree): every row as predicted"
n="$(grep -cxF -- "$ANCHOR" "$HERE/deploy.sh")"
[ "$n" = 1 ] || { echo "RIG: the refusal line is found $n times in deploy.sh, want exactly 1"; exit 2; }
off=0
# name ~ the line that replaces the refusal ~ the registered red set. Split on "~", NOT "|": a replacement holds "||",
# and a "|" split once cut S-OVERREACH's line short and graded a different plant (2026-11-25, recorded).
SABOTAGES=(
  "S-EXTRA~: # planted: the general refusal removed~DA1 DA2 DA3 DA4 DA8 DA9 DA10"
  'S-OVERREACH~[ $# -le 1 ] || usage # planted: the refusal reaches --screen too~DA11 DA12'
)
for s in "${SABOTAGES[@]}"; do
  IFS='~' read -r sname repl want <<< "$s"
  [[ "$want" =~ ^DA[0-9]+(\ DA[0-9]+)*$ ]] || { echo "RIG: $sname's registered red set did not parse ('$want')"; exit 2; }
  [ "$s" = "$sname~$repl~$want" ] || { echo "RIG: $sname's entry did not split into exactly three fields"; exit 2; }
  awk -v a="$ANCHOR" -v r="$repl" '$0 == a { print r; next } { print }' "$HERE/deploy.sh" > "$COPY"
  ! cmp -s "$HERE/deploy.sh" "$COPY" || { echo "RIG: $sname's copy is unchanged"; exit 2; }
  [ "$(grep -cxF -- "$ANCHOR" "$COPY")" = 0 ] || { echo "RIG: $sname's copy still holds the refusal line"; exit 2; }
  [ "$(grep -cxF -- "$repl" "$COPY")" = 1 ] || { echo "RIG: $sname's planted line is not in its copy exactly once"; exit 2; }
  run_all "$COPY"
  if [ "$RED" = "$want" ]; then echo "$sname: AS REGISTERED (red: $RED)"; else echo "$sname: OFF (red: ${RED:-none}; registered: $want)"; off=1; fi
done
[ "$off" = 0 ] && { echo "SABOTAGES: each turns exactly its registered rows red"; exit 0; }
echo "SABOTAGES: OFF"; exit 1
