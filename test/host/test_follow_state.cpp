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

    // §6's note came true in stage 2: two regimes, one switch. The local words
    // are asserted above and must not have moved; these are the airline ones.
    std::printf("  ---- copy: and the two regimes do not read alike either\n");
    check(same(Headline(State::NoCoverage, Regime::Airline), "NO COVERAGE"),
          "§8's chip: mid-ocean says NO COVERAGE, not BELOW COVERAGE");
    check(same(Headline(State::ApproachLost, Regime::Airline), "BELOW COVERAGE"),
          "§8's chip: an airline approach says BELOW COVERAGE");
    check(!same(Headline(State::NoCoverage, Regime::Airline),
                Headline(State::ApproachLost, Regime::Airline)),
          "the two airline absences still do not read the same");
    check(!same(Headline(State::NoCoverage, Regime::Airline),
                Headline(State::SignalLost, Regime::Airline)),
          "and neither reads like the unexpected one");
    check(!same(Headline(State::NoCoverage, Regime::Airline),
                Headline(State::NoCoverage, Regime::Local)),
          "the regime actually changes the words -- CONTROL for the parameter "
          "being wired to anything at all");
    check(same(Headline(State::Airborne, Regime::Airline),
               Headline(State::Airborne, Regime::Local)),
          "CONTROL: and it changes only the two states that §8 moves");
    check(same(Headline(State::SignalLost, Regime::Airline),
               Headline(State::SignalLost, Regime::Local)),
          "CONTROL: SIGNAL LOST is the same fault in both regimes");
    // §6's "Next contact expected around 18:40, near Ireland" is deliberately
    // absent -- we licence no coverage model and no schedule, so the sentence
    // would be invented. Assert the decline, or it comes back.
    {
        const char* e = Explanation(State::NoCoverage, Regime::Airline);
        bool claimsATime = false;
        for (const char* p = e; *p; ++p) if (*p >= '0' && *p <= '9') claimsATime = true;
        check(!claimsATime, "the ocean copy states no time -- we cannot know one");
        check(e[0] != '\0', "CONTROL: it still says something");
    }
    // C4: WAITING has copy now, and it must not borrow a loss state's word.
    check(Headline(State::Waiting)[0] != '\0', "C4: WAITING has a headline");
    check(Explanation(State::Waiting)[0] != '\0', "C4: ... and the load-bearing line");
    {
        const char* w = Headline(State::Waiting);
        const char* x = Explanation(State::Waiting);
        bool borrowed = false;
        const char* both[2] = { w, x };
        for (int i = 0; i < 2; ++i)
            for (const char* p = both[i]; *p; ++p)
                if ((p[0] == 'l' && p[1] == 'o' && p[2] == 's' && p[3] == 't') ||
                    (p[0] == 'L' && p[1] == 'O' && p[2] == 'S' && p[3] == 'T'))
                    borrowed = true;
        check(!borrowed,
              "C4: nothing has been lost, so WAITING never says 'lost'");
    }

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

    // -------------------------------------------------------------------------
    // §9's worked example, which is also the control for the interpolation
    // -------------------------------------------------------------------------
    std::printf("  ---- great-circle interpolation: over the pole, not through Morocco\n");
    {
        const Endpoint den = LookupAirport("DEN");
        const Endpoint del = LookupAirport("DEL");
        check(den.known && del.known, "both ends of the worked example are in the table");

        float mLat = 0.0f, mLon = 0.0f;
        InterpolateGreatCircle(den, del, 0.5f, mLat, mLon);
        // §9 [MEASURED]: "a 111.6 degree great circle with its midpoint at
        // 83.9 N, 88.6 E". Hand-computed in the spec, transcribed here.
        check(std::fabs(mLat - 83.9f) < 0.6f, "DEN->DEL's midpoint is at 83.9 N");
        check(std::fabs(mLon - 88.6f) < 1.5f, "... and 88.6 E -- north of Siberia");

        // THE CONTROL, and it is the whole reason this function is not two
        // averages: a naive lat/lon lerp lands in the Atlantic off Morocco,
        // some 5,000 km away, and looks entirely plausible on a 240 px disc.
        const float naiveLat = (den.lat + del.lat) * 0.5f;
        const float naiveLon = (den.lon + del.lon) * 0.5f;
        check(std::fabs(naiveLat - mLat) > 40.0f,
              "CONTROL: averaging the latitudes is wrong by 40+ degrees");
        check(GreatCircleKm(mLat, mLon, naiveLat, naiveLon) > 4000.0f,
              "CONTROL: ... which is over 4,000 km of error");

        // Endpoints, and monotonicity along the route.
        float eLat = 0.0f, eLon = 0.0f;
        InterpolateGreatCircle(den, del, 0.0f, eLat, eLon);
        check(GreatCircleKm(eLat, eLon, den.lat, den.lon) < 1.0f, "f=0 is the origin");
        InterpolateGreatCircle(den, del, 1.0f, eLat, eLon);
        check(GreatCircleKm(eLat, eLon, del.lat, del.lon) < 1.0f, "f=1 is the destination");

        const float total = GreatCircleKm(den.lat, den.lon, del.lat, del.lon);
        check(std::fabs(total - 12406.0f) < 60.0f, "and the route is §9's 12,406 km");
        bool monotone = true;
        float prev = -1.0f;
        for (float f = 0.0f; f <= 1.0001f; f += 0.05f) {
            float pLat = 0.0f, pLon = 0.0f;
            InterpolateGreatCircle(den, del, f, pLat, pLon);
            const float flown = GreatCircleKm(den.lat, den.lon, pLat, pLon);
            if (flown < prev - 1.0f) monotone = false;
            prev = flown;
            // Every interpolated point must lie ON the route: the two legs must
            // sum to the whole, which a point off the great circle cannot do.
            const float legs = flown + GreatCircleKm(pLat, pLon, del.lat, del.lon);
            check(std::fabs(legs - total) < 25.0f, "an interpolated point lies on the route");
        }
        check(monotone, "progress along the interpolation never goes backwards");

        // Coincident endpoints: nothing to interpolate, and no divide by zero.
        InterpolateGreatCircle(den, den, 0.5f, eLat, eLon);
        check(std::fabs(eLat - den.lat) < 0.01f && std::fabs(eLon - den.lon) < 0.01f,
              "a route to the same field returns the field, not a NaN");
    }

    // -------------------------------------------------------------------------
    // §7.1 REGIME -- and the reason its input is a maximum
    // -------------------------------------------------------------------------
    std::printf("  ---- regime: inferred from the flight's extent, never configured\n");
    check(RegimeFor(3.0f, 8.0f, true) == Regime::Local,
          "a circuit inside the home radius is the local regime");
    check(RegimeFor(12.0f, 8.0f, true) == Regime::Airline,
          "a flight that leaves it is not");
    check(RegimeFor(8.0f, 8.0f, true) == Regime::Local,
          "exactly at the radius has not left it");
    check(RegimeFor(4000.0f, 8.0f, false) == Regime::Local,
          "with no home position the LOCAL face is chosen -- because it declines "
          "visibly, and the arc face would point a wedge at nothing");
    check(RegimeFor(NAN, 8.0f, true) == Regime::Local,
          "NaN extent does not become an airliner");

    // THE CONTROL THAT MATTERS. The claim is that feeding the flight's MAXIMUM
    // extent latches the regime for the flight, and that feeding the LIVE
    // separation instead would flap the face once per circuit. Both halves are
    // asserted, because only the second says the input choice was load-bearing.
    {
        HomeContext h = HomeAt(HLAT, HLON, true);   // radiusKm 8
        FlightStats s;
        uint32_t now = 1000;
        // Out to 9 km -- just past the boundary -- and back overhead.
        const float legKm[] = { 1.0f, 5.0f, 9.0f, 5.0f, 1.0f, 0.2f };
        bool latched = true, liveWouldFlap = false;
        for (float km : legKm) {
            const Fix f = FixNorthOf(HLAT, HLON, km, PATTERN_MSL, 90.0f, 0.0f);
            s.OnFix(f, now, h);
            now += 5000;
            if (s.furthestKm > 8.0f && RegimeFor(s.furthestKm, h.radiusKm, true) != Regime::Airline)
                latched = false;
            // What the same function says about the CURRENT separation:
            const float live = SeparationKm(f.lat, f.lon, h.lat, h.lon);
            if (s.furthestKm > 8.0f && RegimeFor(live, h.radiusKm, true) == Regime::Local)
                liveWouldFlap = true;
        }
        check(latched, "once the flight has left the radius the regime stays out");
        check(liveWouldFlap,
              "CONTROL: the live separation WOULD have flapped back -- which is "
              "why the input is the maximum");
        check(s.furthestKm > 8.9f && s.furthestKm < 9.1f,
              "and the maximum is the peak, not the last leg");
    }

    // -------------------------------------------------------------------------
    // §8 NO_COVERAGE -- the dead-reckoned estimate
    // -------------------------------------------------------------------------
    std::printf("  ---- dead reckoning: forward along the route, or not at all\n");
    // 500 kt is 926 km/h; on an 1,852 km route that is exactly half the route
    // per hour, so these numbers are checkable by hand.
    check(std::fabs(ProgressDeadReckoned(0.2f, 1852.0f, 500.0f, 3600.0f) - 0.7f) < 0.005f,
          "an hour at 500 kt on an 1,852 km route advances half the arc");
    check(std::fabs(ProgressDeadReckoned(0.2f, 1852.0f, 500.0f, 1800.0f) - 0.45f) < 0.005f,
          "half an hour advances a quarter of it");
    check(ProgressDeadReckoned(0.2f, 1852.0f, 20.0f, 3600.0f) == 0.2f,
          "below 40 kt nothing is claimed -- the marker does not creep");
    check(ProgressDeadReckoned(0.2f, 1852.0f, 500.0f, 0.0f) == 0.2f,
          "no elapsed time, no advance");
    check(ProgressDeadReckoned(0.2f, 0.0f, 500.0f, 3600.0f) == 0.2f,
          "a zero-length route cannot be progressed along");
    check(ProgressDeadReckoned(0.2f, 1852.0f, NAN, 3600.0f) == 0.2f,
          "NaN speed does not propagate into a screen position");
    check(ProgressDeadReckoned(0.2f, 1852.0f, 500.0f, 36000.0f) <= 1.0f,
          "ten hours of silence still lands on the arc, not past its end");
    check(ProgressDeadReckoned(-3.0f, 1852.0f, 500.0f, 0.0f) == 0.0f,
          "a nonsense starting progress is clamped before anything else happens");
    // CONTROL: without this, every "returns the input unchanged" assertion above
    // would also pass for a function that never estimates anything at all.
    check(ProgressDeadReckoned(0.2f, 1852.0f, 500.0f, 3600.0f) > 0.2f,
          "CONTROL: a sane set of inputs really does advance the marker");

    // -------------------------------------------------------------------------
    // §8 SIGNAL_LOST -- how stale the picture is, as a glanceable magnitude
    // -------------------------------------------------------------------------
    std::printf("  ---- elapsed: the headline in SIGNAL_LOST\n");
    {
        char b[16];
        FormatElapsed(45u, b, sizeof(b));     check(same(b, "45s"),   "45 s");
        FormatElapsed(60u, b, sizeof(b));     check(same(b, "1m"),    "one minute rolls to minutes");
        FormatElapsed(3599u, b, sizeof(b));   check(same(b, "59m"),   "59 m stays minutes");
        FormatElapsed(3600u, b, sizeof(b));   check(same(b, "1h00"),  "an hour is zero-padded");
        FormatElapsed(4500u, b, sizeof(b));   check(same(b, "1h15"),  "1 h 15 m");
        FormatElapsed(86399u, b, sizeof(b));  check(same(b, "23h59"), "just under a day");
        FormatElapsed(86400u, b, sizeof(b));  check(same(b, "1d"),    "a day rolls to days");
        FormatElapsed(200000u, b, sizeof(b)); check(same(b, "2d"),    "and stays there");
        // CONTROL: every assertion above compares against a literal, so a
        // formatter that wrote one fixed string would fail -- but a formatter
        // that wrote nothing at all would leave the buffer untouched and could
        // pass by accident on a reused buffer. Prove it writes.
        char fresh[16] = { 'z', 'z', 'z', '\0' };
        FormatElapsed(4500u, fresh, sizeof(fresh));
        check(!same(fresh, "zzz"), "CONTROL: the buffer is actually written");
    }

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
    // ---- EN ROUTE IS NOT THE SAME AS LOCATABLE -----------------------------
    //
    // The bug this pins, found on glass 2026-08-29: a parked aircraft claimed
    // a landing time. Its guard was org.known && dst.known && havePos, and a
    // jet at a gate satisfies all three -- every input present, the claim still
    // false. An arrival is a statement about being ON THE WAY, so the STATE has
    // to be asked, not merely the data.
    std::printf("  ---- an arrival claim requires being en route, not locatable\n");
    {
        // Flying, or flying and out of contact: an estimate still means something.
        check( Machine::IsEnRoute(State::Airborne),     "Airborne is en route");
        check( Machine::IsEnRoute(State::NoCoverage),   "NoCoverage is en route");
        check( Machine::IsEnRoute(State::SignalLost),   "SignalLost is en route");
        check( Machine::IsEnRoute(State::ApproachLost), "ApproachLost is en route");

        // Not flying. No arithmetic over these produces an arrival.
        check(!Machine::IsEnRoute(State::Ground),  "Ground is NOT en route -- the bug");
        check(!Machine::IsEnRoute(State::Landed),  "Landed is NOT en route");
        check(!Machine::IsEnRoute(State::Waiting), "Waiting is NOT en route");
        check(!Machine::IsEnRoute(State::Idle),    "Idle is NOT en route");

        // CONTROL: this must not become a synonym for "we have data". Ground is
        // the state where every input is available and the conclusion is wrong,
        // so if IsEnRoute ever collapses into an input check, this fails.
        check(Machine::IsEnRoute(State::Airborne) != Machine::IsEnRoute(State::Ground),
              "CONTROL: en route distinguishes flying from parked");
    }

    // ---- THE EXPLANATION LINE MUST CLEAR THE AIRPORT LABELS ----------------
    //
    // Found on glass 2026-08-29: "Expected at this range." ran into "BCN", and
    // "No ground receivers out here" into the same label one state over. It
    // read as string-length dependent -- short strings looked fine, long ones
    // did not -- so it was invisible to review of any single string.
    //
    // It was never about the copy. EVERY explanation is wider than the free
    // span at the old row, because the row itself overlapped the label boxes.
    // This asserts the geometry against all of them at once, so a layout that
    // happens to suit the sentences we wrote today cannot pass.
    std::printf("  ---- every explanation clears the arc-end labels\n");
    {
        constexpr int S = 240, LINEH = 8, CHARW = 6;   // built-in font, size 1
        // FROM THE FACE, not a copy of it -- see follow::ARC_EXPLAIN_Y.
        const int EXPLAIN_Y = (int)follow::ARC_EXPLAIN_Y;
        // The two code labels, at radius 84 on the 135..405 degree arc.
        const int ox = 61, oy = 179, dx = 179, dy = 179;   // computed centres
        const int half = 2 * CHARW;                        // a 4-char code
        const discgeom::Box boxes[2] = {
            { ox - half, oy - LINEH / 2, ox + half, oy + LINEH / 2 },
            { dx - half, dy - LINEH / 2, dx + half, dy + LINEH / 2 },
        };

        const int avail = discgeom::ClearCentredWidthPx(EXPLAIN_Y, LINEH, S, boxes, 2);
        check(avail > 0, "the explanation row has usable width at all");

        // A centred line of that width must not reach either label box.
        const int lo = S / 2 - avail / 2, hi = S / 2 + avail / 2;
        for (int i = 0; i < 2; ++i) {
            const bool vOverlap = !(boxes[i].y1 <= EXPLAIN_Y || boxes[i].y0 >= EXPLAIN_Y + LINEH);
            const bool hOverlap = !(hi <= boxes[i].x0 || lo >= boxes[i].x1);
            check(!(vOverlap && hOverlap), "the fitted line does not overlap a code label");
        }

        // Every explanation, clamped to that width, still fits.
        // The shortest explanation we ship: if the row cannot hold even this,
        // the row is wrong regardless of what the other strings do.
        int shortestPx = 1 << 30;
        {
            const State every[] = { State::NoCoverage, State::SignalLost,
                                    State::ApproachLost, State::Waiting };
            const Regime both[2] = { Regime::Local, Regime::Airline };
            for (State st : every)
                for (Regime r : both) {
                    const char* e = Explanation(st, r);
                    int n = 0; while (e[n]) ++n;
                    if (n > 0 && n * CHARW < shortestPx) shortestPx = n * CHARW;
                }
        }
        check(shortestPx < (1 << 30), "found a shortest explanation to measure against");

        const State all[] = { State::NoCoverage, State::SignalLost,
                              State::ApproachLost, State::Waiting };
        for (State st : all) {
            const Regime regs[2] = { Regime::Local, Regime::Airline };
            for (Regime r : regs) {
                const char* e = Explanation(st, r);
                int n = 0; while (e[n]) ++n;
                const int wanted = n * CHARW;
                // Either it fits, or the face clamps it -- both are fine. What
                // must never happen is the row being too narrow to say anything.
                // THE REAL REQUIREMENT. Clearing the labels is true by
                // construction of ClearCentredWidthPx, so asserting it cannot
                // fail. What CAN fail -- and did, at the old row -- is the free
                // span collapsing below what the copy needs: at y=176 it was
                // 100 px against a 138 px shortest explanation, so every string
                // was clamped into nonsense or drawn over the label.
                if (wanted > 0)
                    check(avail >= shortestPx,
                          "the row fits the shortest explanation without clamping");
            }
        }

        // CONTROL: at the OLD row the same check must FAIL, or this test would
        // have passed against the bug it exists for.
        const int oldAvail = discgeom::ClearCentredWidthPx(176, LINEH, S, boxes, 2);
        check(oldAvail < avail,
              "CONTROL: the old row (176) is narrower -- it overlapped the labels");
        check(oldAvail * 1 < 138,
              "CONTROL: the old row could not fit even the shortest explanation");
    }

    return failures ? 1 : 0;
}
