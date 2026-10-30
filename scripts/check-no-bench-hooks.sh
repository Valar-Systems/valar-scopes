#!/usr/bin/env bash
# No bench hook ships. Run on EVERY slug-ful firmware build in CI (firmware.yml).
#
# Bench envs (e.g. env:blipscope-s3-128-alertbench, -DALERT_BENCH) compile hooks
# that fake aircraft and override settings from the serial console. They must
# never reach a customer. A comment saying "bench only" is advice; this is the
# check that runs. Each hook prints a unique marker string, and a marker in an
# image means the hook's code is in that image.
#
# PHOTO MODE IS ONE OF THEM. env:missileer-photo (-DPHOTO_MODE) is the Missileer
# firmware for product photos: no auto-rotation, no auto-dim, no OTA. Its code
# prints "[photo-mode]", so a shipping image carrying that string is a photo build
# about to reach a customer. The config half of the fence is
# scripts/check_photo_mode.mjs.
#
# THE CHECK HAS TO BE ABLE TO SEE. An absence found by a scanner that cannot read
# the image is not an absence, so every scan first requires an ANCHOR string that
# every real image carries ("[build] env="); no anchor = BLIND = exit 2, never a
# pass. --selftest plants a marker in a fake image and requires the scan to FAIL,
# and requires an anchor-less file to come back BLIND.
#
# AND THE MARKER HAS TO BE REAL. The selftest plants each marker as this list spells
# it, which proves the scanner reads the list -- not that the hook prints the same
# spelling. --expect closes that for a hook whose env CI builds: it scans THAT env's
# real image and requires its marker to be present (and every other marker absent).
# CI runs it on the missileer-photo leg, so "[photo-mode]" is proven to survive into
# an image before its absence from a shipping image is believed.
#
# Usage:  scripts/check-no-bench-hooks.sh <image.bin>
#         scripts/check-no-bench-hooks.sh --expect <marker> <image.bin>
#         scripts/check-no-bench-hooks.sh --selftest
# Exit:   0 clean (or, with --expect, exactly the expected marker)
#         1 a bench hook is in the image (or, with --expect, the marker is missing
#           or another one is present)
#         2 blind (anchor not found)

set -u

# Every bench hook's marker. Add a hook -> add its marker here, in the same PR.
MARKERS=( "[alert-bench]" "[touch-bench]" "[photo-mode]" "[bench-force-starve]" "[wx-bench]" )
ANCHOR="[build] env="

anchored() {
  if [ ! -f "$1" ] || ! grep -q -a -F -e "$ANCHOR" "$1"; then
    echo "BLIND: '$ANCHOR' not found in $1 -- cannot tell a clean image from an unreadable one"
    return 1
  fi
  return 0
}

scan() {
  local f="$1"
  anchored "$f" || return 2
  local hit=0 m   # m is LOCAL: the selftest loops over markers with the same name
  for m in "${MARKERS[@]}"; do
    if grep -q -a -F -e "$m" "$f"; then
      echo "FAIL: bench hook marker '$m' is in $f"
      hit=1
    fi
  done
  [ "$hit" -eq 0 ] && echo "ok: no bench hook in $f (${#MARKERS[@]} marker(s) checked, anchor present)"
  return "$hit"
}

# The positive control: this image IS the hook's build, so its marker must be there,
# and no other hook's may be.
expect() {
  local want="$1" f="$2" bad=0 m known=0
  for m in "${MARKERS[@]}"; do [ "$m" = "$want" ] && known=1; done
  if [ "$known" -eq 0 ]; then
    echo "FAIL: '$want' is not in MARKERS -- a shipping image would never be scanned for it"
    return 1
  fi
  anchored "$f" || return 2
  if ! grep -q -a -F -e "$want" "$f"; then
    echo "FAIL: '$want' is NOT in $f, which is the build that prints it -- its absence from a shipping image proves nothing"
    bad=1
  fi
  for m in "${MARKERS[@]}"; do
    [ "$m" = "$want" ] && continue
    if grep -q -a -F -e "$m" "$f"; then
      echo "FAIL: '$m' is also in $f -- another bench hook rode along"
      bad=1
    fi
  done
  [ "$bad" -eq 0 ] && echo "ok: $f carries '$want' and no other bench marker (anchor present)"
  return "$bad"
}

if [ "${1:-}" = "--selftest" ]; then
  t="$(mktemp -d)"
  printf 'xx[build] env=blipscope-s3-128xx' > "$t/clean.bin"
  printf 'no anchor here' > "$t/blind.bin"
  rc=0
  scan "$t/clean.bin"  >/dev/null; [ $? -eq 0 ] && echo "selftest ok: clean image passes"          || { echo "selftest FAIL: clean image"; rc=1; }
  # EVERY marker is planted and must be caught on its own -- a marker added to the list
  # but misspelled against its hook would otherwise never be exercised.
  for m in "${MARKERS[@]}"; do
    printf 'xx[build] env=blipscope-s3-128xx%sxx' "$m" > "$t/hooked.bin"
    scan "$t/hooked.bin" >/dev/null; [ $? -eq 1 ] && echo "selftest ok: planted $m is caught" || { echo "selftest FAIL: planted $m NOT caught"; rc=1; }
  done
  scan "$t/blind.bin"  >/dev/null; [ $? -eq 2 ] && echo "selftest ok: anchor-less file is BLIND"     || { echo "selftest FAIL: anchor-less file not BLIND"; rc=1; }
  # --expect, all four ways it can answer.
  printf 'xx[build] env=missileer-photoxx[photo-mode]xx' > "$t/photo.bin"
  printf 'xx[build] env=missileer-photoxx[photo-mode]xx[alert-bench]xx' > "$t/photo-hooked.bin"
  expect "[photo-mode]" "$t/photo.bin"        >/dev/null; [ $? -eq 0 ] && echo "selftest ok: --expect passes the image that carries its marker"   || { echo "selftest FAIL: --expect on the photo image"; rc=1; }
  expect "[photo-mode]" "$t/clean.bin"        >/dev/null; [ $? -eq 1 ] && echo "selftest ok: --expect fails an image without its marker"         || { echo "selftest FAIL: --expect passed a markerless image"; rc=1; }
  expect "[photo-mode]" "$t/photo-hooked.bin" >/dev/null; [ $? -eq 1 ] && echo "selftest ok: --expect fails when another bench marker rides along" || { echo "selftest FAIL: --expect missed a second marker"; rc=1; }
  expect "[photo-mode]" "$t/blind.bin"        >/dev/null; [ $? -eq 2 ] && echo "selftest ok: --expect on an anchor-less file is BLIND"          || { echo "selftest FAIL: --expect not BLIND"; rc=1; }
  expect "[not-a-marker]" "$t/photo.bin"      >/dev/null; [ $? -eq 1 ] && echo "selftest ok: --expect refuses a marker missing from MARKERS"    || { echo "selftest FAIL: --expect accepted an unlisted marker"; rc=1; }
  rm -rf "$t"
  exit "$rc"
fi

if [ "${1:-}" = "--expect" ]; then
  [ $# -eq 3 ] || { echo "usage: $0 --expect <marker> <image.bin>"; exit 2; }
  expect "$2" "$3"
  exit $?
fi

[ $# -eq 1 ] || { echo "usage: $0 <image.bin> | --expect <marker> <image.bin> | --selftest"; exit 2; }
scan "$1"
