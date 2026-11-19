#!/usr/bin/env bash
# A bounded wait for one commit's check runs, read-only. Polls every 60 s for at most MAX_MIN minutes; done when two reads
# 60 s apart both show 0 unfinished and the same count (jobs behind `needs:` appear late, so one read of "all finished" is
# not enough). Prints the tally and the runs that did not succeed, from the final read. Used by scripts/merge-by-rule.sh.
#   bash scripts/ci-wait.sh <full-sha> [MAX_MIN] [BRANCH]
# With BRANCH (merge-by-rule passes its base): only that branch's own PUSH CI for <sha> counts -- the check runs of the
# workflow runs whose event is push, head_sha is <sha> and head_branch is BRANCH, each run re-checked here, not only by the
# API's own filters. A release, tag-push or schedule run on the same commit is not the base's CI, so it can neither pass
# nor block a merge (2026-11-19: REH-A's release run on main's head would have refused every merge). No such run means a
# count of 0, which never passes, so the wait times out (exit 3). Without BRANCH, every check run on the commit counts.
sha="$1"; max="${2:-40}"; branch="${3:-}"; repo="Valar-Systems/valar-scopes"
poll="${CI_WAIT_POLL_S:-60}"   # the selftest shortens the poll; nothing else sets it
read_runs() {
  if [ -z "$branch" ]; then
    gh api --paginate "repos/$repo/commits/$sha/check-runs?per_page=100" -q '.check_runs[] | "\(.name)\t\(.status)\t\(.conclusion)"' | sort
    return
  fi
  local suites
  suites="$(gh api --paginate "repos/$repo/actions/runs?head_sha=$sha&event=push&branch=$branch&per_page=100" \
    -q ".workflow_runs[] | select(.event == \"push\" and .head_sha == \"$sha\" and .head_branch == \"$branch\") | .check_suite_id" | tr '\n' ' ')"
  gh api --paginate "repos/$repo/commits/$sha/check-runs?per_page=100" -q '.check_runs[] | "\(.check_suite.id)\t\(.name)\t\(.status)\t\(.conclusion)"' \
    | awk -F'\t' -v S="$suites" 'BEGIN { n = split(S, a, " "); for (i = 1; i <= n; i++) keep[a[i]] = 1 } ($1 in keep) { print $2 "\t" $3 "\t" $4 }' | sort
}
prev=-1; out=""
for ((i = 0; i < max; i++)); do
  out="$(read_runs)"
  n=$(printf '%s\n' "$out" | grep -c .)
  unf=$(printf '%s\n' "$out" | awk -F'\t' '$2 != "completed"' | grep -c .)
  if [ "$unf" -eq 0 ] && [ "$n" -gt 0 ] && [ "$n" -eq "$prev" ]; then
    echo "$(date -u +%H:%M:%SZ) unfinished 0 stable $n timed-out no"
    printf '%s\n' "$out" | awk -F'\t' '{print $3}' | sort | uniq -c
    printf '%s\n' "$out" | awk -F'\t' '$3 != "success"'
    exit 0
  fi
  [ "$unf" -eq 0 ] && prev=$n || prev=-1
  sleep "$poll"
done
echo "$(date -u +%H:%M:%SZ) timed-out yes; last read: $(printf '%s\n' "$out" | grep -c .) runs"
printf '%s\n' "$out" | awk -F'\t' '{print $2"/"$3}' | sort | uniq -c
exit 3
