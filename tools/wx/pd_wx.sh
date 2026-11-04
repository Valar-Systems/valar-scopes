#!/usr/bin/env bash
# PD3/PD4 (docs/wx-opuk.md): /wx on production as the test identity, through smoke's own helper (test-identity.sh).
# Prints labels, statuses and headers only -- never a coordinate and never a credential. A scratch file in the deploy
# checkout (dot-named, removed after use; never committed).
set -uo pipefail
SCRIPT_DIR="${PROXY_SCRIPTS:?set PROXY_SCRIPTS to the deploy checkout's proxy/scripts}"
. "$SCRIPT_DIR/test-identity.sh"
test_identity_or_reexec "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/$(basename "${BASH_SOURCE[0]}")" "$@"
blip_auth_curl
BASE="${BASE:-https://scopes.valarsystems.com}"
eval "$(grep -E '^(LAT|LON|R)=' "$SCRIPT_DIR/smoke-prod.sh" | head -3)"
read -r _ LHR_LAT LHR_LON < <(grep '^LHR ' "$SCRIPT_DIR/../test/fixtures/wx-airports-uk.txt")
probe() {   # label product lat lon r
  local hdr body
  hdr="$(mktemp)"; body="$(mktemp)"
  local st
  st="$(curl -s --max-time 25 -o "$body" -D "$hdr" -w '%{http_code}' "${AUTH[@]}" \
        "$BASE/api/v1/blipscope/wx/$2?lat=$3&lon=$4&r=$5")"
  local magic etag age frame err
  magic="$(head -c 4 "$body" | tr -cd 'A-Z0-9')"
  etag="$(grep -i '^etag:' "$hdr" | tr -d '\r' | grep -oE 'wx1-[a-z0-9]+' | head -1)"
  age="$(grep -i '^x-wx-max-age:' "$hdr" | tr -d '\r' | cut -d' ' -f2)"
  frame="$(grep -ci '^x-wx-frame:' "$hdr")"
  err="$(grep -oE '"error":"[a-z_]+"' "$body" | head -1)"
  printf '%-22s HTTP %s  magic [%s]  etag-product [%s]  X-Wx-Max-Age [%s]  X-Wx-Frame %s  %s\n' \
    "$1" "$st" "$magic" "${etag#wx1-}" "${age:-none}" "$([ "$frame" = 1 ] && echo present || echo absent)" "$err"
  rm -f "$hdr" "$body"
}
probe "/wx/cref Bend (smoke)" cref "$LAT" "$LON" "$R"
probe "/wx/cref KMCI" cref 39.30 -94.71 161
probe "/wx/cref LHR" cref "$LHR_LAT" "$LHR_LON" 161
probe "/wx/opuk LHR" opuk "$LHR_LAT" "$LHR_LON" 161
printf '/healthz wx: %s\n' "$(curl -s --max-time 15 "$BASE/healthz" | grep -oE '"wx":\{[^}]*\}[^}]*\}' | head -1)"
