// THE PURITY GATE. This file is the reason D1 is "done" rather than "written".
//
// ===========================================================================
// src/game/DrillMachine.cpp must compile against NOTHING but the C++ standard
// library. Not "should" — the build has to fail if that stops being true, or
// the property is a comment somebody will edit past.
//
// The mechanism is the absence of an include path. run.sh compiles the drill TU
// with `-I src/game` and no Arduino, no ESP-IDF, no LovyanGFX, so `#include
// <Arduino.h>` cannot resolve and the build stops. That is the gate; this file
// is the CONTROL that proves the gate can fire.
//
// A gate nobody has watched fail is not known to be a gate — see
// valar-eam-feed/docs/verification-ledger.md, which is a list of exactly that
// mistake. So run.sh compiles this file too, with --expect-fail, and fails the
// whole run if it unexpectedly SUCCEEDS.
//
// Which is the negative control the work order asks for, made permanent rather
// than performed once: "proven by adding one, watching it fail, and removing
// it". Removing it is the part that decays. This stays.
// ===========================================================================

// The header that must not be reachable. If this line ever compiles, the drill
// TU is no longer isolated from the Arduino core and D1's guarantee is gone.
#include <Arduino.h>

int main() {
  // Unreachable: the include above is expected to stop the compiler. If control
  // ever reaches here the build succeeded, run.sh reports that as the failure
  // it is, and this line is what makes the binary do something observable
  // rather than link away to nothing.
  return millis() ? 0 : 1;
}
