#!/usr/bin/env bash
# SCRATCH: one MM4 /basemap request on staging as the test identity (docs/basemap-branch-log.md, the staging read).
# Same path as d3-request.sh: test-identity.sh re-runs this through with-test-identity.mts (test-key-staging) and
# blip_auth_curl sends the headers on curl's stdin. Prints one line: label, start and end (UTC, and epoch seconds),
# status, size, magic and the ETag (the ETag is the store id and a query digest: no key, no device id).
#   BASE=<staging> bash mm4-request.sh <label> [<if-none-match etag>]
set -uo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/wt-main/proxy/scripts"
BASE="${BASE:-https://scopes-staging.valarsystems.com}"
. "$SCRIPT_DIR/test-identity.sh"
test_identity_or_reexec "${BASH_SOURCE[0]}" "$@"
blip_auth_curl
Q="lat=39.8617&lon=-104.6731&r=97&eps=100584&layers=water,rivers,roads,peaks,places,terrain&levels=24743664:246,49487328:246,98974656:246"
BODY="$(mktemp)"; HDR="$(mktemp)"
EXTRA=()
[ -n "${2:-}" ] && EXTRA=(-H "If-None-Match: $2")
T0="$(date -u +%s)"; S0="$(date -u +%H:%M:%SZ)"
ST="$(curl -s --max-time 60 -o "$BODY" -D "$HDR" -w '%{http_code}' --blip-auth "${EXTRA[@]}" "$BASE/api/v1/blipscope/basemap?$Q")"
T1="$(date -u +%s)"; S1="$(date -u +%H:%M:%SZ)"
MAGIC="$(head -c 4 "$BODY" | tr -cd 'A-Z')"
ETAG="$(grep -i '^etag:' "$HDR" | tr -d '\r' | cut -d' ' -f2-)"
ERR="$(grep -oE '"error":"[a-z_]+"' "$BODY" | head -1)"
echo "$1 start $S0 ($T0) end $S1 ($T1): HTTP $ST, $(wc -c < "$BODY" | tr -d ' ') bytes, magic '${MAGIC}', etag ${ETAG:-<none>} ${ERR}"
rm -f "$BODY" "$HDR"
