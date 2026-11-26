# sitefeed_line <healthz body>: smoke's one site-feed line (the malformed-flags marker). Sourced by smoke-prod.sh, which
# passes the /healthz body it already fetched; graded by sitefeed-line.selftest.sh, which CI runs (workers.yml).
#
#        sitefeed dbFlagsBad=<bad>/<n> age=<age_s>s stale=<bool>   the site feed's last served snapshot
#   WARN sitefeed dbFlagsBad=<bad>/<n> ...                         bad is not 0: aircraft hidden for malformed dbFlags
#   WARN sitefeed unavailable (<reason>)                           available false: disabled, empty or read_error
#   WARN sitefeed: /healthz has no sitefeed field                  an empty body, or a Worker before the marker
#
# Both numbers are printed because a jump is a ratio. "-" is a field the block does not carry (never a made-up value).
# It ALWAYS returns 0 and touches neither of smoke's counters: a WARN is information, never a FAIL.
sitefeed_line() {
  local body="$1" block bad n age stale reason mark
  # The block is flat (no nested object, no array: index.ts), so cut exactly it, never past its closing brace.
  block="$(printf '%s' "$body" | grep -oE '"sitefeed":\{[^{}]*\}' | head -1)"
  if [ -z "$block" ]; then
    printf 'WARN sitefeed: /healthz has no sitefeed field\n'; return 0
  fi
  if ! printf '%s' "$block" | grep -qE '"available":true'; then
    reason="$(printf '%s' "$block" | grep -oE '"reason":"[a-z_]{1,24}"' | cut -d'"' -f4)"
    printf 'WARN sitefeed unavailable (%s)\n' "${reason:-unknown}"; return 0
  fi
  bad="$(printf '%s' "$block" | grep -oE '"dbFlagsBad":[0-9]+' | cut -d: -f2)"
  n="$(printf '%s' "$block" | grep -oE '"n":[0-9]+' | cut -d: -f2)"
  age="$(printf '%s' "$block" | grep -oE '"age_s":[0-9]+' | cut -d: -f2)"
  stale="$(printf '%s' "$block" | grep -oE '"stale":(true|false)' | cut -d: -f2)"
  mark="    "
  [ "$bad" != "0" ] && mark="WARN"
  printf '%s sitefeed dbFlagsBad=%s/%s age=%ss stale=%s\n' "$mark" "${bad:--}" "${n:--}" "${age:--}" "${stale:--}"
  return 0
}
