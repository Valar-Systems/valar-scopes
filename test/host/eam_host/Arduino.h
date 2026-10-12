#pragma once

/* ============================================================================
 * Arduino.h for the E6 long-message test: the REAL arduino-esp32 String.
 *
 * test/host/arduino_shim/ has a minimal String written for CoordParse. This is
 * the other choice, made for a different question: E6 asks what a long message
 * COSTS, and the answer depends on String's allocation rule (SSO size, the
 * (len+16)&~15 growth, realloc on assignment). A shim that behaves the same at
 * the API would answer that with its own rule. So run.sh copies WString.{h,cpp},
 * stdlib_noniso.{h,c} and pgmspace.h out of the installed framework
 * (~/.platformio/packages/framework-arduinoespressif32/cores/esp32) into
 * .pio/hosttest/realwstring/ and compiles them here, unmodified. No framework
 * installed -> the test is BLIND (exit 2), never a silent skip.
 *
 * Only what the code under test needs is declared below. Everything else an
 * Arduino sketch would expect is deliberately absent, so a dependency that
 * creeps into EamFeedClient/EamModels shows up as a compile error, not as a
 * quiet stub.
 * ==========================================================================*/

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "WString.h"

// The scheduler clock EamFeedClient reads. Driven by the test (FakeMillis), so a
// "30 s later" poll is a number, not a sleep.
uint32_t millis();
