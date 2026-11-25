#!/usr/bin/env bash
# A bounded wait for one commit's CI, read-only. Polls every 60 s for at most MAX_MIN minutes. Used by scripts/merge-by-rule.sh.
#   bash scripts/ci-wait.sh <full-sha> [MAX_MIN] [BRANCH]
# With BRANCH (merge-by-rule passes its base): only that branch's own PUSH CI for <sha> counts, and the PUSH RUNS decide it
# (2026-11-21, after photo-drift's posted check-run landed inside a push run's check suite on main's head):
#   - the runs: the workflow runs whose event is push, head_sha is <sha> and head_branch is BRANCH, each re-checked here, not
#     only by the API's own filters. A release, tag-push or schedule run on the same commit is not the base's CI;
#   - FINISHED: at least one such run, every one completed, and the run set identical across two reads POLL apart. A queued
#     run with no jobs yet, or a run still in progress with jobs behind `needs:` not yet created, is never finished;
#   - the JOBS (the jobs API, latest attempt) are a KNOWN SET of two url shapes, because that API lists EVERY check-run in
#     the run's suite, a check-run another workflow POSTS onto the commit included (read 2026-11-21 on 2761765e71bd):
#       actions/runs/<that run's id>/job/<id>  a job: counted in N, the tally and RED
#       runs/<id>                              a posted row: DROPPED and COUNTED per run, never RED
#       anything else                          UNREADABLE: exit 4, never kept, never dropped
#   - RED: a run concluding outside success/skipped/neutral (startup_failure included: such a run has no job at all), or any
#     kept JOB concluding failure/cancelled/timed_out/action_required/startup_failure/stale (a continue-on-error job inside a
#     green run still blocks, as before).
#   Exit 0 finished and green; 1 finished and red; 3 not finished (no push run never finishes); 4 UNREADABLE (a job of an
#   unknown url shape); 2 a gh error (the rig).
# Without BRANCH, unchanged: every check run on the commit counts; done when two reads 60 s apart both show 0 unfinished and
# the same count (jobs behind `needs:` appear late, so one read of "all finished" is not enough); exit 0, or 3 on a timeout.
sha="$1"; max="${2:-40}"; branch="${3:-}"; repo="Valar-Systems/valar-scopes"
poll="${CI_WAIT_POLL_S:-60}"   # the selftest shortens the poll; nothing else sets it

GREEN_RUN=" success skipped neutral "
RED_JOB=" failure cancelled timed_out action_required startup_failure stale "
URL_BASE="https://github.com/$repo/"
push_runs() {   # one read: "id<TAB>name<TAB>status<TAB>conclusion" per push run on (sha, branch)
  gh api --paginate "repos/$repo/actions/runs?head_sha=$sha&event=push&branch=$branch&per_page=100" \
    -q ".workflow_runs[] | select(.event == \"push\" and .head_sha == \"$sha\" and .head_branch == \"$branch\") | \"\\(.id)\\t\\(.name)\\t\\(.status)\\t\\(.conclusion)\"" 2>>"$ERR"
}
jobs_of() {     # $1 a run id: "name<TAB>status<TAB>conclusion<TAB>html_url" per job of its latest attempt
  gh api --paginate "repos/$repo/actions/runs/$1/jobs?filter=latest&per_page=100" -q '.jobs[] | "\(.name)\t\(.status)\t\(.conclusion)\t\(.html_url)"' 2>>"$ERR"
}
all_jobs() {    # $1 the run rows: "class<TAB>run id<TAB>run name<TAB>name<TAB>status<TAB>conclusion<TAB>url", sorted; 1 on a gh error
  local id name rest j out=""
  while IFS=$'\t' read -r id name rest; do
    [ -n "$id" ] || continue
    j="$(jobs_of "$id")" || return 1
    out="$out$(printf '%s\n' "$j" | awk -F'\t' -v I="$id" -v N="$name" 'NF {print I "\t" N "\t" $0}')"$'\n'
  done <<< "$1"
  # THE KNOWN SET: K = a job of THAT run, P = a posted row, U = neither
  printf '%s' "$out" | grep . | awk -F'\t' -v B="$URL_BASE" '{
    u = $6; k = B "actions/runs/" $1 "/job/"; p = B "runs/"
    if (index(u, k) == 1 && substr(u, length(k) + 1) ~ /^[0-9]+$/) c = "K"
    else if (index(u, p) == 1 && substr(u, length(p) + 1) ~ /^[0-9]+$/) c = "P"
    else c = "U"
    print c "\t" $0 }' | sort
  return 0
}
per_run() {     # $1 the classified rows, $2 the class: "<word> k (<names>) from run <run name>" per run holding that class
  printf '%s\n' "$1" | awk -F'\t' -v C="$2" -v W="$3" '$1 == C {n[$3]++; s[$3] = s[$3] (s[$3] == "" ? "" : ", ") $4}
    END {for (r in n) print W " " n[r] " (" s[r] ") from run " r}' | sort
}
unreadable() {  # $1 the classified rows: exit 4 if any row is of neither shape
  local nu
  nu=$(printf '%s\n' "$1" | awk -F'\t' '$1 == "U"' | grep -c .)
  [ "$nu" -eq 0 ] && return 0
  echo "$(date -u +%H:%M:%SZ) UNREADABLE: $nu job(s) whose html_url matches neither known shape"
  per_run "$1" U unreadable
  exit 4
}
push_ci() {
  local i runs="" prev="" nr=0 open=0 allc cls kept nj np n red ujobs
  ERR="$(mktemp)"; trap 'rm -f "$ERR"' EXIT
  rig() { echo "RIG: gh api failed: $(tail -1 "$ERR")"; exit 2; }
  for ((i = 0; i < max; i++)); do
    runs="$(push_runs)" || rig
    runs="$(printf '%s\n' "$runs" | grep . | sort)"
    nr=$(printf '%s\n' "$runs" | grep -c .)
    open=$(printf '%s\n' "$runs" | awk -F'\t' 'NF && $3 != "completed"' | grep -c .)
    allc=$([ "$open" -eq 0 ] && echo 1 || echo 0)   # THE STATUS RULE: every push run completed
    if [ "$nr" -gt 0 ] && [ "$allc" = 1 ] && [ "$runs" = "$prev" ]; then
      cls="$(all_jobs "$runs")" || rig
      unreadable "$cls"
      kept="$(printf '%s\n' "$cls" | awk -F'\t' '$1 == "K" {print $4 "\t" $5 "\t" $6}' | sort)"
      ujobs=$(printf '%s\n' "$kept" | awk -F'\t' 'NF && $2 != "completed"' | grep -c .)
      if [ "$ujobs" -eq 0 ]; then
        nj=$(printf '%s\n' "$cls" | grep -c .)
        np=$(printf '%s\n' "$cls" | awk -F'\t' '$1 == "P"' | grep -c .)
        n=$(printf '%s\n' "$kept" | grep -c .)
        red=$( { printf '%s\n' "$runs" | awk -F'\t' -v G="$GREEN_RUN" 'NF && index(G, " " $4 " ") == 0'
                 printf '%s\n' "$kept" | awk -F'\t' -v R="$RED_JOB" 'NF && index(R, " " $3 " ") > 0'; } | grep -c .)
        echo "$(date -u +%H:%M:%SZ) unfinished 0 stable $n timed-out no"
        echo "push runs $nr: $(printf '%s\n' "$runs" | awk -F'\t' 'NF {print $4}' | sort | uniq -c | awk '{printf "%s%s %s", (NR > 1 ? ", " : ""), $1, $2}')"
        echo "jobs read $nj, posted dropped $np, kept $n"
        per_run "$cls" P dropped
        printf '%s\n' "$kept" | awk -F'\t' 'NF {print $3}' | sort | uniq -c
        printf '%s\n' "$kept" | awk -F'\t' 'NF && $3 != "success"'
        printf '%s\n' "$runs" | awk -F'\t' 'NF && $4 != "success" {print "run " $2 "\t" $3 "\t" $4}'
        [ "$red" -eq 0 ] && exit 0
        exit 1
      fi
    fi
    if [ "$nr" -gt 0 ] && [ "$allc" = 1 ]; then prev="$runs"; else prev=""; fi
    sleep "$poll"
  done
  cls="$(all_jobs "$runs")" || rig
  unreadable "$cls"
  echo "$(date -u +%H:%M:%SZ) timed-out yes; last read: $nr runs, $(printf '%s\n' "$cls" | grep -c .) jobs, $(printf '%s\n' "$cls" | awk -F'\t' '$1 == "P"' | grep -c .) posted"
  printf '%s\n' "$runs" | awk -F'\t' 'NF {print $3 "/" $4}' | sort | uniq -c
  exit 3
}
[ -n "$branch" ] && push_ci

read_runs() {
  gh api --paginate "repos/$repo/commits/$sha/check-runs?per_page=100" -q '.check_runs[] | "\(.name)\t\(.status)\t\(.conclusion)"' | sort
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
