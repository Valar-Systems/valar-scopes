#!/usr/bin/env bash
# Selftest for basemap-verdict.sh (docs/basemap-deploy.md, the D0 + D2 PR's smoke row). Seven canned answers; the
# WHOLE verdict list must equal the expected one (no fragment matching). Then the sabotage -- the validate() call
# skipped -- must change exactly case 3 (a 200 with BMPK magic and an ETag that the validator refuses as kCrc) to PASS,
# and the selftest must see it: if the sabotage changes nothing, it did not apply, and that is a failure too.
set -u
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export BASEMAP_PROXY_DIR="$(cd "$HERE/.." && pwd)"
FX="$HERE/../../test/fixtures/wx/basemap"
T="$(mktemp -d)"
trap 'rm -rf "$T"' EXIT

printf '{"error":"not_configured"}' > "$T/notconf.json"
printf '{"error":"not_found"}' > "$T/notfound.json"
printf '{"error":"basemap_unavailable"}' > "$T/unavail.json"
: > "$T/empty.bin"
printf 'HTTP/1.1 200 OK\r\ncontent-type: application/octet-stream\r\netag: "bm1-1-0123456789ab-0123456789abcdef01234567"\r\n\r\n' > "$T/etag.hdr"
printf 'HTTP/1.1 200 OK\r\ncontent-type: application/octet-stream\r\n\r\n' > "$T/noetag.hdr"
printf 'HTTP/1.1 503 Service Unavailable\r\ncontent-type: application/json\r\n\r\n' > "$T/json.hdr"
for f in syn-min neg-crc; do
  [ -f "$FX/$f.bmpk" ] || { printf 'SELFTEST BLIND: fixture %s.bmpk is missing\n' "$f"; exit 2; }
done

# status | body | headers
CASES=(
  "503|$T/notconf.json|$T/json.hdr"
  "200|$FX/syn-min.bmpk|$T/etag.hdr"
  "200|$FX/neg-crc.bmpk|$T/etag.hdr"
  "200|$FX/syn-min.bmpk|$T/noetag.hdr"
  "404|$T/notfound.json|$T/json.hdr"
  "503|$T/unavail.json|$T/json.hdr"
  "200|$T/empty.bin|$T/etag.hdr"
)
EXPECT="PASS PASS FAIL FAIL FAIL FAIL FAIL"
SABOTAGED="PASS PASS PASS FAIL FAIL FAIL FAIL"

verdicts() {   # $1: the verdict file to source
  ( . "$1"; for c in "${CASES[@]}"; do IFS='|' read -r s b h <<< "$c"; basemap_verdict "$s" "$b" "$h" | cut -d' ' -f1; done ) | tr '\n' ' ' | sed 's/ $//'
}

rc=0
got="$(verdicts "$HERE/basemap-verdict.sh")"
printf 'real      : %s\nexpected  : %s\n' "$got" "$EXPECT"
[ "$got" = "$EXPECT" ] || { printf 'SELFTEST FAIL: the verdicts differ\n'; rc=1; }

sab="$T/sabotaged.sh"
sed 's/_bm_validate "\$body"/echo kOk/' "$HERE/basemap-verdict.sh" > "$sab"
if cmp -s "$sab" "$HERE/basemap-verdict.sh"; then
  printf 'SELFTEST FAIL: the sabotage did not apply (the file is unchanged)\n'; rc=1
else
  got="$(verdicts "$sab")"
  printf 'sabotaged : %s\nexpected  : %s\n' "$got" "$SABOTAGED"
  [ "$got" = "$SABOTAGED" ] || { printf 'SELFTEST FAIL: the sabotage did not turn exactly case 3\n'; rc=1; }
fi
[ "$rc" -eq 0 ] && printf 'SELFTEST PASSED\n'
exit "$rc"
