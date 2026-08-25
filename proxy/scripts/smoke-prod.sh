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
#   export BLIP_DEVICE='<bench device id>' # REQUIRED -- the only auth path
#   ./scripts/smoke-prod.sh               # defaults to production
#   BASE=https://scopes-staging.valarsystems.com ./scripts/smoke-prod.sh
#
# BLIP_DEVICE is MANDATORY since the shared BLIP_KEYS list was removed
# (2026-08-13): a bare X-Blip-Key is no longer a credential. See the AUTH block
# below. The run announces which credential it expects and asserts it before any
# check that depends on it.
#
# NOTE ON ENVIRONMENTS: staging has its OWN DEVICE_KEY_SECRET, so a
# production key answers 401 there. That presents as seven unrelated failures
# and is a credential mismatch, not a staging defect.
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

# ---- WHICH CREDENTIAL THIS SCRIPT AUTHENTICATES WITH ------------------------
#
# The shared BLIP_KEYS list was removed 2026-08-13, so a bare X-Blip-Key is no
# longer a credential at all: the only accepted form is a per-device key PLUS the
# X-Blip-Device id it was minted for. This script is the only thing checking
# production, so it was made to survive that change rather than discover it.
#
# BLIP_DEVICE is therefore REQUIRED. Set it and
# every authed call below carries the id.
#
# ONE ARRAY, used at every authed call site, so a header cannot be added to some
# requests and forgotten on others -- which is how you get a script that proves
# the device path works on the two endpoints someone remembered.
DEVICE="${BLIP_DEVICE:-}"
if [ -z "$DEVICE" ]; then
  # Refuse rather than run. Without the id every authed check below 401s, and a
  # wall of failures reads as "production is down" when it means "you forgot an
  # environment variable" -- the most expensive way to learn that.
  echo "BLIP_DEVICE is not set, and since BLIP_KEYS was removed a bare key is not" >&2
  echo "a credential. export BLIP_DEVICE='<your bench device id>' and re-run." >&2
  exit 2
fi
AUTH=(-H "X-Blip-Key: $KEY" -H "X-Blip-Device: $DEVICE")

# What the auth path is EXPECTED to be, asserted below rather than assumed.
# Only "device" is reachable now; the variable survives so the assertion stays
# an assertion rather than becoming a hardcoded string nobody re-reads, and so
# the next credential migration has somewhere to express itself.
EXPECT_AUTH="${BLIP_EXPECT_AUTH:-device}"

pass=0
warn=0
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
echo "auth: expecting X-Blip-Auth: $EXPECT_AUTH${DEVICE:+  (device id supplied)}"

# 1. public health
hit "/healthz" 200 "$BASE/healthz"

# WHICH CREDENTIAL THIS SCRIPT IS RUNNING ON, asserted before anything that
# depends on it.
#
# The corollary in CLAUDE.md: when a check protects a property, confirm the
# check's own environment has that property. With BLIP_KEYS gone, every
# check below is meant to be exercising the per-device path -- and if this script
# were somehow still authenticating on a shared credential, all of them would
# pass while proving nothing about the only auth path that now exists. That is
# the failure mode this script exists to not have.
#
# Run FIRST because everything downstream is downstream of it: a wrong answer
# here makes the rest of the run uninterpretable rather than merely wrong.
auth_path_check() {
  local got
  got="$(curl -s -o /dev/null -D- --max-time 25 "${AUTH[@]}" "$BASE/v1/config" \
         | tr -d '\r' | grep -i '^x-blip-auth:' | cut -d' ' -f2)"
  printf '\n===== auth path (X-Blip-Auth) =====\n'
  printf 'expect %s, got %s\n' "$EXPECT_AUTH" "${got:-<no header>}"
  if [ "$got" = "$EXPECT_AUTH" ]; then
    printf 'RESULT: PASS\n'; pass=$((pass+1))
  else
    if [ -z "$got" ]; then
      printf 'NOTE: no header at all means auth never ran -- a 401, or a Worker\n'
      printf '      predating the X-Blip-Auth change.\n'
    fi
    printf 'RESULT: FAIL\n'; fail=$((fail+1))
  fi
}
auth_path_check

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

# WHICH UPSTREAMS PRODUCTION ACTUALLY HAS ON, read off the live Worker.
#
# This exists because the unit test that looks like it covers this cannot. It
# builds an Env by hand, so it asserts the code DEFAULTS -- and its old name,
# "ships with adsb.lol as the only enabled position source", stayed green while
# becoming false, because production enables adsb.fi through wrangler.toml and no
# hand-built env can see that. Input from the test's own side of the contract.
#
# /healthz reports feedHealth(env) from the deployed bindings, so this is the
# other side: the running Worker's own answer, not a restatement of intent.
#
# The assertion is deliberately two-sided. "adsb_fi present" alone would also
# pass if every feed were listed as enabled; "airplanes_live absent" alone would
# pass on a Worker with no upstreams at all. Together they pin the actual posture:
# the primary is on, and the operator-PROHIBITED source is not.
#
# Its OWN fetch rather than a reuse of $LAST_BODY. Reusing it is what made the
# hex-candidate gate report "no live hex available" on a sky holding fourteen
# aircraft (see section 4b), and this check would fail the same silent way: an
# overwritten body yields no match, which is indistinguishable here from a
# genuinely wrong upstream posture. One extra request buys an unambiguous read.
printf '\n===== /healthz upstream posture =====\n'
HEALTH_BODY="$(curl -s --max-time 15 "$BASE/healthz")"
UPSTREAMS="$(printf '%s' "$HEALTH_BODY" | grep -oE '"upstreams":\[.*\]')"
FI_ON="$(printf '%s' "$UPSTREAMS" | grep -oE '"id":"adsb_fi","enabled":(true|false)' | head -1)"
AL_ON="$(printf '%s' "$UPSTREAMS" | grep -oE '"id":"airplanes_live","enabled":(true|false)' | head -1)"
printf 'adsb_fi        : %s   (expect enabled -- the chain primary)\n' "${FI_ON:-<absent>}"
printf 'airplanes_live : %s   (expect NOT enabled -- prohibited by operator)\n' "${AL_ON:-<absent>}"
if [ "$FI_ON" = '"id":"adsb_fi","enabled":true' ] \
   && [ "$AL_ON" = '"id":"airplanes_live","enabled":false' ]; then
  printf 'RESULT: PASS\n'; pass=$((pass+1))
else
  printf 'RESULT: FAIL -- production is not running the upstream posture we think it is.\n'
  printf '        adsb.fi off means positions fall back to adsb.lol, which 429s us hard.\n'
  printf '        airplanes_live on would be a written-refusal breach. Check wrangler.toml.\n'
  fail=$((fail+1))
fi

# 2. blips over Bend
hit "/v1/blips (Bend ${LAT},${LON} r=${R})" 200 \
  "${AUTH[@]}" "$BASE/v1/blips?lat=$LAT&lon=$LON&r=$R&limit=40"

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
    "${AUTH[@]}" "$BASE/v1/enrich/$HEX"
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
chain_ac_total=0
chain_ac_tiles=0
chain_ok=0
printf '\n===== upstream chain (freshness + both relays serving) =====\n'
printf 'expect: every tile fresh within %ss, and >1 distinct relay across the probes\n' "$MAX_AGE_S"
for t in $CHAIN_TILES; do
  tlat="${t%%,*}"; tlon="${t##*,}"
  hdrs="$(mktemp)"; body="$(curl -s -D "$hdrs" --max-time 25 \
    "${AUTH[@]}" "$BASE/v1/blips?lat=$tlat&lon=$tlon&r=40&limit=50")"
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
  # COUNT ONLY WHAT WAS VALIDATED, and only past this point. Above it the
  # response is not yet known to be a picture at all.
  #
  # This ordering is not cosmetic. While writing this check I ran the probe by
  # hand, got zero aircraft on all six tiles, and nearly reported a fleet-wide
  # outage -- it was a 401, because I had sent X-Blip-Key without X-Blip-Device.
  # A refusal read as an empty sky is precisely the bug class this section
  # exists to catch, and counting before validating would have built it in.
  ac="$(printf '%s' "$body" | grep -oE '"a":\[.*' | grep -oE '\[\[|\],\[' | grep -c . || true)"
  chain_ac_total=$((chain_ac_total + ${ac:-0}))
  [ "${ac:-0}" -gt 0 ] && chain_ac_tiles=$((chain_ac_tiles+1))
  chain_ok=$((chain_ok+1))
  age=$(( $(date -u +%s) - pt ))
  # A SMALL NEGATIVE AGE IS CLOCK SKEW, NOT A DEFECT. `age` subtracts the
  # upstream's own timestamp from THIS workstation's clock, and those are two
  # independent NTP-disciplined machines -- so a tile fetched the instant it was
  # produced can legitimately land a second or two in the "future".
  #
  # Observed 2026-08-20 on the first run after six days dark: six fresh MISS
  # tiles came back 0,0,1,1,1,-1. The -1 failed, and the run reported the only
  # production check we have as broken over a one-second difference between
  # adsb.fi's clock and this laptop's.
  #
  # That is the muted-alarm failure mode: a check that fires for a reason
  # unrelated to what it watches gets ignored, and then it watches nothing. The
  # tolerance is deliberately SMALL -- a large negative still fails, because a
  # timestamp far in the future is a real signal (a bogus upstream `now`, or a
  # feed replaying old data with a forward-stamped clock).
  SKEW_TOLERANCE_S=5
  if [ "$age" -gt "$MAX_AGE_S" ]; then
    printf '  %-16s HTTP %-4s upstream=%-12s cache=%-6s age=%-4ss FAIL (stale)\n' "$tlat,$tlon" "$st" "${up:-none}" "${ch:-none}" "$age"
    chain_fail=$((chain_fail+1))
  elif [ "$age" -lt "-$SKEW_TOLERANCE_S" ]; then
    # Named separately because "stale" is the WRONG WORD for a future timestamp,
    # and reading it cost real time: it sends you looking for an old tile when
    # the condition is the opposite one.
    printf '  %-16s HTTP %-4s upstream=%-12s cache=%-6s age=%-4ss FAIL (timestamp in the future, beyond %ss skew tolerance)\n' "$tlat,$tlon" "$st" "${up:-none}" "${ch:-none}" "$age" "$SKEW_TOLERANCE_S"
    chain_fail=$((chain_fail+1))
  else
    printf '  %-16s HTTP %-4s upstream=%-12s cache=%-6s age=%-4ss ok\n' "$tlat,$tlon" "$st" "${up:-none}" "${ch:-none}" "$age"
  fi
  case " $CHAIN_UPSTREAMS " in *" ${up:-none} "*) ;; *) CHAIN_UPSTREAMS="$CHAIN_UPSTREAMS ${up:-none}" ;; esac
done
# shellcheck disable=SC2086
CHAIN_DISTINCT="$(printf '%s\n' $CHAIN_UPSTREAMS | grep -c .)"
# ---------------------------------------------------------------------------
# THE SKY MUST NOT BE EMPTY.
#
# Everything above passes on a feed returning ZERO AIRCRAFT: the tiles were
# fetched with limit=1 and only the picture TIMESTAMP was read, so a throttled
# or broken upstream answering 200 with an empty array looked identical to a
# working one. That is the failure that hid adsb.lol routeset for seven weeks,
# and the one CLAUDE.md already records for anonymous adsb.lol calls, where a
# throttled 200 with ac:[] reads as an empty sky.
#
# ASSERTED ON THE TOTAL, NOT PER TILE, deliberately. The six tiles span every
# time zone on purpose, so at any UTC hour some are legitimately quiet.
# Measured 2026-08-25 18:10 UTC:
#
#   Heathrow 34   JFK 50   LAX 50   Zurich 12   Tokyo 1   Sydney 2
#
# Tokyo and Sydney were at 03:10 and 04:10 local. A per-tile floor fails on both,
# and an alarm that fires for a reason unrelated to what it watches gets ignored
# -- and then it watches nothing.
#
# The floor is 10 against a measured 149, and is NOT tuned to that margin: the
# failure being caught serves NOTHING, which reads 0. Anything between 1 and 9
# across six major metros already deserves a look.
#
# The second condition stops one busy tile masking five dead ones.
#
# Force the failing branch to check this gate still works:  CHAIN_AC_FLOOR=99999
# ---------------------------------------------------------------------------
CHAIN_AC_FLOOR=${CHAIN_AC_FLOOR:-10}
printf 'aircraft seen: %d across %d tiles, %d tiles non-empty (floor %d, need >=2 non-empty)\n' \
  "$chain_ac_total" "$chain_n" "$chain_ac_tiles" "$CHAIN_AC_FLOOR"
if [ "$chain_ok" -lt 2 ]; then
  printf 'RESULT: FAIL -- CANNOT JUDGE THE SKY: only %d of %d tiles returned a\n' \
    "$chain_ok" "$chain_n"
  printf '        well-formed picture. That is a refusal or a transport fault, NOT an\n'
  printf '        empty sky -- read the per-tile HTTP codes above. A probe reporting\n'
  printf '        absence must first prove it can observe presence.\n'
  fail=$((fail+1))
elif [ "$chain_ac_total" -lt "$CHAIN_AC_FLOOR" ] || [ "$chain_ac_tiles" -lt 2 ]; then
  printf 'RESULT: FAIL -- the feed is serving an EMPTY SKY (%d aircraft across %d tiles).\n' \
    "$chain_ac_total" "$chain_n"
  printf '        The timestamps above may look perfectly fresh. That is the point:\n'
  printf '        a throttled upstream answers 200, an empty array, and a current clock.\n'
  fail=$((fail+1))
fi

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
    "${AUTH[@]}" -H "X-Blip-Model: $slug" "$BASE/v1/config"
done

# 4. a seeded type photo. The photo key is only discoverable through an enrich
#    (the device never guesses one), so fetch the pointer exactly the way a device
#    does: enrich a live hex, read `p`, then GET it.
P=""
if [ -n "${HEX:-}" ]; then
  P="$(curl -s --max-time 25 "${AUTH[@]}" "$BASE/v1/enrich/$HEX" | grep -oE '"p":"[^"]+"' | head -1 | cut -d'"' -f4)"
fi
if [ -n "$P" ]; then
  # `asset`, not `hit`: the body is a JPEG and must not reach a terminal, and the
  # old call passed --write-out to hit() to suppress it -- which overrode hit's own
  # -w and left `status` holding "image/jpeg 1249 bytes". It could never equal 200,
  # so this check reported FAIL for a photo the Worker was serving perfectly. It
  # had never actually run before 2026-08-08 (no live hex, so it always SKIPPED),
  # which is how a check that cannot pass survived this long.
  asset "/v1/photo (via enrich pointer $P)" "image/" "$BASE$P" "${AUTH[@]}"
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
  curl -s --max-time 25 "${AUTH[@]}" \
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
  curl -s --max-time 25 "${AUTH[@]}" -o "$TMP_OLD" "$BASE$P_OLD"
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
    curl -s --max-time 25 "${AUTH[@]}" -o "$TMP_NEW" "$BASE$P_NEW"
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

# ---- device enrollment (docs/device-enrollment.md) --------------------------
#
# WHY THE URLS ARE EXTRACTED FROM THE FIRMWARE AND NOT TYPED HERE. Enrollment
# first shipped with the device popup opening scopes.valarsystems.com/enroll
# while the Worker routed only /blipscope/enroll -- a 404 sitting behind sixteen
# passing tests, because every test requested the path the tests had picked.
# Transcribing the URL into this file would reproduce that mistake exactly one
# layer further out. So this reads the strings out of the firmware source and
# fetches each one: the artifact, not the intent.
#
# Redirects are FOLLOWED here, unlike the leaderboard check above, because the
# question is "does what the device tells a customer to type reach the page" --
# a question about the whole hop chain. The 301 itself is pinned in
# proxy/test/enroll.test.ts.
CFG_SRC="$(git rev-parse --show-toplevel 2>/dev/null)/src/ConfigurationWebServer.cpp"
if [ -r "$CFG_SRC" ]; then
  ENROLL_PATHS="$(grep -oE 'scopes\.valarsystems\.com/[A-Za-z0-9/_.-]*' "$CFG_SRC" \
                  | sed 's|^scopes\.valarsystems\.com||' | sort -u)"
  if [ -z "$ENROLL_PATHS" ]; then
    printf '\n===== enrol URLs found in firmware =====\n'
    printf 'expect at least one scopes.valarsystems.com/... in %s\n' "${CFG_SRC##*/}"
    printf 'got    none -- either the config page stopped offering enrollment,\n'
    printf '       or this pattern stopped matching it. Both are worth knowing.\n'
    printf 'RESULT: FAIL\n'; fail=$((fail+1))
  fi
  for p in $ENROLL_PATHS; do
    body="$(curl -sL -w $'\n%{http_code}' --max-time 25 "$BASE$p?id=a1b2c3d4e5f6a7b8")"
    status="${body##*$'\n'}"; body="${body%$'\n'*}"
    printf '\n===== firmware enrol URL %s (followed) =====\n' "$p"
    printf 'expect HTTP 200 and the enrol page\n'
    case "$body" in
      *"Verify your device"*) marker=1 ;;
      *) marker=0 ;;
    esac
    printf 'got    HTTP %s, page marker %s\n' "$status" \
      "$([ "$marker" = 1 ] && echo present || echo MISSING)"
    if [ "$status" = "200" ] && [ "$marker" = "1" ]; then
      printf 'RESULT: PASS\n'; pass=$((pass+1))
    else
      printf -- '--- first 400 bytes ---\n%.400s\n' "$body"
      printf 'RESULT: FAIL\n'; fail=$((fail+1))
    fi
  done
else
  printf '\n===== firmware enrol URLs =====\nRESULT: SKIP (no checkout: %s)\n' "$CFG_SRC"
  skipped=$((skipped+1))
fi

# THE SITEKEY REACHED THE MARKUP. An unset TURNSTILE_SITEKEY renders the page in
# its "cannot verify" state, which is indistinguishable from a blocked network --
# so without this check a deployment missing the binding looks like a customer
# ISP problem. Turnstile sitekeys start "0x"; an empty attribute cannot match.
contains "enrol page carries a real sitekey (TURNSTILE_SITEKEY is bound)" 200 \
  'data-sitekey="0x' "$BASE/blipscope/enroll?id=a1b2c3d4e5f6a7b8"

# NO SOLVE, NO KEY -- asserted against production, with a token that cannot pass.
#
# This is also the only thing that proves BOTH secrets are actually on the
# deployment: handleEnroll answers 503 not_configured when DEVICE_KEY_SECRET or
# TURNSTILE_SECRET_KEY is missing, so a 403 here means the endpoint got as far as
# asking Cloudflare and was told no. A 503 would mean the gate is not running at
# all, which is exactly the state that looks fine until someone tries to enroll.
enroll_post() {
  local name="$1" want="$2" marker="$3" payload="$4"
  local body status ok
  body="$(curl -s -w $'\n%{http_code}' --max-time 25 -X POST \
          -H 'content-type: application/json' -d "$payload" "$BASE/blipscope/enroll")"
  status="${body##*$'\n'}"; body="${body%$'\n'*}"
  case "$body" in *"$marker"*) ok=1 ;; *) ok=0 ;; esac
  printf '\n===== %s =====\n' "$name"
  printf 'expect HTTP %s containing %s\n' "$want" "$marker"
  printf 'got    HTTP %s / %s\n' "$status" "$body"
  # A key in the body is the one outcome that must never happen here, whatever
  # the status says -- checked separately so a 200 with a key can never be read
  # as a passing status code.
  case "$body" in
    *'"key"'*) printf 'FATAL: a key was returned without a solve.\n'; ok=0 ;;
  esac
  if [ "$status" = "$want" ] && [ "$ok" = "1" ]; then
    printf 'RESULT: PASS\n'; pass=$((pass+1))
  else
    printf 'RESULT: FAIL\n'; fail=$((fail+1))
  fi
}
enroll_post "enroll POST, unsolved token -> 403 (both secrets bound, gate live)" \
  403 '"unverified"' '{"id":"a1b2c3d4e5f6a7b8","token":"not-a-real-solve"}'
enroll_post "enroll POST, malformed id -> 400 (id shape checked before anything)" \
  400 '"bad_request"' '{"id":"../../etc","token":"not-a-real-solve"}'

# =============================================================================
# THE ROUTE SOURCE, CHECKED FOR DATA AND NOT JUST FOR A PULSE
#
# adsb.lol /api/0/routeset returned "201 Created" with an EMPTY body from
# 2026-07-08 until at least 2026-08-25 -- SEVEN WEEKS -- and nothing said so.
# Every route silently fell through to adsbdb, a service we have no written
# permission to use, while /healthz reported every upstream "closed".
#
# IT WAS INVISIBLE BY DESIGN. chain.ts records an alive-but-empty response as
# breaker SUCCESS, on the deliberate reasoning that "the breaker guards
# transport health, not data coverage". That is right for a breaker and it is
# exactly why a breaker cannot be the only check: the failure was in content.
#
# So this asserts DATA, in the order that tells the two failures apart -- a
# probe reporting absence must first prove it can observe presence.
#
#   1. REACHABILITY CONTROL. Post a deliberately invalid body. Their published
#      OpenAPI documents 422 for that, so a 422 proves the application is
#      reached. During the outage this returned the same 201/empty as a VALID
#      request -- the tell: if a malformed body is not rejected, nothing is
#      parsing it and the app is not there.
#   2. ONLY THEN the real query: a scheduled callsign must return 200 + array.
#
# Failing (1) and failing (2) mean different things -- their edge is broken vs
# their data is thin -- and collapsing them is what makes an alarm useless.
# =============================================================================
printf '\n===== adsb.lol routeset (the PRIMARY route source) =====\n'
ROUTESET_URL="https://api.adsb.lol/api/0/routeset"

RS_CTRL_CODE="$(curl -s -o /dev/null -w '%{http_code}' --max-time 20 -X POST "$ROUTESET_URL" \
                 -H 'content-type: application/json' -d '{"garbage":true}' 2>/dev/null)"
printf 'reachability control (invalid body, expect 422): HTTP %s\n' "$RS_CTRL_CODE"

# EXPIRES, IN CODE, NOT IN A COMMENT.
#
# adsb.lol routeset is broken upstream and we are not fixing it -- the D1
# mirror replaces it, after which THIS WHOLE CHECK IS DELETED (see the cutover
# checklist in ROADMAP.md). Until then a hard FAIL would leave the only
# production check we have permanently red, during exactly the window we are
# making the largest changes, and "one expected FAIL" is one step from "three
# FAILs are probably normal".
#
# So it warns -- but only until a DATE. A comment saying "expected red until
# cutover" becomes permanent furniture the moment the mirror slips. Past this
# date it is a FAIL again regardless, and the suite going red forces the
# conversation instead of quietly tolerating a third state.
#
# The date is the mirror timeline (cables ~2 weeks from 2026-08-25, and the
# image cut cannot precede the cutover) plus a week of slack.
ROUTESET_WARN_UNTIL="${ROUTESET_WARN_UNTIL:-2026-09-15}"
TODAY="$(date -u +%Y-%m-%d)"
if [ "$RS_CTRL_CODE" != "422" ]; then
  printf '        the routeset APPLICATION is not being reached -- an invalid body was\n'
  printf '        not rejected, so nothing is parsing requests. Every route lookup is\n'
  printf '        falling through to adsbdb. Fixed by the D1 mirror, not by adsb.lol.\n'
  # [[ ]] not [ ]: inside POSIX test, < is a REDIRECTION, so the first
  # version tried to open a file named 2026-09-15 and took the FAIL branch.
  # ISO-8601 dates compare correctly as strings, which is why the format matters.
  if [[ "$TODAY" < "$ROUTESET_WARN_UNTIL" ]]; then
    printf 'RESULT: WARN -- known, expires %s (then this becomes a FAIL).\n' \
      "$ROUTESET_WARN_UNTIL"
    warn=$((warn+1))
  else
    printf 'RESULT: FAIL -- the grace period ended %s and the mirror has not landed.\n' \
      "$ROUTESET_WARN_UNTIL"
    fail=$((fail+1))
  fi
else
  RS_BODY="$(curl -s --max-time 20 -X POST "$ROUTESET_URL" -H 'content-type: application/json' \
              -d '{"planes":[{"callsign":"AAL175","lat":40.6,"lng":-73.8}]}' 2>/dev/null)"
  printf 'live query (AAL175): %s\n' "$(printf '%s' "$RS_BODY" | head -c 160)"
  case "$RS_BODY" in
    \[*)  printf 'RESULT: PASS -- app reachable AND a JSON array came back.\n'
          pass=$((pass+1)) ;;
    *)    printf 'RESULT: FAIL -- app reachable but no usable array.\n'
          printf '        A DATA gap, not an outage -- a different fix from the above.\n'
          fail=$((fail+1)) ;;
  esac
fi

printf '\n================ SUMMARY ================\n'
printf 'PASS: %d   FAIL: %d   WARN: %d   SKIPPED: %d\n' "$pass" "$fail" "$warn" "$skipped"
if [ "$warn" -gt 0 ]; then
  printf 'WARN: routeset -- known, expires %s\n' "${ROUTESET_WARN_UNTIL:-?}"
fi
if [ "$skipped" -gt 0 ]; then
  printf 'NOTE: skipped checks did NOT run. Do not read this as a clean pass.\n'
fi
# ASSERT THE WARN COUNT. A warning nobody counts is a warning nobody reads,
# and an UNBOUNDED warn count turns a binary gate into a judgement call. One
# known warn is fine; two means something new started warning and nobody said
# so. With this, "1 known warn, 0 unknown" is still a usable pass/fail -- which
# is the entire point of demoting the routeset check rather than deleting it.
EXPECTED_WARNS="${EXPECTED_WARNS:-1}"
if [ "$warn" -ne "$EXPECTED_WARNS" ]; then
  printf 'RESULT: FAIL -- %d warning(s), expected exactly %d. A NEW warning appeared,\n' \
    "$warn" "$EXPECTED_WARNS"
  printf '        or an expected one stopped firing. Either way it needs a human.\n'
  exit 1
fi
[ "$fail" -eq 0 ] || exit 1
