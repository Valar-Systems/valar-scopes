#!/usr/bin/env bash
# SCRATCH: the merge rule for all sessions (reviewer, 2026-11-06 ~04:22Z, Q-M1), for one approved PR:
#   1. H = main's head; H's CI must be FINISHED (ci-wait.sh: two reads 60 s apart, 0 unfinished, nothing failed);
#   2. in the SAME command as the merge, re-read origin/main and merge only if it is still H (squash,
#      --match-head-commit = the approved head, no --admin);
#   3. afterwards: the squash's parent is H, and its tree equals merge-tree(H, PR head); then delete the remote branch.
# Any deviation stops with exit 1 and merges nothing further.
#   bash merge-by-rule.sh <pr> <approved head, 12+ hex> <branch>
set -uo pipefail
D="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"; G="git -C $D/wt-main"
PR="$1"; WANT="$2"; BR="$3"
$G fetch -q origin || exit 1
P="$($G rev-parse "origin/$BR")"; case "$P" in "$WANT"*) ;; *) echo "STOP: $BR is at ${P:0:12}, not the approved $WANT"; exit 1 ;; esac
H="$($G ls-remote origin refs/heads/main | cut -f1)"; echo "H ${H:0:12} at $(date -u +%H:%M:%SZ)"
OUT="$(bash "$D/ci-wait.sh" "$H" 30)"; echo "$OUT" | head -3
echo "$OUT" | head -1 | grep -q "unfinished 0 stable [0-9]* timed-out no" || { echo "STOP: H's CI did not finish"; exit 1; }
echo "$OUT" | grep -q -E "^ +[0-9]+ (failure|cancelled|timed_out|action_required)" && { echo "STOP: H's CI has a failure"; exit 1; }
NOW="$($G ls-remote origin refs/heads/main | cut -f1)"
if [ "$NOW" = "$H" ]; then echo "$(date -u +%H:%M:%SZ) main still ${H:0:12}: merging"; gh pr merge "$PR" --repo Valar-Systems/valar-scopes --squash --match-head-commit "$P" 2>&1 | tail -2
else echo "STOP: main moved to ${NOW:0:12} (read ${H:0:12}); nothing merged"; exit 1; fi
S="$(gh pr view "$PR" --repo Valar-Systems/valar-scopes --json state,mergeCommit -q 'select(.state=="MERGED") | .mergeCommit.oid')"
[ -n "$S" ] || { echo "STOP: #$PR is not merged"; exit 1; }
$G fetch -q origin
PARENT="$($G log -1 --format=%P "$S")"; WT="$($G merge-tree --write-tree "$H" "$P")"; GT="$($G rev-parse "$S^{tree}")"
echo "#$PR squash ${S:0:12}: parent ${PARENT:0:12} $([ "$PARENT" = "$H" ] && echo '== H' || echo '!= H'); tree ${GT:0:12} vs merge-tree ${WT:0:12}: $([ "$WT" = "$GT" ] && echo EQUAL || echo DIFFERENT)"
[ "$PARENT" = "$H" ] && [ "$WT" = "$GT" ] || { echo "STOP: the squash is not the approved merge"; exit 1; }
$G push -q origin --delete "$BR" && echo "remote branch $BR deleted ($($G ls-remote origin "refs/heads/$BR" | wc -l) refs left)"
