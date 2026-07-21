#!/usr/bin/env bash
# watch-upstream.sh -- sustained watch on the feed, for qualifying a posture change
# (e.g. adsb.lol-only). Polls /v1/blips at a device-like cadence and records what a
# CUSTOMER would actually experience, which is not the same as the upstream's raw
# error rate:
#
#   fresh  -- 200 + X-Cache: HIT/MISS         -> live picture
#   stale  -- 200 + X-Cache: STALE            -> last good picture, device shows
#                                                its stale indicator
#   warm   -- 503 "warming"                   -> cold tile, upstream slow; device
#                                                keeps its last picture
#   down   -- 503 upstream_unavailable        -> chain exhausted; nothing to show
#   other  -- anything else (401/429/5xx)
#
# adsb.lol's 429s are UPSTREAM; a device never sees a 429. It sees staleness. That
# is why this measures stale/down rates rather than trying to count 429s.
#
# The key is read from your environment and never printed.
#   export BLIP_KEY='...'
#   ./scripts/watch-upstream.sh                 # default 24 h, 30 s cadence
#   HOURS=1 INTERVAL=15 ./scripts/watch-upstream.sh
#   BASE=https://scopes-staging.valarsystems.com ./scripts/watch-upstream.sh

set -uo pipefail

BASE="${BASE:-https://scopes.valarsystems.com}"
KEY="${BLIP_KEY:-}"
LAT="${LAT:-44.06}"; LON="${LON:--121.32}"; R="${R:-160}"
INTERVAL="${INTERVAL:-30}"
HOURS="${HOURS:-24}"
OUT="${OUT:-upstream-watch-$(date +%Y%m%d-%H%M%S).log}"

[ -n "$KEY" ] || { echo "BLIP_KEY not set. export BLIP_KEY='<prod key>'" >&2; exit 2; }

fresh=0; stale=0; warm=0; down=0; other=0; n=0
worst_start=""; worst_len=0; cur_start=""; cur_len=0

echo "watch-upstream: $BASE  every ${INTERVAL}s for ${HOURS}h  -> $OUT"
echo "# ts status cache upstream tile_age_s" > "$OUT"

# via awk: bash arithmetic cannot handle a fractional HOURS (handy for smoke runs)
DURATION_S=$(awk -v h="$HOURS" 'BEGIN{printf "%d", h*3600}')
end=$(( $(date +%s) + DURATION_S ))
while [ "$(date +%s)" -lt "$end" ]; do
  ts=$(date +%H:%M:%S)
  hdr=$(curl -s -D - -o /tmp/_uw_body --max-time 25 \
        -H "X-Blip-Key: $KEY" "$BASE/v1/blips?lat=$LAT&lon=$LON&r=$R&limit=40" 2>/dev/null)
  status=$(printf '%s' "$hdr" | grep -oE 'HTTP/[0-9.]+ [0-9]+' | tail -1 | awk '{print $2}')
  cache=$(printf '%s'  "$hdr" | grep -i '^x-cache:'    | tr -d '\r' | awk '{print $2}')
  up=$(printf '%s'     "$hdr" | grep -i '^x-upstream:' | tr -d '\r' | awk '{print $2}')
  # tile age: body carries the ORIGINAL t, so a stale tile reports honestly
  t=$(grep -oE '"t":[0-9]+' /tmp/_uw_body 2>/dev/null | head -1 | cut -d: -f2)
  age=""; [ -n "${t:-}" ] && age=$(( $(date +%s) - t ))

  n=$((n+1)); bad=0
  case "${status:-000}/${cache:-}" in
    200/HIT|200/MISS) fresh=$((fresh+1)) ;;
    200/STALE)        stale=$((stale+1)); bad=1 ;;
    503/*)            if grep -q warming /tmp/_uw_body 2>/dev/null; then warm=$((warm+1)); else down=$((down+1)); fi; bad=1 ;;
    *)                other=$((other+1)); bad=1 ;;
  esac

  # track the longest unbroken degraded run -- "worst window" is what a customer
  # actually notices, far more than an average.
  if [ "$bad" -eq 1 ]; then
    [ -z "$cur_start" ] && cur_start="$ts"
    cur_len=$((cur_len+1))
    if [ "$cur_len" -gt "$worst_len" ]; then worst_len=$cur_len; worst_start=$cur_start; fi
  else
    cur_start=""; cur_len=0
  fi

  echo "$ts ${status:-000} ${cache:-none} ${up:-none} ${age:-na}" >> "$OUT"
  sleep "$INTERVAL"
done

pct() { [ "$n" -gt 0 ] && awk -v a="$1" -v b="$n" 'BEGIN{printf "%.2f%%", 100*a/b}' || echo "n/a"; }
{
  echo
  echo "======== SUMMARY ($n polls, ${INTERVAL}s apart) ========"
  printf 'fresh : %5d  %s\n' "$fresh" "$(pct $fresh)"
  printf 'stale : %5d  %s\n' "$stale" "$(pct $stale)"
  printf 'warm  : %5d  %s\n' "$warm"  "$(pct $warm)"
  printf 'down  : %5d  %s\n' "$down"  "$(pct $down)"
  printf 'other : %5d  %s\n' "$other" "$(pct $other)"
  echo
  echo "worst unbroken degraded run: $worst_len polls (~$((worst_len*INTERVAL))s) starting $worst_start"
  echo "customer experience in that window: $( [ "$down" -gt 0 ] && echo 'blank/last picture' || echo 'last picture + stale indicator' )"
} | tee -a "$OUT"
