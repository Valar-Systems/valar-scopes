#!/usr/bin/env bash
# Weather is not in a shipping image (Daniel, 2026-11-07 ~16:03Z: weather and maps ship together, so v19 ships without
# it). Run on every slug-ful firmware build in CI until the joint release turns [weather] layer on, and that PR removes the
# step. A comment saying "off" is advice; a stored key must not be able to turn it on, so this reads the built image.
#
# THE CHECK HAS TO BE ABLE TO SEE. An absence found by a scanner that cannot read the image is not an absence, so every scan
# first requires the ANCHOR every real image carries; no anchor = BLIND = exit 2, never a pass. check-no-bench-hooks.sh is
# the same shape.
#
# AND THE STRINGS HAVE TO BE REAL. --expect scans a build that HAS weather (blipscope-s3-128-wxlook, in CI) and requires
# every string to be present, so their absence from a shipping image means something. A typo here would otherwise pass for
# ever.
#
# Usage:  scripts/check-no-weather.sh <image.bin>            exit 0 none present; 1 one is present; 2 blind
#         scripts/check-no-weather.sh --expect <image.bin>   exit 0 all present; 1 one is missing; 2 blind
#         scripts/check-no-weather.sh --selftest

set -u

# The request's path, the page's box and look select, and the layer's own buffers.
STRINGS=( "/wx/" 'name="wx-layer"' "Brighter rain" "[wx] buffers" )
ANCHOR="[build] env="

anchored() {
  if [ ! -f "$1" ] || ! grep -q -a -F -e "$ANCHOR" "$1"; then
    echo "BLIND: '$ANCHOR' not found in $1 -- cannot tell a clean image from an unreadable one"
    return 1
  fi
  return 0
}

scan() {
  local f="$1" hit=0 s
  anchored "$f" || return 2
  for s in "${STRINGS[@]}"; do
    if grep -q -a -F -e "$s" "$f"; then
      echo "FAIL: weather string '$s' is in $f"
      hit=1
    fi
  done
  [ "$hit" -eq 0 ] && echo "ok: no weather in $f (${#STRINGS[@]} string(s) checked, anchor present)"
  return "$hit"
}

expect() {
  local f="$1" bad=0 s
  anchored "$f" || return 2
  for s in "${STRINGS[@]}"; do
    if ! grep -q -a -F -e "$s" "$f"; then
      echo "FAIL: '$s' is NOT in $f, which is a build with weather -- its absence from a shipping image proves nothing"
      bad=1
    fi
  done
  [ "$bad" -eq 0 ] && echo "ok: $f carries all ${#STRINGS[@]} weather strings (anchor present)"
  return "$bad"
}

if [ "${1:-}" = "--selftest" ]; then
  t="$(mktemp -d)"
  fail=0
  printf 'xx[build] env=blipscope-s3-128xx' > "$t/clean.bin"
  printf 'no anchor here' > "$t/blind.bin"
  scan "$t/clean.bin" >/dev/null; [ "$?" -eq 0 ] && echo "selftest ok: a clean image passes" || { echo "selftest FAIL: a clean image did not pass"; fail=1; }
  scan "$t/blind.bin" >/dev/null; [ "$?" -eq 2 ] && echo "selftest ok: an anchor-less file is BLIND" || { echo "selftest FAIL: an anchor-less file was not BLIND"; fail=1; }
  for s in "${STRINGS[@]}"; do
    { cat "$t/clean.bin"; printf '%s' "$s"; } > "$t/planted.bin"
    scan "$t/planted.bin" >/dev/null
    [ "$?" -eq 1 ] && echo "selftest ok: '$s' planted fails" || { echo "selftest FAIL: '$s' planted did not fail"; fail=1; }
  done
  { cat "$t/clean.bin"; for s in "${STRINGS[@]}"; do printf '%s ' "$s"; done; } > "$t/all.bin"
  expect "$t/all.bin" >/dev/null; [ "$?" -eq 0 ] && echo "selftest ok: --expect passes an image with all of them" || { echo "selftest FAIL: --expect on a full image"; fail=1; }
  expect "$t/clean.bin" >/dev/null; [ "$?" -eq 1 ] && echo "selftest ok: --expect fails an image without them" || { echo "selftest FAIL: --expect on a clean image"; fail=1; }
  rm -rf "$t"
  exit "$fail"
fi

if [ "${1:-}" = "--expect" ]; then
  [ $# -eq 2 ] || { echo "usage: $0 --expect <image.bin>"; exit 2; }
  expect "$2"
  exit $?
fi
[ $# -eq 1 ] || { echo "usage: $0 <image.bin> | --expect <image.bin> | --selftest"; exit 2; }
scan "$1"
exit $?
