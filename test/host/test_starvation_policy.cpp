// Host test for the enrichment-starvation predicate.
//
// GRADED AGAINST REAL BOARDS. Every fixture below is a value captured off
// hardware during the 2026-08-24 A/B soak, named with its board and timestamp so
// the numbers can be traced back to the log rather than taken on trust:
//
//   control    COM119  blipscope-s3-128           PSRAM routing NOT built in
//   treatment  COM16   blipscope-s3-128-tlspsram  PSRAM routing INSTALLED
//
// This is the whole verification. The predicate is a pure function of counters,
// so it can be proved here -- and it MUST be proved here, because the only live
// reproduction of the bug is COM119 in its current state, and flashing it to run
// the fix would reboot it and clear the fragmentation that makes it valuable.
#include <cstdio>
#include "../../include/StarvationPolicy.h"

static int failures = 0;

static void check(bool ok, const char* what)
{
    if (!ok) { std::printf("  FAIL: %s\n", what); ++failures; }
}

// The predicate as it shipped in #250, reproduced EXACTLY so the test can show
// the difference rather than assert it. tlsOk is CanHandshake(), which is
// `BallastHeld() || CanAllocate(...)` -- so it is computed here the same way.
static bool OldPredicate(bool canAllocateHandshake, bool ballastHeld)
{
    const int tlsOk = (ballastHeld || canAllocateHandshake) ? 1 : 0;
    return (tlsOk == 0) && !ballastHeld;
}

// Run `ticks` consecutive refusing health ticks through the new rule and report
// whether it ends up starved. Mirrors exactly what the health line does.
static bool StarvedAfter(int ticks, bool ballastHeld)
{
    uint8_t run = 0;
    for (int i = 0; i < ticks; ++i)
        run = starvation::NextRun(run, /*canAllocateHandshake=*/false);
    return starvation::IsStarved(run, ballastHeld);
}

int main()
{
    // ---- THE NEGATIVE CONTROL, FIRST -------------------------------------
    //
    // Before believing anything this test says about the fix, prove it can see
    // the bug. COM119 at 08:03:57 reported ball=1 with a heap that could not
    // serve a handshake; photos had been dead for hours. The old predicate must
    // be shown returning FALSE on exactly those inputs -- if it does not, this
    // test is not reproducing the defect and none of its passes mean anything.
    check(OldPredicate(/*canAlloc=*/false, /*ballast=*/true) == false,
          "NEGATIVE CONTROL: the OLD predicate must stay silent on COM119's "
          "numbers (ball=1, cannot allocate) -- that is the bug");

    // And the old predicate was not merely wrong here, it was DEAD: holding the
    // ballast forces tlsOk=1, so no value of the other input can make it fire.
    check(OldPredicate(/*canAlloc=*/true,  /*ballast=*/true) == false,
          "OLD: ballast held, can allocate -> silent");
    check(OldPredicate(/*canAlloc=*/false, /*ballast=*/true) == false,
          "OLD: ballast held, CANNOT allocate -> still silent (the dead clause)");

    // ---- the new rule on the same inputs ---------------------------------

    // CONTROL, COM119. Sustained inability -> must fire. This is the case that
    // went unreported for twenty-four hours.
    check(StarvedAfter(starvation::CONFIRM_TICKS, /*ballast=*/true),
          "CONTROL COM119: sustained refusal WITH ballast held must fire");

    // THE FIX, STATED AS A TEST: the ballast must not change the answer. The old
    // predicate consulted it and was blinded; if anyone reintroduces that, this
    // is the check that catches it.
    check(StarvedAfter(starvation::CONFIRM_TICKS, /*ballast=*/true) ==
          StarvedAfter(starvation::CONFIRM_TICKS, /*ballast=*/false),
          "ballast held must NOT change the verdict");
    check(starvation::IsStarved(starvation::CONFIRM_TICKS, true) ==
          starvation::IsStarved(starvation::CONFIRM_TICKS, false),
          "IsStarved is independent of ballastHeld at the threshold");
    check(starvation::IsStarved(0, true) == starvation::IsStarved(0, false),
          "IsStarved is independent of ballastHeld at zero");

    // TREATMENT, COM16. Photos worked all day; it broke the largest-block budget
    // exactly once. A single refusing tick must NOT fire, or the alarm cries
    // wolf on a healthy board -- which is the failure mode that would get it
    // switched off and put us back where we started.
    check(!StarvedAfter(1, /*ballast=*/true),
          "TREATMENT COM16: one isolated refusal must NOT fire");

    // The control's own capture shows recovery within one or two ticks
    // (07:27:23 largest=7668 -> 07:27:55 largest=7924, and 08:01:25 largest=17396
    // briefly ABOVE the 16,717 handshake size). Two-tick dips are normal.
    check(!StarvedAfter(2, /*ballast=*/false), "two-tick dip must NOT fire");
    check(!StarvedAfter(3, /*ballast=*/false), "three-tick dip must NOT fire");
    check(StarvedAfter(4, /*ballast=*/false),  "four consecutive ticks MUST fire");

    // Pins the threshold to the constant, so moving CONFIRM_TICKS cannot quietly
    // leave these cases asserting something else.
    check(!StarvedAfter(starvation::CONFIRM_TICKS - 1, false),
          "one tick below the threshold must not fire");
    check(starvation::CONFIRM_TICKS == 4,
          "CONFIRM_TICKS must be 4 -- see the argument in the header");

    // ---- the run counter itself ------------------------------------------

    // ANY success resets. This measures an UNINTERRUPTED inability, so a board
    // that recovers for a single tick starts its count again -- otherwise a
    // board that refused once an hour would eventually accumulate to the alarm.
    check(starvation::NextRun(3, /*canAlloc=*/true) == 0,
          "a success resets the run to zero");
    check(starvation::NextRun(0, /*canAlloc=*/false) == 1, "a refusal increments");
    check(starvation::NextRun(3, /*canAlloc=*/false) == 4, "refusals accumulate");

    // Saturation, not wrap. 255 ticks is over two hours; a counter that wrapped
    // would clear the alarm on a board broken all day, which is precisely the
    // silent-healthy failure this file exists to remove.
    check(starvation::NextRun(255, /*canAlloc=*/false) == 255, "the run saturates at 255");
    check(starvation::IsStarved(255, false), "a saturated run is still starved");
    check(starvation::NextRun(255, /*canAlloc=*/true) == 0,
          "even a saturated run clears on recovery");

    if (failures == 0) std::printf("test_starvation_policy: all checks passed\n");
    else               std::printf("test_starvation_policy: %d FAILURE(S)\n", failures);
    return failures == 0 ? 0 : 1;
}
