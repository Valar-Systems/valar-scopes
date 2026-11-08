#!/usr/bin/env bash
# The web build's own keys are in no device image (docs/web-kiosk-airfield.md, KI4; ruled 2026-11-08 ~20:01Z). "kiosk"
# and "airports-remote" are read only under BLIPSCOPE_WEB_RENDERER or in the web shim, so a device image that carries
# either has a path the design says it cannot have. Run on every firmware build in CI.
#
# THE CHECK HAS TO BE ABLE TO SEE. Every scan first requires an ANCHOR the file is known to carry ("[build] env=" in a
# device image); no anchor = BLIND = exit 2, never a pass.
#
# AND THE STRINGS HAVE TO BE REAL. --expect scans the web build's wasm, which must carry both, so their absence from a
# device image means something. The wasm has no device banner, so its anchor is a path it is known to forward.
#
# Usage:  scripts/check-web-only.sh <image.bin>                          exit 0 none present; 1 one is present; 2 blind
#         scripts/check-web-only.sh --expect [--anchor <s>] <file.wasm>   exit 0 all present; 1 one is missing; 2 blind
#         scripts/check-web-only.sh --selftest

set -u

STRINGS=( "kiosk" "airports-remote" )
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
      echo "FAIL: the web build's key '$s' is in $f"
      hit=1
    fi
  done
  [ "$hit" -eq 0 ] && echo "ok: no web-only key in $f (${#STRINGS[@]} string(s) checked, anchor present)"
  return "$hit"
}

expect() {
  local f="$1" bad=0 s
  anchored "$f" || return 2
  for s in "${STRINGS[@]}"; do
    if ! grep -q -a -F -e "$s" "$f"; then
      echo "FAIL: '$s' is NOT in $f, the web build -- its absence from a device image proves nothing"
      bad=1
    fi
  done
  [ "$bad" -eq 0 ] && echo "ok: $f carries all ${#STRINGS[@]} web-only keys (anchor present)"
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
  expect "$t/all.bin" >/dev/null; [ "$?" -eq 0 ] && echo "selftest ok: --expect passes a file with all of them" || { echo "selftest FAIL: --expect refused a file with all of them"; fail=1; }
  expect "$t/clean.bin" >/dev/null; [ "$?" -eq 1 ] && echo "selftest ok: --expect fails a file without them" || { echo "selftest FAIL: --expect passed a file without them"; fail=1; }
  rm -rf "$t"
  exit "$fail"
fi

if [ "${1:-}" = "--expect" ]; then
  shift
  if [ "${1:-}" = "--anchor" ]; then ANCHOR="${2:-}"; shift 2; fi
  [ $# -eq 1 ] && [ -n "$ANCHOR" ] || { echo "usage: $0 --expect [--anchor <s>] <file>"; exit 2; }
  expect "$1"
  exit $?
fi
[ $# -eq 1 ] || { echo "usage: $0 <image.bin> | --expect [--anchor <s>] <file> | --selftest"; exit 2; }
scan "$1"
exit $?
