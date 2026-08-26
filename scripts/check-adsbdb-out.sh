#!/usr/bin/env bash
# check-adsbdb-out.sh -- the launch gate: no flashable image may contain a
# reachable adsbdb endpoint.
#
#   ./scripts/check-adsbdb-out.sh             # every image CI builds
#   ./scripts/check-adsbdb-out.sh s3-128      # one env, by substring
#   ./scripts/check-adsbdb-out.sh --selftest  # prove the matrix parser; builds nothing
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
# The same asymmetry is why a source grep is not even a weak substitute here:
# main's AircraftManager.cpp mentions "adsbdb" on 30 lines and every one of them
# is prose explaining what was removed. Grep says 30; the image says 0.
#
# =============================================================================
# WHY EVERY SKU, NOT ONE
#
# Eleven flashable images, different build_src_filter sets, different link
# results. Proving one and assuming ten is the sampling mistake that let a single
# dropped KV key hide inside a 619,103-key load every other check called clean.
#
# The control is planted PER ENV for the same reason: a scanner proven against
# one binary has not been proven against the next. On the first full sweep that
# paid immediately -- blipscope-pro-s3-175-amoled failed to build, and it failed
# for a reason introduced by the very change this gate was written to check.
#
# =============================================================================
# WHERE THE ENV LIST COMES FROM
#
# It is PARSED from .github/workflows/firmware.yml. It used to be transcribed
# here under a comment reading "if CI gains a row and this does not, that SKU
# ships unscanned" -- which is a rule in a comment, and this repo has a long
# record of those not holding. The set of flashable images is DEFINED by the CI
# matrix, so the gate reads the matrix rather than a copy of it.
#
# A broken parser makes this gate quieter, never louder: a SKU it fails to see is
# reported as neither PASS nor FAIL -- it simply is not in the table, and a table
# of all-PASS rows reads as success whatever is missing from it. So the parse is
# proven by --selftest, floored, and anchored before any of its output is used.
# =============================================================================
set -uo pipefail

cd "$(dirname "$0")/.." || exit 1

MATRIX=".github/workflows/firmware.yml"

# A sanity FLOOR, not a transcribed list. "Everything is missing" is the most
# likely shape of a broken parser and the one that reads as success, so the
# cheapest guard against it is a number the real matrix comfortably clears.
MIN_MATRIX_ENVS=8

# The anchor: the default SKU. A parse that does not contain it is not a parse of
# this matrix, however many rows it returned. Renaming the default SKU is
# supposed to fail here.
ANCHOR_ENV="blipscope-s3-128"

# The matrix's `- { env: NAME ... }` rows.
#
# Commented rows are EXCLUDED (the leading `-` is anchored): a row behind a `#`
# is not built by CI, so scanning it would be scanning a stale local artifact.
# Rows with no `slug:` ARE included -- those are compile-only SKUs, built and
# scanned but never published. An image you can flash is an image that must be
# clean, whether or not a release ever carries it.
parse_matrix_envs() {
  sed -n 's/^[[:space:]]*-[[:space:]]*{[[:space:]]*env:[[:space:]]*\([A-Za-z0-9_-][A-Za-z0-9_-]*\).*/\1/p' "$1"
}

# ---------------------------------------------------------------------------
# LEFTOVER-PLANT RECOVERY. RUNS IN EVERY MODE, BEFORE ANYTHING ELSE.
#
# THE TRAP IS NOT ENOUGH. `trap unplant EXIT INT TERM` (armed further down) reads
# like a guarantee and is not one: it catches Ctrl-C and SIGTERM, and SIGKILL
# cannot be trapped at all.
#
# Observed 2026-08-26 rather than reasoned about. An eleven-SKU sweep was
# force-killed mid-run, and the working tree was left holding
#
#     if (stored < -12345) Serial.println("api.adsbdb.com/v0/aircraft/");
#
# -- a live, reachable adsbdb call site, planted by the very script whose job is
# to prove no such thing exists.
#
# The consequence is worse than an untidy tree. The next person builds, the image
# carries the endpoint, and the one thing that would catch it is this gate --
# which they have no reason to re-run, because the last thing they saw it do was
# pass. A tool that can leave behind the defect it looks for is worse than no
# tool, because it also supplies the reassurance.
#
# So recovery happens at STARTUP, where no signal can prevent it, in EVERY mode
# including --selftest. Loudly: a tree that silently repaired itself teaches
# nobody that the interruption mattered.
#
# One more thing that cost real time, recorded because it is not visible from
# here. The first attempt to stop that sweep REPORTED SUCCESS and did not stop
# it: two `check-adsbdb-out.sh` processes kept running, planting and unplanting
# this file underneath the investigation. The plant sitting in the tree was read
# as a trap that had failed to fire, when it was simply a run still going. If you
# find a plant here, check for a live process before concluding anything --
# `ps -W | grep bash`, or Get-CimInstance Win32_Process on the command line.
# ---------------------------------------------------------------------------
PLANT_FILE="src/ConfigMigration.cpp"

unplant() {
  if [ -f "$PLANT_FILE.gatebak" ]; then mv -f "$PLANT_FILE.gatebak" "$PLANT_FILE"; fi
}

if [ -f "$PLANT_FILE.gatebak" ]; then
  echo "NOTE: $PLANT_FILE.gatebak exists -- a previous gate run was interrupted mid-plant." >&2
  echo "      Restoring $PLANT_FILE before doing anything else." >&2
  unplant
fi
# Prove the restore worked rather than assuming the file it put back was clean.
# If the control is still in the source, stop: every build below would carry it,
# and the gate would be measuring its own plant.
if grep -q "GATE CONTROL" "$PLANT_FILE" 2>/dev/null; then
  echo "FATAL: $PLANT_FILE still contains the gate control string, and no backup" >&2
  echo "       was available to restore. Recover it with:" >&2
  echo "           git checkout -- $PLANT_FILE" >&2
  echo "       Do not build or flash from this tree until you have." >&2
  exit 2
fi

# --- the parser's own proof ---------------------------------------------------
# Runs in CI ahead of the builds. Every case here is a way the parse could go
# WRONG QUIETLY; none of them would make a build fail or a scan report a hit.
#
# THE ASSERTION IS AN EXACT LIST, AND THAT IS NOT STYLE.
#
# The first draft asserted presence/absence per token -- `case "$got" in
# *" charlie-retired "*)`. Rehearsed against a parser broken on purpose (the `^`
# anchor removed, so commented rows match), it reported eight PASSes and
# SELFTEST PASSED. sed's s/// rewrites only the MATCHED span and prints the whole
# line, so a commented row came back as `          #charlie-retired` -- the token
# with a hash welded to its front. It was there, and the check looking for it
# could not see it, because the check looked for the SHAPE it expected the
# failure to take.
#
# So: normalise, and compare the WHOLE list against the whole expected list.
# Anything the parser emits that should not be there fails the comparison
# whatever it is wearing.
selftest() {
  tmp="$(mktemp)" || return 2
  {
    printf '    strategy:\n      matrix:\n        include:\n'
    printf '          - { env: alpha-s3-128,  slug: s3-128 }\n'
    printf '          - { env: bravo-s3-146 }\n'
    printf '          # - { env: charlie-retired, slug: retired }\n'
    printf '          #- { env: delta-retired }\n'
    printf '    steps:\n'
    printf '      - name: a step, not a matrix row, mentioning env: echo-not-a-row\n'
    printf '      - run: pio run -e foxtrot-not-a-row\n'
  } > "$tmp"
  # The fixture holds six rows and exactly two are images CI builds:
  #   alpha-s3-128    published row            -> must be scanned
  #   bravo-s3-146    slug-less, compile-only  -> must be scanned
  #   charlie-retired commented, space after # -> not built, must not appear
  #   delta-retired   commented, no space      -> not built, must not appear
  #   echo-not-a-row  a step name with "env:"  -> not a row at all
  #   foxtrot-not-a-row  a pio call in run:    -> not a row at all
  want="alpha-s3-128 bravo-s3-146"
  got="$(parse_matrix_envs "$tmp" | tr -d '\r' | tr '\n' ' ' | sed 's/  */ /g; s/^ //; s/ *$//')"
  rm -f "$tmp"

  rc=0
  if [ "$got" = "$want" ]; then
    printf '  PASS  fixture parses to exactly: %s\n' "$got"
  else
    printf '  FAIL  fixture parse mismatch\n'
    printf '          want: [%s]\n' "$want"
    printf '          got:  [%s]\n' "$got"
    rc=1
  fi

  # And against the real file, the three guards the live path relies on. The
  # fixture proves the regex; these prove the regex is pointed at the file the
  # builds actually come from.
  real="$(parse_matrix_envs "$MATRIX")"
  shape_ok=1
  for e in $real; do
    case "$e" in
      *[!A-Za-z0-9_-]*) printf '  FAIL  parsed token is not an env name: [%s]\n' "$e"; shape_ok=0; rc=1 ;;
    esac
  done
  [ "$shape_ok" -eq 1 ] && printf '  PASS  every parsed token is a well-formed env name\n'
  n=0
  for e in $real; do n=$((n + 1)); done
  if [ "$n" -ge "$MIN_MATRIX_ENVS" ]; then
    printf '  PASS  %s parses to %d env(s), floor %d\n' "$MATRIX" "$n" "$MIN_MATRIX_ENVS"
  else
    printf '  FAIL  %s parses to only %d env(s), floor %d\n' "$MATRIX" "$n" "$MIN_MATRIX_ENVS"
    rc=1
  fi
  case " $(printf '%s ' $real)" in
    *" $ANCHOR_ENV "*) printf '  PASS  anchor %s is in the parse\n' "$ANCHOR_ENV" ;;
    *) printf '  FAIL  anchor %s missing -- this is not the matrix\n' "$ANCHOR_ENV"; rc=1 ;;
  esac

  if [ "$rc" -eq 0 ]; then
    printf 'SELFTEST PASSED\n'
  else
    printf 'SELFTEST FAILED\n' >&2
  fi
  return "$rc"
}

if [ "${1:-}" = "--selftest" ]; then
  selftest
  exit $?
fi

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

FILTER="${1:-}"

if [ ! -f "$MATRIX" ]; then
  echo "FATAL: $MATRIX not found; the env list has no source." >&2
  exit 2
fi
ENVS="$(parse_matrix_envs "$MATRIX")"
n_matrix=0
for e in $ENVS; do n_matrix=$((n_matrix + 1)); done
if [ "$n_matrix" -lt "$MIN_MATRIX_ENVS" ]; then
  echo "FATAL: parsed only $n_matrix env(s) from $MATRIX, floor is $MIN_MATRIX_ENVS." >&2
  echo "       Refusing to scan -- a short list reports the SKUs it missed as neither pass nor fail." >&2
  exit 2
fi
case " $(printf '%s ' $ENVS)" in
  *" $ANCHOR_ENV "*) ;;
  *)
    echo "FATAL: anchor env $ANCHOR_ENV absent from the parse of $MATRIX." >&2
    echo "       Whatever was parsed, it is not this project's build matrix." >&2
    exit 2
    ;;
esac
# A token that is not an env name means the parse is off, and it must stop HERE
# rather than downstream. Fed to the loop it becomes `pio run -e '#charlie'`,
# which fails to build and prints BUILD FAILED -- a row that reads as a broken
# SKU when what is broken is this script. See the selftest's note on why the
# deliberately-broken parser produced exactly that token.
for e in $ENVS; do
  case "$e" in
    *[!A-Za-z0-9_-]*)
      echo "FATAL: parsed [$e] from $MATRIX, which is not an env name." >&2
      echo "       The matrix parser is wrong; nothing below it can be trusted." >&2
      exit 2
      ;;
  esac
done

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

# PLANT_FILE and unplant() are defined near the top, with the startup recovery
# that has to run before any mode dispatches.
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
# Still worth arming: it covers the ordinary Ctrl-C, which is the common case.
# It is the belt; the startup recovery near the top of this file is the braces,
# and the comment there explains why the belt alone was not enough.
trap unplant EXIT INT TERM

count_in() {
  # grep -c PRINTS a count and EXITS 1 when that count is zero. `|| echo 0`
  # therefore emits TWO lines and breaks the arithmetic downstream.
  local n
  n="$(grep -ac "$2" "$1" 2>/dev/null | head -1)"
  echo "${n:-0}"
}

# ---------------------------------------------------------------------------
# BUILD OUTPUT IS KEPT, AND THE EXIT STATUS IS READ.
#
# Both because of one row. On 2026-08-26 a clean eleven-SKU sweep returned
#
#     orbitscope-s3-146   MISSING   0   1   UNTRUSTWORTHY (control blind)
#
# and the gate could say nothing else about it: `pio run ... >/dev/null 2>&1`
# had thrown away the only evidence, and the exit status was never looked at.
# Rebuilding that env by hand, planted, put the control in the image on the
# first try -- so the failure was transient, and establishing even that much
# took a manual reproduction the script should have made unnecessary.
#
# The repo rule this broke is its own: never filter the output of a command you
# are running to detect failure. It was being broken INSIDE the gate, on the
# command whose failure the gate exists to notice.
#
# The exit status matters separately from the output. `pio run` failing leaves
# the PREVIOUS binary in .pio, and a stale binary answers a grep perfectly
# happily. The control catches the dangerous direction (a stale clean image
# reads as MISSING, as above) but not a stale PLANTED one, which would report
# `found` on a build that never happened -- a control believed for the wrong
# reason. Reading the status closes that.
# ---------------------------------------------------------------------------
LOGDIR=".pio/gatelogs"
mkdir -p "$LOGDIR" 2>/dev/null

say_why() {
  # $1 = log file, $2 = what it was doing. Print enough to place the failure.
  printf '\n  --- %s: last 25 lines of %s ---\n' "$2" "$1"
  if [ -f "$1" ]; then tail -25 "$1" | sed 's/^/  | /'; else printf '  | (no log written)\n'; fi
  printf '\n'
}

fail=0
scanned=0
matched=0
printf '%-30s %-8s %-9s %-6s %s\n' "ENV" "CONTROL" "ENDPOINT" "BARE" "VERDICT"
printf -- '--------------------------------------------------------------------------\n'

for env in $ENVS; do
  if [ -n "$FILTER" ]; then
    case "$env" in *"$FILTER"*) ;; *) continue ;; esac
  fi
  matched=$((matched + 1))
  BIN=".pio/build/$env/firmware.bin"
  LOG_PLANT="$LOGDIR/$env.control.log"
  LOG_REAL="$LOGDIR/$env.real.log"

  # ---- 1. plant the control and require the scanner to FIND it -------------
  if ! plant; then
    printf '%-30s %-8s %-9s %-6s %s\n' "$env" "ERR" "-" "-" "PLANT FAILED"
    fail=$((fail+1)); unplant; continue
  fi
  plant_rc=0
  pio run -e "$env" > "$LOG_PLANT" 2>&1 || plant_rc=$?
  CONTROL="MISSING"
  if [ -f "$BIN" ] && grep -aq "api.adsbdb.com" "$BIN"; then CONTROL="found"; fi
  unplant

  if [ "$plant_rc" -ne 0 ]; then
    # Whatever the grep said, it read a binary this build did not produce.
    printf '%-30s %-8s %-9s %-6s %s\n' "$env" "n/a" "-" "-" "BUILD FAILED (control phase, rc=$plant_rc)"
    fail=$((fail+1)); say_why "$LOG_PLANT" "$env control build"; continue
  fi

  # ---- 2. the real scan, on a clean rebuild -------------------------------
  # This rebuild is also what CI publishes: firmware-<slug>.bin is copied out of
  # .pio AFTER this gate runs, so the image attached to a release is the exact
  # image scanned here -- and an unplant that silently failed would surface as
  # ENDPOINT=1 on this row rather than as a planted string in a shipped binary.
  real_rc=0
  pio run -e "$env" > "$LOG_REAL" 2>&1 || real_rc=$?
  if [ "$real_rc" -ne 0 ]; then
    printf '%-30s %-8s %-9s %-6s %s\n' "$env" "$CONTROL" "-" "-" "BUILD FAILED (rc=$real_rc)"
    fail=$((fail+1)); say_why "$LOG_REAL" "$env build"; continue
  fi
  if [ ! -f "$BIN" ]; then
    printf '%-30s %-8s %-9s %-6s %s\n' "$env" "$CONTROL" "-" "-" "NO BINARY (build reported success)"
    fail=$((fail+1)); say_why "$LOG_REAL" "$env build"; continue
  fi

  endpoint=0
  for pat in $PATTERNS; do
    endpoint=$(( endpoint + $(count_in "$BIN" "$pat") ))
  done
  bare="$(count_in "$BIN" "adsbdb")"

  verdict="PASS"
  why=""
  # A control that cannot see a planted string makes every zero beside it meaningless.
  if [ "$CONTROL" != "found" ]; then
    verdict="UNTRUSTWORTHY (control blind)"; fail=$((fail+1)); why="$LOG_PLANT"
  elif [ "$endpoint" -ne 0 ]; then
    verdict="FAIL (endpoint in image)"; fail=$((fail+1))
  elif [ "$bare" -gt "$EXPECTED_BARE" ]; then
    verdict="FAIL (unexpected bare hit)"; fail=$((fail+1))
  fi

  scanned=$((scanned+1))
  printf '%-30s %-8s %-9s %-6s %s\n' "$env" "$CONTROL" "$endpoint" "$bare" "$verdict"
  # The control built AND linked cleanly and the planted string still is not in
  # the image -- that is the case worth reading a log over, so print it.
  if [ -n "$why" ]; then say_why "$why" "$env control build (exit 0, string absent)"; fi
done

printf -- '--------------------------------------------------------------------------\n'
printf 'BARE=%d is EXPECTED: the ConfigMigration literal that retires the old\n' "$EXPECTED_BARE"
printf 'local-details value. A recogniser, not a call site. Do not re-investigate.\n\n'

# A FILTER THAT MATCHED NOTHING IS A TYPO, NOT A CLEAN SWEEP.
if [ -n "$FILTER" ] && [ "$matched" -eq 0 ]; then
  printf 'GATE INCONCLUSIVE: filter "%s" matched none of the %d matrix env(s).\n' "$FILTER" "$n_matrix" >&2
  exit 2
fi

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

printf 'scanned %d of %d matrix image(s).\n' "$scanned" "$n_matrix"
if [ "$fail" -eq 0 ]; then
  printf 'GATE PASSED: no flashable image contains a reachable adsbdb endpoint.\n'
  exit 0
fi
printf 'GATE FAILED: %d problem(s) above.\n' "$fail"
exit 1
