# wx_products_section <healthz body>: smoke's per-product weather lines (docs/wx-healthz-products.md, Q-HZ1 and H6).
# Sourced by smoke-prod.sh, which passes the /healthz body it already fetched; graded by wx-products-lines.selftest.sh,
# which CI runs. One line per product in /healthz's wx object, in its order:
#
#        wx.<id> routed=<true|false|-> age=<seconds|null|-> stale=<true|false|null|->
#   WARN wx.<id> ...                                          a product whose stale is true
#        wx not_configured                                    "wx":"not_configured": no WX bucket bound
#   WARN wx unreadable                                        an empty body, or no wx object to cut
#
# "-" is a field the deployed Worker does not report (the shape before docs/wx-healthz-products.md). It ALWAYS returns
# 0 and touches neither of smoke's counters: a WARN is information, never a FAIL, so it never changes smoke's exit code.
wx_products_section() {
  local body="$1" pre='"wx":' block entries entry id routed age stale mark
  if printf '%s' "$body" | grep -qE '"wx":"not_configured"'; then
    printf '     wx not_configured\n'; return 0
  fi
  # The wx object holds only flat objects, one per product: cut exactly that, never past its closing brace.
  block="$(printf '%s' "$body" | grep -oE '"wx":\{("[a-z0-9]{2,8}":\{[^{}]*\},?)*\}' | head -1)"
  if [ -z "$block" ]; then
    printf 'WARN wx unreadable\n'; return 0
  fi
  entries="$(printf '%s' "${block#"$pre"}" | grep -oE '"[a-z0-9]{2,8}":\{[^{}]*\}')"
  while IFS= read -r entry; do
    [ -n "$entry" ] || continue
    id="$(printf '%s' "$entry" | sed -E 's/^"([a-z0-9]+)":.*/\1/')"
    routed="$(printf '%s' "$entry" | grep -oE '"routed":(true|false)' | cut -d: -f2)"
    age="$(printf '%s' "$entry" | grep -oE '"frameAgeS":(null|-?[0-9]+)' | cut -d: -f2)"
    stale="$(printf '%s' "$entry" | grep -oE '"stale":(true|false|null)' | cut -d: -f2)"
    mark="    "
    [ "$stale" = "true" ] && mark="WARN"
    printf '%s wx.%s routed=%s age=%s stale=%s\n' "$mark" "$id" "${routed:--}" "${age:--}" "${stale:--}"
  done <<< "$entries"
  return 0
}
