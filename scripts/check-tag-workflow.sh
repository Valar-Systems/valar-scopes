#!/usr/bin/env bash
# Is the commit you are about to tag able to promote its own release?
#
#   scripts/check-tag-workflow.sh [<commit>]    # default HEAD
#   scripts/check-tag-workflow.sh --selftest    # prove it can refuse
#
# Exit 0 the commit can promote; 1 it cannot; 2 the rig is broken.
#
# WHY THIS RUNS BEFORE THE TAG, NOT AFTER. A release event executes the
# workflow file from the TAGGED COMMIT, not from main -- observed on a scratch
# tag on 2026-09-20, where promote/demote steps that existed only on that commit
# ran. So a tag cut before the promote step is merged carries a workflow with no
# promote step at all: the release is created as a prerelease, builds, passes
# its gate, and then sits as a prerelease forever with nothing to advance it.
#
# That failure is SAFE -- `latest` never moves -- and that is exactly what makes
# it expensive. Its only symptom is that nothing happens, which is the hardest
# thing there is to diagnose on release night.
#
# WHAT IT CHECKS, and deliberately not more: the version job uploads version.txt
# and then promotes with `--prerelease=false`, IN THAT ORDER. Promotion is the
# last act; if it ever came first, `latest` would move before version.txt was
# on the release, and releases/latest/download/version.txt would 404 for the
# whole fleet -- the shape of both outages of 2026-09-17/18.

set -u
export MSYS_NO_PATHCONV=1   # git show <rev>:<path> is mangled by MSYS otherwise

WF=".github/workflows/firmware.yml"

# Reads workflow text on stdin. Prints a verdict line; returns 0/1.
check_text() {
  local text up pr
  text="$(cat)"
  up="$(printf '%s\n' "$text" | grep -n 'gh release upload .*version\.txt' | head -1 | cut -d: -f1)"
  pr="$(printf '%s\n' "$text" | grep -n 'gh release edit .*--prerelease=false' | head -1 | cut -d: -f1)"
  if [ -z "$pr" ]; then
    echo "REFUSE: no promote step (gh release edit ... --prerelease=false)."
    echo "        A release tagged here would build and then stay a prerelease forever."
    return 1
  fi
  if [ -z "$up" ]; then
    echo "REFUSE: no version.txt upload step, so promotion would advance a release"
    echo "        that devices cannot read a version from."
    return 1
  fi
  if [ "$pr" -le "$up" ]; then
    echo "REFUSE: promotion (line $pr) comes BEFORE the version.txt upload (line $up)."
    echo "        latest would move while version.txt is absent -- a fleet-wide 404."
    return 1
  fi
  echo "ok: version.txt uploaded at line $up, promoted at line $pr -- promotion is last."
  return 0
}

if [ "${1:-}" = "--selftest" ]; then
  rc=0
  expect() {   # name, wanted-exit, text
    local got
    printf '%s\n' "$3" | check_text >/dev/null; got=$?
    if [ "$got" -eq "$2" ]; then echo "  ok    $1"; else echo "  FAIL  $1 (exit $got, wanted $2)"; rc=1; fi
  }
  expect "upload then promote is accepted" 0 \
'      run: gh release upload "$t" version.txt --clobber
      run: gh release edit "$t" --prerelease=false'
  expect "no promote step is REFUSED" 1 \
'      run: gh release upload "$t" version.txt --clobber'
  expect "promote before upload is REFUSED" 1 \
'      run: gh release edit "$t" --prerelease=false
      run: gh release upload "$t" version.txt --clobber'
  expect "no version.txt upload is REFUSED" 1 \
'      run: gh release edit "$t" --prerelease=false'
  [ "$rc" -eq 0 ] && echo "SELFTEST PASSED" || echo "SELFTEST FAILED"
  exit "$rc"
fi

REV="${1:-HEAD}"
if ! git rev-parse --verify --quiet "$REV^{commit}" >/dev/null; then
  echo "FATAL: $REV is not a commit here. (A shallow clone cannot see old tags.)" >&2
  exit 2
fi
if ! git show "$REV:$WF" >/dev/null 2>&1; then
  echo "FATAL: $REV has no $WF." >&2
  exit 2
fi
echo "checking $(git rev-parse --short "$REV"):"
git show "$REV:$WF" | check_text
