#!/usr/bin/env bash
# Selftest for confirm-deploy.sh. No network: this sandbox cannot bind a socket,
# and the classification is a pure function anyway -- which is why it was split
# out. All four outcomes, plus the timing bug that started this.
set -u
. "$(cd "$(dirname "$0")" && pwd)/confirm-deploy.sh"
pass=0; fail=0
t() { got="$(classify "$2" "$3" "$4")"
      if [ "$got" = "$1" ]; then echo "  ok   $5"; pass=$((pass+1))
      else echo "  FAIL $5: got $got, expected $1"; fail=$((fail+1)); fi }

echo "== confirm-deploy classification =="
t CONFIRMED      1 abc1234   abc1234 "reachable, sha matches -> confirmed"
t WRONG_SHA      1 deadbee   abc1234 "reachable, different sha -> wrong sha"
t UNSTAMPED      1 UNSTAMPED abc1234 "reachable, no commit stamp -> unstamped"
t CANNOT_OBSERVE 0 ""        abc1234 "unreachable -> cannot observe"
# The distinction the old script could not make: unreachable and wrong-sha both
# produced "commit=<none>" and the same reassuring re-check message.
t CANNOT_OBSERVE 0 abc1234   abc1234 "unreachable OUTRANKS a stale body -- never 'confirmed'"

echo "== exit codes are distinct =="
for pair in "CONFIRMED 0" "WRONG_SHA 1" "CANNOT_OBSERVE 2" "UNSTAMPED 3"; do
  set -- $pair
  report "$1" x y h https 5 >/dev/null; rc=$?
  if [ "$rc" = "$2" ]; then echo "  ok   $1 exits $2"; pass=$((pass+1))
  else echo "  FAIL $1 exits $rc, expected $2"; fail=$((fail+1)); fi
done

echo "  $pass passed, $fail failed"
[ "$fail" -eq 0 ]
