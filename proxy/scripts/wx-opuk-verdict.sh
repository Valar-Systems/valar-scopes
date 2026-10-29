# wx_opuk_verdict <healthz body>: one line, "PASS -- ..." or "FAIL -- ...", for opuk's covered share (docs/wx-opuk.md,
# K9 and its pinned cases). Sourced by smoke-prod.sh; graded by wx-opuk-verdict.selftest.sh, which CI runs.
#
#   wx "not_configured"                  PASS  no bucket bound: the inert answer, as the /wx check's
#   no wx.opuk entry                     FAIL  the deployed Worker predates opuk (production is behind main)
#   frameAgeS null                       PASS  not ingesting yet: no opuk frame exists
#   a frame, share missing or null       FAIL  the ingest stored a frame without its share
#   a frame, share 0                     FAIL  every UK cell is "no coverage": the UK is served blank
#   a frame, share above 1               FAIL  not a share
#   a frame, 0 < share <= 1              PASS
wx_opuk_verdict() {
  local body="$1" entry age share
  if printf '%s' "$body" | grep -qE '"wx":"not_configured"'; then
    printf 'PASS -- no WX bucket bound (the inert answer): no opuk frame is expected\n'; return
  fi
  entry="$(printf '%s' "$body" | grep -oE '"opuk":\{[^}]*\}' | head -1)"
  if [ -z "$entry" ]; then
    printf 'FAIL -- /healthz has no wx.opuk: the deployed Worker predates opuk, or wx is unreadable\n'; return
  fi
  age="$(printf '%s' "$entry" | grep -oE '"frameAgeS":(null|-?[0-9]+)' | head -1 | cut -d: -f2)"
  share="$(printf '%s' "$entry" | grep -oE '"coveredShare":(null|[0-9.eE+-]+)' | head -1 | cut -d: -f2)"
  if [ -z "$age" ]; then
    printf 'FAIL -- wx.opuk has no readable frameAgeS: %s\n' "$entry"; return
  fi
  if [ "$age" = "null" ]; then
    printf 'PASS -- not ingesting yet: no opuk frame\n'; return
  fi
  if [ -z "$share" ] || [ "$share" = "null" ]; then
    printf 'FAIL -- an opuk frame %ss old with no covered share\n' "$age"; return
  fi
  if awk -v s="$share" 'BEGIN { exit !(s + 0 > 0 && s + 0 <= 1) }'; then
    printf 'PASS -- opuk frame %ss old, covered share %s\n' "$age" "$share"
  else
    printf 'FAIL -- opuk covered share %s (0 means the UK is served "no coverage" everywhere; above 1 is not a share)\n' "$share"
  fi
}
