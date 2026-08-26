#!/usr/bin/env bash
# check-adsbdb-out.sh -- the launch gate: no flashable image may contain a
# reachable adsbdb endpoint.
#
#   ./scripts/check-adsbdb-out.sh            # every CI matrix env
#   ./scripts/check-adsbdb-out.sh s3-128     # one env, by substring
#
# =============================================================================
# WHY THIS SCANS THE ARTIFACT AND NOT THE SOURCE
#
# "Delete the call sites" is a statement about source. What ships is a linked
# image, and the two disagreed twice while this was being written:
#
#   1. The config page's help text carried "api.adsbdb.com" as an HTML STRING
#      LITERAL. Every call site was gone and the URL still shipped. A source grep
#      for call sites passes; the image does not.
#
#   2. The first version of this gate planted an UNREFERENCED probe string to
#      prove the scanner worked, and the scan reported 0 -- because --gc-sections
#      had discarded it. Present in the .o, absent from the .elf and the .bin.
#
# (2) is why the control is referenced from a runtime-dependent branch the
# compiler cannot fold away, and it sharpens what this gate proves: no REACHABLE
# adsbdb endpoint. A string the linker dropped was never a call site.
#
# =============================================================================
# WHY EVERY SKU, NOT ONE
#
# Eleven flashable images, different build_src_filter sets, different link
# results. Proving one and assuming ten is the sampling mistake that let a single
# dropped KV key hide inside a 619,103-key load every other check called clean.
#
# The control is planted PER ENV for the same reason: a scanner proven against
# one binary has not been proven against the next.
# =============================================================================
set -uo pipefail

# Resolve python by RUNNING each candidate, not by asking whether it exists. On
# Windows `command -v python3` finds the Microsoft Store stub -- present on PATH,
# and it errors the moment you execute it. Existence and working are different
# questions, and this gate was bitten once by treating them as one.
PY=""
for cand in python3 python py; do
  if command -v "$cand" >/dev/null 2>&1 && "$cand" -c "pass" >/dev/null 2>&1; then
    PY="$cand"; break
  fi
done
if [ -z "$PY" ]; then
  echo "FATAL: no WORKING python on PATH; the gate control cannot be planted." >&2
  exit 2
fi

cd "$(dirname "$0")/.." || exit 1
FILTER="${1:-}"

# Transcribed from .github/workflows -- the CI matrix is the other side of this
# contract. If CI gains a row and this does not, that SKU ships unscanned.
ENVS="
blipscope-s3-128
blipscope-s3-146
blipscope-pro-s3-21
blipscope-pro-s3-175-amoled
missileer-s3-146
orbitscope-s3-146
quakescope-s3-146
quillscope-s3-146
reelscope-s3-146
claudescope-s3-146
speedscope-s3-146
"

# Endpoint patterns. The bare token "adsbdb" is deliberately NOT one of these.
PATTERNS="api.adsbdb.com adsbdb.com v0/aircraft v0/callsign"

# THE ONE EXPECTED BARE HIT, ANNOTATED SO NOBODY RE-INVESTIGATES IT.
#
#   src/ConfigMigration.cpp:   if (det == "adsbdb")
#
# ConfigMigration rev 4 rewrites a stored local-details of "adsbdb" to "cloud".
# That literal is how the migration RECOGNISES the value it exists to retire, so
# it is the opposite of a call site. Deleting it would leave every device that
# opted into detail lookups holding a string that maps to nothing, which
# AircraftManager parses as Off -- silently removing card details from exactly
# the population that asked for them.
#
# Expect 1. A 2 means something new appeared and is worth reading.
EXPECTED_BARE=1

PLANT_FILE="src/ConfigMigration.cpp"
PLANT_ANCHOR='const int stored = prefs.getInt("cfg-rev", 0);'
PLANT_LINE='    if (stored < -12345) Serial.println("api.adsbdb.com/v0/aircraft/"); // GATE CONTROL'

plant() {
  cp "$PLANT_FILE" "$PLANT_FILE.gatebak" || return 1
  "$PY" -c '
import sys
p, anchor, line = sys.argv[1], sys.argv[2], sys.argv[3]
s = open(p, encoding="utf-8").read()
if anchor not in s:
    sys.exit("plant anchor missing")
open(p, "w", encoding="utf-8").write(s.replace(anchor, anchor + "\n" + line, 1))
' "$PLANT_FILE" "$PLANT_ANCHOR" "$PLANT_LINE"
}
unplant() {
  if [ -f "$PLANT_FILE.gatebak" ]; then mv -f "$PLANT_FILE.gatebak" "$PLANT_FILE"; fi
}
# Never leave the plant behind, whatever happens.
trap unplant EXIT INT TERM

count_in() {
  # grep -c PRINTS a count and EXITS 1 when that count is zero. `|| echo 0`
  # therefore emits TWO lines and breaks the arithmetic downstream.
  local n
  n="$(grep -ac "$2" "$1" 2>/dev/null | head -1)"
  echo "${n:-0}"
}

fail=0
scanned=0
printf '%-30s %-8s %-9s %-6s %s\n' "ENV" "CONTROL" "ENDPOINT" "BARE" "VERDICT"
printf -- '--------------------------------------------------------------------------\n'

for env in $ENVS; do
  if [ -n "$FILTER" ]; then
    case "$env" in *"$FILTER"*) ;; *) continue ;; esac
  fi
  BIN=".pio/build/$env/firmware.bin"

  # ---- 1. plant the control and require the scanner to FIND it -------------
  if ! plant; then
    printf '%-30s %-8s %-9s %-6s %s\n' "$env" "ERR" "-" "-" "PLANT FAILED"
    fail=$((fail+1)); unplant; continue
  fi
  pio run -e "$env" >/dev/null 2>&1
  CONTROL="MISSING"
  if [ -f "$BIN" ] && grep -aq "api.adsbdb.com" "$BIN"; then CONTROL="found"; fi
  unplant

  # ---- 2. the real scan, on a clean rebuild -------------------------------
  pio run -e "$env" >/dev/null 2>&1
  if [ ! -f "$BIN" ]; then
    printf '%-30s %-8s %-9s %-6s %s\n' "$env" "$CONTROL" "-" "-" "BUILD FAILED"
    fail=$((fail+1)); continue
  fi

  endpoint=0
  for pat in $PATTERNS; do
    endpoint=$(( endpoint + $(count_in "$BIN" "$pat") ))
  done
  bare="$(count_in "$BIN" "adsbdb")"

  verdict="PASS"
  # A control that cannot see a planted string makes every zero beside it meaningless.
  if [ "$CONTROL" != "found" ]; then
    verdict="UNTRUSTWORTHY (control blind)"; fail=$((fail+1))
  elif [ "$endpoint" -ne 0 ]; then
    verdict="FAIL (endpoint in image)"; fail=$((fail+1))
  elif [ "$bare" -gt "$EXPECTED_BARE" ]; then
    verdict="FAIL (unexpected bare hit)"; fail=$((fail+1))
  fi

  scanned=$((scanned+1))
  printf '%-30s %-8s %-9s %-6s %s\n' "$env" "$CONTROL" "$endpoint" "$bare" "$verdict"
done

printf -- '--------------------------------------------------------------------------\n'
printf 'BARE=%d is EXPECTED: the ConfigMigration literal that retires the old\n' "$EXPECTED_BARE"
printf 'local-details value. A recogniser, not a call site. Do not re-investigate.\n\n'

# A GATE THAT SCANNED NOTHING MUST NOT REPORT SUCCESS.
#
# An earlier run of this script printed GATE PASSED after an arithmetic error
# killed the only row it was going to emit. Zero failures and zero measurements
# are indistinguishable in a failure counter, and the passing shape is the one a
# broken gate produces most easily -- so the row count is asserted too.
if [ "$scanned" -eq 0 ]; then
  printf 'GATE INCONCLUSIVE: 0 images scanned. This is NOT a pass.\n' >&2
  exit 2
fi

printf 'scanned %d image(s).\n' "$scanned"
if [ "$fail" -eq 0 ]; then
  printf 'GATE PASSED: no flashable image contains a reachable adsbdb endpoint.\n'
  exit 0
fi
printf 'GATE FAILED: %d problem(s) above.\n' "$fail"
exit 1
