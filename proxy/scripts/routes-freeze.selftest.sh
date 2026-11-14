#!/usr/bin/env bash
# routes-freeze.selftest.sh -- W4's command-line face, FZC1-FZC4 (docs/route-weekly-code.md, W4 and W7). The rule
# itself is G.freeze, graded by FZ1-FZ4 in routes-guards.selftest.mts; this grades what the workflow sees: the exit
# code and the stated line. Future is 2099 and past is 2000, so the rows hold whatever day they run on.
set -u
cd "$(dirname "${BASH_SOURCE[0]}")/.."
pass=0; fail=0; failed=""
ok()  { echo "PASS $1"; pass=$((pass+1)); }
bad() { echo "FAIL $1: $2"; fail=$((fail+1)); failed="$failed $1"; }
run() { # run <value|UNSET>: prints "<exit> <line>"
  local out rc
  if [ "$1" = UNSET ]; then out=$(env -u ROUTES_FREEZE_UNTIL bash scripts/routes-freeze.sh 2>&1); rc=$?
  else out=$(ROUTES_FREEZE_UNTIL="$1" bash scripts/routes-freeze.sh 2>&1); rc=$?; fi
  echo "$rc $out"
}
r=$(run 2099-01-01T00:00:00Z); case "$r" in "4 freeze: SKIP -- frozen until 2099-01-01T00:00:00Z"*) ok FZC1 ;; *) bad FZC1 "$r" ;; esac
r=$(run 2000-01-01T00:00:00Z); case "$r" in "0 freeze: run -- the freeze ended at 2000-01-01T00:00:00Z"*) ok FZC2 ;; *) bad FZC2 "$r" ;; esac
r=$(run UNSET);                case "$r" in "0 freeze: run -- no freeze set"*) ok FZC3 ;; *) bad FZC3 "$r" ;; esac
r=$(run "next tuesday");       case "$r" in "1 freeze: FAIL -- ROUTES_FREEZE_UNTIL is set but is not an ISO UTC time"*) ok FZC4 ;; *) bad FZC4 "$r" ;; esac
echo "rows: $pass passed, $fail failed"
if [ "$fail" -gt 0 ]; then echo "SELFTEST FAILED:$failed"; exit 1; fi
echo "SELFTEST PASSED ($pass rows)"
