#!/usr/bin/env bash
# smoke-prod.sh -- post-deploy smoke test for the Blipscope proxy.
#
# The device key is read from your environment and is NEVER printed, logged, or
# echoed by this script; only the header name appears in output. Run it yourself:
#
#   export BLIP_KEY='...'                 # your prod device key (not stored here)
#   ./scripts/smoke-prod.sh               # defaults to production
#   BASE=https://scopes-staging.valarsystems.com ./scripts/smoke-prod.sh
#
# Prints PASS/FAIL per check plus the raw response body for eyeballing.

set -uo pipefail

BASE="${BASE:-https://scopes.valarsystems.com}"
KEY="${BLIP_KEY:-}"
# Bend, OR -- the reference tile used throughout the checklist.
LAT="${LAT:-44.06}"
LON="${LON:--121.32}"
R="${R:-160}"

if [ -z "$KEY" ]; then
  echo "BLIP_KEY is not set. export BLIP_KEY='<your prod key>' and re-run." >&2
  exit 2
fi

pass=0
fail=0

# hit <name> <expected-status> <curl args...>
hit() {
  local name="$1" want="$2"; shift 2
  local body status
  body="$(curl -s -w $'\n%{http_code}' --max-time 25 "$@")"
  status="${body##*$'\n'}"
  body="${body%$'\n'*}"
  printf '\n===== %s =====\n' "$name"
  printf 'expect HTTP %s, got %s\n' "$want" "$status"
  printf -- '--- body ---\n%s\n' "$body"
  if [ "$status" = "$want" ]; then
    printf 'RESULT: PASS\n'; pass=$((pass+1))
  else
    printf 'RESULT: FAIL\n'; fail=$((fail+1))
  fi
  LAST_BODY="$body"
  LAST_STATUS="$status"
}

skipped=0
skip() {
  printf '\n===== %s =====\nRESULT: SKIPPED -- %s\n' "$1" "$2"
  skipped=$((skipped+1))
}

echo "smoke-prod against $BASE  (key sent as X-Blip-Key; value never printed)"

# 1. public health
hit "/healthz" 200 "$BASE/healthz"

# 2. blips over Bend
hit "/v1/blips (Bend ${LAT},${LON} r=${R})" 200 \
  -H "X-Blip-Key: $KEY" "$BASE/v1/blips?lat=$LAT&lon=$LON&r=$R&limit=40"

# Pull a live hex out of that response: rows are
# [hex, cs, lat, lon, alt, gs, track, vrate, category, age].
# Distinguish the two very different reasons we might not have one -- conflating
# them once made a 401 read as "the sky is empty", which is exactly backwards.
HEX=""
if [ "$LAST_STATUS" != "200" ]; then
  skip "/v1/enrich" "/v1/blips returned $LAST_STATUS, so there is no hex to enrich. Fix that FIRST -- this check did not run and is not passing."
elif ! HEX="$(printf '%s' "$LAST_BODY" | grep -oE '\["[0-9a-f~]{6}"' | head -1 | tr -d '["')" || [ -z "$HEX" ]; then
  HEX=""
  skip "/v1/enrich" "/v1/blips returned 200 with zero aircraft -- a genuinely empty tile right now, not a proxy fault. Re-run when there is traffic over Bend."
else
  hit "/v1/enrich/$HEX (live hex from the blips response)" 200 \
    -H "X-Blip-Key: $KEY" "$BASE/v1/enrich/$HEX"
fi

# 3. config for every model slug the firmware can send (variant::SLUG values).
#    Note s3-128 / s3-175-amoled have no MODEL_DEFAULTS row -- they resolve to the
#    BASE config, which is correct, not a miss.
for slug in s3-146 s3-21 s3-128 c3-128 s3-175-amoled; do
  hit "/v1/config (model=$slug)" 200 \
    -H "X-Blip-Key: $KEY" -H "X-Blip-Model: $slug" "$BASE/v1/config"
done

# 4. a seeded type photo. The photo key is only discoverable through an enrich
#    (the device never guesses one), so fetch the pointer exactly the way a device
#    does: enrich a live hex, read `p`, then GET it.
P=""
if [ -n "${HEX:-}" ]; then
  P="$(curl -s --max-time 25 -H "X-Blip-Key: $KEY" "$BASE/v1/enrich/$HEX" | grep -oE '"p":"[^"]+"' | head -1 | cut -d'"' -f4)"
fi
if [ -n "$P" ]; then
  hit "/v1/photo (via enrich pointer $P)" 200 \
    -H "X-Blip-Key: $KEY" "$BASE$P" --output /dev/null --write-out '%{content_type} %{size_download} bytes'
elif [ -n "${HEX:-}" ]; then
  skip "/v1/photo" "live hex $HEX resolved no photo pointer (that type has no stock photo). Not a failure -- but /v1/photo went untested."
else
  skip "/v1/photo" "no live hex available, so the pointer could not be resolved. /v1/photo went UNTESTED."
fi

# Public, so it works regardless of auth -- proves the photo library rendered at
# ingest. Deliberately its OWN check rather than a stand-in for /v1/photo: passing
# this while /v1/photo was skipped must not read as "the photo path works".
hit "/credits (public; photo library rendered)" 200 "$BASE/credits"

printf '\n================ SUMMARY ================\n'
printf 'PASS: %d   FAIL: %d   SKIPPED: %d\n' "$pass" "$fail" "$skipped"
if [ "$skipped" -gt 0 ]; then
  printf 'NOTE: skipped checks did NOT run. Do not read this as a clean pass.\n'
fi
[ "$fail" -eq 0 ] || exit 1
