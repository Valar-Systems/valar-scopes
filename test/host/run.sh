#!/usr/bin/env bash
#
# Host tests for the pure game translation units.
#
# =============================================================================
# WHY THIS IS A SHELL SCRIPT AND NOT A PlatformIO ENV
#
# `[env:native]` would put the host build under the same tool that manages the
# firmware build, and that is exactly the coupling this is meant to avoid: the
# point of a pure TU is that it needs none of that. A compiler, two files, no
# framework. If this script ever needs a library, something has gone wrong with
# the thing it is testing rather than with the script.
#
# The compiler is PlatformIO's own MinGW package, so nothing is installed
# system-wide and removing ~/.platformio/packages/toolchain-gccmingw32 undoes
# it. It is found rather than assumed, and this script says so out loud when it
# is missing instead of failing with a confusing "g++: not found".
# =============================================================================
#
# Usage:  bash test/host/run.sh
# Exit:   0 all good · 1 a test failed · 2 the rig itself is broken

set -o pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="$ROOT/.pio/hosttest"
mkdir -p "$OUT"

# --- find a host compiler ----------------------------------------------------
CXX="${CXX:-}"
if [ -z "$CXX" ]; then
  for cand in \
    "$HOME/.platformio/packages/toolchain-gccmingw32/bin/g++.exe" \
    "$(command -v g++ 2>/dev/null)" \
    "$(command -v clang++ 2>/dev/null)"; do
    if [ -n "$cand" ] && [ -x "$cand" ]; then CXX="$cand"; break; fi
  done
fi
if [ -z "$CXX" ]; then
  echo "FAIL: no host C++ compiler."
  echo "      Install PlatformIO's self-contained one:  pio pkg install -g -p windows_x86"
  echo "      or set CXX to a g++/clang++ on PATH."
  exit 2
fi
echo "compiler: $CXX"
"$CXX" --version | head -1

# The include path is the gate. `src/game` and nothing else: no Arduino core, no
# ESP-IDF, no LovyanGFX. Adding a -I here is how the guarantee would be lost, so
# any addition needs a reason written next to it.
INCLUDES="-I$ROOT/src/game"
# ONLY for the GameFormat test binary, and deliberately NOT in $INCLUDES: the
# generated fixture is the SERVER's output, and src/game must not be able to
# reach it. A firmware TU that could include it would have the expected answers
# available to the code being graded, which is the end of the test.
FIXTURE_INCLUDES="-I$ROOT/test/fixtures"
# -static-*: the MinGW build otherwise needs libstdc++-6.dll and
# libgcc_s_dw2-1.dll from the toolchain's bin/ on PATH, and without them the
# test binary exits 127 -- "error while loading shared libraries" -- which is a
# non-zero exit that reads exactly like a failing test. A self-contained binary
# removes the ambiguity rather than papering over it with a PATH export.
FLAGS="-std=c++11 -Wall -Wextra -Werror -O1 -static-libgcc -static-libstdc++"

fail=0

# --- 1. the tests ------------------------------------------------------------
echo
echo "== DrillMachine =="
if ! "$CXX" $FLAGS $INCLUDES \
      "$ROOT/src/game/DrillMachine.cpp" \
      "$ROOT/test/host/test_drill_machine.cpp" \
      -o "$OUT/test_drill_machine.exe" 2>"$OUT/build.log"; then
  echo "FAIL: the drill tests did not compile"
  cat "$OUT/build.log"
  exit 2
fi
# `set +e` and NOT a matching `set -e` afterwards: this script never had
# errexit, and turning it on here silently killed the run at the first `grep`
# that found nothing -- which is the NORMAL case for the forbidden-symbol scan.
# The script then exited 1 having printed no reason, i.e. it read as "a test
# failed", which is precisely the confusion this block was added to remove.
set +e
"$OUT/test_drill_machine.exe"
rc=$?
if [ "$rc" -eq 127 ] || [ "$rc" -gt 2 ]; then
  # A BINARY THAT WILL NOT LAUNCH IS NOT A FAILING TEST, and reporting it as
  # one sends the reader to the wrong place. 127 is the missing-DLL case; a
  # crash is anything above the suite own codes (1 = assertions failed,
  # 2 = the suite did not run).
  echo "FAIL: the test binary did not run (exit $rc). This is the RIG, not the code."
  exit 2
elif [ "$rc" -ne 0 ]; then
  fail=1
fi

echo
echo
echo "== ConfigMigration =="
# Pure half only: Apply() touches NVS and cannot run here. The RULE is what
# matters and the rule is a constexpr predicate, so it is graded off-device --
# same split as GameFormat.
if ! "$CXX" $FLAGS $INCLUDES \
      "$ROOT/test/host/test_config_migration.cpp" \
      -o "$OUT/test_config_migration.exe" 2>"$OUT/build.log"; then
  echo "FAIL: the config-migration test did not compile"
  cat "$OUT/build.log"
  exit 2
fi
"$OUT/test_config_migration.exe"
rc=$?
if [ "$rc" -eq 127 ] || [ "$rc" -gt 2 ]; then
  echo "FAIL: the binary did not run (exit $rc). This is the RIG, not the code."
  exit 2
elif [ "$rc" -ne 0 ]; then
  fail=1
fi

echo
echo "== StarvationPolicy =="
# Pure predicate, graded against values captured off two real boards during the
# 2026-08-24 A/B soak. It is host-tested rather than bench-tested on purpose: the
# only live reproduction of the bug is COM119 in its current fragmented state,
# and flashing it to try the fix would reboot it and destroy the fixture.
if ! "$CXX" $FLAGS $INCLUDES       "$ROOT/test/host/test_starvation_policy.cpp"       -o "$OUT/test_starvation_policy.exe" 2>"$OUT/build.log"; then
  echo "FAIL: the starvation-policy test did not compile"
  cat "$OUT/build.log"
  exit 2
fi
"$OUT/test_starvation_policy.exe"
rc=$?
if [ "$rc" -eq 127 ] || [ "$rc" -gt 2 ]; then
  echo "FAIL: the binary did not run (exit $rc). This is the RIG, not the code."
  exit 2
elif [ "$rc" -ne 0 ]; then
  fail=1
fi

echo "== GameFormat (device/server agreement) =="
#
# The fixture is generated by the OTHER side of the contract -- valar-eam-feed's
# scripts/emit-device-fixture.ts, which fetches the deployed /config and calls
# the server's own formatter for every row. This suite never touches the
# network: the live half is a separate scheduled alarm, because a network call
# inside a unit suite becomes a retry, then a skip, then nothing.
if [ ! -f "$ROOT/test/fixtures/game_config_fixture.h" ]; then
  # NOT skipped. A missing fixture means the agreement claim cannot be made at
  # all, and a suite that quietly drops its only cross-repo check while printing
  # a pass is the failure this project keeps writing down.
  echo "FAIL: test/fixtures/game_config_fixture.h is missing, so the agreement"
  echo "      claim cannot be made. Regenerate it from the server repo --"
  echo "      see test/fixtures/README.md for the command."
  exit 2
fi
if ! "$CXX" $FLAGS $INCLUDES $FIXTURE_INCLUDES \
      "$ROOT/src/game/GameFormat.cpp" \
      "$ROOT/test/host/test_game_format.cpp" \
      -o "$OUT/test_game_format.exe" 2>"$OUT/build_fmt.log"; then
  echo "FAIL: the format tests did not compile"
  cat "$OUT/build_fmt.log"
  exit 2
fi
"$OUT/test_game_format.exe"
rc=$?
if [ "$rc" -eq 127 ] || [ "$rc" -gt 2 ]; then
  echo "FAIL: the format test binary did not run (exit $rc). This is the RIG, not the code."
  exit 2
elif [ "$rc" -ne 0 ]; then
  fail=1
fi

# --- 1b. THE SAME TU, COMPILED FOR THE BOARD ---------------------------------
#
# RULED after <stddef.h>. The host suite proves the LOGIC; it does not prove the
# TU compiles for the device, and for one commit the header claimed it did.
# MinGW pulls size_t in through <stdint.h> transitively and the xtensa toolchain
# does not -- so the host rig was green, the header said the TU was portable,
# and the device build failed.
#
# That is a different failure from every other entry in the ledger: the check
# was working and passing for exactly what it covers, while its own description
# claimed more. A correct instrument with an inflated scope.
#
# THE FIX IS THAT THE TWO CLAIMS ARE MADE TOGETHER. Compiled here, in the same
# gate, on the same command: they cannot drift. Compiled separately -- host in
# one place, device in CI -- the host number eventually gets reported as the
# whole answer, which is precisely what happened.
#
# Compile-only (-c). Linking would need the ESP-IDF sysroot and a startup file,
# which is the coupling this rig exists to avoid; what is being asserted is that
# the source is valid for the target's toolchain and headers, and -c asserts
# exactly that and nothing it cannot back up.
echo
echo "== the same TU, for the board =="
XCXX="$(ls "$HOME"/.platformio/packages/toolchain-xtensa-esp-elf/bin/xtensa-esp32s3-elf-g++.exe 2>/dev/null | head -1)"
if [ -z "$XCXX" ] || [ ! -x "$XCXX" ]; then
  # NOT SKIPPED SILENTLY. A missing cross-compiler means this run proves the
  # host half only -- which is the exact claim that was overstated -- so it is
  # reported as the rig being incomplete rather than passed over.
  echo "FAIL: no xtensa toolchain found. This run can only prove the HOST half,"
  echo "      and reporting it as a pass is the mistake this section exists for."
  echo "      Get it with:  pio run -e blipscope-s3-128"
  fail=1
else
  echo "cross:    $XCXX"
  for tu in DrillMachine GameFormat; do
    if "$XCXX" $FLAGS $INCLUDES -c "$ROOT/src/game/$tu.cpp" \
         -o "$OUT/$tu.target.o" 2>"$OUT/target.log"; then
      echo "ok   $tu.cpp compiles for the ESP32-S3 with the same narrow includes"
    else
      echo "FAIL: $tu.cpp does NOT compile for the board:"
      cat "$OUT/target.log"
      fail=1
    fi
  done
fi

# --- 1c. the usage counters (README "Telemetry") ------------------------------
#
# The load-bearing assertion is that the payload CANNOT carry an identity: its
# builder takes integers, and the test asserts the rendered value is digits and
# commas with a control that it is not merely empty. Pure, so it grades here
# rather than needing a board.
if ! "$CXX" $FLAGS $INCLUDES       "$ROOT/test/host/test_usage_report.cpp"       -o "$OUT/test_usage_report.exe" 2>"$OUT/build.log"; then
  echo "FAIL: the usage counter tests did not compile"
  cat "$OUT/build.log"
  exit 2
fi
"$OUT/test_usage_report.exe"
rc=$?
if [ "$rc" -eq 127 ] || [ "$rc" -gt 2 ]; then
  echo "FAIL: the binary did not run (exit $rc). This is the RIG, not the code."
  exit 2
elif [ "$rc" -ne 0 ]; then
  fail=1
fi

# --- 2. the purity gate, and the control that proves it can fire -------------
#
# Compiling no_arduino.cpp MUST fail. If it succeeds, the drill TU is no longer
# isolated and the guarantee this whole rig exists for is gone -- so a SUCCESS
# here is the failure. Stderr goes to its own file rather than /dev/null: when
# this does fire, the reason is the thing worth reading (ledger 15's rule).
echo
echo "== purity gate =="
if "$CXX" $FLAGS $INCLUDES \
      "$ROOT/test/host/no_arduino.cpp" \
      -o "$OUT/no_arduino.exe" 2>"$OUT/no_arduino.log"; then
  echo "FAIL: test/host/no_arduino.cpp COMPILED."
  echo "      An Arduino header is reachable from the pure build, so the drill TU"
  echo "      is no longer isolated. That is D1's whole guarantee."
  fail=1
else
  # AND CONFIRM IT FAILED FOR THE RIGHT REASON -- which took two attempts, and
  # the first one is worth writing down because it is this project's signature
  # mistake sitting inside the gate that exists to prevent it.
  #
  # The first version grepped the log for "Arduino.h". Running the control for
  # this control -- widening INCLUDES to reach the real Arduino core -- the
  # build still failed, but on `freertos/FreeRTOS.h` two levels deeper, and the
  # log's first line reads "In file included from .../Arduino.h:33". The grep
  # matched. The gate printed "ok". Arduino.h was fully reachable.
  #
  # A refusal for the wrong reason is indistinguishable from success (ledger
  # 29), and here the wrong reason was the header being FOUND. So the check is
  # now for the specific not-found diagnostic, and it is anchored to the header
  # name so a deeper failure cannot satisfy it.
  if grep -qE "(fatal error|error):[[:space:]]*Arduino\.h:[[:space:]]*No such file" \
       "$OUT/no_arduino.log"; then
    echo "ok   the gate fires: Arduino.h is NOT FOUND from the pure build"
  else
    echo "FAIL: the compile refused, but not because Arduino.h was unreachable."
    echo "      A deeper failure means the header WAS found and the TU is not isolated."
    cat "$OUT/no_arduino.log"
    fail=1
  fi
fi

# --- 3. no forbidden symbols crept in via some other route -------------------
#
# The include path stops a HEADER. It does not stop somebody declaring
# `extern "C" unsigned long millis();` by hand, which would compile cleanly and
# link against the core on-device. Cheap to check, and it is the obvious way
# around the gate above.
echo
echo "== forbidden symbols =="
# COMMENTS ARE STRIPPED FIRST, and the patterns require a CALL or a member
# access rather than a bare word. The first version matched the word "millis"
# inside its own explanatory comment and "micros" inside "microseconds", and
# reported the pure TU as impure -- a check that cannot pass is as useless as
# one that cannot fail, and this one failed on a file that was correct.
strip_comments() {
  sed -e 's://.*::' -e 's:^[[:space:]]*\*.*::' "$1"
}
: > "$OUT/symbols.log"
for f in "$ROOT/src/game/DrillMachine.cpp" "$ROOT/src/game/DrillMachine.h" \
         "$ROOT/src/game/GameFormat.cpp" "$ROOT/src/game/GameFormat.h"; do
  strip_comments "$f" \
    | grep -nE '\b(millis|micros|delay|delayMicroseconds|analogRead|digitalWrite)[[:space:]]*\(|\bSerial\.|\bESP\.|\bString[[:space:]]+[a-zA-Z_]' \
    | sed "s|^|$(basename "$f"):|" >> "$OUT/symbols.log"
done
if [ -s "$OUT/symbols.log" ]; then
  echo "FAIL: Arduino-core symbols appear in the pure TU:"
  cat "$OUT/symbols.log"
  fail=1
else
  echo "ok   no Arduino-core symbols in the pure TU"
fi

echo
if [ "$fail" -eq 0 ]; then
  echo "ALL HOST TESTS PASSED"
else
  echo "HOST TESTS FAILED"
fi
exit "$fail"
