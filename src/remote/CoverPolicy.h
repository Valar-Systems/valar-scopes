#pragma once

/* ===========================================================================
 * THE REMOTE'S MOTION POLICY: the pure half (docs/remote-edition.md section 4).
 *
 * Every rule about what a touch may make a window or curtain do lives HERE, once. The manager feeds
 * touch edges and the link's snapshot of the device on screen; this says what each edge means. No
 * chip, clock, socket or screen, so every rule is graded on a host (test/host/test_remote_policy.cpp)
 * before it reaches the glass. C++11, like every host-tested header.
 *
 *   ONLY A TOUCH COMMANDS. Intents come out of OnPress / OnHeld / OnRelease, and out of OnIdle only
 *   to END a stroke already in progress. A state change, a reconnect or a boot produces nothing --
 *   there is no function here that takes a device update and returns an intent.
 *
 *   STOP ON TOUCH-DOWN. While the device is MOVING the press edge itself is STOP and the rest of the
 *   stroke is consumed. MOVING is wider than "the device says OPENING/CLOSING", because a window that
 *   moves while its own state says IDLE is the dangerous case:
 *     - the last reported op (kept when the link goes offline: a STOP is attempted when the device
 *       was last seen moving -- a STOP the device never hears costs nothing),
 *     - the position changed in the last SETTLE_MS (Glasscalibur's stall back-off drives 24,500
 *       steps AFTER publishing IDLE),
 *     - this policy emitted OPEN/CLOSE in the last EXPECT_MS (the first touch after a command is
 *       STOP even before the device has reported the motion).
 *
 *   A DELIBERATE GESTURE STARTS MOTION (ruled 2026-10-30): Hold by default, DoubleTap or SingleTap
 *   only as a per-device opt-in. The touch constants are the radar's MEASURED ones, taken from
 *   LongPressPolicy.h rather than restated, so the two policies cannot drift apart.
 * ======================================================================== */

#include <cstdint>
#include <cstdlib>

#include "LongPressPolicy.h"

namespace coverpolicy {

// ---- measured on this board's CST816 (include/LongPressPolicy.h is the one source) ----
constexpr uint32_t RING_START_MS = longpress::RING_START_MS;  // the arc appears; above the tap p99 (183 ms)
constexpr uint32_t REJOIN_MS     = longpress::REJOIN_MS;      // a split stroke rejoins inside this...
constexpr int      REJOIN_PX     = longpress::REJOIN_PX;      // ...this close to the finger's last position
constexpr int      SWIPE_PX      = longpress::SWIPE_PX;       // this much inside the ring start is a swipe
constexpr int      DRIFT_PX      = longpress::DRIFT_PX;       // after it, slow drift up to this is still the hold

// ---- this edition's own ----
constexpr uint32_t HOLD_WINDOW_MS  = 1000;  // default hold to start a window (ruled 2026-10-30)
constexpr uint32_t HOLD_CURTAIN_MS = 600;   // default hold to start a curtain
constexpr uint32_t HOLD_MIN_MS     = 400;   // the configurable range; below RING_START the arc could not show
constexpr uint32_t HOLD_MAX_MS     = 3000;
constexpr int      BAND_PX         = 10;    // split layout: this far either side of the centre line is neither
constexpr uint32_t SETTLE_MS       = 1500;  // a position change this recent still counts as moving
constexpr uint32_t EXPECT_MS       = 2000;  // after OPEN/CLOSE, touches are STOP for this long
constexpr uint32_t DOUBLE_MS       = 400;   // the second tap's press within this of the first's release...
constexpr int      DOUBLE_PX       = 30;    // ...and this close to it
constexpr uint32_t RESEND_EVERY_MS = 500;   // STOP resend cadence while still moving (ruled 2026-10-30:
constexpr uint8_t  RESEND_MAX      = 3;     //   up to 3 in 1.5 s)

enum class Conn : uint8_t { Unknown, Live, Offline };   // Live = a full state dump since the last connect
enum class Op : uint8_t { Idle, Opening, Closing };
enum class Kind : uint8_t { Window, Curtain };
enum class Gesture : uint8_t { Hold, DoubleTap, SingleTap };
enum class Layout : uint8_t { Split, Face };

/// The link's snapshot of one device, as last reported. Kept as-is while offline.
struct Device {
    Conn     conn = Conn::Unknown;
    Op       op = Op::Idle;               // the last reported operation
    Op       lastDirection = Op::Idle;    // the most recent non-Idle operation seen (the face cycle reads it)
    uint16_t positionPermille = 0;        // 0 closed .. 1000 open
    bool     motionSeen = false;          // lastMotionMs means something
    uint32_t lastMotionMs = 0;            // last time op != Idle was reported, or the position changed
    bool     alarmPending = false;        // an obstruction or tamper alarm awaiting acknowledgement
};

struct Settings {
    Gesture  gesture = Gesture::Hold;
    uint32_t holdMs = HOLD_WINDOW_MS;
    Layout   layout = Layout::Split;
};

enum class Intent : uint8_t { None, Stop, Open, Close, Ack, Next, Prev };
enum class Cause : uint8_t { None, TouchDownWhileMoving, MotionBeganUnderFinger, Hold, Tap, DoubleTap, Swipe, AckRelease };

struct Out {
    Intent intent = Intent::None;
    Cause  cause = Cause::None;
};

inline Out Make(Intent i, Cause c)
{
    Out o;
    o.intent = i;
    o.cause = c;
    return o;
}

enum class Target : uint8_t { None, Open, Close, Face };

enum class Phase : uint8_t {
    Idle,      // no stroke
    Pressed,   // down, under the ring start: a release now is a tap or a swipe
    Holding,   // down, past the ring start
    Grace,     // released while holding: waiting REJOIN_MS for a dropout to rejoin
    Consumed,  // STOPPED, or FIRED: nothing else comes of this stroke
    Moved,     // travelled too far to be a hold: a release is a swipe or nothing
};

struct Stroke {
    Phase    phase = Phase::Idle;
    uint32_t pressMs = 0;     // start of the hold; KEPT across a rejoin, so progress continues
    uint32_t releaseMs = 0;   // when the grace began
    int      x0 = 0, y0 = 0;  // where the stroke began
    int      lastX = 0, lastY = 0;
    int      relDx = 0, relDy = 0;   // the release delta, kept through the grace
    Target   target = Target::None;  // fixed at the press
    bool     liveSincePress = false; // the device has been Live on every edge of this stroke
};

/// One per device: the stroke in progress, the double-tap memory and the expect-motion window.
struct State {
    Stroke   st;
    bool     tapPending = false;
    uint32_t tapMs = 0;               // the first tap's RELEASE
    int      tapX = 0, tapY = 0;
    Target   tapTarget = Target::None;
    bool     commanded = false;       // this policy emitted OPEN/CLOSE...
    uint32_t commandMs = 0;           // ...at this time
};

// ----------------------------------------------------------------------------- what the device is doing

inline bool Moving(const Device& d, const State& s, uint32_t now)
{
    if (d.op != Op::Idle) return true;
    if (d.motionSeen && now - d.lastMotionMs <= SETTLE_MS) return true;
    if (s.commanded && now - s.commandMs <= EXPECT_MS) return true;
    return false;
}

/// May motion start now? Also what the screen offers: OPEN/CLOSE are drawn disabled when this is false.
inline bool ControlsEnabled(const Device& d, const State& s, uint32_t now)
{
    return d.conn == Conn::Live && !Moving(d, s, now) && !d.alarmPending;
}

inline uint32_t ClampHoldMs(Kind k, uint32_t ms)
{
    if (ms == 0) return k == Kind::Curtain ? HOLD_CURTAIN_MS : HOLD_WINDOW_MS;   // unset: the kind's default
    if (ms < HOLD_MIN_MS) return HOLD_MIN_MS;
    if (ms > HOLD_MAX_MS) return HOLD_MAX_MS;
    return ms;
}

// ----------------------------------------------------------------------------- where, and what

inline Target TargetAt(const Settings& g, int y, int screen)
{
    if (g.layout == Layout::Face) return Target::Face;
    const int mid = screen / 2;
    if (y < mid - BAND_PX) return Target::Open;
    if (y > mid + BAND_PX) return Target::Close;
    return Target::None;
}

/// The face button's cycle: open -> stop -> close -> stop, like a garage door.
inline Intent FaceNext(const Device& d)
{
    if (d.positionPermille <= 5) return Intent::Open;
    if (d.positionPermille >= 995) return Intent::Close;
    if (d.lastDirection == Op::Opening) return Intent::Close;   // stopped part-way: reverse
    if (d.lastDirection == Op::Closing) return Intent::Open;
    return d.positionPermille < 500 ? Intent::Open : Intent::Close;   // no known direction: the farther end
}

inline Intent Resolve(Target t, const Device& d)
{
    switch (t) {
    case Target::Open:  return Intent::Open;
    case Target::Close: return Intent::Close;
    case Target::Face:  return FaceNext(d);
    default:            return Intent::None;
    }
}

/// The one test every motion-starting site goes through.
inline bool Eligible(const Stroke& t, const Device& d, const State& s, uint32_t now)
{
    return t.target != Target::None && t.liveSincePress && ControlsEnabled(d, s, now);
}

inline Out Fire(State& s, Target t, const Device& d, uint32_t now, Cause c)
{
    const Intent i = Resolve(t, d);
    if (i == Intent::Open || i == Intent::Close) {
        s.commanded = true;
        s.commandMs = now;
    }
    return Make(i, c);
}

inline int Drift(const Stroke& t, int x, int y)
{
    const int dx = std::abs(x - t.x0), dy = std::abs(y - t.y0);
    return dx > dy ? dx : dy;
}

/// A finished stroke that travelled: a horizontal swipe moves between devices, anything else is nothing.
/// Not under an alarm takeover, where acknowledging is the only thing on offer.
inline Out Swipe(const Device& d, int dx, int dy)
{
    if (d.alarmPending) return Out();
    if (std::abs(dx) < std::abs(dy)) return Out();
    return Make(dx < 0 ? Intent::Next : Intent::Prev, Cause::Swipe);
}

inline bool Travelled(int dx, int dy)
{
    const int adx = std::abs(dx), ady = std::abs(dy);
    return (adx > ady ? adx : ady) >= SWIPE_PX;
}

// ----------------------------------------------------------------------------- the edges

inline Out OnPress(State& s, const Device& d, const Settings& g, uint32_t now, int x, int y, int screen)
{
    // STOP FIRST, on the press edge, whatever the gesture, layout, alarm or rejoin.
    if (Moving(d, s, now)) {
        s.st = Stroke();
        s.st.phase = Phase::Consumed;
        s.tapPending = false;
        return Make(Intent::Stop, Cause::TouchDownWhileMoving);
    }
    // A dropout rejoining: the same hold, progress kept.
    if (s.st.phase == Phase::Grace && now - s.st.releaseMs <= REJOIN_MS
        && std::abs(x - s.st.lastX) <= REJOIN_PX && std::abs(y - s.st.lastY) <= REJOIN_PX) {
        s.st.phase = Phase::Holding;
        s.st.lastX = x;
        s.st.lastY = y;
        if (d.conn != Conn::Live) s.st.liveSincePress = false;
        return Out();
    }
    // A new stroke. A grace this press interrupts ends with NOTHING: an ambiguous sequence never moves
    // anything.
    s.st = Stroke();
    s.st.phase = Phase::Pressed;
    s.st.pressMs = now;
    s.st.x0 = s.st.lastX = x;
    s.st.y0 = s.st.lastY = y;
    s.st.target = TargetAt(g, y, screen);
    s.st.liveSincePress = (d.conn == Conn::Live);
    return Out();
}

inline Out OnHeld(State& s, const Device& d, const Settings& g, uint32_t now, int x, int y)
{
    Stroke& t = s.st;
    if (t.phase != Phase::Pressed && t.phase != Phase::Holding) return Out();
    // Motion began under a finger that has not fired: STOP. (A stroke that fired is Consumed, so it
    // never stops the motion it started.)
    if (Moving(d, s, now)) {
        t.phase = Phase::Consumed;
        return Make(Intent::Stop, Cause::MotionBeganUnderFinger);
    }
    if (d.conn != Conn::Live) t.liveSincePress = false;
    t.lastX = x;
    t.lastY = y;
    if (Drift(t, x, y) >= (t.phase == Phase::Pressed ? SWIPE_PX : DRIFT_PX)) {
        t.phase = Phase::Moved;
        return Out();
    }
    const uint32_t held = now - t.pressMs;
    if (t.phase == Phase::Pressed && held >= RING_START_MS) t.phase = Phase::Holding;
    if (g.gesture == Gesture::Hold && held >= g.holdMs && Eligible(t, d, s, now)) {
        t.phase = Phase::Consumed;   // FIRED
        return Fire(s, t.target, d, now, Cause::Hold);
    }
    return Out();
}

/// A short stroke ended (released under the ring start): a swipe, an ack, a tap, or nothing.
inline Out EndShort(State& s, const Device& d, const Settings& g, uint32_t now, int dx, int dy)
{
    const Stroke t = s.st;
    s.st = Stroke();
    if (Travelled(dx, dy)) return Swipe(d, dx, dy);
    if (d.alarmPending) return Make(Intent::Ack, Cause::AckRelease);
    if (g.gesture == Gesture::SingleTap) {
        if (Eligible(t, d, s, now)) return Fire(s, t.target, d, now, Cause::Tap);
        return Out();
    }
    if (g.gesture == Gesture::DoubleTap) {
        const bool second = s.tapPending && t.pressMs - s.tapMs <= DOUBLE_MS
            && std::abs(t.x0 - s.tapX) <= DOUBLE_PX && std::abs(t.y0 - s.tapY) <= DOUBLE_PX
            && t.target == s.tapTarget;
        if (second) {
            s.tapPending = false;
            if (Eligible(t, d, s, now)) return Fire(s, t.target, d, now, Cause::DoubleTap);
            return Out();
        }
        s.tapPending = true;   // this tap is the first of a possible pair
        s.tapMs = now;
        s.tapX = t.x0;
        s.tapY = t.y0;
        s.tapTarget = t.target;
        return Out();
    }
    return Out();   // Hold: a tap does nothing
}

/// A held stroke's rejoin grace ran out: the finger is not coming back.
inline Out EndGrace(State& s, const Device& d, const Settings& g, uint32_t now)
{
    const Stroke t = s.st;
    s.st = Stroke();
    if (Travelled(t.relDx, t.relDy)) return Swipe(d, t.relDx, t.relDy);
    if (d.alarmPending) return Make(Intent::Ack, Cause::AckRelease);
    // A SingleTap user's longer press is still their tap: they may not be able to make a short one.
    if (g.gesture == Gesture::SingleTap && Eligible(t, d, s, now)) return Fire(s, t.target, d, now, Cause::Tap);
    return Out();
}

inline Out OnRelease(State& s, const Device& d, const Settings& g, uint32_t now, int x, int y)
{
    Stroke& t = s.st;
    const int dx = x - t.x0, dy = y - t.y0;
    switch (t.phase) {
    case Phase::Pressed:
        return EndShort(s, d, g, now, dx, dy);
    case Phase::Holding:
        // Under an alarm nothing can fire, so there is nothing to wait for: acknowledge on the release.
        if (d.alarmPending) {
            s.st = Stroke();
            if (Travelled(dx, dy)) return Out();
            return Make(Intent::Ack, Cause::AckRelease);
        }
        t.phase = Phase::Grace;
        t.releaseMs = now;
        t.relDx = dx;
        t.relDy = dy;
        return Out();
    case Phase::Moved:
        s.st = Stroke();
        return Travelled(dx, dy) ? Swipe(d, dx, dy) : Out();
    default:   // Consumed or Idle
        s.st = Stroke();
        return Out();
    }
}

/// A frame with no finger. Only ever ENDS a stroke already in progress; with none it returns nothing,
/// whatever the device is doing.
inline Out OnIdle(State& s, const Device& d, const Settings& g, uint32_t now)
{
    if (s.tapPending && now - s.tapMs > DOUBLE_MS) s.tapPending = false;
    if (s.st.phase == Phase::Grace && now - s.st.releaseMs > REJOIN_MS) return EndGrace(s, d, g, now);
    return Out();
}

// ----------------------------------------------------------------------------- STOP resend

/// After a STOP: resend every RESEND_EVERY_MS while the device still reports moving, at most
/// RESEND_MAX times. A new STOP restarts it (StartStop). OPEN and CLOSE have no resend path at all.
struct Resend {
    bool     active = false;
    uint8_t  sent = 0;
    uint32_t lastMs = 0;
};

inline void StartStop(Resend& r, uint32_t now)
{
    r.active = true;
    r.sent = 0;
    r.lastMs = now;
}

inline bool ResendDue(Resend& r, uint32_t now, bool stillMoving)
{
    if (!r.active) return false;
    if (!stillMoving || r.sent >= RESEND_MAX) { r.active = false; return false; }
    if (now - r.lastMs < RESEND_EVERY_MS) return false;
    ++r.sent;
    r.lastMs = now;
    return true;
}

} // namespace coverpolicy
