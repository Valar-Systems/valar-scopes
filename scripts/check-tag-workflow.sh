#!/usr/bin/env bash
# Is the commit you are about to tag able to promote its own release, and only
# behind the human gate?
#
#   scripts/check-tag-workflow.sh [<commit>]    # default HEAD
#   scripts/check-tag-workflow.sh --selftest    # prove it can refuse
#
# Exit 0 the commit can promote, gated; 1 it cannot; 2 the rig is broken.
#
# WHY THIS RUNS BEFORE THE TAG, NOT AFTER. A release event executes the
# workflow file from the TAGGED COMMIT, not from main -- observed on a scratch
# tag on 2026-09-20. So a tag cut from a commit with the wrong promotion shape
# runs that shape, whatever main says by then.
#
# WHAT IT CHECKS. Promotion (`gh release edit ... --prerelease=false`) lives in
# its own `promote` job, and:
#   - that job has `needs: version`, so it cannot start before the receipt gate
#     and the version.txt upload have finished;
#   - it has `environment: release`, the environment with a required reviewer,
#     so it cannot start until a person approves it after flashing a bench board;
#   - its `if:` has always() or !cancelled() AND `needs.version.result ==
#     'success'`. The property is "only a successful `version` reaches promote",
#     and it fails in BOTH directions without both halves:
#       * no status function: GitHub applies an implicit success() over EVERY
#         ancestor job, so a red slug-less harness leg in the build matrix skips
#         promotion forever. Observed 2026-09-22 on 12ec327 (scratch run
#         35816020466): `version` passed, `promote` skipped, no review asked.
#         An explicit success() is the same thing, and failure() would promote
#         only when something upstream HAD failed, so neither counts;
#       * a status function but no result check: a failed `version` reaches
#         promotion.
#     The previous rule here BANNED status functions. It encoded the first
#     failure as the requirement and could not see it;
#   - it reads version.txt from the release's own download URL before promoting,
#     which is where the ordering is actually proven now;
#   - and the promotion switch appears NOWHERE ELSE in the file. A second copy
#     left in `version` would promote ungated, with the gated job beside it
#     looking correct.
# The `version` job must still upload version.txt.
#
# Line order used to be the proof, when promotion was the last step of
# `version`. It is not any more: two jobs have no line order GitHub respects,
# only `needs:`.
#
# The parse is by indentation (jobs at two spaces, job keys at four), with
# full-line comments stripped first so prose that mentions always() does not
# count. Equivalent YAML spellings it does not recognise (`environment:` with a
# `name:` child, for example) are REFUSED, which is the safe direction.

set -u
export MSYS_NO_PATHCONV=1   # git show <rev>:<path> is mangled by MSYS otherwise

WF=".github/workflows/firmware.yml"

# Prints one job's lines (header excluded) from comment-stripped text on stdin.
job_body() {
  awk -v name="$1" '
    /^  [A-Za-z0-9_-]+:[ \t]*$/ { injob = ($0 ~ "^  " name ":[ \t]*$"); next }
    /^[^ ]/                     { injob = 0 }
    injob                       { print }
  '
}

# Everything EXCEPT the promote job, from comment-stripped text on stdin.
outside_promote() {
  awk '
    /^  [A-Za-z0-9_-]+:[ \t]*$/ { inp = ($0 ~ /^  promote:[ \t]*$/); if (!inp) print; next }
    /^[^ ]/                     { inp = 0 }
    !inp                        { print }
  '
}

# Reads workflow text on stdin. Prints a verdict line; returns 0/1.
check_text() {
  local text promote version outside
  text="$(grep -v '^[[:space:]]*#' | tr -d '\r')"
  promote="$(printf '%s\n' "$text" | job_body promote)"
  version="$(printf '%s\n' "$text" | job_body version)"
  outside="$(printf '%s\n' "$text" | outside_promote)"

  if ! printf '%s\n' "$text" | grep -qE '^  promote:[[:space:]]*$'; then
    echo "REFUSE: no \`promote\` job. Promotion must be its own job behind the"
    echo "        release environment, not a step anywhere else."
    return 1
  fi
  if printf '%s\n' "$outside" | grep -q -- '--prerelease=false'; then
    echo "REFUSE: the promotion switch (--prerelease=false) appears OUTSIDE the"
    echo "        promote job. That copy runs without the human gate."
    return 1
  fi
  if ! printf '%s\n' "$promote" | grep -q 'gh release edit .*--prerelease=false'; then
    echo "REFUSE: the promote job does not run gh release edit ... --prerelease=false."
    echo "        A release tagged here would build and stay a prerelease forever."
    return 1
  fi
  if ! printf '%s\n' "$promote" | grep -qE '^    needs:[[:space:]]*(version|\[[[:space:]]*version[[:space:]]*\])[[:space:]]*$'; then
    echo "REFUSE: the promote job lacks \`needs: version\`. It could start before"
    echo "        the receipt gate passed and version.txt was uploaded."
    return 1
  fi
  if ! printf '%s\n' "$promote" | grep -qE '^    environment:[[:space:]]*release[[:space:]]*$'; then
    echo "REFUSE: the promote job lacks \`environment: release\`. Nothing would"
    echo "        wait for a person before the fleet moves."
    return 1
  fi
  local cond
  cond="$(printf '%s\n' "$promote" | grep -E '^    if:' | head -1)"
  # Only always() and !cancelled() take the ancestry out of the decision. An
  # explicit success() is as ancestry-wide as the implicit one, and failure()
  # would promote only when something upstream had failed.
  if ! printf '%s\n' "$cond" | grep -qE 'always\(\)|!cancelled\(\)' \
     || printf '%s\n' "$cond" | grep -qE '(^|[^.a-z])(success|failure)\(\)'; then
    echo "REFUSE: the promote job's if: lacks always() or !cancelled() (or also uses"
    echo "        success()/failure()), so the build matrix's ancestry decides. A red"
    echo "        harness leg then skips promotion forever (seen on 12ec327, 2026-09-22)."
    return 1
  fi
  if ! printf '%s\n' "$cond" | grep -qE "needs\.version\.result[[:space:]]*==[[:space:]]*'success'"; then
    echo "REFUSE: the promote job's if: takes the ancestry out of the decision but has"
    echo "        no needs.version.result == 'success'. A failed \`version\` could reach promotion."
    return 1
  fi
  if ! printf '%s\n' "$promote" | grep -q 'releases/download/'; then
    echo "REFUSE: the promote job does not read version.txt from the release's own"
    echo "        download URL before promoting."
    return 1
  fi
  if ! printf '%s\n' "$version" | grep -q 'gh release upload .*version\.txt'; then
    echo "REFUSE: the version job does not upload version.txt, so promotion would"
    echo "        advance a release that devices cannot read a version from."
    return 1
  fi
  echo "ok: promote needs version, runs in environment release, is reached only by a"
  echo "    successful version, checks its own version.txt; nothing else promotes."
  return 0
}

if [ "${1:-}" = "--selftest" ]; then
  rc=0
  expect() {   # name, wanted-exit, text
    local got
    printf '%s\n' "$3" | check_text >/dev/null; got=$?
    if [ "$got" -eq "$2" ]; then echo "  ok    $1"; else echo "  FAIL  $1 (exit $got, wanted $2)"; rc=1; fi
  }
  V='  version:
    if: always() && github.event_name == "release"
    needs: build
    steps:
      - run: gh release upload "$t" version.txt --clobber
      - if: failure()
        run: gh release edit "$t" --prerelease'
  P='  promote:
    if: ${{ !cancelled() && github.event_name == '"'release'"' && needs.version.result == '"'success'"' }}
    needs: version
    environment: release
    steps:
      - run: curl -fsSL "https://github.com/$R/releases/download/$t/version.txt"
      - run: gh release edit "$t" --prerelease=false'
  PROMOTE_IN_VERSION='      - run: gh release edit "$t" --prerelease=false'
  good="jobs:
$V

$P"
  # Controls first: the accepted shapes must stay accepted, or every REFUSE
  # below could be passing because the checker refuses everything.
  expect "the gated shape is accepted" 0 "$good"
  expect "a comment mentioning always() in promote does not count" 0 \
    "$(printf '%s\n' "$good" | sed 's/^    needs: version$/    # never always() here\n    needs: version/')"
  expect "needs: [version] is accepted" 0 \
    "$(printf '%s\n' "$good" | sed 's/^    needs: version$/    needs: [version]/')"

  expect "promotion inside version, no promote job (the 496b249 shape) is REFUSED" 1 "jobs:
$V
$PROMOTE_IN_VERSION"
  expect "promotion in version AS WELL AS a promote job is REFUSED" 1 "jobs:
$V
$PROMOTE_IN_VERSION

$P"
  expect "promote without needs is REFUSED" 1 \
    "$(printf '%s\n' "$good" | grep -v '^    needs: version$')"
  expect "promote needing something else is REFUSED" 1 \
    "$(printf '%s\n' "$good" | sed 's/^    needs: version$/    needs: build/')"
  expect "promote without environment is REFUSED" 1 \
    "$(printf '%s\n' "$good" | grep -v '^    environment: release$')"
  expect "promote in another environment is REFUSED" 1 \
    "$(printf '%s\n' "$good" | sed 's/^    environment: release$/    environment: staging/')"
  # The if: line, both directions. The property is "only a successful version
  # reaches promote", so each way of losing either half is its own case.
  expect "always() with the result check is accepted" 0 \
    "$(printf '%s\n' "$good" | sed 's/!cancelled()/always()/')"
  expect "no status function (the 12ec327 shape: ancestry skips it) is REFUSED" 1 \
    "$(printf '%s\n' "$good" | sed "s/^    if: .*/    if: github.event_name == 'release'/")"
  expect "explicit success() instead of !cancelled() is REFUSED" 1 \
    "$(printf '%s\n' "$good" | sed 's/!cancelled()/success()/')"
  expect "failure() instead of !cancelled() is REFUSED" 1 \
    "$(printf '%s\n' "$good" | sed 's/!cancelled()/failure()/')"
  expect "!cancelled() AND success() together is REFUSED" 1 \
    "$(printf '%s\n' "$good" | sed 's/!cancelled()/!cancelled() \&\& success()/')"
  expect "status function without the result check is REFUSED" 1 \
    "$(printf '%s\n' "$good" | sed "s/ \&\& needs.version.result == 'success'//")"
  expect "a result check on the wrong job is REFUSED" 1 \
    "$(printf '%s\n' "$good" | sed 's/needs.version.result/needs.build.result/')"
  expect "promote without the switch is REFUSED" 1 \
    "$(printf '%s\n' "$good" | grep -v 'prerelease=false')"
  expect "promote reading latest instead of its own URL is REFUSED" 1 \
    "$(printf '%s\n' "$good" | sed 's#releases/download/\$t#releases/latest/download#')"
  expect "no version.txt upload is REFUSED" 1 \
    "$(printf '%s\n' "$good" | grep -v 'gh release upload')"
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
