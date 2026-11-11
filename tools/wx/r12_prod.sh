#!/usr/bin/env bash
# R1 and R2 on production (docs/wx-opera-r12-prod.md): /wx/cref as the test identity, through smoke's own helper
# (proxy/scripts/test-identity.sh), at the points r12_points.py wrote. Prints labels, statuses and headers only --
# never a coordinate and never a credential.
#   bash tools/wx/r12_prod.sh <points.tsv>
set -uo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SCRIPT_DIR="$(cd "$HERE/../../proxy/scripts" && pwd)"
. "$SCRIPT_DIR/test-identity.sh"
test_identity_or_reexec "$HERE/$(basename "${BASH_SOURCE[0]}")" "$@"
blip_auth_curl
BASE="${BASE:-https://scopes.valarsystems.com}"
PTS="${1:?usage: r12_prod.sh <points.tsv>}"
T="$(mktemp -d)"

hz="$(curl -s --max-time 20 "$BASE/healthz")"
printf 'healthz commit [%s]\n' "$(printf '%s' "$hz" | grep -oE '"commit":"[0-9a-f]+' | head -1 | cut -d'"' -f4)"
printf 'healthz wx: %s\n' "$(printf '%s' "$hz" | grep -oE '"wx":\{[^}]*\}[^}]*\}[^}]*\}' | head -1)"

ok_isles=0; ok_others=0; ok_r2=0; n_isles=0; n_others=0; n_r2=0
while IFS=$'\t' read -r label lat lon want; do
  [ -n "$label" ] || continue
  st="$(curl -s --max-time 25 -o "$T/body" -D "$T/hdr" -w '%{http_code}' "${AUTH[@]}" \
        "$BASE/api/v1/blipscope/wx/cref?lat=$lat&lon=$lon&r=161")"
  etag="$(grep -i '^etag:' "$T/hdr" | tr -d '\r' | grep -oE 'wx1-[a-z0-9]+' | head -1)"; etag="${etag#wx1-}"
  age="$(grep -i '^x-wx-max-age:' "$T/hdr" | tr -d '\r' | cut -d' ' -f2)"
  b32="$(od -An -tx1 -j32 -N1 "$T/body" 2>/dev/null | tr -d ' \n')"
  err="$(grep -oE '"error":"[a-z_]+"' "$T/body" 2>/dev/null | head -1)"
  if [ "$want" = opuk ]; then
    ok=0; [ "$st" = 200 ] && [ "$etag" = opuk ] && [ "$age" = 720 ] && ok=1
  else
    ok=0; [ "$st" = 200 ] && [ "$etag" = cref ] && [ "$b32" = e0 ] && [ -z "$age" ] && ok=1
  fi
  case "$label" in
    "R2 "*) n_r2=$((n_r2 + 1)); ok_r2=$((ok_r2 + ok)) ;;
    *) if [ "$want" = opuk ]; then n_isles=$((n_isles + 1)); ok_isles=$((ok_isles + ok)); else n_others=$((n_others + 1)); ok_others=$((ok_others + ok)); fi ;;
  esac
  printf '%-34s want %-4s  HTTP %s  etag [%s]  X-Wx-Max-Age [%s]  byte32 [%s]  %s %s\n' \
    "$label" "$want" "$st" "$etag" "${age:-none}" "$b32" "$([ $ok = 1 ] && echo ok || echo OFF)" "$err"
done < "$PTS"
rm -rf "$T"
echo "R1 isles: $ok_isles of $n_isles as predicted; R1 others: $ok_others of $n_others; R2 sample: $ok_r2 of $n_r2"
[ "$ok_isles" = 5 ] && [ "$n_isles" = 5 ] && [ "$ok_others" = 35 ] && [ "$n_others" = 35 ] && [ "$ok_r2" = "$n_r2" ] && [ "$n_r2" = 9 ] \
  && { echo "R1, R2 sample: ALL AS PREDICTED"; exit 0; }
echo "R1, R2 sample: NOT AS PREDICTED"; exit 1
