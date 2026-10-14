#pragma once

/* ===========================================================================
 * LONG PRESS TO TOGGLE ZOOM: the pure half (docs/v17-long-press-zoom.md).
 *
 * The device feeds touch edges and frames in; this says what each one means. It
 * needs no chip, no clock and no screen, so every rule is graded on a host
 * (test/host/test_long_press.cpp) before it reaches the panel.
 *
 *   0 .. RING_START_MS        nothing visible; a release is a TAP, exactly as before
 *   RING_START_MS .. THRESHOLD  the ring fills; a release starts the REJOIN grace. If the finger
 *                             does not come back, the grace ends: a swipe if the stroke moved
 *                             40 px (exactly as before -- rule 3), otherwise the cancel
 *   THRESHOLD, still held     FIRE once (toggle zoom); the stroke is then CONSUMED
 *
 * WHY THE RING STARTS AT 250 ms. 905 recorded taps on this batch: median 75 ms,
 * p99 183 ms. Below the ring start a release is the old tap, untouched, so no tap
 * gets any added latency and a slow tap is not stolen.
 *
 * WHY A REJOIN GRACE. With IrqCtl EnTouch|EnChange the CST816D may stop reporting a
 * finger that does not move, and the driver then reads "no touch" while the chip
 * still has it (src/gametest_main.cpp:38-45). Five recorded holds of 500 ms or more
 * were all single strokes, but a split is possible, and would show as a release
 * then a press within a frame or two at the same spot. While the ring is filling a
 * release does NOTHING visible anyway (an early release cancels), so waiting
 * REJOIN_MS to see whether the finger "comes back" costs no latency at all.
 * ======================================================================== */

#include <cstdint>
#include <cstdlib>

namespace longpress {

constexpr uint32_t RING_START_MS = 250;  // above the recorded tap p99 (183 ms)
constexpr uint32_t THRESHOLD_MS  = 700;  // start value; Daniel tunes it on the glass
constexpr uint32_t REJOIN_MS     = 150;  // a split stroke rejoins inside this
constexpr int      SWIPE_PX      = 40;   // == SWIPE_MIN: this much movement INSIDE the ring start is a swipe
constexpr int      DRIFT_PX      = 80;   // after the ring start, slow drift up to this is still the hold
constexpr int      REJOIN_PX     = 20;   // a rejoining press lands this close to the finger's LAST position

// DRIFT (review, 2026-10-10). A swipe is fast; a finger settling during a hold is slow. Daniel's
// 3,973 ms hold drifted 58 px and was read as a swipe; the largest drift in any logged 1-4 s hold is
// 67 px. So: 40 px inside the first 250 ms is a swipe, exactly as today; after that, drift up to 80 px
// is still the hold; and once the hold has FIRED, nothing the finger does makes it a tap or a swipe.
//
// RULE 3 (review, 2026-10-12). A stroke released BEFORE the threshold having moved 40 px or more is a
// swipe, exactly as v16 classifies it. On 10 Oct a 428 ms swipe that moved 78 px was under 40 px at the
// ring start, so drift tolerance kept it as a hold and the early release cancelled it: an ordinary swipe
// did nothing. Drift up to 80 px is still tolerated WHILE HELD (so a drifting hold that reaches the
// threshold still toggles); it is only an early RELEASE that is judged at 40 px, by the classifier's own
// measure -- and it is judged when the REJOIN GRACE ENDS, not at the lift. A dropout during a drifting
// hold therefore still rejoins (one hold); a real lift swipes ~150 ms later. The grace ends two ways --
// expiry (OnIdle) and a press that is not a rejoin (OnPress) -- and both go through EndGrace.

enum class Phase : uint8_t {
    Idle,     // no stroke this policy owns (or not eligible)
    Pressed,  // held, under the ring start
    Ring,     // held, ring filling
    Grace,    // released while the ring was filling: waiting REJOIN_MS for the finger to return
    Fired,    // zoom toggled: the rest of this stroke is consumed
    Moved,    // a fast swipe (40 px inside the ring start) or a slow drag past 80 px: hands off
};

struct State {
    Phase    phase = Phase::Idle;
    uint32_t pressMs = 0;    // start of the hold; KEPT across a rejoin, so progress continues
    uint32_t releaseMs = 0;  // when the grace began
    int      x0 = 0, y0 = 0; // where the hold began (the ring's centre)
    int      lastX = 0, lastY = 0;   // the finger's latest position (a rejoin is measured from here)
    int      driftMax = 0;           // largest drift from the press point before the fire, in px
    uint32_t driftMaxAtMs = 0;       // hold time at which that drift was reached
    int      relDx = 0, relDy = 0;   // the caller's release delta at the lift, kept through the grace (rule 3)
};

/// How a stroke in the rejoin grace ended, once it is known the finger is not coming back.
struct GraceEnd {
    bool ended = false;   // false: the grace has not ended (still waiting, or there was none)
    bool swipe = false;   // rule 3: it moved 40 px or more -- classify (dx, dy) as today, i.e. a swipe
    int  dx = 0, dy = 0;  // the lift's own delta, the same numbers the caller's classifier reads
};

/// The ONE place a grace ends (expiry and a non-rejoin press both come here): rule 3, then a clean state.
inline GraceEnd EndGrace(State& s)
{
    GraceEnd e;
    e.ended = true;
    e.dx = s.relDx;
    e.dy = s.relDy;
    const int adx = std::abs(e.dx), ady = std::abs(e.dy);
    e.swipe = (adx > ady ? adx : ady) >= SWIPE_PX;   // RULE 3
    s = State{};
    return e;
}

/// Drift on the larger axis, the same measure the tap/swipe classifier uses.
inline int Drift(const State& s, int x, int y)
{
    const int dx = std::abs(x - s.x0), dy = std::abs(y - s.y0);
    return dx > dy ? dx : dy;
}

/// What a press edge is.
enum class PressIs : uint8_t { NewStroke, Rejoin, Bypass };

/// A press edge's outcome: what the press is, and -- if it ended a stroke that was in its rejoin grace
/// because it is NOT that stroke's rejoin -- how that stroke ended (`prev.ended`).
struct PressOutcome {
    PressIs  is = PressIs::NewStroke;
    GraceEnd prev;
};

/// A press edge. `eligible`: the Radar face with nothing on top (decided by the caller).
inline PressOutcome OnPress(State& s, uint32_t now, int x, int y, bool eligible)
{
    PressOutcome out;
    if (s.phase == Phase::Grace) {
        if (now - s.releaseMs <= REJOIN_MS && std::abs(x - s.lastX) <= REJOIN_PX && std::abs(y - s.lastY) <= REJOIN_PX) {
            s.phase = Phase::Ring;   // the same hold, progress kept (pressMs untouched), whatever it drifted
            out.is = PressIs::Rejoin;
            return out;
        }
        // Too late or too far: the finger did not come back, so the grace ends HERE -- through the same
        // rule as expiry -- and this press is a new stroke.
        out.prev = EndGrace(s);
    }
    if (!eligible) { s = State{}; out.is = PressIs::Bypass; return out; }
    s = State{};
    s.phase = Phase::Pressed;
    s.pressMs = now;
    s.x0 = s.lastX = x;
    s.y0 = s.lastY = y;
    out.is = PressIs::NewStroke;
    return out;
}

/// What a frame with the finger down does.
enum class HeldIs : uint8_t { Nothing, RingStart, Fire, Moved };

inline HeldIs OnHeld(State& s, uint32_t now, int x, int y)
{
    // RULE 1: once fired, the finger may go anywhere -- the stroke is consumed (OnRelease).
    if (s.phase != Phase::Pressed && s.phase != Phase::Ring) return HeldIs::Nothing;
    s.lastX = x;
    s.lastY = y;
    const uint32_t held = now - s.pressMs;
    const int d = Drift(s, x, y);
    if (d > s.driftMax) { s.driftMax = d; s.driftMaxAtMs = held; }
    // RULE 2: fast movement (inside the ring start) is a swipe at 40 px; slow drift during the ring is
    // tolerated to 80 px. Either way past the limit the existing classifier owns the release.
    if (d >= (s.phase == Phase::Pressed ? SWIPE_PX : DRIFT_PX)) {
        s.phase = Phase::Moved;
        return HeldIs::Moved;
    }
    if (held >= THRESHOLD_MS) {
        s.phase = Phase::Fired;
        return HeldIs::Fire;
    }
    if (s.phase == Phase::Pressed && held >= RING_START_MS) {
        s.phase = Phase::Ring;
        return HeldIs::RingStart;
    }
    return HeldIs::Nothing;
}

/// What a release edge is.
enum class ReleaseIs : uint8_t {
    PassThrough, // classify as today (tap or swipe) -- the ONLY outcome under the ring start
    Grace,       // ring was filling: say nothing yet, wait for a rejoin
    Consumed,    // the hold fired: no tap, no swipe, no card
};

/// A release edge. `dx, dy` is the caller's OWN release delta (release point - press point) -- the very
/// numbers its tap/swipe classifier reads. During the ring it is KEPT, not judged: rule 3 judges it when
/// the grace ends (EndGrace), so a dropout that rejoins is never mistaken for a lift.
inline ReleaseIs OnRelease(State& s, uint32_t now, int dx, int dy)
{
    switch (s.phase) {
    case Phase::Ring:
        s.phase = Phase::Grace;
        s.releaseMs = now;
        s.relDx = dx;
        s.relDy = dy;
        return ReleaseIs::Grace;
    case Phase::Fired:
        s = State{};
        return ReleaseIs::Consumed;
    default:
        s = State{};
        return ReleaseIs::PassThrough;
    }
}

/// A frame with no finger: the grace expiring ends the stroke (rule 3: a swipe if it moved 40 px,
/// otherwise the cancel -- nothing happens and the ring goes away). `ended` is false while waiting.
inline GraceEnd OnIdle(State& s, uint32_t now)
{
    if (s.phase == Phase::Grace && now - s.releaseMs > REJOIN_MS) return EndGrace(s);
    return GraceEnd{};
}

/// Is the ring on screen? (Filling while held, and held on through the grace so a dropout
/// does not blink it.)
inline bool RingVisible(const State& s) { return s.phase == Phase::Ring || s.phase == Phase::Grace; }

/// Ring progress 0..1000. FROZEN at the release during the grace: a finger that let go early must
/// never see the ring complete and then nothing happen.
inline uint32_t RingPermille(const State& s, uint32_t now)
{
    if (!RingVisible(s)) return 0;
    const uint32_t held = (s.phase == Phase::Grace ? s.releaseMs : now) - s.pressMs;
    if (held <= RING_START_MS) return 0;
    if (held >= THRESHOLD_MS) return 1000;
    return (held - RING_START_MS) * 1000UL / (THRESHOLD_MS - RING_START_MS);
}

/// The toggle: from any step but the closest, to the closest; from the closest, to the top.
/// `top` is the ladder's top index. Returns -1 on a single-step ladder (nothing to toggle).
inline int ToggleTarget(int zoomIdx, int top)
{
    if (top <= 0) return -1;
    return zoomIdx == 0 ? top : 0;
}

} // namespace longpress
