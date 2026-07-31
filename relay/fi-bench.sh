#!/usr/bin/env bash
#
# adsb.fi bench poller -- runs ON a relay box, under adsb.fi's TESTING grant.
#
# WHY THIS EXISTS INSTEAD OF A CHAIN CHANGE
#   adsb.fi is licence-blocked for production (personal/non-commercial terms, no
#   redistribution right -- see proxy/src/upstreams/adsb_fi.ts), so it is NOT in
#   any serving path and the Worker keeps UPSTREAM_ADSB_FI_ENABLED = "false".
#   That means a chain-order test would never exercise it. This poller drives the
#   relay's /fi upstream directly, so we get 24 h of comparable numbers WITHOUT
#   touching what devices are served. Its traffic lands in the same
#   /var/log/nginx/relay.log that relay/measure.mjs already parses.
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
Description=Blipscope adsb.fi bench poller (measurement only; licence-blocked source)
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
