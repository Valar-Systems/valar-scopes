#!/usr/bin/env bash
# SV1-SV6 (docs/route-mirror-age.md, section 3): routes_age_verdict, each case in warn mode and in fail mode, compared as
# WHOLE lines, never by fragment. ROUTES_VERDICT names the file to grade, so a sabotage can be graded by the same cases.
#
#   bash scripts/routes-age-verdict.selftest.sh        exit 0 every case as expected, 1 otherwise
set -u
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
. "${ROUTES_VERDICT:-$HERE/routes-age-verdict.sh}"
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
B="2026-08-26T17:25:54.476Z"
HEAD='{"ok":true,"commit":"fedcba9876543210fedcba9876543210fedcba98","upstreams":[{"id":"adsb_fi","enabled":true}],"wx":{},"basemap":"not_configured","weatherscope":{}'
printf '%s,"routes":{"builtAt":"%s","ageDays":3,"rows":619103,"stale":false,"maxAgeDays":14}}' "$HEAD" "$B" > "$T/sv1"
printf '%s,"routes":{"builtAt":"%s","ageDays":79,"rows":619103,"stale":true,"maxAgeDays":14}}' "$HEAD" "$B" > "$T/sv2"
printf '%s,"routes":{"builtAt":null,"ageDays":null,"rows":null,"stale":true,"maxAgeDays":14,"why":"absent"}}' "$HEAD" > "$T/sv3"
printf '%s}' "$HEAD" > "$T/sv4"
printf '<html>error</html>' > "$T/sv5"; : > "$T/sv5e"
fails=0
check() { # <id> <mode> <file> <expected whole line>
  local got; got="$(routes_age_verdict "$3" "$2")"
  if [ "$got" = "$4" ]; then printf 'PASS %s/%s\n' "$1" "$2"; else printf 'FAIL %s/%s: got [%s] want [%s]\n' "$1" "$2" "$got" "$4"; fails=$((fails+1)); fi
}
for m in warn fail; do
  W=WARN; [ "$m" = fail ] && W=FAIL
  check SV1 "$m" "$T/sv1" "PASS -- built $B, 3 d old (limit 14 d)"
  check SV2 "$m" "$T/sv2" "$W -- STALE: built $B, 79 d old (limit 14 d)"
  check SV3 "$m" "$T/sv3" "$W -- no age: absent"
  check SV4 "$m" "$T/sv4" "$W -- /healthz has no routes field (a Worker before this change)"
  check SV5 "$m" "$T/sv5" "$W -- /healthz body unreadable"
  check SV5e "$m" "$T/sv5e" "$W -- /healthz body unreadable"
done
# SV6 (control): the line carries none of the body's other fields.
got="$(routes_age_verdict "$T/sv2" warn)"
case "$got" in
  *fedcba98*|*adsb_fi*|*upstreams*|*commit*) printf 'FAIL SV6: the line carries a field outside routes: [%s]\n' "$got"; fails=$((fails+1)) ;;
  "") printf 'FAIL SV6: empty line\n'; fails=$((fails+1)) ;;
  *) printf 'PASS SV6\n' ;;
esac
if [ "$fails" -eq 0 ]; then printf 'SELFTEST PASSED\n'; exit 0; fi
printf 'SELFTEST FAILED: %d\n' "$fails"; exit 1
