#!/usr/bin/env bash
# A bounded wait for one commit's check runs, read-only. Polls every 60 s for at most MAX_MIN minutes; done when two reads
# 60 s apart both show 0 unfinished and the same count (jobs behind `needs:` appear late, so one read of "all finished" is
# not enough). Prints the tally and the runs that did not succeed, from the final read. Used by scripts/merge-by-rule.sh.
#   bash scripts/ci-wait.sh <full-sha> [MAX_MIN]
sha="$1"; max="${2:-40}"; repo="Valar-Systems/valar-scopes"
prev=-1; out=""
for ((i = 0; i < max; i++)); do
  out="$(gh api --paginate "repos/$repo/commits/$sha/check-runs?per_page=100" -q '.check_runs[] | "\(.name)\t\(.status)\t\(.conclusion)"' | sort)"
  n=$(printf '%s\n' "$out" | grep -c .)
  unf=$(printf '%s\n' "$out" | awk -F'\t' '$2 != "completed"' | grep -c .)
  if [ "$unf" -eq 0 ] && [ "$n" -gt 0 ] && [ "$n" -eq "$prev" ]; then
    echo "$(date -u +%H:%M:%SZ) unfinished 0 stable $n timed-out no"
    printf '%s\n' "$out" | awk -F'\t' '{print $3}' | sort | uniq -c
    printf '%s\n' "$out" | awk -F'\t' '$3 != "success"'
    exit 0
  fi
  [ "$unf" -eq 0 ] && prev=$n || prev=-1
  sleep 60
done
echo "$(date -u +%H:%M:%SZ) timed-out yes; last read: $(printf '%s\n' "$out" | grep -c .) runs"
printf '%s\n' "$out" | awk -F'\t' '{print $2"/"$3}' | sort | uniq -c
exit 3
