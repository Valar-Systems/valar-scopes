#!/usr/bin/env bash
# Verify the blipscope-s3-128-fetchtrace IMAGE, not the ini that was meant to
# produce it.
#
# WHY THIS EXISTS. The env's whole value is being the shipping image plus one
# instrumentation flag. Every way that can silently go wrong is invisible in
# platformio.ini and obvious in the ELF:
#
#   * PlatformIO REPLACES build_flags on `extends` rather than appending, so a
#     derived env that forgets ${cloud.prod} compiles clean with NO backend at
#     all -- this repo has shipped that exact bug (see CLAUDE.md).
#   * -U lands in CCFLAGS and -D in CPPDEFINES, so an undefine beats a LATER
#     redefine whatever order the file reads in.
#   * A stray ${cloud.staging} points the board at a different backend, which
#     for a controlled comparison against .32 silently changes the variable.
#   * -DSOAK_TEST arriving by inheritance would arm SoakHarness's synthetic taps
#     -- instrumentation that perturbs the thing it observes.
#
# Exit 0 = the image is the shipping build plus FETCH_TRACE and nothing else.
set -uo pipefail

ENV_NAME="blipscope-s3-128-fetchtrace"
# Optional ELF override: the seam that keeps the FAILING branches reachable once
# the real image is healthy. A gate nobody can make fail is a gate nobody can
# check. `bash scripts/check-fetchtrace.sh .pio/build/blipscope-s3-128-densesky/firmware.elf`
# must report FAIL on the backend and on SoakHarness -- that env is staging +
# SOAK_TEST, i.e. precisely the two mistakes this script exists to catch.
ELF="${1:-.pio/build/${ENV_NAME}/firmware.elf}"

PROD_HOST="scopes.valarsystems.com"
STAGING_HOST="scopes-staging.valarsystems.com"

pass=0; fail=0
ok()  { printf '  \033[32mPASS\033[0m  %s\n' "$1"; pass=$((pass+1)); }
no()  { printf '  \033[31mFAIL\033[0m  %s\n' "$1"; fail=$((fail+1)); }

if [ ! -f "$ELF" ]; then
  echo "no ELF at $ELF -- build it first:"
  echo "    pio run -e $ENV_NAME"
  exit 2
fi

# Extract once; every assertion reads this same file so an extraction problem
# cannot make one check pass and another fail for unrelated reasons.
#
# `strings` is NOT present in Git Bash on Windows, which is where this actually
# runs. Discovered by this script refusing to report on its first run rather than
# by it cheerfully announcing that the staging host and SoakHarness were both
# absent -- which is exactly what a missing extractor would have "proved".
SYMS="$(mktemp)"; trap 'rm -f "$SYMS"' EXIT
if command -v strings >/dev/null 2>&1; then
  strings -a "$ELF" > "$SYMS" 2>/dev/null
else
  tr -c '[:print:]' '\n' < "$ELF" 2>/dev/null | awk 'length($0)>=4' > "$SYMS"
fi
if [ ! -s "$SYMS" ]; then
  echo "FATAL: could not extract strings from $ELF -- every 'absent' below would be"
  echo "a lie. Refusing to report."
  exit 2
fi

echo "checking $ELF"
echo

# ---------------------------------------------------------------------------
# ANCHOR FIRST. A probe that reports absence must prove it can observe presence;
# otherwise "nothing found" is indistinguishable from "cannot look". Both of the
# absence checks below would pass trivially against an unreadable binary.
# ---------------------------------------------------------------------------
if grep -qF "Blipscope" "$SYMS"; then
  ok "ANCHOR: ELF is readable and contains known firmware strings"
else
  no "ANCHOR: cannot find even 'Blipscope' -- the absence checks below mean NOTHING"
  echo
  echo "Stopping: an unvalidated probe reporting 'all clear' is the worst outcome here."
  exit 2
fi

# ---- the backend must be production, and staging must be nowhere near it -----
#
# MATCH THE WHOLE LINE, NOT A SUBSTRING, and the reason is not pedantry: the
# first version of this check used `grep -F` and PASSED on the staging build.
# The production host is compiled into EVERY image regardless of backend,
# because the enrolment popup always targets production
# (ConfigurationWebServer.cpp:261) -- so a substring search was reading the
# enrol URL and calling it the feed backend. That is the single assertion this
# script exists for, and it was a false positive; only running it against a
# known-bad image exposed that.
#
# CLOUD_FEED_BASE lands in .rodata as its own NUL-delimited string, so it is a
# whole line in the extraction. The enrol URLs are embedded mid-line inside the
# config page's one-line JS blob and cannot match -x.
grep -qxF "https://$PROD_HOST" "$SYMS" \
  && ok "backend is PRODUCTION ($PROD_HOST)" \
  || no "PRODUCTION feed base missing -- env lost \${cloud.prod}; the board would have NO backend"

grep -qxF "https://$STAGING_HOST" "$SYMS" \
  && no "STAGING feed base present ($STAGING_HOST) -- wrong backend, comparison with .32 is void" \
  || ok "no staging feed base"

# ---- the instrumentation must actually be compiled in -----------------------
# Format strings from the three FETCH_TRACE printfs. If FETCH_TRACE did not take,
# the env is just a slow way to reflash the shipping image and the investigation
# would produce no new data while looking like it ran.
grep -qF "[soak-state]" "$SYMS" \
  && ok "FETCH_TRACE took: [soak-state] present" \
  || no "FETCH_TRACE did NOT take: no [soak-state] -- this image logs nothing new"

grep -qF "task: req kind=" "$SYMS" \
  && ok "request-taken trace present" \
  || no "request-taken trace missing"

grep -qF "task: done ok=" "$SYMS" \
  && ok "request-done trace present" \
  || no "request-done trace missing"

# ---- and the harness must NOT be ------------------------------------------
# SoakHarness synthesises taps. Its presence would mean the board pokes itself
# while we measure an idle board.
if grep -qE "SoakHarness|soak-gesture|NextTouchSample" "$SYMS"; then
  no "SoakHarness linked in -- synthetic taps would perturb the measurement"
else
  ok "SoakHarness absent: the board will sit genuinely idle"
fi

echo
printf 'fetch-trace image: %d passed, %d failed\n' "$pass" "$fail"
[ "$fail" -eq 0 ] || exit 1
