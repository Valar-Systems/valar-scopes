#!/usr/bin/env bash
#
# adsb.fi bench poller -- runs ON a relay box.
#
# OBSOLETE SINCE 2026-07-31. DO NOT INSTALL. Stop it where it still runs:
#   systemctl disable --now blipscope-fi-bench
#
# WHY IT EXISTED
#   adsb.fi was then out of the serving chain, so no chain-order test exercised
#   it and there was no live traffic to measure. This poller drove the relay's
#   /fi upstream directly to get 24 h of comparable numbers WITHOUT touching what
#   devices were served. Its traffic lands in the same /var/log/nginx/relay.log
#   that relay/measure.mjs parses.
#
# WHY IT MUST NOT KEEP RUNNING
#   adsb.fi is now the chain PRIMARY: that same path carries live customer
#   traffic, so the thing this measured is measuring itself. Worse, it is
#   ACTIVELY HARMFUL in two ways. It spends the same 1 req/s per-IP budget our
#   written permission is conditional on, for nothing. And its requests are
#   indistinguishable from the Worker's by URI -- same prefix, same tile, same
#   cadence -- so it silently inflates the figure we report to the operator whose
#   goodwill the permission rests on. measure.mjs now separates the two by
#   remote_addr (this polls via localhost) and prints a warning when it sees any.
#
# WHAT IT MIRRORS
#   The same tile and radius as the adsb.lol soak (Bend 44.10/-121.30 dist 89 NM)
#   at the same ~15 s device poll cadence, so the 429 rate / X-Cache distribution /
#   longest-degraded-run comparison is apples to apples. Plus one hex lookup a
#   minute, cycling through hexes actually present in the tile, to approximate the
#   enrichment workload (a fixed hex would just HIT the 24 h cache forever and
#   measure nothing).
#
# RATE BUDGET -- do not raise without re-checking
#   adsb.fi's public limit is 1 req/s PER IP and 4xx/429s count toward it. This
#   runs at 4 position polls + 1 hex poll per minute = ~0.08 req/s, ~8% of the
#   limit, from each relay's own IP. Well inside the grant; keep it that way.
#
# INSTALL (per box):  bash fi-bench.sh --install     then: journalctl -u blipscope-fi-bench -f
# STOP:               systemctl disable --now blipscope-fi-bench
set -euo pipefail

TILE_LAT="${TILE_LAT:-44.10}"
TILE_LON="${TILE_LON:--121.30}"
TILE_DIST="${TILE_DIST:-89}"      # NM -- matches the adsb.lol soak tile exactly
POS_INTERVAL="${POS_INTERVAL:-15}" # s -- the device's active poll cadence
HEX_EVERY="${HEX_EVERY:-4}"        # one hex lookup per N position polls

UNIT=/etc/systemd/system/blipscope-fi-bench.service
SELF=/usr/local/bin/blipscope-fi-bench.sh

if [ "${1:-}" = "--install" ]; then
  install -m755 "$0" "$SELF"
  cat > "$UNIT" <<EOF
[Unit]
Description=Blipscope adsb.fi bench poller (OBSOLETE -- adsb.fi is the chain primary; do not enable)
After=network-online.target nginx.service

[Service]
Type=simple
ExecStart=$SELF
Restart=always
RestartSec=10

[Install]
WantedBy=multi-user.target
EOF
  systemctl daemon-reload
  systemctl enable --now blipscope-fi-bench
  echo "[fi-bench] installed and started; follow with: journalctl -u blipscope-fi-bench -f"
  exit 0
fi

KEY="$(tr -d ' \t\r\n' < /etc/nginx/relay.key)"
CURL=(curl -sk --resolve localhost:443:127.0.0.1 -H "X-Relay-Key: $KEY" --max-time 20)
POS_URL="https://localhost/fi/v3/lat/$TILE_LAT/lon/$TILE_LON/dist/$TILE_DIST"

echo "[fi-bench] polling $POS_URL every ${POS_INTERVAL}s (hex every $HEX_EVERY polls)"
i=0
hexes=()
while true; do
  body="$("${CURL[@]}" "$POS_URL" || true)"

  # Refresh the hex pool from the tile we just fetched. Using live hexes keeps the
  # enrichment probe realistic (varied airframes) instead of re-HITting one entry.
  if [ -n "$body" ]; then
    mapfile -t fresh < <(printf '%s' "$body" \
      | grep -o '"hex":"[0-9a-f]\{6\}"' | cut -d'"' -f4 | head -40 || true)
    [ "${#fresh[@]}" -gt 0 ] && hexes=("${fresh[@]}")
  fi

  if [ $((i % HEX_EVERY)) -eq 0 ] && [ "${#hexes[@]}" -gt 0 ]; then
    hex="${hexes[$((RANDOM % ${#hexes[@]}))]}"
    "${CURL[@]}" -o /dev/null "https://localhost/fi/v2/hex/$hex" || true
  fi

  i=$((i + 1))
  sleep "$POS_INTERVAL"
done
