#pragma once

// Follow Mode -- the state machine and the copy.  Spec: §5 and §6.
//
// "The feature is the states, not the picture" (§19). This file is the feature.
//
// =============================================================================
// PURE ON PURPOSE
//
// Everything here takes its inputs as arguments and returns a value. Nothing
// reads a member of AircraftManager, touches the display, or calls millis().
// Two reasons, and the second is the one that matters later:
//
//  1. It is the only way to reason about a state machine whose entire job is to
//     be right about ambiguous evidence.
//  2. §17 requires a HOST TEST that sets a distinctive follow value and asserts
//     it appears in no outbound payload. That test cannot exist while the
//     builders live inside a translation unit that needs Arduino. Extracting
//     them is named in the spec as the prerequisite, and doing it now costs
//     nothing; doing it later means rewriting whatever grew in the meantime.
//
// The only Arduino dependency is String, for the copy builders. The state
// machine itself is free of it.
//
// =============================================================================
// §5.1 -- ABSENCE IS THREE STATES, NOT ONE
//
// The core of the design and the thing most likely to be got wrong. A naive
// machine has one absence state called LOST and treats it as an error. That is
// wrong here for a reason about people rather than code: ABSENCE IS THE NORMAL
// OPERATING CONDITION of this feature, and the three kinds mean completely
// different things to the person watching.
//
//   NO_COVERAGE     he is somewhere ground receivers do not reach   EXPECTED
//   APPROACH_LOST   last seen descending toward the field           EXPECTED
//   SIGNAL_LOST     he should be visible and is not                 not expected
//
// The device can tell them apart. NO_COVERAGE is a POSITION argument.
// APPROACH_LOST is a PROFILE argument -- descending, slowing, inside the home
// radius. SIGNAL_LOST is what is left over.
//
// Collapsing them is what makes this feature frightening instead of reassuring,
// and no amount of good copy rescues a machine that cannot tell expected absence
// from unexpected absence.
//
// =============================================================================
// §5.4 -- THE RAIL
//
// A LOST SIGNAL MUST NEVER BE REPORTED AS A LANDING. Getting "he's down safely"
// when the truth is "we stopped hearing him" is the worst thing this product
// could do, and the whole machine is arranged to prevent it. Landed() fires only
// on confident evidence: sustained onGround, or low-and-slow inside the home
// radius. Everything else routes to an absence state and is worded as one.
//
// If you change one thing in this file, do not change that.

#include <Arduino.h>

#include <cmath>
#include <cstdint>

namespace follow {

// §5.2 lifecycle states. WAITING has no face yet -- C4 is [UNKNOWN] and §6 says
// its copy is not written, so the machine models it but nothing renders it.
enum class State : uint8_t {
    Idle,          // no follow target set; the screen is hidden entirely
    Waiting,       // target set, nothing seen yet          [C4: no face yet]
    Ground,        // fixes arriving, onGround true
    Airborne,
    NoCoverage,    // expected absence, by position
    SignalLost,    // unexpected absence
    ApproachLost,  // expected absence, by profile
    Landed,
};

// One ADS-B fix, in the units the card already uses. Everything here is
// available per-fix today (§5.3) -- nothing new is asked of the feed.
struct Fix {
    bool  onGround      = false;
    float baroAltFt     = 0.0f;
    float geoAltFt      = 0.0f;
    float velocityKt    = 0.0f;
    float verticalRateFpm = 0.0f;
    float lat           = 0.0f;
    float lon           = 0.0f;
};

// What the machine knows about home. Per C5 this is a LOOKUP, not a
// calibration -- the field elevation arrives at boot rather than after two
// flights, which makes the machine simpler rather than different.
struct HomeContext {
    float lat        = 0.0f;
    float lon        = 0.0f;
    float radiusKm   = 8.0f;   // "inside the home radius"
    float elevationFt = 0.0f;  // published field elevation; AGL = alt - this
    bool  known      = false;  // false -> AGL reasoning is unavailable, say so
};

// -----------------------------------------------------------------------------
// CONSTANTS, ALL OF WHICH ARE GUESSES UNTIL A REAL FLIGHT IS LOGGED (§18.3).
//
// Named and gathered rather than scattered, so that when the first logged lesson
// arrives there is exactly one place to correct and the corrections are visibly
// corrections. Guessing these from first principles is, in the spec's words, how
// you get a state machine that is elegant and wrong -- so they are marked, not
// defended.
// -----------------------------------------------------------------------------
struct Tuning {
    uint8_t  airborneConfirmFixes = 2;        // so one spurious fix cannot announce a takeoff
    uint32_t landedConfirmMs      = 30000;    // sustained onGround before LANDED
    uint32_t trackLostMs          = 180000;   // ~3 min, per §5.3 "start ~3 min"
    uint32_t newFlightGapMs       = 1800000;  // 30 min: a resumed track is a new flight
    float    approachDescentFpm   = -300.0f;  // descending, for the profile argument
    float    approachAltAglFt     = 3000.0f;  // low enough that the field is plausible
    float    lowLevelAglFt        = 2500.0f;  // below this, ground coverage is patchy
    float    slowKt               = 80.0f;    // low-and-slow, with the radius, means landed
};

// Great-circle-ish separation in km, matching the approximation used elsewhere in
// this codebase for short baselines (AircraftManager::IsOverhead).
inline float SeparationKm(float aLat, float aLon, float bLat, float bLon)
{
    const float dLat = (aLat - bLat) * 111.0f;
    const float dLon = (aLon - bLon) * 111.0f * cosf(radians(aLat));
    return sqrtf(dLat * dLat + dLon * dLon);
}

inline bool InsideHome(const Fix& f, const HomeContext& home)
{
    return home.known && SeparationKm(f.lat, f.lon, home.lat, home.lon) <= home.radiusKm;
}

// Height above the field, when the field elevation is known. Falls back to the
// barometric figure, which is wrong by the field elevation -- so callers that
// care must check home.known rather than trusting the number.
inline float AglFt(const Fix& f, const HomeContext& home)
{
    const float alt = f.geoAltFt > 0.0f ? f.geoAltFt : f.baroAltFt;
    return home.known ? (alt - home.elevationFt) : alt;
}

class Machine {
public:
    State Current() const { return state; }
    const Fix& LastFix() const { return last; }
    uint32_t LastFixMs() const { return lastFixMs; }

    void SetTarget(bool haveTarget)
    {
        if (!haveTarget) { state = State::Idle; airborneRun = 0; haveLast = false; return; }
        if (state == State::Idle) state = State::Waiting;
    }

    // A fix arrived for the followed aircraft.
    void OnFix(const Fix& f, uint32_t nowMs, const HomeContext& home, const Tuning& t = Tuning{})
    {
        if (state == State::Idle) return;

        // A long enough gap makes the resumed track a NEW FLIGHT rather than the
        // same one -- otherwise a lesson picked up tomorrow appends to yesterday.
        if (haveLast && (nowMs - lastFixMs) > t.newFlightGapMs) {
            airborneRun = 0;
            newFlight = true;
        }

        last = f;
        lastFixMs = nowMs;
        haveLast = true;

        if (f.onGround) {
            if (state == State::Airborne || IsAbsent(state)) {
                // Sustained onGround is one of the two confident landing
                // arguments (§5.4). Start the clock rather than announcing.
                if (groundSinceMs == 0) groundSinceMs = nowMs;
                if ((nowMs - groundSinceMs) >= t.landedConfirmMs) state = State::Landed;
            } else {
                groundSinceMs = groundSinceMs ? groundSinceMs : nowMs;
                if (state != State::Landed) state = State::Ground;
            }
            airborneRun = 0;
            return;
        }

        groundSinceMs = 0;
        if (airborneRun < 255) ++airborneRun;
        if (airborneRun >= t.airborneConfirmFixes) {
            state = State::Airborne;
            newFlight = false;
        }
    }

    // No fix this pass. nowMs advances; the machine decides which KIND of absence
    // this is -- which is the whole point of §5.1.
    void OnNoFix(uint32_t nowMs, const HomeContext& home, const Tuning& t = Tuning{})
    {
        if (state == State::Idle || state == State::Waiting || state == State::Landed)
            return;
        if (!haveLast)
            return;
        if ((nowMs - lastFixMs) < t.trackLostMs)
            return; // not absent yet; a gap is not a state

        // THE SECOND CONFIDENT LANDING ARGUMENT: low and slow inside the home
        // radius, with onGround never seen. This is the only other route to
        // Landed, and it is deliberately narrow.
        const float agl = AglFt(last, home);
        const bool inside = InsideHome(last, home);
        if (inside && home.known && agl < 200.0f && last.velocityKt < t.slowKt) {
            state = State::Landed;
            return;
        }

        // PROFILE ARGUMENT -- descending toward the field. Expected absence.
        if (inside && last.verticalRateFpm <= t.approachDescentFpm && agl <= t.approachAltAglFt) {
            state = State::ApproachLost;
            return;
        }

        // POSITION ARGUMENT -- low near home is pattern work, where ground
        // coverage is patchy by nature. §10: a local dropout must NOT use the
        // alarming copy; it is the local analogue of APPROACH_LOST. Getting this
        // backwards makes the device look broken every single circuit.
        if (inside && home.known && agl <= t.lowLevelAglFt) {
            state = State::NoCoverage;
            return;
        }

        // Everything left over.
        state = State::SignalLost;
    }

    static bool IsAbsent(State s)
    {
        return s == State::NoCoverage || s == State::SignalLost || s == State::ApproachLost;
    }

    // Expected absence reads differently from unexpected absence, and callers
    // (alert gating, colour choice) should ask this rather than re-deriving it.
    static bool IsExpectedAbsence(State s)
    {
        return s == State::NoCoverage || s == State::ApproachLost;
    }

private:
    State    state = State::Idle;
    Fix      last{};
    bool     haveLast = false;
    bool     newFlight = false;
    uint32_t lastFixMs = 0;
    uint32_t groundSinceMs = 0;
    uint8_t  airborneRun = 0;
};

// =============================================================================
// §6 -- THE COPY. This is the product, not the rendering.
//
// A hobbyist radar with a coverage gap is a shrug. A device someone's spouse is
// watching that goes dark mid-Atlantic is frightening, and "we designed the gap
// in" is only true if the words on the screen are right.
//
//   1. NAME THE MECHANISM. "No receivers here" is calming because it explains.
//      "Signal lost" alone is not.
//   2. NEVER IMPLY CERTAINTY WE DO NOT HAVE. No landing claim without evidence.
//   3. DIFFERENT WORDS FOR DIFFERENT STATES. If NO_COVERAGE and SIGNAL_LOST read
//      the same, the state machine's work is wasted.
//
// ASCII only: there is no setFont() in the radar draw path, so the glyph set is
// the default font's. The spec writes middots; the device cannot render one, and
// a UTF-8 middot arrives as two bytes of garbage. Same finding as RouteLabel.h.
// =============================================================================

inline const char* Headline(State s)
{
    switch (s) {
        case State::Idle:         return "";
        case State::Waiting:      return "WAITING";        // [C4] no face yet
        case State::Ground:       return "ON THE GROUND";
        case State::Airborne:     return "AIRBORNE";
        case State::NoCoverage:   return "BELOW COVERAGE"; // §10: NOT "signal lost"
        case State::SignalLost:   return "SIGNAL LOST";
        case State::ApproachLost: return "ON APPROACH - SIGNAL LOST";
        case State::Landed:       return "ON THE GROUND";
    }
    return "";
}

// The reassuring second line. Every one of these names the MECHANISM -- it is a
// statement about OUR equipment rather than about him, which is the whole
// difference between an alarming absence and an explained one.
inline const char* Explanation(State s)
{
    switch (s) {
        case State::NoCoverage:
            return "Ground receivers do not reach where he is now.";
        case State::SignalLost:
            return "He is out of receiver range, not off the radar.";
        case State::ApproachLost:
            return "Low-level coverage is patchy. This usually means landed.";
        default:
            return "";
    }
}

// ntfy title. Empty means "do not send" -- SIGNAL_LOST is screen-only by default
// (§15): the asymmetry is the argument, since a missed lost-alert costs mild
// worry and an unwanted one costs panic.
inline String AlertTitle(State s, const String& target)
{
    switch (s) {
        case State::Airborne:     return target + " is airborne";
        case State::Landed:       return target + " is down";
        case State::ApproachLost: return target + " - last seen on approach";
        default:                  return String("");
    }
}

} // namespace follow
