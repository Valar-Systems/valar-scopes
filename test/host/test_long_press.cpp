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
    { // RULE 2: fast movement -- 40 px inside the first 250 ms -- is a swipe, exactly as today
        State s; OnPress(s, 0, 100, 100, true);
        check(OnHeld(s, 104, 100, 100) == HeldIs::Nothing, "still, at 104 ms");
        check(OnHeld(s, 208, 100, 100 - SWIPE_PX) == HeldIs::Moved, "RULE 2: 40 px by 208 ms (inside the ring start) is a swipe");
        check(HoldFrames(s, 260, 900, 100, 100 - SWIPE_PX).fire == 0, "and it never fires afterwards");
        check(OnRelease(s, 900) == ReleaseIs::PassThrough, "its release is classified as today (a swipe)");
    }
    { // RULE 2: slow drift during the ring is tolerated to 80 px
        State s; OnPress(s, 0, 100, 100, true);
        HoldFrames(s, 0, 300, 100, 100);
        check(OnHeld(s, 352, 100 + DRIFT_PX - 1, 100) == HeldIs::Nothing, "RULE 2: 79 px of slow drift during the ring is still the hold");
        check(OnHeld(s, THRESHOLD_MS, 100 + DRIFT_PX - 1, 100) == HeldIs::Fire, "and it fires at the threshold");
        check(s.driftMax == DRIFT_PX - 1 && s.driftMaxAtMs == 352, "the largest drift and when it was reached are recorded");
        State u; OnPress(u, 0, 100, 100, true);
        HoldFrames(u, 0, 300, 100, 100);
        check(OnHeld(u, 352, 100 + DRIFT_PX, 100) == HeldIs::Moved, "RULE 2: 80 px during the ring is a slow drag -- hands off");
        check(OnRelease(u, 400) == ReleaseIs::PassThrough, "its release is classified as today");
    }
    { // (e): a deliberate slow drift of ~60 px toggles once, never swipes
        State s; OnPress(s, 0, 120, 120, true);
        Tally t = {};
        for (uint32_t now = 0; now <= 1200; now += 52) {
            const int x = 120 + (int)(now > 260 ? (now - 260) / 10 : 0); // ~5 px a frame after the ring start: 60+ px by 1.2 s
            const HeldIs h = OnHeld(s, now, x < 120 + 70 ? x : 120 + 70, 120);
            if (h == HeldIs::Fire) ++t.fire;
            if (h == HeldIs::Moved) ++t.moved;
        }
        check(t.fire == 1 && t.moved == 0, "(e) a slow 60-70 px drift: exactly one toggle, no swipe");
        check(OnRelease(s, 1200) == ReleaseIs::Consumed, "(e) and its release is consumed");
    }
    { // RULE 1: after the fire, however far the finger drifts, the release is never a tap or a swipe
        State s; OnPress(s, 0, 100, 100, true);
        HoldFrames(s, 0, THRESHOLD_MS - 1, 100, 100);
        check(OnHeld(s, THRESHOLD_MS, 100, 100) == HeldIs::Fire, "RULE 1 setup: the hold fires at the threshold");
        check(OnHeld(s, THRESHOLD_MS + 104, 100 + 150, 100) == HeldIs::Nothing, "RULE 1: 150 px of drift after the fire is ignored");
        check(OnRelease(s, THRESHOLD_MS + 156) == ReleaseIs::Consumed, "RULE 1: the release is CONSUMED, never a swipe");
    }
    { // the rejoin is measured from the finger's LAST position (it may have drifted)
        State s; OnPress(s, 0, 100, 100, true);
        HoldFrames(s, 0, 300, 100, 100);
        OnHeld(s, 352, 150, 100);   // 50 px of slow drift
        OnRelease(s, 400);
        check(OnPress(s, 480, 152, 100, true) == PressIs::Rejoin, "a press 2 px from the drifted position rejoins");
        State u; OnPress(u, 0, 100, 100, true);
        HoldFrames(u, 0, 300, 100, 100);
        OnHeld(u, 352, 150, 100); OnRelease(u, 400);
        check(OnPress(u, 480, 100, 100, true) == PressIs::NewStroke, "a press back at the ORIGINAL point, 50 px from the finger, is a new stroke");
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
