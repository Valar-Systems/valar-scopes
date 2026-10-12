#!/usr/bin/env bash
# /diag/fb is in the Missileer -fbdiag bench images and NOWHERE in the shipping ones.
#
# Checks the BINARIES (firmware.elf and the firmware.bin that is actually flashed), not
# the ini that was meant to produce them.
#
# The presence side is the positive control: "0 occurrences" in a shipping image means
# absent only if the same probe finds the strings in the bench image.
#
# WHY THIS IS ITS OWN SCRIPT AND NOT A check-no-bench-hooks.sh MARKER. That script's
# markers are global: a marker there fails every image that carries it. The radar edition's
# SHIPPING image serves /diag/fb by design, so "/diag/fb" as a global marker would fail the
# radar leg. The rule here is Missileer-only (ruling 2026-09-28: debug builds only, compiled
# out of release builds entirely), so the check is Missileer-only too.
#
# Usage:
#   scripts/check-missileer-fbdiag.sh              all four Missileer images (local; build them first:
#                                                    pio run -e missileer-s3-128 -e missileer-s3-146 \
#                                                            -e missileer-s3-128-fbdiag -e missileer-s3-146-fbdiag)
#   scripts/check-missileer-fbdiag.sh --env <env>  one shipping env and its <env>-fbdiag sibling (CI: the
#                                                    missileer-* legs build both, then run this)
#   scripts/check-missileer-fbdiag.sh --selftest   prove the probe can fail: a planted marker is caught,
#                                                    a missing marker is caught, an anchor-less file fails
#
# Exit 0 = shipping images carry no route, no handler and no viewer; bench images carry all.
set -uo pipefail

# Strings only the /diag/fb route and its viewer page put in the image.
MARKERS=(
  "/diag/fb"                          # both route paths (and nothing else in a Missileer build)
  "X-Blipscope-Frame-Format"          # the handler's response header
  "out of PSRAM for a frame snapshot" # the handler's own error text
  "Blipscope screen"                  # the viewer page's heading
)
ANCHOR="valar-eam-feed.onrender.com"  # EAM_FEED_BASE, in every Missileer image

pass=0; fail=0
ok()  { printf '  PASS  %s\n' "$1"; pass=$((pass+1)); }
no()  { printf '  FAIL  %s\n' "$1"; fail=$((fail+1)); }

extract() {  # strings(1) is not in Git Bash on Windows
  if command -v strings >/dev/null 2>&1; then strings -a "$1"
  else tr -c '[:print:]' '\n' < "$1" | awk 'length($0)>=4'; fi
}

check_file() {  # path, label, want (present|absent)
  local path="$1" label="$2" want="$3" syms n m
  if [ ! -f "$path" ]; then no "$label missing -- build it first"; return; fi
  syms="$(mktemp)"; extract "$path" > "$syms"
  if ! grep -qF "$ANCHOR" "$syms"; then
    no "$label: ANCHOR '$ANCHOR' not found -- the probe can't read this file"; rm -f "$syms"; return
  fi
  for m in "${MARKERS[@]}"; do
    n=$(grep -cF -- "$m" "$syms" || true)
    if [ "$want" = absent ]; then
      [ "$n" -eq 0 ] && ok "$label: no '$m'" || no "$label: '$m' found $n time(s) -- shipping image carries /diag/fb"
    else
      [ "$n" -gt 0 ] && ok "$label: '$m' x$n" || no "$label: '$m' missing -- MISSILEER_DIAG_FB did not take"
    fi
  done
  rm -f "$syms"
}

check() {  # env, want (present|absent)
  local env="$1" want="$2" f
  for f in firmware.elf firmware.bin; do
    check_file ".pio/build/$env/$f" "$env/$f" "$want"
  done
}

if [ "${1:-}" = "--selftest" ]; then
  # Each case runs check_file on a planted fake image and requires a specific verdict. The
  # counters are saved and restored around each case: the selftest grades the verdicts, it
  # does not add the fakes' PASS/FAIL lines to its own score.
  t="$(mktemp -d)"; rc=0
  expect() {  # file, want, expected (pass|fail), description
    local p0=$pass f0=$fail got
    check_file "$1" "selftest" "$2" >/dev/null
    if [ $((fail - f0)) -gt 0 ]; then got=fail; else got=pass; fi
    pass=$p0; fail=$f0
    if [ "$got" = "$3" ]; then echo "selftest ok: $4"; else echo "selftest FAIL: $4 (got $got, want $3)"; rc=1; fi
  }
  printf 'xx%sxx' "$ANCHOR" > "$t/clean.bin"
  expect "$t/clean.bin" absent pass "a clean image passes the shipping check"
  expect "$t/clean.bin" present fail "a clean image fails the bench (positive-control) check"
  all="xx$ANCHOR"
  for m in "${MARKERS[@]}"; do
    # EVERY marker is planted on its own and must be caught on its own -- a marker added to the
    # list but misspelled against the code would otherwise never be exercised.
    printf 'xx%sxx\n%s\n' "$ANCHOR" "$m" > "$t/one.bin"
    expect "$t/one.bin" absent fail "planted '$m' is caught in a shipping image"
    all="$all"$'\n'"$m"
  done
  printf '%s\n' "$all" > "$t/all.bin"
  expect "$t/all.bin" present pass "an image carrying every marker passes the bench check"
  for m in "${MARKERS[@]}"; do
    # The positive control must notice a marker going missing, one at a time.
    printf '%s\n' "$all" | grep -vxF -- "$m" > "$t/short.bin"
    expect "$t/short.bin" present fail "a bench image missing '$m' is caught"
  done
  printf 'no anchor here' > "$t/blind.bin"
  expect "$t/blind.bin" absent fail "an anchor-less file never passes the shipping check"
  expect "$t/missing.bin" absent fail "a missing file never passes the shipping check"
  rm -rf "$t"
  exit "$rc"
fi

if [ "${1:-}" = "--env" ]; then
  [ $# -eq 2 ] || { echo "usage: $0 --env <missileer-env>"; exit 2; }
  env="$2"
  echo "shipping image $env (must NOT carry /diag/fb):"
  check "$env" absent
  echo "bench image $env-fbdiag (positive control, MUST carry it):"
  check "$env-fbdiag" present
  echo
  echo "$pass passed, $fail failed"
  [ "$fail" -eq 0 ]
  exit
fi

[ $# -eq 0 ] || { echo "usage: $0 [--env <missileer-env> | --selftest]"; exit 2; }
echo "shipping Missileer images (must NOT carry /diag/fb):"
check missileer-s3-128 absent
check missileer-s3-146 absent
echo "bench -fbdiag images (positive control, MUST carry it):"
check missileer-s3-128-fbdiag present
check missileer-s3-146-fbdiag present
echo
echo "$pass passed, $fail failed"
[ "$fail" -eq 0 ]
