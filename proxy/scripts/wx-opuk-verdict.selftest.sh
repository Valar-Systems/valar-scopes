#!/usr/bin/env bash
# Selftest for wx-opuk-verdict.sh (docs/wx-opuk.md, K9's smoke half). Nine canned /healthz bodies; the WHOLE verdict
# list must equal the expected one (no fragment matching). Then the sabotage -- the zero test dropped -- must change
# exactly case 5 (share 0) to PASS, and the selftest must see it: if the sabotage changes nothing, it did not apply,
# and that is a failure too.
set -u
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

CASES=(
  '{"ok":true,"wx":"not_configured"}'
  '{"ok":true,"wx":{"cref":{"frameAgeS":30}}}'
  '{"ok":true,"wx":{"cref":{"frameAgeS":30},"opuk":{"frameAgeS":null,"coveredShare":null}}}'
  '{"ok":true,"wx":{"cref":{"frameAgeS":30},"opuk":{"frameAgeS":120,"coveredShare":0.7513}}}'
  '{"ok":true,"wx":{"cref":{"frameAgeS":30},"opuk":{"frameAgeS":120,"coveredShare":0}}}'
  '{"ok":true,"wx":{"cref":{"frameAgeS":30},"opuk":{"frameAgeS":120,"coveredShare":null}}}'
  '{"ok":true,"wx":{"cref":{"frameAgeS":30},"opuk":{"frameAgeS":120}}}'
  '{"ok":true,"wx":{"cref":{"frameAgeS":30},"opuk":{"frameAgeS":120,"coveredShare":1.5}}}'
  '{"ok":true,"wx":{"cref":{"frameAgeS":30},"opuk":{"frameAgeS":120,"coveredShare":1e-7}}}'
)
EXPECT="PASS FAIL PASS PASS FAIL FAIL FAIL FAIL PASS"
SABOTAGED="PASS FAIL PASS PASS PASS FAIL FAIL FAIL PASS"

verdicts() {   # $1: the verdict file to source
  ( . "$1"; for b in "${CASES[@]}"; do wx_opuk_verdict "$b" | cut -d' ' -f1; done ) | tr '\n' ' ' | sed 's/ $//'
}

rc=0
got="$(verdicts "$HERE/wx-opuk-verdict.sh")"
printf 'real      : %s\nexpected  : %s\n' "$got" "$EXPECT"
[ "$got" = "$EXPECT" ] || { printf 'SELFTEST FAIL: the verdicts differ\n'; rc=1; }

tmp="$(mktemp)"
sed 's/s + 0 > 0 \&\& s + 0 <= 1/s + 0 >= 0 \&\& s + 0 <= 1/' "$HERE/wx-opuk-verdict.sh" > "$tmp"
if cmp -s "$tmp" "$HERE/wx-opuk-verdict.sh"; then
  printf 'SELFTEST FAIL: the sabotage did not apply (the file is unchanged)\n'; rc=1
else
  sab="$(verdicts "$tmp")"
  printf 'sabotaged : %s\nexpected  : %s\n' "$sab" "$SABOTAGED"
  [ "$sab" = "$SABOTAGED" ] || { printf 'SELFTEST FAIL: the sabotage did not turn exactly case 5\n'; rc=1; }
fi
rm -f "$tmp"
[ "$rc" -eq 0 ] && printf 'SELFTEST PASSED\n'
exit "$rc"
