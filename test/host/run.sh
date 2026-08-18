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
set +e
"$OUT/test_drill_machine.exe"
rc=$?
set -e
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
for f in "$ROOT/src/game/DrillMachine.cpp" "$ROOT/src/game/DrillMachine.h"; do
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
