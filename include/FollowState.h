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
// As of the local face this file has NO Arduino dependency at all. The one that
// remained was `String` in AlertTitle, and it was removed rather than worked
// around: the builder now formats into a caller-supplied buffer. That is better
// on the device anyway (no heap churn in an alert path) and it is what makes the
// host test below possible -- see test/host/test_follow_state.cpp.
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

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace follow {

// Arduino's radians() is a macro on a header this file deliberately does not
// include. The constant, not the macro.
constexpr float DEG_TO_RAD_F = 0.01745329252f;

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
//
// =============================================================================
// TWO KINDS OF KNOWLEDGE, AND CONFLATING THEM BROKE §10
//
// This struct carried ONE flag, `known`, and stage 1 sets it false because the
// field elevation is not delivered yet (C5 -- the corpus has AltitudeFeet for
// 34,128 fields and no running code writes it to KV). That was correct about
// elevation and WRONG about position, because InsideHome() was gated on the
// same flag.
//
// The consequence was the exact failure §10 names in bold: with `known` false,
// InsideHome() returned false for every fix, every absence arm that needs a
// position fell through, and every pattern dropout came out as SIGNAL LOST --
// "Getting this backwards makes the device look broken every single circuit."
//
// The device has ALWAYS known where home is. It is the configured location; it
// is what the radar is centred on. The two facts are independent and they are
// now two flags:
//
//   positionKnown   we know where home is             -- true whenever the
//                                                        device has a location
//   elevationKnown  we know the field's elevation     -- false until C5's
//                                                        delivery half exists
//
// The rule for reading them: any argument about WHERE he is needs
// positionKnown. Any argument about HOW HIGH he is above the ground needs
// elevationKnown, and without it the honest move is to decline the argument
// rather than to substitute MSL -- at a 3,400 ft field a 1,000 ft circuit reads
// 4,400 ft, which is above every threshold in Tuning and silently wrong.
struct HomeContext {
    float lat        = 0.0f;
    float lon        = 0.0f;
    float radiusKm   = 8.0f;   // "inside the home radius"
    float elevationFt = 0.0f;  // published field elevation; AGL = alt - this
    bool  positionKnown  = false; // false -> no argument about WHERE he is
    bool  elevationKnown = false; // false -> no argument about HOW HIGH he is
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
    const float dLon = (aLon - bLon) * 111.0f * cosf(aLat * DEG_TO_RAD_F);
    return sqrtf(dLat * dLat + dLon * dLon);
}

// A POSITION argument, so it needs the position half of home and nothing else.
inline bool InsideHome(const Fix& f, const HomeContext& home)
{
    return home.positionKnown &&
           SeparationKm(f.lat, f.lon, home.lat, home.lon) <= home.radiusKm;
}

// MSL, from the best source the fix carries. Named for what it is: no caller
// should be able to mistake this for a height above the ground.
inline float AltitudeMslFt(const Fix& f)
{
    return f.geoAltFt > 0.0f ? f.geoAltFt : f.baroAltFt;
}

// Height above the field. ONLY meaningful when home.elevationKnown -- and rather
// than returning a plausible-looking wrong number when it is not, this returns
// NaN, so a caller that forgot to check produces something visibly broken
// instead of something quietly off by the field elevation.
inline float AglFt(const Fix& f, const HomeContext& home)
{
    if (!home.elevationKnown) return NAN;
    return AltitudeMslFt(f) - home.elevationFt;
}

// -----------------------------------------------------------------------------
// THE FLIGHT'S NUMBERS (§11) -- accumulated per flight, frozen on landing.
//
// Pure, and here rather than in FollowLog.h, because it accumulates from Fixes
// and because FollowLog needs Preferences and could therefore never be graded on
// the workstation. Four numbers is not much arithmetic, but "how long was he
// up?" is exactly the kind of thing that is off by an hour on a wrap and nobody
// notices until someone flies at midnight.
//
// Altitude is MSL and is named for it. C5's elevation half is not delivered, so
// there is no honest AGL here -- and a post-flight card is the worst place for a
// number that is quietly wrong by the field elevation, because it is the one
// part of Follow the customer keeps looking at after the fact.
// -----------------------------------------------------------------------------
struct FlightStats {
    bool     started      = false;
    uint32_t startMs      = 0;
    uint32_t lastMs       = 0;
    float    maxAltMslFt  = 0.0f;
    float    topSpeedKt   = 0.0f;
    float    furthestKm   = 0.0f;

    void Reset() { *this = FlightStats{}; }

    void OnFix(const Fix& f, uint32_t nowMs, const HomeContext& home)
    {
        // The clock starts at the first AIRBORNE fix, not at the first fix.
        // An aeroplane that sat on the apron with its transponder on for forty
        // minutes did not fly for forty minutes, and a duration that says it did
        // is the card's easiest way to be wrong.
        if (f.onGround)
            return;
        if (!started) { started = true; startMs = nowMs; }
        lastMs = nowMs;

        const float alt = AltitudeMslFt(f);
        if (alt > maxAltMslFt) maxAltMslFt = alt;
        if (f.velocityKt > topSpeedKt) topSpeedKt = f.velocityKt;
        if (home.positionKnown) {
            const float d = SeparationKm(f.lat, f.lon, home.lat, home.lon);
            if (d > furthestKm) furthestKm = d;
        }
    }

    // millis() wraps at 49.7 days. Unsigned subtraction gives the right answer
    // across the wrap, which is why both stamps are uint32 and neither is ever
    // compared with < to the other.
    uint32_t DurationSec() const
    {
        return started ? (uint32_t)((lastMs - startMs) / 1000u) : 0u;
    }
};

// -----------------------------------------------------------------------------
// DECLINE, DO NOT PRINT: the same rule AglFt() follows (§18.3)
//
// The bench showed `-900 ft MSL` on the local face, under an `AIRBORNE`
// headline, rendered without complaint. That is the failure this whole file is
// arranged against, one layer out: a number the device cannot defend, printed in
// the same typeface as one it can.
//
// TWO RULES, because one of them does not catch the reported case and saying so
// matters more than a tidy single test:
//
//  1. PHYSICAL BOUNDS, which CANNOT be the whole answer, and Bar Yehuda is the
//     reason. A bounds check has to pick a floor, and any floor tight enough to
//     reject -900 ft rejects a REAL PLACE: Bar Yehuda, in the Dead Sea basin,
//     is an operating airfield at -1,266 ft. A check that rejects real places is
//     one that whoever hits it next will loosen, and loosened checks stay loose.
//     So the floor is set below the real world (-1,500 ft, ceiling ~60,000 ft)
//     and it catches only readings that are broken as PHYSICS.
//     -900 ft passes it. That is not a gap in the rule; it is the rule being
//     honest about what a bound can know.
//
//  2. CONTRADICTION WITH THE STATE, which is what actually catches it. A bound
//     asks "is this value possible anywhere?" and the answer for -900 ft is yes.
//     The useful question is "is it possible HERE, given what we already
//     claim?" -- and if the machine says AIRBORNE while the altitude is at or
//     below sea level, the two disagree. We do not know which is wrong, so the
//     honest render is neither: decline the number, keep the headline, which is
//     the thing the customer is actually reading.
//
//     The generalisation, which is worth more than this instance: WHEN A VALUE
//     CANNOT BE JUDGED IN ISOLATION, JUDGE IT AGAINST SOMETHING ELSE THE DEVICE
//     ALREADY ASSERTS. Two cheap readings that must agree beat one expensive
//     threshold that has to be right on its own.
//
// The cost of rule 2 is an aircraft genuinely airborne below sea level over the
// Dead Sea, which loses a readout and keeps its state. That is the right trade
// on a device sold for watching one aeroplane fly circuits.
constexpr float ALT_FLOOR_FT   = -1500.0f;
constexpr float ALT_CEILING_FT = 60000.0f;
constexpr float SPEED_CEILING_KT = 1000.0f;

inline bool PlausibleAltFt(float ft)
{
    return std::isfinite(ft) && ft >= ALT_FLOOR_FT && ft <= ALT_CEILING_FT;
}

inline bool PlausibleSpeedKt(float kt)
{
    return std::isfinite(kt) && kt >= 0.0f && kt <= SPEED_CEILING_KT;
}

/// Rule 1 AND rule 2 together. `airborne` is the machine's own verdict.
inline bool ReportableAltFt(float ft, bool airborne)
{
    if (!PlausibleAltFt(ft)) return false;
    if (airborne && ft <= 0.0f) return false;
    return true;
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
        // `home` is unused on this path and stays in the signature deliberately:
        // OnFix and OnNoFix are the machine's two inputs and callers should not
        // have to remember which of them currently happens to consult home. The
        // day a takeoff needs the home radius, the call sites do not change.
        (void)home;
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

        const bool  inside = InsideHome(last, home);
        const float agl    = AglFt(last, home); // NaN unless elevationKnown

        // THE SECOND CONFIDENT LANDING ARGUMENT: low and slow inside the home
        // radius, with onGround never seen. This is the only other route to
        // Landed, and it is deliberately narrow.
        //
        // It needs a real AGL and therefore a real field elevation. Without one
        // the argument is simply not available, and per §5.4 the machine
        // declines it rather than approximating: a landing claimed on a
        // substituted number is the exact failure the rail exists to prevent.
        // Every NaN comparison below is false, which is the behaviour wanted --
        // but it is written explicitly because relying on that is a trap.
        if (inside && home.elevationKnown && agl < 200.0f && last.velocityKt < t.slowKt) {
            state = State::Landed;
            return;
        }

        // PROFILE ARGUMENT -- descending toward the field. Expected absence.
        // Also needs AGL: "1,200 ft over the field" is the copy, and quoting an
        // MSL figure as though it were a height above the field is how the
        // hedged message stops being honest.
        if (inside && home.elevationKnown &&
            last.verticalRateFpm <= t.approachDescentFpm && agl <= t.approachAltAglFt) {
            state = State::ApproachLost;
            return;
        }

        // POSITION ARGUMENT -- and it is a POSITION argument, which is why it
        // survives the missing elevation when the two above do not.
        //
        // §5.1 states it plainly: "NO_COVERAGE is a POSITION argument." An
        // aircraft last seen inside the home radius that stopped being heard is
        // in the pattern or on the ground, and either way "ground receivers do
        // not reach where he is now" is TRUE. §10: a local dropout must NOT use
        // the alarming copy; it is the local analogue of APPROACH_LOST, and
        // getting it backwards makes the device look broken every single
        // circuit.
        //
        // The altitude test is kept as a CEILING and only where it can be
        // afforded: with a known field elevation, an aircraft loitering high
        // over home is not below coverage and should read SIGNAL LOST. Without
        // one, position alone decides -- which is the honest reading of §5.1 and
        // is right for the regime that ships first, where the followed aircraft
        // is a trainer in the circuit rather than an airliner overhead.
        if (inside && (!home.elevationKnown || agl <= t.lowLevelAglFt)) {
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
// REGIME -- WHICH FACE, AND WHY IT IS INFERRED RATHER THAN CONFIGURED
//
// §7.1: "an aircraft that stays inside the home radius is local; one that
// leaves it is not." One sentence, and it does the whole job -- there is no
// config key for this and there must not be, because the customer following a
// trainer and the customer following a son's airliner both just typed a tail
// number into the same box.
//
// THE HYSTERESIS IS FREE, and that is why the input is what it is. This takes
// the FLIGHT'S FURTHEST EXTENT, not the current separation. FlightStats keeps
// furthestKm as a maximum that never falls and resets on a new flight, so a
// circuit that clips the radius boundary cannot flap the face back and forth
// once per lap -- the regime latches for the flight and starts fresh with the
// next one. Feeding this the live separation instead would make the face
// oscillate exactly where the aircraft spends most of its time.
enum class Regime : uint8_t {
    Local,    // the flight school regime -- rings around home (§10)
    Airline,  // the arc (§8) or the globe (§9)
};

/// Which regime this flight is in. `positionKnown` false is deliberately Local:
/// the local face DECLINES visibly when there is no home ("SET YOUR LOCATION"),
/// while the arc face would happily draw a bearing wedge from a home it does
/// not have -- a confident pointer at nothing. Decline beats a plausible wrong
/// answer here for exactly the reason AglFt() returns NaN.
inline Regime RegimeFor(float furthestKm, float homeRadiusKm, bool positionKnown)
{
    if (!positionKnown) return Regime::Local;
    if (!std::isfinite(furthestKm) || !std::isfinite(homeRadiusKm)) return Regime::Local;
    return furthestKm > homeRadiusKm ? Regime::Airline : Regime::Local;
}

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

// THE NOTE THAT USED TO SIT HERE HAS COME TRUE (stage 2, 2026-08-27). It read:
// "NoCoverage is worded for the LOCAL regime, which is the only one that ships.
// §6 gives it different words out of range... so this becomes regime-dependent
// at that point. It is one string today because there is one regime today, not
// because the two agree." The arc face is that point.
//
// TWO REGIMES, ONE SWITCH, and deliberately not two functions. A parallel
// HeadlineAirline() would be the same fact written twice -- the exact shape
// RouteLabel.h was created to remove, and the shape CI has checkers for
// elsewhere in this repo. The regime is a parameter; the strings stay in one
// place where a reader can see that they differ and how.
//
// The default is Local, so every existing caller and every stage-1 assertion
// keeps its meaning.
//
// WHAT MOVES, AND WHY:
//   NoCoverage    local  "BELOW COVERAGE"  -- §10: a circuit dropout, not a fault
//                 airline "NO COVERAGE"    -- §8's chip: mid-ocean, by design
//   ApproachLost  local  "ON APPROACH - SIGNAL LOST"
//                 airline "BELOW COVERAGE" -- §8's chip, with "expected at this
//                                            range" beneath it
// The airline ApproachLost chip borrows the local regime's NoCoverage words on
// purpose: in each regime the phrase names the same physical thing (he is below
// where the receivers reach), and the two regimes never appear on one screen.
inline const char* Headline(State s, Regime r = Regime::Local)
{
    switch (s) {
        case State::Idle:         return "";
        case State::Waiting:      return "WAITING FOR DEPARTURE";   // C4
        case State::Ground:       return "ON THE GROUND";
        case State::Airborne:     return "AIRBORNE";
        case State::NoCoverage:
            return r == Regime::Airline ? "NO COVERAGE"     // §8's chip
                                        : "BELOW COVERAGE"; // §10: NOT "signal lost"
        case State::SignalLost:   return "SIGNAL LOST";
        case State::ApproachLost:
            return r == Regime::Airline ? "BELOW COVERAGE"  // §8's chip
                                        : "ON APPROACH - SIGNAL LOST";
        case State::Landed:       return "ON THE GROUND";
    }
    return "";
}

// The reassuring second line. Every one of these names the MECHANISM -- it is a
// statement about OUR equipment rather than about him, which is the whole
// difference between an alarming absence and an explained one.
//
// AND ONE THING §6 ASKS FOR IS DELIBERATELY NOT HERE. Its worked example for a
// no-coverage crossing ends "Next contact expected around 18:40, near Ireland."
// We cannot say that. It needs a model of where receiver coverage resumes,
// which we do not have and do not licence, and a time derived from it -- so it
// would be a number invented on the one screen whose job is to explain an
// absence honestly. Same call as cutting C4's countdown, same reason (§6
// principle 2). "He will reappear on the far side" says what we do know.
inline const char* Explanation(State s, Regime r = Regime::Local)
{
    switch (s) {
        case State::NoCoverage:
            return r == Regime::Airline
                ? "No ground receivers out here - this is expected. "
                  "He will reappear on the far side."
                : "Ground receivers do not reach where he is now.";
        case State::SignalLost:
            return "He is out of receiver range, not off the radar.";
        case State::ApproachLost:
            return r == Regime::Airline
                ? "Expected at this range."
                : "Low-level coverage is patchy. This usually means landed.";
        case State::Waiting:
            // C4. The load-bearing line, and it is the same in both regimes:
            // it answers "is it broken, or is he just not flying?" and tells
            // the owner there is nothing to do.
            return "Nothing heard yet today. This screen changes on its own "
                   "when he takes off.";
        default:
            return "";
    }
}

// ntfy title. Returns false for "do not send" -- SIGNAL_LOST is screen-only by
// default (§15): the asymmetry is the argument, since a missed lost-alert costs
// mild worry and an unwanted one costs panic.
//
// Formats into the caller's buffer rather than returning a String. Two reasons,
// and only the first is about the device: an alert path should not allocate, and
// String is the last thing that would make this header need Arduino -- which
// would put §17's host test out of reach for the sake of one concatenation.
//
// §17 NOTE: this is the ONE place the follow target is allowed to leave the
// device, per C3. The owner named an aircraft in a field marked Follow; the
// notification is the thing they asked for. Every other outbound payload must
// not contain it, which is what the §17 test asserts.
inline bool AlertTitle(State s, const char* target, char* out, size_t n)
{
    if (!out || n == 0) return false;
    out[0] = '\0';
    const char* suffix = nullptr;
    switch (s) {
        case State::Airborne:     suffix = " is airborne";              break;
        case State::Landed:       suffix = " is down";                  break;
        case State::ApproachLost: suffix = " - last seen on approach";  break;
        default:                  return false;
    }
    snprintf(out, n, "%s%s", target ? target : "", suffix);
    return true;
}

} // namespace follow
