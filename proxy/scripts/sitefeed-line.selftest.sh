#!/usr/bin/env bash
# Selftest for sitefeed-line.sh, P-MF5s (the malformed-flags registration, Addendum 2): smoke's one site-feed line from
# the /healthz body. Every case compares the WHOLE printed output with the expected one, never a fragment, and checks
# the return status, which is always 0 (the line is information, never a FAIL). Then the plant: the WARN condition
# removed in a temporary copy must change case (b)'s output and no other case's; a plant that changes nothing did not
# apply, and that is a failure too.
set -u
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC="$HERE/sitefeed-line.sh"
rc=0
fail() { printf 'SELFTEST FAIL: %s\n' "$1"; rc=1; }

BODY_A='{"ok":true,"commit":"x","upstreams":[{"id":"adsb_fi","enabled":true}],"sitefeed":{"available":true,"stale":false,"age_s":3,"n":4,"dbFlagsBad":0}}'
BODY_B='{"ok":true,"commit":"x","sitefeed":{"available":true,"stale":false,"age_s":3,"n":4,"dbFlagsBad":1}}'
BODY_C='{"ok":true,"commit":"x","sitefeed":{"available":false,"reason":"read_error"}}'
BODY_D='{"ok":true,"commit":"x","routes":{"stale":false}}'
BODY_E=''
EXPECT_A='     sitefeed dbFlagsBad=0/4 age=3s stale=false'
EXPECT_B='WARN sitefeed dbFlagsBad=1/4 age=3s stale=false'
EXPECT_C='WARN sitefeed unavailable (read_error)'
EXPECT_D='WARN sitefeed: /healthz has no sitefeed field'
EXPECT_E="$EXPECT_D"
# What (b) must print once the WARN condition is planted out: the same line, unmarked.
PLANTED_B='     sitefeed dbFlagsBad=1/4 age=3s stale=false'

check() { # <source file> <case> <body> <expected>: the WHOLE output
  local out
  out="$(. "$1"; sitefeed_line "$3")"
  [ "$out" = "$4" ] || {
    fail "($2) printed:"; printf '%s\n' "$out" | sed 's/^/    /'; printf '  want:\n'; printf '%s\n' "$4" | sed 's/^/    /'
  }
}
status() { # <source file> <case> <body>: the return status, which must be 0
  local s
  (. "$1"; sitefeed_line "$3" >/dev/null); s=$?
  [ "$s" -eq 0 ] || fail "($2) returned $s, not 0"
}

for c in A B C D E; do
  b="BODY_$c"; e="EXPECT_$c"
  check "$SRC" "$c" "${!b}" "${!e}"
  status "$SRC" "$c" "${!b}"
done

# The plant: the WARN condition removed, in a temporary copy (the source is never written).
ANCHOR='[ "$bad" != "0" ] && mark="WARN"'
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
if [ "$(grep -cF -- "$ANCHOR" "$SRC")" -ne 1 ]; then
  fail "the plant's anchor is not in sitefeed-line.sh exactly once"
else
  content="$(cat "$SRC")"
  planted="${content/"$ANCHOR"/: # SABOTAGE P-MF5s: the WARN condition removed}"
  if [ "$planted" = "$content" ]; then
    fail "the plant did not apply"
  else
    printf '%s\n' "$planted" > "$TMP/sitefeed-line.sh"
    check "$TMP/sitefeed-line.sh" "B, planted" "$BODY_B" "$PLANTED_B"
    for c in A C D E; do
      b="BODY_$c"; e="EXPECT_$c"
      check "$TMP/sitefeed-line.sh" "$c, planted" "${!b}" "${!e}"
    done
  fi
fi

[ "$rc" -eq 0 ] && printf 'SELFTEST PASSED (5 cases + the plant)\n'
exit "$rc"
