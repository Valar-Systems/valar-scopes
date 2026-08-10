#!/usr/bin/env bash
# smoke-prod.sh -- post-deploy smoke test for the Blipscope proxy.
#
# IF IT IS NOT IN THIS SCRIPT, NOBODY IS CHECKING IT IN PRODUCTION.
#
# The unit suite runs in-process against miniflare, and CI's --check gates prove
# fonts/ and pages/ agree with their generated modules. Neither one ever resolves
# scopes.valarsystems.com. This script is the ONLY thing that touches the real
# deployment, so a surface absent from here has no production coverage at all --
# not weak coverage, none.
#
# That gap has now been demonstrated twice, both found by hand rather than by a
# test, and both silent:
#
#   1. The self-hosted webfonts. test/leaderboard.test.ts asserts all three serve
#      with the right type and magic bytes -- in miniflare. Had the embed lost a
#      font in the deployed Worker, every visitor's browser would have quietly
#      fallen back to system type and the page would still have looked fine to
#      anyone who had not seen the real one.
#   2. The edition-namespaced move (#142). /leaderboard became
#      /blipscope/leaderboard with a 301 behind it. Nothing here requested either
#      path, so neither the new page nor the redirect keeping old bookmarks alive
#      was ever confirmed against production.
#
# So: when a public surface ships, add it here in the same PR. The checks below
# assert more than a status code on purpose -- a 200 that serves the WRONG thing
# is exactly the failure a status-only probe cannot see.
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
#
# CALLERS MUST NOT PASS -w / --write-out. This composes its own to append the
# status code, and curl lets the LAST one win -- so a caller's --write-out
# silently replaces it, `status` parses as whatever that emitted, and the check
# can then never pass. That is not hypothetical: it is what made the /v1/photo
# check below fail against a photo the Worker was serving correctly.
# For "is this asset served properly", use `asset` instead; it is built for it.
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

# asset <name> <expected-content-type-prefix> <url>
#
# Status + Content-Type + non-empty body, and deliberately NOT `hit`: a woff2
# body is binary and must never be dumped to a terminal. All three assertions
# earn their place, because a font has three separate silent failure modes --
# a 404 (page falls back to system type), a wrong Content-Type (some browsers
# refuse to apply the face), and a zero-length or truncated body (the embed
# dropped it). Any one of them looks perfectly normal to a visitor who has
# never seen the page rendered correctly.
#
# The type match is a PREFIX so "text/html" accepts "text/html; charset=utf-8".
asset() {
  local name="$1" want_type="$2" url="$3"; shift 3
  local out status ctype bytes ok_type
  # Trailing "$@" carries caller extras (an auth header, say). Safe here in a way
  # it is not for `hit`: the -w below is the LAST one on the line, so nothing a
  # caller passes can displace it.
  out="$(curl -s -o /dev/null --max-time 25 "$@" -w '%{http_code}|%{content_type}|%{size_download}' "$url")"
  status="${out%%|*}"; out="${out#*|}"
  ctype="${out%%|*}"; bytes="${out##*|}"
  case "$ctype" in "$want_type"*) ok_type=1 ;; *) ok_type=0 ;; esac
  printf '\n===== %s =====\n' "$name"
  printf 'expect HTTP 200 / %s* / >0 bytes\n' "$want_type"
  printf 'got    HTTP %s / %s / %s bytes\n' "$status" "$ctype" "$bytes"
  if [ "$status" = "200" ] && [ "$ok_type" = "1" ] && [ "${bytes:-0}" -gt 0 ] 2>/dev/null; then
    printf 'RESULT: PASS\n'; pass=$((pass+1))
  else
    printf 'RESULT: FAIL\n'; fail=$((fail+1))
  fi
}

# contains <name> <expected-status> <marker> <url>
#
# Status AND a marker string the body must contain. A page that 200s while
# serving the wrong content -- a stale embed, an error page with a cheerful
# status, another edition's surface -- is invisible to a status-only probe,
# which is the whole reason this helper exists. Pick markers that ONLY the
# correct response can have.
contains() {
  local name="$1" want="$2" marker="$3" url="$4"
  local body status ok_marker
  body="$(curl -s -w $'\n%{http_code}' --max-time 25 "$url")"
  status="${body##*$'\n'}"
  body="${body%$'\n'*}"
  case "$body" in *"$marker"*) ok_marker=1 ;; *) ok_marker=0 ;; esac
  printf '\n===== %s =====\n' "$name"
  printf 'expect HTTP %s and body containing: %s\n' "$want" "$marker"
  printf 'got    HTTP %s, marker %s\n' "$status" "$([ "$ok_marker" = 1 ] && echo present || echo MISSING)"
  if [ "$status" = "$want" ] && [ "$ok_marker" = "1" ]; then
    printf 'RESULT: PASS\n'; pass=$((pass+1))
  else
    # Only on failure, and capped: enough to see what came back instead without
    # burying the summary under a full page of HTML.
    printf -- '--- first 400 bytes of body ---\n%.400s\n' "$body"
    printf 'RESULT: FAIL\n'; fail=$((fail+1))
  fi
}

# redirects_to <name> <expected-location> <url>
#
# Asserts the 301 itself, NOT the destination -- curl is told not to follow.
# The redirect is the compatibility layer for bookmarks and printed links made
# before #142; if it quietly disappeared, every check that requests the CURRENT
# path would still pass while old links died. That is precisely the failure this
# script existed to catch and did not.
redirects_to() {
  local name="$1" want_loc="$2" url="$3"
  local out status loc
  out="$(curl -s -o /dev/null --max-time 25 -w '%{http_code}|%{redirect_url}' "$url")"
  status="${out%%|*}"; loc="${out#*|}"
  printf '\n===== %s =====\n' "$name"
  printf 'expect HTTP 301 -> %s\n' "$want_loc"
  printf 'got    HTTP %s -> %s\n' "$status" "${loc:-<none>}"
  if [ "$status" = "301" ] && [ "$loc" = "$want_loc" ]; then
    printf 'RESULT: PASS\n'; pass=$((pass+1))
  else
    printf 'RESULT: FAIL\n'; fail=$((fail+1))
  fi
}

echo "smoke-prod against $BASE  (key sent as X-Blip-Key; value never printed)"

# 1. public health
hit "/healthz" 200 "$BASE/healthz"

# The build stamp, which is the answer to "what commit is this actually running?".
# A DEPLOYED Worker reporting "dev" means someone ran `wrangler deploy` by hand
# instead of scripts/deploy.sh, bypassing the dirty-tree and unpushed-commit
# guards -- so the running code may correspond to no commit at all. That is the
# one thing about a deployment that cannot be reconstructed after the fact, which
# is why it is checked here rather than trusted.
COMMIT="$(printf '%s' "${LAST_BODY:-}" | grep -oE '"commit":"[^"]*"' | cut -d'"' -f4)"
printf '\n===== /healthz build stamp =====\n'
printf 'commit reported: %s\n' "${COMMIT:-<absent>}"
if [ -n "$COMMIT" ] && [ "$COMMIT" != "dev" ]; then
  case "$COMMIT" in
    *-dirty)
      printf 'RESULT: FAIL -- deployed from a DIRTY tree (ALLOW_DIRTY=1). This bundle\n'
      printf '        contains changes that are in no commit and no PR.\n'
      fail=$((fail+1)) ;;
    *)
      printf 'RESULT: PASS\n'; pass=$((pass+1)) ;;
  esac
else
  printf 'RESULT: FAIL -- expected a commit SHA from scripts/deploy.sh.\n'
  printf '        "dev" or absent means a hand-run `wrangler deploy` bypassed the guards.\n'
  fail=$((fail+1))
fi

# 2. blips over Bend
hit "/v1/blips (Bend ${LAT},${LON} r=${R})" 200 \
  -H "X-Blip-Key: $KEY" "$BASE/v1/blips?lat=$LAT&lon=$LON&r=$R&limit=40"

# Pull a live hex out of that response: rows are
# [hex, cs, lat, lon, alt, gs, track, vrate, category, age].
# Distinguish the two very different reasons we might not have one -- conflating
# them once made a 401 read as "the sky is empty", which is exactly backwards.
HEX=""
# Capture the WHOLE candidate list here, while $LAST_BODY is still the blips
# response. Section 4b needs several hexes, not one, and by the time it runs
# $LAST_BODY has been overwritten several times over -- most recently by a JPEG.
# Re-parsing it down there silently yielded zero candidates and reported the gate
# as "no live hex available at all" on a sky holding fourteen aircraft.
HEX_CANDIDATES=""
if [ "$LAST_STATUS" = "200" ]; then
  HEX_CANDIDATES="$(printf '%s' "$LAST_BODY" | grep -oE '\["[0-9a-f~]{6}"' | tr -d '["' | head -12)"
fi
if [ "$LAST_STATUS" != "200" ]; then
  skip "/v1/enrich" "/v1/blips returned $LAST_STATUS, so there is no hex to enrich. Fix that FIRST -- this check did not run and is not passing."
elif ! HEX="$(printf '%s' "$LAST_BODY" | grep -oE '\["[0-9a-f~]{6}"' | head -1 | tr -d '["')" || [ -z "$HEX" ]; then
  HEX=""
  skip "/v1/enrich" "/v1/blips returned 200 with zero aircraft -- a genuinely empty tile right now, not a proxy fault. Re-run when there is traffic over Bend."
else
  hit "/v1/enrich/$HEX (live hex from the blips response)" 200 \
    -H "X-Blip-Key: $KEY" "$BASE/v1/enrich/$HEX"
fi

# 2b. THE UPSTREAM CHAIN: is the served picture fresh, and are both relays serving?
#
# WHY THIS EXISTS. The freshness failover -- the chain declining to stop on a relay
# that answers HTTP 200 carrying a stale picture -- is covered by three tests in
# test/blips.test.ts and had never fired in production as of 2026-08-08. Those tests
# run in miniflare against a mock. Nothing here ever touched the mechanism against the
# real deployment, and it is a mechanism that ONLY matters when something is already
# wrong, which is the worst kind of thing to find broken at the moment you need it.
#
# WHAT THIS ACTUALLY ASSERTS, deliberately narrower than "failover works":
#   1. Every probed tile comes back with a picture inside the degraded threshold. The
#      original bug was relay-a returning a stale 200 and the chain stopping there; if
#      that regresses, the age crosses the line here and this fails.
#   2. More than one relay id appears across the probes. Tile partitioning puts BOTH
#      relays in the normal serving path, so a dead or misconfigured relay-b stops
#      being invisible -- under the failover-only chain relay-b served 0.4 % of Worker
#      traffic and could have been down for weeks with no symptom at all.
#
# WHAT IT DOES NOT ASSERT: that a stale relay actually triggers a failover. Forcing
# that in production means deliberately degrading a live relay, which is not worth it.
# This is the strongest honest check that breaks nothing -- do not let it be read as
# proof the failover fires.
#
# The age ceiling is 90 s: comfortably above the 45 s degraded threshold (so a healthy
# fetch never trips it) and far below the 600 s SWR ceiling. A tile served as X-Cache
# STALE can legitimately be older than 90 s, so X-Cache is printed on every line --
# check it before treating a single breach as an upstream fault.
MAX_AGE_S="${MAX_AGE_S:-90}"
# Spread over real metros, which is both where aircraft are and where the pilot boards
# will be. Several tiles, because ONE tile only ever exercises ONE relay under
# partitioning -- a single probe could pass with the other relay dead.
CHAIN_TILES="47.40,8.55 51.47,-0.45 40.64,-73.78 33.94,-118.41 35.55,139.78 -33.94,151.18"
CHAIN_UPSTREAMS=""
chain_fail=0
chain_n=0
printf '\n===== upstream chain (freshness + both relays serving) =====\n'
printf 'expect: every tile fresh within %ss, and >1 distinct relay across the probes\n' "$MAX_AGE_S"
for t in $CHAIN_TILES; do
  tlat="${t%%,*}"; tlon="${t##*,}"
  hdrs="$(mktemp)"; body="$(curl -s -D "$hdrs" --max-time 25 \
    -H "X-Blip-Key: $KEY" "$BASE/v1/blips?lat=$tlat&lon=$tlon&r=40&limit=1")"
  st="$(awk 'NR==1{print $2}' "$hdrs")"
  up="$(tr -d '\r' < "$hdrs" | awk -F': ' 'tolower($1)=="x-upstream"{print $2}')"
  ch="$(tr -d '\r' < "$hdrs" | awk -F': ' 'tolower($1)=="x-cache"{print $2}')"
  rm -f "$hdrs"
  # Picture timestamp: {"v":1,"t":<epoch seconds>,...}. Its absence is itself a fault.
  pt="$(printf '%s' "$body" | grep -oE '"t":[0-9]+' | head -1 | cut -d: -f2)"
  chain_n=$((chain_n+1))
  if [ "$st" != "200" ] || [ -z "$pt" ]; then
    printf '  %-16s HTTP %-4s upstream=%-12s cache=%-6s age=?     FAIL\n' "$tlat,$tlon" "$st" "${up:-none}" "${ch:-none}"
    chain_fail=$((chain_fail+1)); continue
  fi
  age=$(( $(date -u +%s) - pt ))
  if [ "$age" -gt "$MAX_AGE_S" ] || [ "$age" -lt 0 ]; then
    printf '  %-16s HTTP %-4s upstream=%-12s cache=%-6s age=%-4ss FAIL (stale)\n' "$tlat,$tlon" "$st" "${up:-none}" "${ch:-none}" "$age"
    chain_fail=$((chain_fail+1))
  else
    printf '  %-16s HTTP %-4s upstream=%-12s cache=%-6s age=%-4ss ok\n' "$tlat,$tlon" "$st" "${up:-none}" "${ch:-none}" "$age"
  fi
  case " $CHAIN_UPSTREAMS " in *" ${up:-none} "*) ;; *) CHAIN_UPSTREAMS="$CHAIN_UPSTREAMS ${up:-none}" ;; esac
done
# shellcheck disable=SC2086
CHAIN_DISTINCT="$(printf '%s\n' $CHAIN_UPSTREAMS | grep -c .)"
printf 'relays seen across %d tiles:%s (%s distinct)\n' "$chain_n" "$CHAIN_UPSTREAMS" "$CHAIN_DISTINCT"
if [ "$chain_fail" -gt 0 ]; then
  printf 'RESULT: FAIL -- %d of %d tiles were stale or errored.\n' "$chain_fail" "$chain_n"
  fail=$((fail+1))
elif [ "${CHAIN_DISTINCT:-0}" -lt 2 ]; then
  printf 'RESULT: FAIL -- only one relay answered. Either its partner is down (check the\n'
  printf '        other relay directly) or partitioning is off (check UPSTREAM_FEED_ORDER).\n'
  printf '        Serving is FINE right now -- this is the failover leg being unproven.\n'
  fail=$((fail+1))
else
  printf 'RESULT: PASS\n'
  pass=$((pass+1))
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
  # `asset`, not `hit`: the body is a JPEG and must not reach a terminal, and the
  # old call passed --write-out to hit() to suppress it -- which overrode hit's own
  # -w and left `status` holding "image/jpeg 1249 bytes". It could never equal 200,
  # so this check reported FAIL for a photo the Worker was serving perfectly. It
  # had never actually run before 2026-08-08 (no live hex, so it always SKIPPED),
  # which is how a check that cannot pass survived this long.
  asset "/v1/photo (via enrich pointer $P)" "image/" "$BASE$P" -H "X-Blip-Key: $KEY"
elif [ -n "${HEX:-}" ]; then
  skip "/v1/photo" "live hex $HEX resolved no photo pointer (that type has no stock photo). Not a failure -- but /v1/photo went untested."
else
  skip "/v1/photo" "no live hex available, so the pointer could not be resolved. /v1/photo went UNTESTED."
fi

# 4b. THE FULL-BLEED PHOTO GATE (#209). The proxy hands a device a photo PATH and
#     the device fetches whatever it is given, so the choice of artifact is made
#     here and nowhere else. Firmware up to FW 6 draws into a fixed 150x100 slot
#     with a drawJpg call that CLIPS rather than scales: hand one of those a 240
#     square and it renders the top-left corner of the image, on every card, with
#     a 200 on the wire and nothing in any log.
#
#     Status is therefore useless as a check here -- both variants 200 -- and so
#     is the blob key, which is opaque. This asserts the DIMENSIONS OF THE PIXELS
#     ACTUALLY SERVED, because the only difference that matters is invisible in
#     every other signal. Both directions are pinned: new firmware must get 240,
#     and old firmware must STILL get 150x100, which is the half that protects
#     every device already in the field.
#
#     Observed on the bench 2026-08-10, and the reason this check exists: square
#     blobs were being written under keys the serving validator rejected, so the
#     proxy silently fell back to rectangles for everyone. Nothing failed. It just
#     quietly did not work.

# jpeg_dims <file> -- "WxH" from the first SOF marker, or "unknown". Pure bash +
# od so the script keeps its only dependency on curl. Scans a bounded prefix:
# the SOF sits within the first few hundred bytes of a baseline JPEG, and walking
# 12 KB of hex a byte at a time in bash is slow enough to notice.
jpeg_dims() {
  local hex i m len
  hex="$(od -An -v -tx1 "$1" 2>/dev/null | tr -d ' \n')"
  len=${#hex}
  [ "$len" -gt 4000 ] && len=4000
  for (( i=0; i<len-18; i+=2 )); do
    if [ "${hex:$i:2}" = "ff" ]; then
      m="${hex:$((i+2)):2}"
      case "$m" in
        c0|c1|c2)
          printf '%dx%d' $((16#${hex:$((i+14)):4})) $((16#${hex:$((i+10)):4}))
          return ;;
      esac
    fi
  done
  printf 'unknown'
}

# variant_photo <hex> <fw> <model> -- the photo path the proxy hands that device,
# or "". The hex is a PARAMETER and not the ambient $HEX: the caller below probes
# several candidates, and `VAR=x somefunc` leaks or restores VAR depending on
# whether bash is in POSIX mode, so relying on it would make this gate's target
# depend on how the script was invoked.
variant_photo() {
  curl -s --max-time 25 -H "X-Blip-Key: $KEY" \
    -H "X-Blip-FW: $2" -H "X-Blip-Model: $3" \
    "$BASE/v1/enrich/$1" | grep -oE '"p":"[^"]+"' | head -1 | cut -d'"' -f4
}

# WHICH hex this gate runs against is not a detail. It used to take the single
# first row of the blips response, so whether the most important check in this
# file executed at all was decided by which aircraft happened to be closest to
# Bend -- and a typeless airframe, an SH36 or a C152 all resolve no photo. A
# survey of 14 live hexes on 2026-08-10 found 3 of them in that state, so the
# gate was skipping roughly a fifth of runs for a reason that had nothing to do
# with the thing it tests.
#
# So it SEARCHES the response instead of accepting its first row: walk the live
# hexes until one resolves a photo. Still real traffic through the real path --
# no synthetic fixture that could drift away from what devices actually ask for
# -- but coverage no longer depends on the order aircraft happen to be in.
GATE_HEX=""; P_OLD=""; P_NEW=""; TRIED=0
for CAND in $HEX_CANDIDATES; do
  TRIED=$((TRIED+1))
  CAND_P="$(variant_photo "$CAND" 6 s3-128)"
  if [ -n "$CAND_P" ]; then
    GATE_HEX="$CAND"
    P_OLD="$CAND_P"
    P_NEW="$(variant_photo "$CAND" 7 s3-128)"
    break
  fi
done

printf '\n===== /v1/enrich photo variant gate (FW 6 vs FW 7) =====\n'
if [ -z "$GATE_HEX" ]; then
  if [ "$TRIED" = "0" ]; then
    skip "photo variant gate" "no live hex available at all. The FW-gated photo variant went UNTESTED in production."
  else
    # Distinct from "no traffic", and worth its own wording: aircraft WERE overhead
    # and not one of them resolved a photo. With 213 types in the library that is
    # improbable enough to be worth looking at, but it is not provably a fault --
    # a quiet field can legitimately hold nothing but unphotographed types -- so it
    # reports as skipped with the count, not as a pass and not as a failure.
    skip "photo variant gate" "tried all $TRIED live hexes and none resolved a photo. Not provably a fault, but with 213 types ingested it is worth checking the library rather than assuming a quiet sky."
  fi
else
  printf 'gate hex: %s (row %d of the live response -- the first with a photo)\n' "$GATE_HEX" "$TRIED"
  printf 'FW 6 -> %s\n' "${P_OLD:-<none>}"
  printf 'FW 7 -> %s\n' "${P_NEW:-<none>}"

  TMP_OLD="$(mktemp)"; TMP_NEW="$(mktemp)"
  curl -s --max-time 25 -H "X-Blip-Key: $KEY" -o "$TMP_OLD" "$BASE$P_OLD"
  D_OLD="$(jpeg_dims "$TMP_OLD")"
  printf 'FW 6 pixels: %s (must be 150x100 -- this is what the field runs)\n' "$D_OLD"

  if [ "$D_OLD" = "150x100" ]; then
    printf 'RESULT: PASS\n'; pass=$((pass+1))
  else
    printf 'RESULT: FAIL -- fielded firmware would clip this to its top-left corner\n'; fail=$((fail+1))
  fi

  # A missing square is a legitimate state mid-ingest: the proxy serves NO photo
  # rather than a rectangle it cannot place, and the card shows its silhouette.
  # Report it as SKIPPED, never as PASS -- "the gate did nothing" and "the gate
  # worked" must not print the same word.
  if [ -z "$P_NEW" ]; then
    skip "photo variant gate (FW 7 square)" "no square ingested for this type yet; proxy correctly served no photo rather than a rectangle. The FW 7 half went UNTESTED."
  else
    curl -s --max-time 25 -H "X-Blip-Key: $KEY" -o "$TMP_NEW" "$BASE$P_NEW"
    D_NEW="$(jpeg_dims "$TMP_NEW")"
    printf 'FW 7 pixels: %s (must be square, and NOT the same blob as FW 6)\n' "$D_NEW"
    case "$D_NEW" in
      240x240|412x412|480x480) sq=1 ;;
      *) sq=0 ;;
    esac
    if [ "$sq" = "1" ] && [ "$P_NEW" != "$P_OLD" ]; then
      printf 'RESULT: PASS\n'; pass=$((pass+1))
    else
      printf 'RESULT: FAIL -- full-bleed firmware is not being served a square\n'; fail=$((fail+1))
    fi
  fi
  rm -f "$TMP_OLD" "$TMP_NEW"
fi

# 5. THE PUBLIC WEB SURFACE. Everything below is unauthenticated -- it needs no
#    key and is what a customer's browser actually loads. None of it was checked
#    here before; see the two silent failures named in the header.

# Public, so it works regardless of auth -- proves the photo library rendered at
# ingest. Deliberately its OWN check rather than a stand-in for /v1/photo: passing
# this while /v1/photo was skipped must not read as "the photo path works".
#
# Also a LICENCE OBLIGATION, not a nicety: the stock photos are CC-BY, and the
# attribution they require is this page. If it stops serving, the photos on every
# device in the field are being used out of compliance.
contains "/credits (public; photo library + CC-BY attribution)" 200 "Blipscope credits" "$BASE/credits"

# The self-hosted webfonts. Requested BY PATH rather than scraped out of the page
# so this still fails loudly if the page stops asking for them (which would mean
# the page changed, not that the fonts are fine).
for font in inter mono grotesk; do
  asset "/fonts/$font.woff2 (self-hosted webfont)" "font/woff2" "$BASE/fonts/$font.woff2"
done

# The leaderboard page and its JSON, at the CURRENT edition-namespaced paths.
# Markers: the page's <title>, and the scoring version in the JSON -- a board
# served with the wrong scoring model is a correctness bug the status hides.
contains "/blipscope/leaderboard (page)" 200 "Blipscope Spotting Leaderboard" \
  "$BASE/blipscope/leaderboard"
contains "/blipscope/leaderboard.json (data)" 200 '"scoring":"claims-v2"' \
  "$BASE/blipscope/leaderboard.json"

# The deprecated path must keep 301ing to the namespaced one. Retire this check
# only when the redirect itself is retired -- see "When the aliases can be
# deleted" in docs/web-url-convention.md.
redirects_to "/leaderboard -> /blipscope/leaderboard (301 kept alive)" \
  "$BASE/blipscope/leaderboard" "$BASE/leaderboard"

# The edition hub and the Blipscope support page. The root answered 404 with a
# JSON error object until these shipped, so anyone who trimmed a path -- or
# followed the store's redirect -- landed on a machine error.
contains "/ (edition hub)" 200 "Valar Scopes" "$BASE/"
contains "/blipscope/support (support page)" 200 "Blipscope Support" "$BASE/blipscope/support"

# The support page is where the STORE sends buyers, so it is the one page whose
# absence a customer notices before we do. Checked as its own line rather than
# folded into the check above: the destination of an external redirect we do not
# control deserves to fail by name.
contains "/blipscope/support (Shopify redirect destination)" 200 "Still stuck" \
  "$BASE/blipscope/support"

# NO /missileer/support CHECK, and deliberately not a skip() either. Missileer is
# still in development and does not need a support page yet, so there is nothing
# here that went unverified -- counting it as SKIPPED would inflate the skip tally
# and dilute the summary's "skipped checks did NOT run" warning, which has to keep
# meaning "something we should have checked, and didn't".
#
# When that page ships it lands in valar-eam-feed (this Worker proxies the whole
# /missileer/* prefix to that origin), and the check belongs here in the same
# change as the hub link -- which test/pages.test.ts asserts is currently absent,
# so the page and the link cannot drift apart.

printf '\n================ SUMMARY ================\n'
printf 'PASS: %d   FAIL: %d   SKIPPED: %d\n' "$pass" "$fail" "$skipped"
if [ "$skipped" -gt 0 ]; then
  printf 'NOTE: skipped checks did NOT run. Do not read this as a clean pass.\n'
fi
[ "$fail" -eq 0 ] || exit 1
