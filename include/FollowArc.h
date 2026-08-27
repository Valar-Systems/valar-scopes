#pragma once

// Follow Mode -- the arc face's arithmetic.  Spec: docs/follow-mode-consolidated.md §8.
//
// =============================================================================
// PURE, FOR THE SAME REASON AS EVERYTHING ELSE IN STAGE 1
//
// The arc face is a rendering layer over the stage-1 state machine (§19) -- but
// the numbers it renders are not free. Progress along a great circle, a bearing,
// a time to arrival: each is a small piece of spherical trigonometry that is
// wrong in a way nobody sees on a 240 px disc. A marker at 41% instead of 44%
// looks exactly like a marker at 44%.
//
// So the maths lives here with no Arduino and no display, and it is graded on
// the workstation against hand-computed reference values.
//
// =============================================================================
// GREAT CIRCLE, NOT THE FLAT APPROXIMATION
//
// FollowState.h's SeparationKm is equirectangular, and that is correct THERE:
// it compares against a home radius of a few km, where the great-circle
// correction is far below the 150 m sampling threshold it is being measured
// against (see FollowTrack.h).
//
// Here the baselines are transatlantic. DEN->DEL is 12,406 km over the pole; the
// flat approximation does not merely lose precision on that, it is meaningless,
// because it has no notion of the pole at all. Different problem, different
// formula, and the two are deliberately named differently so a future reader
// does not "unify" them.

#include <cmath>
#include <cstdint>

#include "Airports.h"

namespace follow {

constexpr float EARTH_R_KM = 6371.0088f;   // IUGG mean radius
constexpr float ARC_DEG2RAD = 0.01745329252f;
constexpr float ARC_RAD2DEG = 57.2957795131f;

// §8's arc sweeps clockwise from 135 degrees to 405, with 0 at 3 o'clock. That
// puts the origin at 7:30 and the destination at 4:30, leaving the bottom of the
// disc clear for the readout -- which is the whole reason for the odd numbers.
constexpr float ARC_START_DEG = 135.0f;
constexpr float ARC_SWEEP_DEG = 270.0f;

/// One end of a route. `known` false means the code did not resolve, which is a
/// state the face must render rather than a failure -- see LookupAirport.
struct Endpoint {
    float lat = 0.0f;
    float lon = 0.0f;
    bool  known = false;
};

/// Resolve an IATA code against the table baked into every board.
///
/// THE BAKED TABLE IS ~250 MAJORS AND THAT IS DELIBERATE FOR NOW. C1 settles the
/// wider delivery (`ap:<CODE>` keys in KV, 34,128 fields, carrying elevation for
/// C5), and when it lands this function grows a lookup and NOTHING ELSE ON THE
/// FACE CHANGES. That seam is why the face is built against the table first:
/// the majors cover the overwhelming majority of airline city pairs, and a code
/// that misses degrades to an honest code-only arc rather than to a wrong one.
inline Endpoint LookupAirport(const char* code)
{
    Endpoint e;
    if (!code || !code[0]) return e;
    for (size_t i = 0; i < AIRPORT_COUNT; ++i) {
        const char* c = AIRPORTS[i].code;
        if (c[0] == code[0] && c[1] == code[1] && c[2] == code[2] && code[3] == '\0') {
            e.lat = AIRPORTS[i].lat;
            e.lon = AIRPORTS[i].lon;
            e.known = true;
            return e;
        }
    }
    return e;
}

/// Great-circle distance in km. Haversine, which is the numerically stable form
/// for the short baselines the naive spherical law of cosines ruins.
inline float GreatCircleKm(float aLat, float aLon, float bLat, float bLon)
{
    const float p1 = aLat * ARC_DEG2RAD, p2 = bLat * ARC_DEG2RAD;
    const float dp = (bLat - aLat) * ARC_DEG2RAD;
    const float dl = (bLon - aLon) * ARC_DEG2RAD;
    const float s1 = sinf(dp * 0.5f), s2 = sinf(dl * 0.5f);
    float a = s1 * s1 + cosf(p1) * cosf(p2) * s2 * s2;
    if (a < 0.0f) a = 0.0f;
    if (a > 1.0f) a = 1.0f;
    return 2.0f * EARTH_R_KM * asinf(sqrtf(a));
}

/// Initial great-circle bearing from A to B, degrees true, 0..360.
inline float BearingDeg(float aLat, float aLon, float bLat, float bLon)
{
    const float p1 = aLat * ARC_DEG2RAD, p2 = bLat * ARC_DEG2RAD;
    const float dl = (bLon - aLon) * ARC_DEG2RAD;
    const float y = sinf(dl) * cosf(p2);
    const float x = cosf(p1) * sinf(p2) - sinf(p1) * cosf(p2) * cosf(dl);
    float d = atan2f(y, x) * ARC_RAD2DEG;
    // atan2 gives (-180, 180]; the compass wants [0, 360). Written as one
    // normalisation rather than an if, because the sign of fmod differs across
    // languages and this file's numbers get transcribed (see CLAUDE.md on
    // cross-language rounding).
    d = fmodf(d, 360.0f);
    if (d < 0.0f) d += 360.0f;
    return d;
}

/// How far along the route, 0..1.
///
/// ALONG-TRACK RATIO, not a cross-track projection. The aircraft is rarely
/// exactly on the great circle -- §9 makes a feature of that gap -- but the arc
/// is a PROGRESS BAR, and what a progress bar answers is "how much is left".
/// Distance-flown over distance-total answers that for any routing; projecting
/// onto the line first would answer a question nobody asked and would go
/// backwards on a dogleg.
///
/// Clamped, because a diversion can legitimately put the aircraft further from
/// the origin than the destination is, and a marker off the end of the arc is a
/// rendering bug rather than information.
inline float ProgressAlong(const Endpoint& origin, const Endpoint& dest,
                           float acLat, float acLon)
{
    if (!origin.known || !dest.known) return 0.0f;
    const float total = GreatCircleKm(origin.lat, origin.lon, dest.lat, dest.lon);
    if (!(total > 1.0f)) return 0.0f;   // same field, or nonsense: no progress to show
    const float flown = GreatCircleKm(origin.lat, origin.lon, acLat, acLon);
    const float f = flown / total;
    return f < 0.0f ? 0.0f : (f > 1.0f ? 1.0f : f);
}

/// Screen angle, in degrees, of a point at `progress` along the arc.
inline float ArcAngleDeg(float progress)
{
    const float p = progress < 0.0f ? 0.0f : (progress > 1.0f ? 1.0f : progress);
    return ARC_START_DEG + ARC_SWEEP_DEG * p;
}

/// Minutes to arrival at the current ground speed, or -1 when it cannot be said.
///
/// DECLINES RATHER THAN GUESSES, the same rule as ReportableAltFt. A stationary
/// or garbage ground speed divides into an arbitrarily large number, and "1,847
/// min" on the primary readout is a claim the customer cannot check. Below
/// 40 kt no airliner is making progress toward anywhere, so there is no honest
/// arrival time to give.
inline int MinutesToArrival(float remainingKm, float groundSpeedKt)
{
    if (!std::isfinite(remainingKm) || !std::isfinite(groundSpeedKt)) return -1;
    if (remainingKm < 0.0f) return -1;
    if (groundSpeedKt < 40.0f) return -1;
    const float kmh = groundSpeedKt * 1.852f;
    const float h = remainingKm / kmh;
    if (h > 24.0f) return -1;            // longer than any flight: the inputs disagree
    return (int)lroundf(h * 60.0f);
}

} // namespace follow
