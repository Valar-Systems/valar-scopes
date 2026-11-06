#!/usr/bin/env bash
# SCRATCH: one A1 /basemap request on staging as the test identity (docs/basemap-a1.md §6 + §8 4c, registered at
# f7bf74af). Same path as mm4-request.sh: test-identity.sh re-runs this through with-test-identity.mts (test-key-staging)
# and blip_auth_curl sends the headers on curl's stdin. Prints one line: label, start and end (UTC ms), elapsed ms,
# status, size, magic, Retry-After, and the ETag's store id only (its query digest is cut). Never a coordinate.
#   BASE=<staging> bash a1-request.sh <label> <lat2> <lon2>
set -uo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/wt-main/proxy/scripts"
BASE="${BASE:-https://scopes-staging.valarsystems.com}"
. "$SCRIPT_DIR/test-identity.sh"
test_identity_or_reexec "${BASH_SOURCE[0]}" "$@"
blip_auth_curl
Q="lat=$2&lon=$3&r=97&eps=100584&layers=water,rivers,roads,peaks,places,terrain&levels=24743664:246,49487328:246,98974656:246"
BODY="$(mktemp)"; HDR="$(mktemp)"
T0="$(date -u +%s%3N)"; S0="$(date -u +%H:%M:%S.%3NZ)"
ST="$(curl -s --max-time 60 -o "$BODY" -D "$HDR" -w '%{http_code}' --blip-auth "$BASE/api/v1/blipscope/basemap?$Q")"
T1="$(date -u +%s%3N)"; S1="$(date -u +%H:%M:%S.%3NZ)"
MAGIC="$(head -c 4 "$BODY" | tr -cd 'A-Z')"
ETAG="$(grep -i '^etag:' "$HDR" | tr -d '\r' | cut -d' ' -f2- | sed -E 's/^"?(bm1-[0-9]+-[0-9a-f]{12})-[0-9a-f]+"?$/\1-…/')"
RA="$(grep -i '^retry-after:' "$HDR" | tr -d '\r' | cut -d' ' -f2-)"
ERR="$(grep -oE '"error":"[a-z_]+"' "$BODY" | head -1)"
echo "$1 start $S0 ($T0) end $S1 ($T1) $((T1 - T0)) ms: HTTP $ST, $(wc -c < "$BODY" | tr -d ' ') bytes, magic '${MAGIC}', retry-after ${RA:-<none>}, etag ${ETAG:-<none>} ${ERR}"
rm -f "$BODY" "$HDR"
