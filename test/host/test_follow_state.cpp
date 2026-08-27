// Host test for Follow Mode's pure half -- the state machine (5), the copy (6)
// and the local face's geometry (10).
//
// =============================================================================
// WHAT THIS EXISTS TO CATCH, AND WHY IT COULD NOT BE CAUGHT ON THE BENCH
//
// The state machine's entire job is to be right about ambiguous evidence, and
// every one of its interesting cases needs a real aeroplane to disappear in a
// particular way. Waiting for that is not a test strategy: a pattern dropout at
// a specific altitude inside a specific radius with a specific descent rate
// happens once an afternoon, and it produces ONE observation with no control
// beside it.
//
// So the machine was written pure (see the header's note) and it is graded here.
//
// =============================================================================
// EVERY CLAIM BELOW IS PAIRED WITH A CONTROL THAT MUST COME OUT DIFFERENTLY
//
// This project's recurring failure is a check that cannot detect its own
// failure. A test asserting "an inside-home dropout is BELOW COVERAGE" passes
// just as happily against a machine that returns BELOW COVERAGE for absolutely
// everything -- which is the same bug as the one being fixed, wearing the
// opposite sign.
//
// So each assertion has a partner that differs in exactly one input and must
// produce a different answer. Where a fix is being pinned, the control is the
// PRE-FIX world: the suite asserts what the old code did, so re-introducing it
// fails here rather than in six months on somebody's desk.
#include <cmath>
#include <cstdio>

// DiscGeometry.h, not Layout.h: Layout resolves the variant and needs the
// board. The chord rule has no hardware in it, so the SAME definition the
// firmware compiles is the one graded here.
#include "../../include/DiscGeometry.h"
#include "../../include/FollowArc.h"
#include "../../include/FollowGeometry.h"
#include "../../include/FollowState.h"

static int failures = 0;
static int checks   = 0;

static void check(bool ok, const char* what)
{
    ++checks;
    if (!ok) { std::printf("  FAIL: %s\n", what); ++failures; }
}

// String equality without <string>, so this binary stays as dependency-free as
// the header it grades.
static bool same(const char* a, const char* b)
{
    while (*a && *b) { if (*a++ != *b++) return false; }
    return *a == *b;
}

using namespace follow;

// ---- fixtures ---------------------------------------------------------------

static HomeContext HomeAt(float lat, float lon, bool elevation)
{
    HomeContext h;
    h.lat = lat; h.lon = lon;
    h.radiusKm = 8.0f;
    h.elevationFt = 3460.0f;   // Bend, OR -- a HIGH field on purpose, see below
    h.positionKnown = true;
    h.elevationKnown = elevation;
    return h;
}

// A fix a given number of km north of a point, at a given MSL altitude.
static Fix FixNorthOf(float lat, float lon, float km, float mslFt, float kt, float fpm)
{
    Fix f;
    f.lat = lat + km / 111.0f;
    f.lon = lon;
    f.geoAltFt = mslFt;
    f.baroAltFt = mslFt;
    f.velocityKt = kt;
    f.verticalRateFpm = fpm;
    f.onGround = false;
    return f;
}

// Fly a machine to AIRBORNE, then stop hearing from it. Returns the state the
// machine settled on. Everything about the scenario is a parameter so the
// controls below can differ in exactly one of them.
static State AbsenceAfter(const Fix& f, const HomeContext& home)
{
    Machine m;
    Tuning t;
    m.SetTarget(true);
    uint32_t now = 1000;
    m.OnFix(f, now, home, t);            // 1st
    now += 5000;
    m.OnFix(f, now, home, t);            // 2nd -> airborneConfirmFixes
    now += t.trackLostMs + 1000;         // and then silence
    m.OnNoFix(now, home, t);
    return m.Current();
}

int main()
{
    const float HLAT = 44.09f, HLON = -121.20f;   // Bend, OR
    const float PATTERN_MSL = 4460.0f;            // 1,000 ft AGL over a 3,460 ft field

    std::printf("== FollowState ==\n");

    // =========================================================================
    // 5.4 -- THE RAIL. A lost signal must never be reported as a landing.
    // =========================================================================
    std::printf("  ---- rail: no landing claim without evidence\n");

    // Low, slow, right over the field, and never heard again. This is the shape
    // that most LOOKS like a landing, which is exactly why it is the rail's
    // test case.
    const Fix lowSlow = FixNorthOf(HLAT, HLON, 1.0f, 3600.0f, 40.0f, -400.0f);

    // WITH a published field elevation the argument is available: 140 ft AGL at
    // 40 kt inside the radius is the 5.4 low-and-slow route to LANDED.
    check(AbsenceAfter(lowSlow, HomeAt(HLAT, HLON, /*elevation=*/true)) == State::Landed,
          "low+slow+inside, elevation KNOWN -> Landed");

    // WITHOUT it, the same fix must NOT be called a landing. This is the
    // control: identical in every input but one, and it must come out
    // different, or the assertion above proves nothing.
    check(AbsenceAfter(lowSlow, HomeAt(HLAT, HLON, /*elevation=*/false)) != State::Landed,
          "CONTROL: the same fix with elevation UNKNOWN must NOT be Landed");

    // And it must not be reported as a fault either -- it is inside the radius,
    // so 10's rule applies.
    check(AbsenceAfter(lowSlow, HomeAt(HLAT, HLON, false)) == State::NoCoverage,
          "... it is BELOW COVERAGE instead: expected absence, not a claim");

    // The rail again from the other side: a machine that never saw ground and
    // never saw low-and-slow must not reach Landed by any route.
    const Fix cruise = FixNorthOf(HLAT, HLON, 60.0f, 9000.0f, 220.0f, 0.0f);
    check(AbsenceAfter(cruise, HomeAt(HLAT, HLON, true)) != State::Landed,
          "a cruise fix that vanishes is never Landed");

    // =========================================================================
    // 5.1 / 10 -- ABSENCE IS THREE STATES, AND THE DEVICE CAN TELL THEM APART
    //
    // THE DEFECT THIS PINS. HomeContext carried one `known` flag covering both
    // where home is and how high it is. Stage 1 sets the elevation half false
    // (C5's delivery is unbuilt), so InsideHome() -- a POSITION question --
    // returned false for every fix, and every pattern dropout came out
    // SIGNAL LOST. Section 10, in bold: "Getting this backwards makes the
    // device look broken every single circuit."
    //
    // The field is deliberately Bend at 3,460 ft, because at a sea-level field
    // the bug is invisible: 1,000 ft AGL is 1,000 ft MSL, which is under every
    // threshold, and substituting MSL for AGL happens to give the right answer.
    // A test written at sea level passes either way.
    // =========================================================================
    std::printf("  ---- absence: three states, told apart\n");

    const Fix inPattern  = FixNorthOf(HLAT, HLON, 3.0f,  PATTERN_MSL, 90.0f, 0.0f);
    const Fix wayOut     = FixNorthOf(HLAT, HLON, 40.0f, PATTERN_MSL, 90.0f, 0.0f);

    check(AbsenceAfter(inPattern, HomeAt(HLAT, HLON, false)) == State::NoCoverage,
          "inside the home radius -> BELOW COVERAGE (expected absence)");

    // CONTROL 1: one input differs -- the position. Same altitude, same speed,
    // same everything, 40 km out instead of 3. Must be the OTHER answer.
    check(AbsenceAfter(wayOut, HomeAt(HLAT, HLON, false)) == State::SignalLost,
          "CONTROL: the same fix 40 km out -> SIGNAL LOST (unexpected absence)");

    // CONTROL 2: the PRE-FIX WORLD. positionKnown false is what the old single
    // `known` flag produced for every stage-1 device. Asserting the old wrong
    // answer here means re-conflating the two flags fails this suite.
    {
        HomeContext blind = HomeAt(HLAT, HLON, false);
        blind.positionKnown = false;
        check(AbsenceAfter(inPattern, blind) == State::SignalLost,
              "CONTROL: with position unknown the circuit dropout is SIGNAL LOST"
              " -- the bug, pinned");
    }

    // The profile argument, which needs AGL and therefore elevation.
    const Fix onApproach = FixNorthOf(HLAT, HLON, 4.0f, 4960.0f, 85.0f, -600.0f);
    check(AbsenceAfter(onApproach, HomeAt(HLAT, HLON, true)) == State::ApproachLost,
          "descending inside the radius, elevation known -> APPROACH LOST");

    // CONTROL: same fix, no elevation. The profile argument is unavailable, so
    // the machine must fall back to the position argument rather than quote an
    // MSL number as if it were a height over the field.
    check(AbsenceAfter(onApproach, HomeAt(HLAT, HLON, false)) == State::NoCoverage,
          "CONTROL: the same descent without elevation declines the profile"
          " argument and reports position instead");

    // A high loiter over home IS a fault, and only a known elevation can say so.
    const Fix highOverHome = FixNorthOf(HLAT, HLON, 2.0f, 24000.0f, 300.0f, 0.0f);
    check(AbsenceAfter(highOverHome, HomeAt(HLAT, HLON, true)) == State::SignalLost,
          "high over home with elevation known -> SIGNAL LOST, not below coverage");

    // =========================================================================
    // AglFt refuses rather than approximates.
    // =========================================================================
    std::printf("  ---- AGL declines when it cannot answer\n");
    check(std::isnan(AglFt(inPattern, HomeAt(HLAT, HLON, false))),
          "AGL with no field elevation is NaN, not a plausible wrong number");
    check(std::fabs(AglFt(inPattern, HomeAt(HLAT, HLON, true)) - 1000.0f) < 1.0f,
          "CONTROL: AGL with the elevation known is the real 1,000 ft");
    check(std::fabs(AltitudeMslFt(inPattern) - PATTERN_MSL) < 1.0f,
          "MSL is available either way, and is named for what it is");

    // =========================================================================
    // 6 -- DIFFERENT WORDS FOR DIFFERENT STATES.
    //
    // "If NO_COVERAGE and SIGNAL_LOST read the same, the state machine's work is
    // wasted." That is a testable claim about strings, so it is tested.
    // =========================================================================
    std::printf("  ---- copy: the states do not read alike\n");
    check(!same(Headline(State::NoCoverage), Headline(State::SignalLost)),
          "BELOW COVERAGE and SIGNAL LOST must not read the same");
    check(same(Headline(State::NoCoverage), "BELOW COVERAGE"),
          "section 10: a local dropout says BELOW COVERAGE");
    check(Explanation(State::NoCoverage)[0] != '\0',
          "expected absence names the mechanism rather than only the symptom");
    check(Explanation(State::Airborne)[0] == '\0',
          "CONTROL: a normal state has no reassurance line to give");
    check(Machine::IsExpectedAbsence(State::NoCoverage) &&
          Machine::IsExpectedAbsence(State::ApproachLost),
          "the two expected absences classify as expected");
    check(!Machine::IsExpectedAbsence(State::SignalLost),
          "CONTROL: the unexpected one does not");

    // 15: SIGNAL_LOST is screen-only by default. The asymmetry is the argument.
    char title[96];
    check(AlertTitle(State::Landed, "N4523K", title, sizeof(title)) &&
          same(title, "N4523K is down"),
          "a landing sends, and carries the tail (C3: the sanctioned channel)");
    check(!AlertTitle(State::SignalLost, "N4523K", title, sizeof(title)),
          "CONTROL: SIGNAL LOST does not send -- an unwanted alert costs panic");
    check(title[0] == '\0',
          "and it leaves nothing in the buffer for a caller to send by accident");

    // =========================================================================
    // 11 -- THE FLIGHT'S NUMBERS
    //
    // Four numbers on a souvenir, and the reason they are graded is that a
    // post-flight card is the one face with nothing live beside it to
    // contradict a wrong figure. The customer looks at it after the fact,
    // repeatedly, and has no way to check it.
    // =========================================================================
    std::printf("  ---- the flight's numbers\n");
    {
        FlightStats fs;
        const HomeContext home = HomeAt(HLAT, HLON, false);

        // FORTY MINUTES ON THE APRON with the transponder on. He did not fly for
        // forty minutes, and a duration that says he did is the card's easiest
        // way to be wrong.
        Fix apron = FixNorthOf(HLAT, HLON, 0.0f, 3460.0f, 0.0f, 0.0f);
        apron.onGround = true;
        for (uint32_t t = 0; t <= 2400000u; t += 60000u)
            fs.OnFix(apron, t, home);
        check(!fs.started && fs.DurationSec() == 0,
              "ground fixes do not start the clock");

        // Then twelve minutes airborne.
        fs.OnFix(FixNorthOf(HLAT, HLON, 1.0f, 4000.0f, 70.0f, 500.0f), 2400000u, home);
        fs.OnFix(FixNorthOf(HLAT, HLON, 9.0f, 5200.0f, 110.0f, 0.0f), 3120000u, home);
        check(fs.DurationSec() == 720, "duration runs from the first AIRBORNE fix: 12 min");

        // CONTROL: measured from the first fix of any kind it would be 52 min.
        check(fs.DurationSec() != 3120, "CONTROL: it is not the whole transponder-on span");

        check(std::fabs(fs.maxAltMslFt - 5200.0f) < 1.0f, "max altitude is the maximum");
        check(std::fabs(fs.topSpeedKt - 110.0f) < 1.0f, "top speed is the maximum");
        check(fs.furthestKm > 8.0f && fs.furthestKm < 10.0f, "furthest point is ~9 km");

        // CONTROL: maxima do not fall back. A later, lower fix must not lower
        // them -- which is what a plain assignment would do and what an
        // end-of-flight snapshot would produce (he lands low and slow, so the
        // last fix of every flight is the smallest one).
        fs.OnFix(FixNorthOf(HLAT, HLON, 0.2f, 3500.0f, 45.0f, -500.0f), 3180000u, home);
        check(std::fabs(fs.maxAltMslFt - 5200.0f) < 1.0f,
              "CONTROL: landing low does not lower the max altitude");
        check(std::fabs(fs.topSpeedKt - 110.0f) < 1.0f,
              "CONTROL: landing slow does not lower the top speed");
        check(fs.furthestKm > 8.0f,
              "CONTROL: coming home does not shrink the furthest point");
    }
    {
        // millis() WRAPS AT 49.7 DAYS, and a device that has been on a shelf for
        // seven weeks is not exotic. Unsigned subtraction is right across the
        // wrap; the test exists because it does not LOOK right.
        FlightStats fs;
        const HomeContext home = HomeAt(HLAT, HLON, false);
        const uint32_t nearMax = 0xFFFFFF00u;
        fs.OnFix(FixNorthOf(HLAT, HLON, 1.0f, 4000.0f, 70.0f, 0.0f), nearMax, home);
        fs.OnFix(FixNorthOf(HLAT, HLON, 2.0f, 4000.0f, 70.0f, 0.0f), nearMax + 600000u, home);
        check(fs.DurationSec() == 600, "duration is correct across a millis() wrap");
    }
    {
        // Without a home position there is no furthest-from-home to compute, and
        // the honest answer is zero rather than a distance from 0N 0E.
        FlightStats fs;
        HomeContext blind = HomeAt(HLAT, HLON, false);
        blind.positionKnown = false;
        fs.OnFix(FixNorthOf(HLAT, HLON, 40.0f, 9000.0f, 200.0f, 0.0f), 1000, blind);
        check(fs.furthestKm == 0.0f, "no home position -> no furthest-point claim");
        check(std::fabs(fs.maxAltMslFt - 9000.0f) < 1.0f,
              "CONTROL: the numbers that do not need home are still recorded");
    }

    // =========================================================================
    // THE READOUT DECLINES WHAT IT CANNOT DEFEND
    //
    // Straight from the bench: the local face printed "-900 ft MSL" under an
    // AIRBORNE headline, in the same typeface as a real number. Two rules,
    // because the obvious one does not catch it -- which is the point.
    // =========================================================================
    std::printf("  ---- the readout declines what it cannot defend\n");

    // Rule 1: physical bounds.
    check(PlausibleAltFt(3500.0f), "a normal cruise altitude is plausible");
    check(!PlausibleAltFt(90000.0f), "90,000 ft is not");
    check(!PlausibleAltFt(-9000.0f), "-9,000 ft is not");
    check(!PlausibleAltFt(NAN), "NaN is not");
    // CONTROL for rule 1's floor: Bar Yehuda is a REAL airfield at -1,266 ft, so
    // the floor must not be zero. A check that rejects real places is one that
    // gets loosened by whoever hits it next, and loosened checks stay loose.
    check(PlausibleAltFt(-1266.0f), "CONTROL: a real below-sea-level airfield passes");

    // Rule 2: contradiction with the state. THIS is the one that catches -900.
    check(PlausibleAltFt(-900.0f),
          "-900 ft passes the BOUNDS check -- which is why bounds alone are not enough");
    check(!ReportableAltFt(-900.0f, /*airborne=*/true),
          "-900 ft under AIRBORNE is declined: the two readings contradict");
    check(ReportableAltFt(-900.0f, /*airborne=*/false),
          "CONTROL: the same -900 ft on the GROUND is reported, not declined");
    check(ReportableAltFt(3500.0f, true),
          "CONTROL: a sane airborne altitude is still reported");

    check(PlausibleSpeedKt(120.0f) && !PlausibleSpeedKt(-3.0f) && !PlausibleSpeedKt(4000.0f),
          "speed is bounded at both ends");

    // =========================================================================
    // THE ROUND PANEL HAS NO EDGE TO CLIP AGAINST (Layout.h)
    // =========================================================================
    std::printf("  ---- chord width: text must fit the curve, not the box\n");
    {
        const int S = 240, LH = 8;
        // The row the bench found running off both ends.
        const int atReadout = discgeom::ChordWidthPx(S - 26, LH, S);
        const int atMiddle  = discgeom::ChordWidthPx(S / 2 - 4, LH, S);
        check(atReadout > 0 && atReadout < atMiddle,
              "the bottom readout row is narrower than the middle");
        check(atMiddle > 200, "the middle of the disc is nearly the full width");
        // The measured failure: 25 characters at 6 px did not fit that row.
        check(25 * 6 > atReadout,
              "\"-900 ft MSL  67 kt  148mi\" does NOT fit -- the reported defect, in numbers");
        check(19 * 6 <= atReadout, "... but nineteen characters do");
        // CONTROL: a square panel of the same size would have fitted it, which is
        // why this is a round-panel bug and not a font-size one.
        check(atReadout < S - 8, "CONTROL: the chord is narrower than the bounding box");
        // Off the glass entirely is 0, not a negative width a caller might use.
        check(discgeom::ChordWidthPx(-40, LH, S) == 0 && discgeom::ChordWidthPx(S + 40, LH, S) == 0,
              "a row off the disc reports zero, never a negative width");
        // Symmetric about the centre line, or one end of the face clips and the
        // other does not -- which is exactly how it was reported.
        check(discgeom::ChordWidthPx(20, LH, S) == discgeom::ChordWidthPx(S - 20 - LH, LH, S),
              "the top and bottom of the disc are treated identically");
    }

    // =========================================================================
    // 8 -- THE ARC FACE'S ARITHMETIC
    //
    // Graded against hand-computed references, because a marker at 41% instead
    // of 44% looks exactly like a marker at 44% on a 240 px disc. Nothing about
    // this is checkable by eye, which is the definition of what belongs here.
    // =========================================================================
    std::printf("== FollowArc ==\n");
    std::printf("  ---- great circle, not the flat approximation\n");
    {
        // The spec's own worked example: DEN -> DEL is 12,406 km.
        const Endpoint den = LookupAirport("DEN");
        const Endpoint del = LookupAirport("DEL");
        check(den.known, "DEN resolves from the baked table");
        check(del.known, "DEL resolves from the baked table");
        if (den.known && del.known) {
            const float d = GreatCircleKm(den.lat, den.lon, del.lat, del.lon);
            check(d > 12200.0f && d < 12600.0f,
                  "DEN->DEL is ~12,406 km (the spec's MEASURED figure)");
            // CONTROL: the equirectangular approximation FollowState uses for the
            // home radius is wildly wrong here -- which is why the two functions
            // exist separately and are named differently.
            const float flat = SeparationKm(den.lat, den.lon, del.lat, del.lon);
            check(flat > d * 1.2f || flat < d * 0.8f,
                  "CONTROL: the flat approximation is NOT usable at this range");
        }
        // A short hop, where haversine must still be right.
        const Endpoint sea = LookupAirport("SEA");
        const Endpoint lax = LookupAirport("LAX");
        if (sea.known && lax.known) {
            const float d = GreatCircleKm(sea.lat, sea.lon, lax.lat, lax.lon);
            check(d > 1450.0f && d < 1620.0f, "SEA->LAX is ~1,537 km (spec MEASURED)");
        }
        check(!LookupAirport("ZZZ").known, "CONTROL: a bogus code does not resolve");
        check(!LookupAirport("").known && !LookupAirport(nullptr).known,
              "CONTROL: empty and null do not resolve, and do not crash");
        check(GreatCircleKm(10.0f, 20.0f, 10.0f, 20.0f) < 0.001f,
              "a point is zero km from itself");
    }

    std::printf("  ---- bearing, normalised to the compass\n");
    {
        // Due north and due east from the equator are the two cases where the
        // answer is exactly known without trigonometry.
        check(std::fabs(BearingDeg(0.0f, 0.0f, 10.0f, 0.0f) - 0.0f) < 0.5f, "due north is 0");
        check(std::fabs(BearingDeg(0.0f, 0.0f, 0.0f, 10.0f) - 90.0f) < 0.5f, "due east is 90");
        check(std::fabs(BearingDeg(10.0f, 0.0f, 0.0f, 0.0f) - 180.0f) < 0.5f, "due south is 180");
        // THE ONE THAT WOULD SHIP WRONG: due west is 270, not -90. atan2 returns
        // a negative and the compass has no negatives.
        const float w = BearingDeg(0.0f, 10.0f, 0.0f, 0.0f);
        check(w > 269.5f && w < 270.5f, "due west normalises to 270, never -90");
        for (float lat = -60.0f; lat <= 60.0f; lat += 17.0f)
            for (float lon = -170.0f; lon <= 170.0f; lon += 43.0f) {
                const float b = BearingDeg(0.0f, 0.0f, lat, lon);
                ++checks;
                if (!(b >= 0.0f && b < 360.0f)) {
                    std::printf("  FAIL: bearing %g out of [0,360)\n", (double)b);
                    ++failures;
                }
            }
    }

    std::printf("  ---- progress and the arc sweep\n");
    {
        const Endpoint a = LookupAirport("JFK");
        const Endpoint b = LookupAirport("LHR");
        check(a.known && b.known, "JFK and LHR both resolve");
        if (a.known && b.known) {
            check(ProgressAlong(a, b, a.lat, a.lon) < 0.01f, "at the origin, progress is 0");
            check(ProgressAlong(a, b, b.lat, b.lon) > 0.99f, "at the destination, progress is 1");
            // Past the destination -- a diversion, or an overfly. Clamped, because
            // a marker off the end of the arc is a rendering bug, not information.
            check(ProgressAlong(a, b, 55.0f, 20.0f) <= 1.0f, "beyond the destination clamps to 1");
        }
        // An unresolved endpoint yields no progress, which is what makes the
        // code-only arc (no marker) the honest degradation rather than a marker
        // sitting at 7:30 pretending to be a position.
        Endpoint unknown;
        check(ProgressAlong(unknown, b, 50.0f, 0.0f) == 0.0f,
              "an unresolved endpoint gives no progress");

        // §8's geometry: origin at 7:30, destination at 4:30.
        check(std::fabs(ArcAngleDeg(0.0f) - 135.0f) < 0.01f, "progress 0 sits at 135 (7:30)");
        check(std::fabs(ArcAngleDeg(1.0f) - 405.0f) < 0.01f, "progress 1 sits at 405 (4:30)");
        check(std::fabs(ArcAngleDeg(0.5f) - 270.0f) < 0.01f, "halfway sits at the top");
        check(std::fabs(ArcAngleDeg(2.0f) - 405.0f) < 0.01f, "CONTROL: out-of-range clamps");
    }

    std::printf("  ---- time to arrival declines rather than guesses\n");
    check(MinutesToArrival(926.0f, 500.0f) == 60,
          "926 km at 500 kt is one hour");
    check(MinutesToArrival(100.0f, 0.0f) < 0,
          "a stopped aircraft has no arrival time -- not a huge one");
    check(MinutesToArrival(100.0f, 20.0f) < 0,
          "and neither does one below 40 kt");
    check(MinutesToArrival(100.0f, NAN) < 0, "NaN speed declines");
    check(MinutesToArrival(60000.0f, 60.0f) < 0,
          "longer than any flight means the inputs disagree -- decline");
    check(MinutesToArrival(926.0f, 500.0f) > 0,
          "CONTROL: a sane pair still produces an answer");

    // =========================================================================
    // 10 -- THE RING LADDER
    // =========================================================================
    std::printf("== FollowGeometry ==\n");
    std::printf("  ---- rings: round labels, and the track always fits\n");

    // Containment is the property that matters: whatever the track's extent, the
    // outermost ring must reach it, or the picture crops the thing it is of.
    for (float maxV = 0.05f; maxV < 400.0f; maxV *= 1.13f) {
        const float step = NiceStep(maxV, 3);
        ++checks;
        if (!(step * 3.0f >= maxV - 1e-4f)) {
            std::printf("  FAIL: %g does not fit inside 3 x %g\n", (double)maxV, (double)step);
            ++failures;
        }
    }

    // Round, in the customer's unit: every step is 1, 2 or 5 times a power of ten.
    for (float maxV = 0.05f; maxV < 400.0f; maxV *= 1.07f) {
        const float step = NiceStep(maxV, 3);
        const float mant = step / powf(10.0f, floorf(log10f(step)));
        const bool roundish = std::fabs(mant - 1.0f) < 0.01f ||
                              std::fabs(mant - 2.0f) < 0.01f ||
                              std::fabs(mant - 5.0f) < 0.01f;
        ++checks;
        if (!roundish) {
            std::printf("  FAIL: step %g is not on the 1/2/5 ladder\n", (double)step);
            ++failures;
        }
    }

    // The scale actually MOVES with the data. Without this the two loops above
    // are both satisfied by a function that returns one constant forever.
    check(NiceStep(1.4f, 3) < NiceStep(140.0f, 3),
          "CONTROL: a bigger track gets a bigger step -- the scale is not fixed");
    check(std::fabs(NiceStep(1.4f, 3) - 0.5f) < 1e-6f, "1.4 over three rings -> 0.5 a ring");
    check(std::fabs(NiceStep(14.0f, 3) - 5.0f) < 1e-6f, "14 over three rings -> 5 a ring");

    // The floor. A parked aeroplane has a zero-extent track, and a zero step is
    // a divide-by-zero in the projection and an unlabelled face.
    check(NiceStep(0.0f, 3) > 0.0f, "a zero-extent track still yields a drawable scale");
    check(NiceStep(-5.0f, 3) > 0.0f, "so does a nonsense one");
    check(NiceStep(NAN, 3) > 0.0f, "and NaN does not propagate into the projection");

    // Projection sanity: home is the centre, and a point due north is above it.
    {
        LocalView v; v.centreLat = HLAT; v.centreLon = HLON; v.radiusKm = 6.0f; v.rings = 3;
        float x = 0, y = 0;
        ProjectLocal(HLAT, HLON, v, 0.0f, 1.0f, 240.0f, 110.0f, x, y);
        check(std::fabs(x - 120.0f) < 0.01f && std::fabs(y - 120.0f) < 0.01f,
              "home projects to the centre of the disc");

        ProjectLocal(HLAT + 3.0f / 111.0f, HLON, v, 0.0f, 1.0f, 240.0f, 110.0f, x, y);
        check(std::fabs(x - 120.0f) < 0.5f && y < 120.0f,
              "3 km north is directly above centre, north-up");
        check(std::fabs(y - (120.0f - 55.0f)) < 1.0f,
              "... and at half the outer ring, since 3 km is half of 6");

        // CONTROL: with the picture rotated 90 degrees (window-up), the same
        // point must move off the vertical. A projection that ignored rotation
        // would pass the two assertions above unchanged.
        ProjectLocal(HLAT + 3.0f / 111.0f, HLON, v, 1.0f, 0.0f, 240.0f, 110.0f, x, y);
        check(std::fabs(y - 120.0f) < 1.0f && std::fabs(x - 120.0f) > 50.0f,
              "CONTROL: window-up rotates the local face like the radar");
    }

    std::printf("\n%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
