#!/usr/bin/env bash
# Selftest for enrich-fixed-verdict.sh (smoke-prod.sh section 2c, enrich on a fixed hex). Canned bodies only: no
# request is made. The WHOLE case list must equal the expected one (no fragment matching), and the op value planted in
# one body must never reach the output. Then three plants, each written to a copy and proven applied (the copy differs
# from the real file) before its result is read, must turn exactly their registered cases:
#   A  the control's NOT FOUND comparison dropped (status and JSON checks kept)  -> exactly F4
#   B  op added to the 2c-ANSWER line                                           -> exactly F1 and F6, and op shows once
#   C  the SMOKE_FIXED_HEX shape check removed                                  -> exactly F8
set -u
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
V="$HERE/enrich-fixed-verdict.sh"
[ -f "$V" ] || { printf 'SELFTEST BLIND: enrich-fixed-verdict.sh is missing\n'; exit 2; }
command -v node >/dev/null 2>&1 || { printf 'SELFTEST BLIND: node is not on PATH\n'; exit 2; }
T="$(mktemp -d)"
trap 'rm -rf "$T"' EXIT

P='/api/v1/blipscope/photo/photo:B739-'
printf '{"v":1,"r":"N1TEST","t":"B739","tn":"BOEING 737-900","pk":"type","p":"%s00000000"}' "$P" > "$T/e0.json"
printf '{"v":1,"r":"N1TEST","t":"B739","tn":"BOEING 737-900","pk":"type","p":"%s00000000"}' "$P" > "$T/e6.json"
printf '{"v":1,"r":"N1TEST","t":"B739","tn":"BOEING 737-900","pk":"type","p":"%s11111111"}' "$P" > "$T/e7.json"
# enrich.ts's answer for a "~" id: the empty body, no p.
printf '{"v":1,"r":"","t":"","tn":"","op":"","o":"","d":""}' > "$T/k.json"
printf '{"v":1,"r":"N2TEST","t":"B739","tn":"BOEING 737-900","op":"OPSECRET","pk":"type","p":"%s00000000"}' "$P" > "$T/e0op.json"
printf 'not json' > "$T/bad.txt"
printf '{"v":2,"r":"N1TEST"}' > "$T/v2.json"

cases() {   # $1: the verdict file to source; one line per case
  ( . "$1"
    printf 'F1: %s | %s | %s | %s | %s\n' "$(enrich_fixed_row 200 "$T/e0.json")" "$(enrich_fixed_row 200 "$T/e6.json")" \
      "$(enrich_fixed_row 200 "$T/e7.json")" "$(enrich_fixed_control 200 "$T/k.json")" \
      "$(enrich_fixed_answer abc123 "$T/e0.json" "$T/e6.json" "$T/e7.json")"
    printf 'F2: %s\n' "$(enrich_fixed_row 500 "$T/e0.json")"
    printf 'F3: %s\n' "$(enrich_fixed_row 200 "$T/bad.txt")"
    printf 'F4: %s\n' "$(enrich_fixed_control 200 "$T/e0.json")"
    printf 'F5: %s\n' "$(enrich_fixed_control 200 "$T/bad.txt")"
    printf 'F6: %s\n' "$(enrich_fixed_answer abc123 "$T/e0op.json" "$T/e6.json" "$T/e7.json")"
    printf 'F7: %s\n' "$(enrich_fixed_hex def456 abc123)"
    printf 'F8: %s\n' "$(enrich_fixed_hex DEF45X9 abc123)"
    printf 'F9: %s\n' "$(enrich_fixed_hex '' '~abc123')"
    printf 'F10: %s\n' "$(enrich_fixed_hex '' abc123)"
    printf 'F11: %s\n' "$(enrich_fixed_row 200 "$T/v2.json")"
  )
}

A0='2c-ANSWER abc123 r="N1TEST" t="B739" tn="BOEING 737-900" pk="type"'
A6='2c-ANSWER abc123 r="N2TEST" t="B739" tn="BOEING 737-900" pk="type"'
PS="p0=\"${P}00000000\" p6=\"${P}00000000\" p7=\"${P}11111111\""
OK='PASS -- HTTP 200, JSON, v 1'
EXPECT="F1: $OK | $OK | $OK | PASS -- NOT FOUND, the code's answer for a non-ICAO id | $A0 $PS
F2: FAIL -- HTTP 500
F3: FAIL -- HTTP 200, but the body is not JSON
F4: FAIL (control) -- not NOT FOUND (r t tn p)
F5: FAIL (control) -- HTTP 200, but the body is not JSON
F6: $A6 $PS
F7: def456 SMOKE_FIXED_HEX
F8: REFUSED 7
F9: NONE this run's T1 is a non-ICAO ~ id
F10: abc123 T1
F11: FAIL -- HTTP 200, JSON, but v is not 1"

turned() {   # the ids of the cases whose line differs from EXPECT, space-separated
  diff <(printf '%s\n' "$EXPECT") <(printf '%s\n' "$1") | grep -E '^> ' | cut -d' ' -f2 | tr -d ':' | tr '\n' ' ' | sed 's/ $//'
}

rc=0
got="$(cases "$V")"
if [ "$got" = "$EXPECT" ]; then
  printf 'real: 11 of 11 cases as expected\n'
else
  printf 'SELFTEST FAIL: the real verdicts differ from the expected list\n'
  diff <(printf '%s\n' "$EXPECT") <(printf '%s\n' "$got")
  rc=1
fi
ops="$(printf '%s\n' "$got" | grep -c 'OPSECRET')"
printf 'op value in the output: %s (want 0)\n' "$ops"
[ "$ops" -eq 0 ] || rc=1

plant() {   # <id> <want turned> <want op count> <sed expression>
  local id="$1" want="$2" wantops="$3" expr="$4" cp="$T/plant-$1.sh" g t o
  sed "$expr" "$V" > "$cp"
  if cmp -s "$cp" "$V"; then printf 'SELFTEST FAIL: plant %s did not apply (the copy is unchanged)\n' "$id"; rc=1; return; fi
  g="$(cases "$cp")"; t="$(turned "$g")"; o="$(printf '%s\n' "$g" | grep -c 'OPSECRET')"
  printf 'plant %s: turned [%s] (want [%s]); op value in the output %s (want %s)\n' "$id" "$t" "$want" "$o" "$wantops"
  [ "$t" = "$want" ] && [ "$o" -eq "$wantops" ] || { printf 'SELFTEST FAIL: plant %s\n' "$id"; rc=1; }
}
plant A 'F4' 0 's/bad="$(_ef_notfound_misses "$f")"/bad=""/'
plant B 'F1 F6' 1 's/ p7=\${q(e7, "p")}/ p7=${q(e7, "p")} op=${q(e0, "op")}/'
plant C 'F8' 0 '/REFUSED %s/d'

[ "$rc" -eq 0 ] && printf 'SELFTEST PASSED\n'
exit "$rc"
