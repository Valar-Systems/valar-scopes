#!/usr/bin/env bash
# /diag/fb is in the Missileer -fbdiag bench images and NOWHERE in the shipping ones.
#
# Checks the BINARIES (firmware.elf and the firmware.bin that is actually flashed), not
# the ini that was meant to produce them. Build all four first:
#     pio run -e missileer-s3-128 -e missileer-s3-146 \
#             -e missileer-s3-128-fbdiag -e missileer-s3-146-fbdiag
#
# The presence side is the positive control: "0 occurrences" in a shipping image means
# absent only if the same probe finds the strings in the bench image.
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

check() {  # env, want (present|absent)
  local env="$1" want="$2" f syms n m
  for f in firmware.elf firmware.bin; do
    local path=".pio/build/$env/$f"
    if [ ! -f "$path" ]; then no "$env/$f missing -- build it first"; continue; fi
    syms="$(mktemp)"; extract "$path" > "$syms"
    if ! grep -qF "$ANCHOR" "$syms"; then
      no "$env/$f: ANCHOR '$ANCHOR' not found -- the probe can't read this file"; rm -f "$syms"; continue
    fi
    for m in "${MARKERS[@]}"; do
      n=$(grep -cF -- "$m" "$syms" || true)
      if [ "$want" = absent ]; then
        [ "$n" -eq 0 ] && ok "$env/$f: no '$m'" || no "$env/$f: '$m' found $n time(s) -- shipping image carries /diag/fb"
      else
        [ "$n" -gt 0 ] && ok "$env/$f: '$m' x$n" || no "$env/$f: '$m' missing -- MISSILEER_DIAG_FB did not take"
      fi
    done
    rm -f "$syms"
  done
}

echo "shipping Missileer images (must NOT carry /diag/fb):"
check missileer-s3-128 absent
check missileer-s3-146 absent
echo "bench -fbdiag images (positive control, MUST carry it):"
check missileer-s3-128-fbdiag present
check missileer-s3-146-fbdiag present
echo
echo "$pass passed, $fail failed"
[ "$fail" -eq 0 ]
