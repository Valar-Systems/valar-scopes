#pragma once

/* ===========================================================================
 * LONG PRESS TO TOGGLE ZOOM: the pure half (docs/v17-long-press-zoom.md).
 *
 * The device feeds touch edges and frames in; this says what each one means. It
 * needs no chip, no clock and no screen, so every rule is graded on a host
 * (test/host/test_long_press.cpp) before it reaches the panel.
 *
 *   0 .. RING_START_MS        nothing visible; a release is a TAP, exactly as before
 *   RING_START_MS .. THRESHOLD  the ring fills; a release starts the REJOIN grace
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
constexpr int      MOVE_PX       = 40;   // == SWIPE_MIN: this much movement is a swipe, not a hold
constexpr int      REJOIN_PX     = 20;   // a rejoining press lands this close to the hold

enum class Phase : uint8_t {
    Idle,     // no stroke this policy owns (or not eligible)
    Pressed,  // held, under the ring start
    Ring,     // held, ring filling
    Grace,    // released while the ring was filling: waiting REJOIN_MS for the finger to return
    Fired,    // zoom toggled: the rest of this stroke is consumed
    Moved,    // moved MOVE_PX or more before firing: an ordinary swipe now, hands off
};

struct State {
    Phase    phase = Phase::Idle;
    uint32_t pressMs = 0;    // start of the hold; KEPT across a rejoin, so progress continues
    uint32_t releaseMs = 0;  // when the grace began
    int      x0 = 0, y0 = 0; // where the hold began (the ring's centre)
};

/// What a press edge is.
enum class PressIs : uint8_t { NewStroke, Rejoin, Bypass };

/// A press edge. `eligible`: the Radar face with nothing on top (decided by the caller).
inline PressIs OnPress(State& s, uint32_t now, int x, int y, bool eligible)
{
    if (s.phase == Phase::Grace) {
        if (now - s.releaseMs <= REJOIN_MS && std::abs(x - s.x0) <= REJOIN_PX && std::abs(y - s.y0) <= REJOIN_PX) {
            s.phase = Phase::Ring;   // the same hold, progress kept (pressMs untouched)
            return PressIs::Rejoin;
        }
        // Too late or too far: the hold was cancelled (the caller logs it from OnFrame or here)
        // and this press is a new stroke.
    }
    if (!eligible) { s = State{}; return PressIs::Bypass; }
    s = State{};
    s.phase = Phase::Pressed;
    s.pressMs = now;
    s.x0 = x;
    s.y0 = y;
    return PressIs::NewStroke;
}

/// What a frame with the finger down does.
enum class HeldIs : uint8_t { Nothing, RingStart, Fire, Moved };

inline HeldIs OnHeld(State& s, uint32_t now, int x, int y)
{
    if (s.phase != Phase::Pressed && s.phase != Phase::Ring) return HeldIs::Nothing;
    if (std::abs(x - s.x0) >= MOVE_PX || std::abs(y - s.y0) >= MOVE_PX) {
        s.phase = Phase::Moved;      // a swipe: the existing classifier owns the release
        return HeldIs::Moved;
    }
    const uint32_t held = now - s.pressMs;
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

inline ReleaseIs OnRelease(State& s, uint32_t now)
{
    switch (s.phase) {
    case Phase::Ring:
        s.phase = Phase::Grace;
        s.releaseMs = now;
        return ReleaseIs::Grace;
    case Phase::Fired:
        s = State{};
        return ReleaseIs::Consumed;
    default:
        s = State{};
        return ReleaseIs::PassThrough;
    }
}

/// A frame with no finger: the grace expiring is a cancel (nothing happens; the ring goes away).
inline bool OnIdleCancels(State& s, uint32_t now)
{
    if (s.phase == Phase::Grace && now - s.releaseMs > REJOIN_MS) {
        s = State{};
        return true;
    }
    return false;
}

/// Is the ring on screen? (Filling while held, and held on through the grace so a dropout
/// does not blink it.)
inline bool RingVisible(const State& s) { return s.phase == Phase::Ring || s.phase == Phase::Grace; }

/// Ring progress 0..1000.
inline uint32_t RingPermille(const State& s, uint32_t now)
{
    if (!RingVisible(s)) return 0;
    const uint32_t held = now - s.pressMs;
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
