#!/usr/bin/env bash
# No bench hook ships. Run on EVERY slug-ful firmware build in CI (firmware.yml).
#
# Bench envs (e.g. env:blipscope-s3-128-alertbench, -DALERT_BENCH) compile hooks
# that fake aircraft and override settings from the serial console. They must
# never reach a customer. A comment saying "bench only" is advice; this is the
# check that runs. Each hook prints a unique marker string, and a marker in an
# image means the hook's code is in that image.
#
# THE CHECK HAS TO BE ABLE TO SEE. An absence found by a scanner that cannot read
# the image is not an absence, so every scan first requires an ANCHOR string that
# every real image carries ("[build] env="); no anchor = BLIND = exit 2, never a
# pass. --selftest plants a marker in a fake image and requires the scan to FAIL,
# and requires an anchor-less file to come back BLIND.
#
# Usage:  scripts/check-no-bench-hooks.sh <image.bin>
#         scripts/check-no-bench-hooks.sh --selftest
# Exit:   0 clean   1 a bench hook is in the image   2 blind (anchor not found)

set -u

# Every bench hook's marker. Add a hook -> add its marker here, in the same PR.
MARKERS=( "[alert-bench]" )
ANCHOR="[build] env="

scan() {
  local f="$1"
  if [ ! -f "$f" ] || ! grep -q -a -F -e "$ANCHOR" "$f"; then
    echo "BLIND: '$ANCHOR' not found in $f -- cannot tell a clean image from an unreadable one"
    return 2
  fi
  local hit=0
  for m in "${MARKERS[@]}"; do
    if grep -q -a -F -e "$m" "$f"; then
      echo "FAIL: bench hook marker '$m' is in $f"
      hit=1
    fi
  done
  [ "$hit" -eq 0 ] && echo "ok: no bench hook in $f (${#MARKERS[@]} marker(s) checked, anchor present)"
  return "$hit"
}

if [ "${1:-}" = "--selftest" ]; then
  t="$(mktemp -d)"
  printf 'xx[build] env=blipscope-s3-128xx' > "$t/clean.bin"
  printf 'xx[build] env=blipscope-s3-128xx%sxx' "${MARKERS[0]}" > "$t/hooked.bin"
  printf 'no anchor here' > "$t/blind.bin"
  rc=0
  scan "$t/clean.bin"  >/dev/null; [ $? -eq 0 ] && echo "selftest ok: clean image passes"          || { echo "selftest FAIL: clean image"; rc=1; }
  scan "$t/hooked.bin" >/dev/null; [ $? -eq 1 ] && echo "selftest ok: planted marker is caught"      || { echo "selftest FAIL: planted marker NOT caught"; rc=1; }
  scan "$t/blind.bin"  >/dev/null; [ $? -eq 2 ] && echo "selftest ok: anchor-less file is BLIND"     || { echo "selftest FAIL: anchor-less file not BLIND"; rc=1; }
  rm -rf "$t"
  exit "$rc"
fi

[ $# -eq 1 ] || { echo "usage: $0 <image.bin> | --selftest"; exit 2; }
scan "$1"
