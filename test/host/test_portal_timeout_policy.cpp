// Host test for the setup-portal timeout ladder.
//
// The policy is three lines, so the value here is NOT in proving the arithmetic.
// It is in pinning the two properties that are easy to destroy by a later
// "simplification", and which fail in opposite directions:
//
//   * make it time out too eagerly  -> the setup hotspot vanishes every few
//     minutes and a moved device cannot be re-provisioned at all. That is the
//     bug this was written for.
//   * make it never time out        -> the device stops retrying its saved
//     network, so a power cut longer than the fast window parks it in setup mode
//     until a human intervenes. That is the bug the timeout was written for.
//
// Both are one edit away from each other, which is why each gets an assertion
// that names the consequence rather than the number.
#include <cstdio>
#include "../../include/PortalTimeoutPolicy.h"

static int failures = 0;
static void check(bool ok, const char* what)
{
    if (!ok) { std::printf("  FAIL: %s\n", what); ++failures; }
}

using namespace portaltimeout;

int main()
{
    std::printf("portal timeout policy\n");

    // ---- first setup: the portal must never be pulled away ------------------
    check(TimeoutSeconds(false, 0) == NEVER, "no credentials, fresh: portal never times out");
    check(TimeoutSeconds(false, 9) == NEVER, "no credentials stays NEVER regardless of count");
    check(TimeoutSeconds(false, 255) == NEVER, "no credentials: not even at saturation");

    // ---- early failures keep the fast self-heal -----------------------------
    // THE POWER-CUT PROPERTY. If these ever stop being the fast value, a board
    // that came up before its router no longer heals itself in a few minutes.
    for (uint8_t n = 0; n < FAST_CYCLES; ++n)
        check(TimeoutSeconds(true, n) == FAST_TIMEOUT_S,
              "early failure keeps the fast retry (router-reboot self-heal)");

    // ---- sustained failure becomes patient ----------------------------------
    // THE MOVED-DEVICE PROPERTY. If these collapse back to the fast value, the
    // hotspot resumes vanishing and a device in a new house cannot be set up.
    check(TimeoutSeconds(true, FAST_CYCLES) == PATIENT_TIMEOUT_S,
          "at the threshold the portal becomes patient");
    check(TimeoutSeconds(true, 200) == PATIENT_TIMEOUT_S, "stays patient");
    check(TimeoutSeconds(true, 255) == PATIENT_TIMEOUT_S, "patient at saturation too");

    // ---- CONTROL: the ladder must actually be a ladder ----------------------
    // Without this, a policy that returned one constant everywhere would satisfy
    // every "== X" above that happened to match it. The test has to be able to
    // fail for the reason it exists.
    check(FAST_TIMEOUT_S != PATIENT_TIMEOUT_S, "CONTROL: the two rungs differ");
    check(PATIENT_TIMEOUT_S > FAST_TIMEOUT_S,  "CONTROL: patient is the longer one");
    check(FAST_CYCLES > 0, "CONTROL: a fast phase exists at all (else self-heal is gone)");

    // ---- the patient rung must still TERMINATE ------------------------------
    // The whole reason this is a ladder and not a latch. A board that has failed
    // a thousand times must still retry its saved network when it comes back.
    check(TimeoutSeconds(true, 255) != NEVER,
          "a credentialed board ALWAYS retries eventually -- never parks forever");

    // ---- the counter ---------------------------------------------------------
    check(NextFailureCount(0, false) == 1, "a failure advances the counter");
    check(NextFailureCount(7, false) == 8, "and keeps advancing");
    check(NextFailureCount(255, false) == 255, "saturates rather than wrapping to 0");
    check(NextFailureCount(9, true) == 0, "a join clears it");
    // Clearing from the PATIENT state is the case that matters: a board that
    // struggled once and then settled must return to fast self-heal.
    check(NextFailureCount(255, true) == 0, "a join clears it even from saturation");

    // A wrap here would silently return a long-failing board to the fast path --
    // the exact regression this policy removes -- so it is asserted explicitly.
    check(NextFailureCount(255, false) != 0, "saturation must not wrap to zero");

    if (failures == 0) std::printf("  ok\n");
    else               std::printf("  %d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
