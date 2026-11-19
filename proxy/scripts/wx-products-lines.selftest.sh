#!/usr/bin/env bash
# Selftest for wx-products-lines.sh (docs/wx-healthz-products.md, H6 and H6s). Every case compares the WHOLE printed
# output with the expected one, never a fragment, and checks the return status. Case (a) is the committed fixture whose
# routed and stale vitest's H3 proves are the real plant's (test/wx-endpoint.test.ts), so its WARN lines come from what
# the code produced. Then the plant: the WARN condition removed in a temporary copy must change case (a)'s output; a
# plant that changes nothing did not apply, and that is a failure too.
set -u
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FIXTURE="$HERE/../test/fixtures/wx/healthz-h3.json"
rc=0
fail() { printf 'SELFTEST FAIL: %s\n' "$1"; rc=1; }

BODY_A="$(cat "$FIXTURE")"
[ "$(printf '%s' "$BODY_A" | grep -oE '"stale":true' | wc -l)" -eq 2 ] || fail "the fixture does not hold exactly two stale:true"
BODY_B="$(printf '%s' "$BODY_A" | sed 's/"stale":true/"stale":false/g')"
[ "$(printf '%s' "$BODY_B" | grep -oE '"stale":true' | wc -l)" -eq 0 ] && [ "$BODY_B" != "$BODY_A" ] \
  || fail "(b)'s edit did not apply"
BODY_C='{"ok":true,"commit":"x","upstreams":[{"id":"adsb_fi","enabled":true}],"wx":{"cref":{"frameAgeS":30},"opuk":{"frameAgeS":120,"coveredShare":0.7513}},"basemap":"not_configured"}'
BODY_D='{"ok":true,"wx":"not_configured"}'

# N6 (docs/wx-opera-backup-fallback.md 9): every OPERA line (id op*) ends with source=<primary|backup|null|->; "-" is a
# Worker before the field, which the committed fixture (H3's plant, made before it) is. NOAA lines are unchanged.
EXPECT_A='WARN wx.cref routed=true age=660 stale=true
     wx.opuk routed=true age=null stale=null source=-
     wx.opis routed=false age=null stale=null source=-
     wx.opib routed=false age=null stale=null source=-
     wx.opmt routed=false age=null stale=null source=-
     wx.opwe routed=false age=null stale=null source=-
     wx.opce routed=false age=null stale=null source=-
WARN wx.opse routed=false age=1860 stale=true source=-
     wx.opba routed=false age=1740 stale=false source=-
     wx.opsw routed=false age=null stale=null source=-
     wx.opsn routed=false age=null stale=null source=-
     wx.shsr routed=false age=null stale=null'
EXPECT_B='     wx.cref routed=true age=660 stale=false
     wx.opuk routed=true age=null stale=null source=-
     wx.opis routed=false age=null stale=null source=-
     wx.opib routed=false age=null stale=null source=-
     wx.opmt routed=false age=null stale=null source=-
     wx.opwe routed=false age=null stale=null source=-
     wx.opce routed=false age=null stale=null source=-
     wx.opse routed=false age=1860 stale=false source=-
     wx.opba routed=false age=1740 stale=false source=-
     wx.opsw routed=false age=null stale=null source=-
     wx.opsn routed=false age=null stale=null source=-
     wx.shsr routed=false age=null stale=null'
EXPECT_C='     wx.cref routed=- age=30 stale=-
     wx.opuk routed=- age=120 stale=- source=-'
# (g), N6: the field as the fallback writes it -- backup, primary, null -- and a NOAA product with no source key
BODY_G='{"ok":true,"wx":{"cref":{"frameAgeS":30,"routed":true,"stale":false},"opuk":{"frameAgeS":60,"coveredShare":0.75,"source":"backup","routed":true,"stale":false},"opis":{"frameAgeS":90,"coveredShare":0.5,"source":"primary","routed":false,"stale":false},"opib":{"frameAgeS":null,"coveredShare":null,"source":null,"routed":false,"stale":null}}}'
EXPECT_G='     wx.cref routed=true age=30 stale=false
     wx.opuk routed=true age=60 stale=false source=backup
     wx.opis routed=false age=90 stale=false source=primary
     wx.opib routed=false age=null stale=null source=null'
EXPECT_D='     wx not_configured'
EXPECT_F='WARN wx unreadable'

check() {   # $1 the function file, $2 the case's label, $3 the body, $4 the expected output
  local got st
  got="$( . "$1"; wx_products_section "$3" )"; st=$?
  if [ "$st" -ne 0 ]; then fail "($2) returned $st, not 0"; fi
  if [ "$got" = "$4" ]; then printf 'case (%s): as expected (%s lines, %s WARN)\n' "$2" "$(printf '%s\n' "$got" | wc -l)" "$(printf '%s\n' "$got" | grep -c '^WARN')"
  else fail "($2) printed:"; printf '%s\n' "$got"; fi
}
F="$HERE/wx-products-lines.sh"
check "$F" a "$BODY_A" "$EXPECT_A"
check "$F" b "$BODY_B" "$EXPECT_B"
check "$F" c "$BODY_C" "$EXPECT_C"
check "$F" d "$BODY_D" "$EXPECT_D"
check "$F" f "" "$EXPECT_F"
check "$F" f2 '{"ok":false}' "$EXPECT_F"
check "$F" g "$BODY_G" "$EXPECT_G"

# (e) smoke calls it exactly once, on a bare line that touches neither counter, and sources it once.
SMOKE="$HERE/smoke-prod.sh"
n="$(grep -cx 'wx_products_section "$HEALTH_BODY"' "$SMOKE")"
[ "$n" -eq 1 ] && printf 'case (e): smoke calls it once, on a bare line\n' || fail "(e) smoke has $n bare call lines, not 1"
n="$(grep -c 'wx_products_section' "$SMOKE")"
[ "$n" -eq 1 ] || fail "(e) smoke names wx_products_section $n times, not once"
n="$(grep -c 'wx-products-lines.sh' "$SMOKE")"
[ "$n" -eq 1 ] || fail "(e) smoke sources wx-products-lines.sh $n times, not once"

# H6s: the WARN condition removed must change case (a).
tmp="$(mktemp)"
sed 's/\[ "\$stale" = "true" \] \&\& mark="WARN"/[ "$stale" = "never" ] \&\& mark="WARN"/' "$F" > "$tmp"
if cmp -s "$tmp" "$F"; then
  fail "H6s: the plant did not apply (the file is unchanged)"
else
  sab="$( . "$tmp"; wx_products_section "$BODY_A" )"
  if [ "$sab" = "$EXPECT_A" ]; then fail "H6s: the planted copy printed case (a) unchanged"
  else printf 'H6s: planted, case (a) changed: %s WARN lines, not 2 -- detected\n' "$(printf '%s\n' "$sab" | grep -c '^WARN')"; fi
fi
rm -f "$tmp"
[ "$rc" -eq 0 ] && printf 'SELFTEST PASSED\n'
exit "$rc"
