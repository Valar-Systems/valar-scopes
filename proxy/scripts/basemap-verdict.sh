# basemap_verdict <http status> <body file> <header file>: one line, "PASS -- ..." or "FAIL -- ...", for the smoke's
# /basemap check (docs/basemap-route.md §9, D0 and D3; docs/basemap-deploy.md, Q5 as ruled). Sourced by smoke-prod.sh;
# graded by basemap-verdict.selftest.sh, which CI runs. Needs BASEMAP_PROXY_DIR (the proxy/ checkout, for npx tsx).
#
#   503 not_configured                          PASS  no BASEMAP binding: the inert answer (D0)
#   200, BMPK magic, an ETag, validate() kOk    PASS  wired (D3, D4)
#   200 that validate() refuses                 FAIL  the device's reader would refuse it too
#   200 without the magic, or without an ETag   FAIL
#   anything else (404, 503 basemap_unavailable, a 5xx, no answer)   FAIL, never "not yet"
#
# The body is binary, so it is never printed.

# The proxy's own validator, so the smoke refuses exactly what the device would. Fails closed: no checkout, no PASS.
_bm_validate() {
  if [ -z "${BASEMAP_PROXY_DIR:-}" ] || [ ! -f "$BASEMAP_PROXY_DIR/scripts/basemap-validate.ts" ]; then printf 'NO_VALIDATOR'; return; fi
  ( cd "$BASEMAP_PROXY_DIR" && npx --no tsx scripts/basemap-validate.ts "$1" 2>/dev/null ) | head -1
}

basemap_verdict() {
  local status="$1" body="$2" hdr="$3" err magic etag st
  err="$(grep -oE '"error":"[a-z_]+"' "$body" 2>/dev/null | head -1)"
  if [ "$status" = "503" ] && [ "$err" = '"error":"not_configured"' ]; then
    printf 'PASS -- not wired (no BASEMAP binding): the inert answer\n'; return
  fi
  if [ "$status" != "200" ]; then
    printf 'FAIL -- HTTP %s %s: neither the inert answer nor a package\n' "$status" "$err"; return
  fi
  magic="$(head -c 4 "$body" 2>/dev/null | tr -cd 'A-Z')"
  etag="$(grep -i '^etag:' "$hdr" 2>/dev/null | tr -d '\r' | cut -d' ' -f2-)"
  if [ "$magic" != "BMPK" ]; then
    printf 'FAIL -- a 200 without BMPK magic\n'; return
  fi
  if [ -z "$etag" ]; then
    printf 'FAIL -- a 200 package without an ETag\n'; return
  fi
  st="$(_bm_validate "$body")"
  if [ "$st" != "kOk" ]; then
    printf 'FAIL -- a 200 package the validator refuses (%s): the device would refuse it too\n' "${st:-no status}"; return
  fi
  printf 'PASS -- wired: a BMPK package, ETag %s, validate() kOk\n' "$etag"
}
