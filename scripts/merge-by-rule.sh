#!/usr/bin/env bash
# The merge rule for all sessions (Q-M1, ruled 2026-11-06 ~04:22Z), for one approved PR, with the merge claim
# (docs/session-claims.md, ruled 2026-11-07):
#   1. H = main's head; H's CI must be FINISHED (ci-wait.sh: two reads 60 s apart, 0 unfinished, nothing failed);
#   2. take the merge claim. Another session's claim means wait, polled, for at most 15 minutes, never race;
#   3. in the SAME command as the merge, re-read origin/main and merge only if it is still H (squash,
#      --match-head-commit = the approved head, no --admin);
#   4. afterwards: the squash's parent is H, and its tree equals merge-tree(H, PR head); then delete the remote branch;
#   5. release the claim, on every exit path.
# Any deviation stops with exit 1 and merges nothing further. "STOP: main moved to" means nothing merged.
#   BLIPSCOPE_SESSION=<name> bash scripts/merge-by-rule.sh <pr> <approved head, 12+ hex> <branch>
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"; G="git -C $ROOT"
# The first Python that actually runs: on Windows, `python3` can be the Store's stub, which fails every call (found by CL10).
claim_python() { local c; for c in python3 python; do command -v "$c" >/dev/null 2>&1 && "$c" -c 'import sys' >/dev/null 2>&1 && { echo "$c"; return 0; }; done; return 1; }
CLAIM_PY="$(claim_python)" || { echo "STOP: no working Python to read the merge claim with"; exit 1; }; CLAIM="$ROOT/scripts/session-claim.py"
PR="${1:-}"; WANT="${2:-}"; BR="${3:-}"
[ -n "$PR" ] && [ -n "$WANT" ] && [ -n "$BR" ] || { echo "usage: BLIPSCOPE_SESSION=<name> $0 <pr> <head> <branch>" >&2; exit 2; }

TOOK=""
release_claim() { [ -n "$TOOK" ] && "$CLAIM_PY" "$CLAIM" release merge >/dev/null && echo "merge claim released"; TOOK=""; }
trap release_claim EXIT
take_claim() {
  local end=$(( $(date +%s) + ${MERGE_CLAIM_WAIT_S:-900} )) rc   # the selftest shortens the wait; nothing else sets it
  while :; do
    "$CLAIM_PY" "$CLAIM" take merge --covers "pr:$PR" --until +10m; rc=$?
    [ "$rc" -eq 0 ] && { TOOK=1; return 0; }
    [ "$rc" -eq 3 ] || { echo "STOP: the merge claim could not be taken (exit $rc)"; "$CLAIM_PY" "$CLAIM" show; return 1; }
    [ "$(date +%s)" -gt "$end" ] && { echo "STOP: another session held the merge claim for the whole wait"; "$CLAIM_PY" "$CLAIM" show; return 1; }
    sleep "${MERGE_CLAIM_POLL_S:-20}"
  done
}

# CL11's control only: take the claim and release it, without touching git or GitHub.
if [ -n "${MERGE_CLAIM_CHECK_ONLY:-}" ]; then
  take_claim || exit 1
  echo "merge claim: held for pr:$PR; MERGE_CLAIM_CHECK_ONLY, stopping here"
  exit 0
fi

$G fetch -q origin || exit 1
P="$($G rev-parse "origin/$BR")"; case "$P" in "$WANT"*) ;; *) echo "STOP: $BR is at ${P:0:12}, not the approved $WANT"; exit 1 ;; esac
H="$($G ls-remote origin refs/heads/main | cut -f1)"; echo "H ${H:0:12} at $(date -u +%H:%M:%SZ)"
OUT="$(bash "$ROOT/scripts/ci-wait.sh" "$H" 30)"; echo "$OUT" | head -3
echo "$OUT" | head -1 | grep -q "unfinished 0 stable [0-9]* timed-out no" || { echo "STOP: H's CI did not finish"; exit 1; }
echo "$OUT" | grep -q -E "^ +[0-9]+ (failure|cancelled|timed_out|action_required)" && { echo "STOP: H's CI has a failure"; exit 1; }
take_claim || exit 1
NOW="$($G ls-remote origin refs/heads/main | cut -f1)"
if [ "$NOW" = "$H" ]; then echo "$(date -u +%H:%M:%SZ) main still ${H:0:12}: merging"; gh pr merge "$PR" --repo Valar-Systems/valar-scopes --squash --match-head-commit "$P" 2>&1 | tail -2
else echo "STOP: main moved to ${NOW:0:12} (read ${H:0:12}); nothing merged"; exit 1; fi
S="$(gh pr view "$PR" --repo Valar-Systems/valar-scopes --json state,mergeCommit -q 'select(.state=="MERGED") | .mergeCommit.oid')"
[ -n "$S" ] || { echo "STOP: #$PR is not merged"; exit 1; }
$G fetch -q origin
PARENT="$($G log -1 --format=%P "$S")"; WT="$($G merge-tree --write-tree "$H" "$P")"; GT="$($G rev-parse "$S^{tree}")"
echo "#$PR squash ${S:0:12}: parent ${PARENT:0:12} $([ "$PARENT" = "$H" ] && echo '== H' || echo '!= H'); tree ${GT:0:12} vs merge-tree ${WT:0:12}: $([ "$WT" = "$GT" ] && echo EQUAL || echo DIFFERENT)"
[ "$PARENT" = "$H" ] && [ "$WT" = "$GT" ] || { echo "STOP: the squash is not the approved merge"; exit 1; }
release_claim
$G push -q origin --delete "$BR" && echo "remote branch $BR deleted ($($G ls-remote origin "refs/heads/$BR" | wc -l) refs left)"
