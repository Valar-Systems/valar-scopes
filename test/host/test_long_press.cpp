// Long press to toggle zoom, the pure half: include/LongPressPolicy.h
// (docs/v17-long-press-zoom.md; predictions frozen at f3bf96ae5474).
//
// SABOTAGE S1: make a fired stroke's release PassThrough instead of Consumed ("no card past the
// threshold" dropped) -> the CONSUMED cases go red.
// SABOTAGE S2: drop the rejoin (OnPress ignores Grace) -> the REJOIN case goes red.
// SABOTAGE A: decide rule 3 at the LIFT instead of at grace end -> exactly RJa, G6a, G6b, G6c, G7a, G7b,
// G8 go red. SABOTAGE B: remove rule 3 (the grace always ends in the cancel) -> exactly G1, G3, G7b, G8.
// (Frozen d2ecd9351e32.)
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

// How a released stroke ENDS: at the lift (the classifier, or consumed), or when its rejoin grace ends
// on idle frames every 52 ms. `atMs` is when, relative to the lift. Unresolved is its own value, never
// quietly a cancel.
enum class End { Tap, Swipe, Cancel, Consumed, Unresolved };
struct Ending { End end; uint32_t atMs; int dx, dy; };
static Ending IdleUntilEnd(State& s, uint32_t lift)
{
    for (uint32_t now = lift + 52; now <= lift + 1000; now += 52) {
        const GraceEnd g = OnIdle(s, now);
        if (g.ended) { Ending e = { g.swipe ? End::Swipe : End::Cancel, now - lift, g.dx, g.dy }; return e; }
    }
    Ending u = { End::Unresolved, 0, 0, 0 };
    return u;
}
static Ending Finish(State& s, uint32_t lift, int dx, int dy)
{
    const ReleaseIs r = OnRelease(s, lift, dx, dy);
    if (r == ReleaseIs::Consumed) { Ending e = { End::Consumed, 0, dx, dy }; return e; }
    if (r == ReleaseIs::PassThrough) {
        Ending e = { (std::abs(dx) < SWIPE_PX && std::abs(dy) < SWIPE_PX) ? End::Tap : End::Swipe, 0, dx, dy };
        return e;
    }
    return IdleUntilEnd(s, lift);
}
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
        check(OnPress(s, 1000, 120, 120, true).is == PressIs::NewStroke, "a press on the eligible Radar starts a stroke");
        Tally t = HoldFrames(s, 1000, 1000 + 183, 120, 120); // the recorded tap p99
        check(t.ring == 0 && t.fire == 0, "a 183 ms hold (the tap p99) shows no ring and fires nothing");
        check(OnRelease(s, 1000 + 183, 0, 0) == ReleaseIs::PassThrough, "its release is classified as today (a tap)");
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
        check(OnRelease(s, 400, 0, 0) == ReleaseIs::Grace, "a release while the ring fills says nothing yet (grace)");
        check(RingPermille(s, 400 + REJOIN_MS) == RingPermille(s, 400), "the ring's progress is frozen during the grace -- it never completes after an early release");
        check(!OnIdle(s, 400 + REJOIN_MS).ended, "still waiting at exactly REJOIN_MS");
        { const GraceEnd g = OnIdle(s, 400 + REJOIN_MS + 1); check(g.ended && !g.swipe, "after the grace: cancel -- no card, no zoom"); }
        check(s.phase == Phase::Idle && !RingVisible(s), "and the ring is gone");
    }
    { // a full hold fires exactly once, at the threshold, and its release is CONSUMED
        State s; OnPress(s, 0, 120, 120, true);
        check(OnHeld(s, THRESHOLD_MS - 1, 120, 120) != HeldIs::Fire, "1 ms short of the threshold: no fire");
        check(OnHeld(s, THRESHOLD_MS, 120, 120) == HeldIs::Fire, "at the threshold, still held: FIRE");
        Tally after = HoldFrames(s, THRESHOLD_MS + 52, THRESHOLD_MS + 2000, 120, 120);
        check(after.fire == 0, "held on for 2 s more: no second fire");
        check(OnRelease(s, THRESHOLD_MS + 2000, 0, 0) == ReleaseIs::Consumed, "CONSUMED: the release opens no card and is no swipe");
    }
    { // a long press over an aircraft: same rule -- the caller passes no hit-test into the policy
        State s; OnPress(s, 0, 120, 120, true);
        Tally t = HoldFrames(s, 0, 780, 121, 119); // finger jitter of 1-2 px
        check(t.fire == 1, "a jittering finger still fires once");
        check(OnRelease(s, 780, 1, -1) == ReleaseIs::Consumed, "CONSUMED over an aircraft too: no card past the threshold");
    }
    { // RULE 2: fast movement -- 40 px inside the first 250 ms -- is a swipe, exactly as today
        State s; OnPress(s, 0, 100, 100, true);
        check(OnHeld(s, 104, 100, 100) == HeldIs::Nothing, "still, at 104 ms");
        check(OnHeld(s, 208, 100, 100 - SWIPE_PX) == HeldIs::Moved, "RULE 2: 40 px by 208 ms (inside the ring start) is a swipe");
        check(HoldFrames(s, 260, 900, 100, 100 - SWIPE_PX).fire == 0, "and it never fires afterwards");
        check(OnRelease(s, 900, 0, -SWIPE_PX) == ReleaseIs::PassThrough, "its release is classified as today (a swipe)");
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
        check(OnRelease(u, 400, DRIFT_PX, 0) == ReleaseIs::PassThrough, "its release is classified as today");
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
        check(OnRelease(s, 1200, 70, 0) == ReleaseIs::Consumed, "(e) and its release is consumed");
    }
    { // RULE 1: after the fire, however far the finger drifts, the release is never a tap or a swipe
        State s; OnPress(s, 0, 100, 100, true);
        HoldFrames(s, 0, THRESHOLD_MS - 1, 100, 100);
        check(OnHeld(s, THRESHOLD_MS, 100, 100) == HeldIs::Fire, "RULE 1 setup: the hold fires at the threshold");
        check(OnHeld(s, THRESHOLD_MS + 104, 100 + 150, 100) == HeldIs::Nothing, "RULE 1: 150 px of drift after the fire is ignored");
        check(OnRelease(s, THRESHOLD_MS + 156, 150, 0) == ReleaseIs::Consumed, "RULE 1: the release is CONSUMED, never a swipe");
    }
    { // the rejoin is measured from the finger's LAST position (it may have drifted)
        State s; OnPress(s, 0, 100, 100, true);
        HoldFrames(s, 0, 300, 100, 100);
        OnHeld(s, 352, 150, 100);   // 50 px of slow drift
        OnRelease(s, 400, 50, 0);
        check(OnPress(s, 480, 152, 100, true).is == PressIs::Rejoin, "RJa a press 2 px from the drifted position rejoins");
        State u; OnPress(u, 0, 100, 100, true);
        HoldFrames(u, 0, 300, 100, 100);
        OnHeld(u, 352, 150, 100); OnRelease(u, 400, 50, 0);
        check(OnPress(u, 480, 100, 100, true).is == PressIs::NewStroke, "RJb a press back at the ORIGINAL point, 50 px from the finger, is a new stroke");
    }
    { // REJOIN: a split stroke inside the grace resumes the same hold, progress kept
        State s; OnPress(s, 0, 100, 100, true);
        HoldFrames(s, 0, 400, 100, 100);
        OnRelease(s, 400, 0, 0);
        check(OnPress(s, 500, 105, 96, true).is == PressIs::Rejoin, "REJOIN: a press 100 ms later and 5 px away is the same hold");
        check(RingPermille(s, 500) > 0, "progress was kept across the dropout");
        check(OnHeld(s, THRESHOLD_MS, 105, 96) == HeldIs::Fire, "and it fires at the ORIGINAL press + threshold");
    }
    { // too late or too far is a new stroke
        State s; OnPress(s, 0, 100, 100, true);
        HoldFrames(s, 0, 400, 100, 100); OnRelease(s, 400, 0, 0);
        check(OnPress(s, 400 + REJOIN_MS + 1, 100, 100, true).is == PressIs::NewStroke, "a press 151 ms later is a new stroke");
        State t; OnPress(t, 0, 100, 100, true);
        HoldFrames(t, 0, 400, 100, 100); OnRelease(t, 400, 0, 0);
        check(OnPress(t, 450, 100 + REJOIN_PX + 1, 100, true).is == PressIs::NewStroke, "a press 21 px away is a new stroke");
    }
    { // not eligible (a card open, another screen): bypassed, everything as today
        State s;
        check(OnPress(s, 0, 100, 100, false).is == PressIs::Bypass, "a press with a card open / off the Radar is bypassed");
        Tally t = HoldFrames(s, 0, 2000, 100, 100);
        check(t.ring == 0 && t.fire == 0, "a 2 s hold there shows no ring and fires nothing");
        check(OnRelease(s, 2000, 0, 0) == ReleaseIs::PassThrough, "its release is classified as today");
    }
    { // RULE 3 (frozen 06e01f3869ee): an early release that moved 40 px or more is a swipe, as v16 classifies it.
        // G1, the 10 Oct stroke: slow at first -- 20 px when the ring starts at 260 ms -- then 78 px by the last
        // frame before the 428 ms release. A steady 78 px / 428 ms would cross 40 px before 250 ms and test the
        // OLD rule instead, which is not the case that was eaten.
        State s; OnPress(s, 0, 120, 160, true);
        int ring = 0, moved = 0;
        for (uint32_t now = 0; now <= 416; now += 52) {
            const int dy = now <= 260 ? (int)(now * 20 / 260) : 20 + (int)((now - 260) * 58 / 156);
            const HeldIs h = OnHeld(s, now, 120, 160 - dy);
            if (h == HeldIs::RingStart) ++ring;
            if (h == HeldIs::Moved) ++moved;
        }
        check(ring == 1 && moved == 0 && s.phase == Phase::Ring, "G1 setup: under 40 px at the ring start, 78 px still inside the 80 px drift tolerance");
        check(Finish(s, 428, 0, -78).end == End::Swipe, "G1 the 10 Oct stroke (428 ms, 78 px) ends as a SWIPE, as v16 classifies it");

        State g2; OnPress(g2, 0, 100, 100, true); HoldFrames(g2, 0, 300, 100, 100);
        OnHeld(g2, 352, 100, 100 - 39);
        check(Finish(g2, 400, 0, -39).end == End::Cancel, "G2 a ring-phase release at 39 px ends as the cancel, unchanged");

        State g3; OnPress(g3, 0, 100, 100, true); HoldFrames(g3, 0, 300, 100, 100);
        OnHeld(g3, 352, 100, 100 - 40);
        check(Finish(g3, 400, 0, -40).end == End::Swipe, "G3 a ring-phase release at 40 px ends as a SWIPE");

        State g4; OnPress(g4, 0, 100, 100, true);
        check(OnHeld(g4, 104, 139, 100) == HeldIs::Nothing, "G4 setup: 39 px inside the ring start is not yet a swipe");
        check(OnRelease(g4, 183, 39, 0) == ReleaseIs::PassThrough, "G4 39 px released before 250 ms -> classified as today (a tap), unchanged");

        // G6: 50 px of drift, a lift, a re-press 120 ms later at the last position: a dropout, one hold.
        State g6; OnPress(g6, 0, 100, 100, true); HoldFrames(g6, 0, 300, 100, 100);
        OnHeld(g6, 352, 150, 100);
        OnRelease(g6, 400, 50, 0);
        check(OnPress(g6, 520, 152, 100, true).is == PressIs::Rejoin, "G6a 50 px drift, lift, re-press at 120 ms -> a REJOIN");
        check(HoldFrames(g6, 572, THRESHOLD_MS + 104, 152, 100).fire == 1, "G6b it fires once, at the ORIGINAL press + threshold");
        check(OnRelease(g6, THRESHOLD_MS + 104, 52, 0) == ReleaseIs::Consumed, "G6c its release is CONSUMED: one hold, no swipe");

        // G7: 50 px of drift, a lift, no re-press: a SWIPE when the grace expires -- not at the lift.
        State g7; OnPress(g7, 0, 100, 100, true); HoldFrames(g7, 0, 300, 100, 100);
        OnHeld(g7, 352, 150, 100);
        check(OnRelease(g7, 400, 50, 0) == ReleaseIs::Grace, "G7a nothing at the lift: the grace opens");
        const Ending e7 = IdleUntilEnd(g7, 400);
        check(e7.end == End::Swipe && e7.atMs > REJOIN_MS && e7.dx == 50 && e7.dy == 0,
              "G7b a SWIPE at grace expiry, not before, with the stroke's own dx/dy");

        // G8: the grace's OTHER end -- a press elsewhere inside it -- goes through the same rule.
        State g8; OnPress(g8, 0, 100, 100, true); HoldFrames(g8, 0, 300, 100, 100);
        OnHeld(g8, 352, 150, 100); OnRelease(g8, 400, 50, 0);
        const PressOutcome p8 = OnPress(g8, 480, 210, 100, true);   // 60 px from the finger, 80 ms after the lift
        check(p8.is == PressIs::NewStroke && p8.prev.ended && p8.prev.swipe && p8.prev.dx == 50,
              "G8 a press elsewhere inside the grace ends the old stroke as a SWIPE and starts a new one");
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
