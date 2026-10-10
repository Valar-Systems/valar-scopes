// Long press to toggle zoom, the pure half: include/LongPressPolicy.h
// (docs/v17-long-press-zoom.md; predictions frozen at f3bf96ae5474).
//
// SABOTAGE S1: make a fired stroke's release PassThrough instead of Consumed ("no card past the
// threshold" dropped) -> the CONSUMED cases go red.
// SABOTAGE S2: drop the rejoin (OnPress ignores Grace) -> the REJOIN case goes red.
#include <cstdio>
#include "../../include/LongPressPolicy.h"

using namespace longpress;

static int failures = 0;
static void check(bool ok, const char* what)
{
    std::printf("  %s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++failures;
}

// Drive a held stroke frame by frame (52 ms, the touch-poll cadence seen in the logs) from
// `from` up to `to` inclusive, at (x, y). Counts each kind of HeldIs.
struct Tally { int ring = 0, fire = 0, moved = 0; };
static Tally HoldFrames(State& s, uint32_t from, uint32_t to, int x, int y)
{
    Tally t;
    for (uint32_t now = from; now <= to; now += 52) {
        switch (OnHeld(s, now, x, y)) {
        case HeldIs::RingStart: ++t.ring; break;
        case HeldIs::Fire: ++t.fire; break;
        case HeldIs::Moved: ++t.moved; break;
        default: break;
        }
    }
    return t;
}

int main()
{
    std::printf("long press (LongPressPolicy.h)\n");

    { // a quick tap is the old tap: no ring, no fire, PassThrough at release
        State s;
        check(OnPress(s, 1000, 120, 120, true) == PressIs::NewStroke, "a press on the eligible Radar starts a stroke");
        Tally t = HoldFrames(s, 1000, 1000 + 183, 120, 120); // the recorded tap p99
        check(t.ring == 0 && t.fire == 0, "a 183 ms hold (the tap p99) shows no ring and fires nothing");
        check(OnRelease(s, 1000 + 183) == ReleaseIs::PassThrough, "its release is classified as today (a tap)");
    }
    { // the ring starts at 250 ms, not before
        State s; OnPress(s, 0, 100, 100, true);
        check(OnHeld(s, 249, 100, 100) == HeldIs::Nothing, "249 ms: no ring yet");
        check(OnHeld(s, 250, 100, 100) == HeldIs::RingStart, "250 ms: the ring starts");
        check(RingVisible(s) && RingPermille(s, 250) == 0, "the ring is visible and empty at its start");
        check(RingPermille(s, 475) == 500, "half full halfway to the threshold");
    }
    { // an early release, after the ring started, does nothing: grace, then cancel
        State s; OnPress(s, 0, 100, 100, true);
        HoldFrames(s, 0, 400, 100, 100);
        check(OnRelease(s, 400) == ReleaseIs::Grace, "a release while the ring fills says nothing yet (grace)");
        check(RingPermille(s, 400 + REJOIN_MS) == RingPermille(s, 400), "the ring's progress is frozen during the grace -- it never completes after an early release");
        check(!OnIdleCancels(s, 400 + REJOIN_MS), "still waiting at exactly REJOIN_MS");
        check(OnIdleCancels(s, 400 + REJOIN_MS + 1), "after the grace: cancel -- no card, no zoom");
        check(s.phase == Phase::Idle && !RingVisible(s), "and the ring is gone");
    }
    { // a full hold fires exactly once, at the threshold, and its release is CONSUMED
        State s; OnPress(s, 0, 120, 120, true);
        check(OnHeld(s, THRESHOLD_MS - 1, 120, 120) != HeldIs::Fire, "1 ms short of the threshold: no fire");
        check(OnHeld(s, THRESHOLD_MS, 120, 120) == HeldIs::Fire, "at the threshold, still held: FIRE");
        Tally after = HoldFrames(s, THRESHOLD_MS + 52, THRESHOLD_MS + 2000, 120, 120);
        check(after.fire == 0, "held on for 2 s more: no second fire");
        check(OnRelease(s, THRESHOLD_MS + 2000) == ReleaseIs::Consumed, "CONSUMED: the release opens no card and is no swipe");
    }
    { // a long press over an aircraft: same rule -- the caller passes no hit-test into the policy
        State s; OnPress(s, 0, 120, 120, true);
        Tally t = HoldFrames(s, 0, 780, 121, 119); // finger jitter of 1-2 px
        check(t.fire == 1, "a jittering finger still fires once");
        check(OnRelease(s, 780) == ReleaseIs::Consumed, "CONSUMED over an aircraft too: no card past the threshold");
    }
    { // movement makes it a swipe, before the threshold
        State s; OnPress(s, 0, 100, 100, true);
        Tally t = HoldFrames(s, 0, 300, 100, 100);
        check(OnHeld(s, 352, 100, 100 - MOVE_PX) == HeldIs::Moved, "40 px of movement before firing: a swipe, hands off");
        check(t.fire == 0 && OnHeld(s, 900, 100, 100 - MOVE_PX) == HeldIs::Nothing, "and it never fires afterwards");
        check(OnRelease(s, 900) == ReleaseIs::PassThrough, "its release is classified as today (a swipe)");
    }
    { // REJOIN: a split stroke inside the grace resumes the same hold, progress kept
        State s; OnPress(s, 0, 100, 100, true);
        HoldFrames(s, 0, 400, 100, 100);
        OnRelease(s, 400);
        check(OnPress(s, 500, 105, 96, true) == PressIs::Rejoin, "REJOIN: a press 100 ms later and 5 px away is the same hold");
        check(RingPermille(s, 500) > 0, "progress was kept across the dropout");
        check(OnHeld(s, THRESHOLD_MS, 105, 96) == HeldIs::Fire, "and it fires at the ORIGINAL press + threshold");
    }
    { // too late or too far is a new stroke
        State s; OnPress(s, 0, 100, 100, true);
        HoldFrames(s, 0, 400, 100, 100); OnRelease(s, 400);
        check(OnPress(s, 400 + REJOIN_MS + 1, 100, 100, true) == PressIs::NewStroke, "a press 151 ms later is a new stroke");
        State t; OnPress(t, 0, 100, 100, true);
        HoldFrames(t, 0, 400, 100, 100); OnRelease(t, 400);
        check(OnPress(t, 450, 100 + REJOIN_PX + 1, 100, true) == PressIs::NewStroke, "a press 21 px away is a new stroke");
    }
    { // not eligible (a card open, another screen): bypassed, everything as today
        State s;
        check(OnPress(s, 0, 100, 100, false) == PressIs::Bypass, "a press with a card open / off the Radar is bypassed");
        Tally t = HoldFrames(s, 0, 2000, 100, 100);
        check(t.ring == 0 && t.fire == 0, "a 2 s hold there shows no ring and fires nothing");
        check(OnRelease(s, 2000) == ReleaseIs::PassThrough, "its release is classified as today");
    }
    { // the toggle
        check(ToggleTarget(2, 4) == 0, "from an intermediate step: to the closest");
        check(ToggleTarget(4, 4) == 0, "from the configured range: to the closest");
        check(ToggleTarget(0, 4) == 4, "from the closest: back to the configured range");
        check(ToggleTarget(0, 0) == -1, "a single-step ladder: nothing to toggle");
    }

    std::printf(failures ? "FAILED: %d\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
