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
#include <cstdio>

#include "Airports.h"

namespace follow {

constexpr float EARTH_R_KM = 6371.0088f;   // IUGG mean radius
constexpr float ARC_DEG2RAD = 0.01745329252f;
constexpr float ARC_RAD2DEG = 57.2957795131f;

// §8's arc sweeps clockwise from 135 degrees to 405, with 0 at 3 o'clock. That
// puts the origin at 7:30 and the destination at 4:30, leaving the bottom of the
// disc clear for the readout -- which is the whole reason for the odd numbers.
/// The row the state EXPLANATION draws on, in 240 px units.
///
/// SHARED WITH THE TEST ON PURPOSE. A host test holding its own copy of the row
/// would keep passing if the face moved, so both read this and a regression
/// cannot hide between them.
///
/// 176 -> 186 -> 190, and the middle step is the instructive one. At 176 the
/// text band (176..184) ran straight through both airport code boxes
/// (y 175..183). Moving to 186 cleared them geometrically -- and put the line
/// three pixels under the ORIGIN label, which read as crowded and would have
/// collided outright for any longer string. That was fixing the instance.
///
/// 190 is the first row clear of the boxes PLUS DISC_OBSTACLE_MARGIN_PX, and it
/// is chosen by that rule rather than by trying rows until the strings we happen
/// to ship looked right.
/// How close to the destination counts as "on approach", in km.
///
/// 40 km is about five minutes at approach speed, and comfortably larger than
/// any terminal area -- the point is to exclude an aircraft several HUNDRED
/// miles out, not to model an approach plate.
/// The projection radius that FRAMES a route on a panel of radius `panelPx`.
///
/// Replaces the 4,000 km globe/arc threshold (#274). Instead of drawing the
/// whole earth at one scale and refusing short routes, the sphere is scaled so
/// the two endpoints sit at FRAME_FILL of the panel radius -- a 900 km hop
/// becomes a close-up of curved terrain, a transoceanic haul a hemisphere.
///
/// An endpoint at angular distance theta/2 from the basis centre (MakeBasis
/// already centres on the great-circle midpoint) projects to screen radius
/// R*sin(theta/2), so R = FILL * panelPx / sin(theta/2).
///
/// FIXED PER ROUTE, AND DELIBERATELY SO. It depends only on the two endpoints,
/// never on the aircraft's position or the flown track, so it cannot change
/// while a flight is displayed. That is what makes the coastline LOD switch
/// safe without hysteresis: the scale changes only when the followed flight
/// changes, which is already a full face teardown. Framing that included the
/// TRACK would shrink R continuously as the track grew, cross the LOD boundary
/// mid-flight, and pop the coastline shape under the viewer -- so if that is
/// ever wanted, hysteresis or a crossfade becomes mandatory at the same moment.
inline float GlobeRadiusForRoute(float km, float panelPx)
{
    constexpr float FILL = 0.80f;
    constexpr float EARTH_KM = 6371.0f;
    const float theta = km / EARTH_KM;
    const float s = sinf(theta * 0.5f);
    // Same airport, or nonsense input: fall back to the whole-earth view rather
    // than dividing by ~0 and scaling to infinity.
    if (!(s > 1e-4f)) return panelPx;
    const float R = FILL * panelPx / s;
    // Never smaller than the panel. A globe that does not reach the edge reads
    // as a shrunken earth rather than a wide shot, and near-antipodal routes
    // would otherwise draw a disc floating in the middle of the screen.
    const float lo = panelPx;
    // Never sharper than the dense coastline data can support: 0.043 deg is
    // ~1 px at R=1332, and past that the close-up is magnifying straight lines.
    const float hi = 1332.0f * (panelPx / 120.0f);
    return R < lo ? lo : (R > hi ? hi : R);
}

constexpr float APPROACH_RADIUS_KM = 40.0f;

constexpr float ARC_EXPLAIN_Y = 190.0f;

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
///
/// THREE LETTERS ONLY, AND A FOUR-LETTER CODE MISSES ON PURPOSE. The route
/// mirror carries both forms -- RouteLabel.h's own examples are DFW-BUR (IATA)
/// and EGYD-EGYD (ICAO) -- and the baked table is IATA. "EGYD" therefore fails
/// the code[3] == '\0' test and resolves to unknown, which is right: the
/// alternative is matching its first three characters against "EGY", a field
/// this table does not contain today but might tomorrow, and drawing an
/// aircraft over the wrong continent. A missing airport is a gap; a wrong one
/// is a bug (Airports.h says the same thing about its own curation).
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

/// A point a fraction `f` of the way along the great circle from a to b.
///
/// SPHERICAL INTERPOLATION, AND THE DIFFERENCE IS NOT SUBTLE. Averaging two
/// latitudes and two longitudes puts the midpoint of DEN->DEL at 34.2 N,
/// 13.8 W -- in the Atlantic, off Morocco. The real midpoint is 83.9 N, 88.6 E,
/// north of Siberia (§9's worked example), because the route goes over the pole.
/// Those are 5,000 km apart, and the wrong one looks perfectly plausible on a
/// 240 px disc, which is exactly the class of error §9 exists to render
/// correctly.
///
/// Used by the globe's route polyline and by the bench's synthetic long-haul.
inline void InterpolateGreatCircle(const Endpoint& a, const Endpoint& b, float f,
                                   float& outLat, float& outLon)
{
    const float p1 = a.lat * ARC_DEG2RAD, l1 = a.lon * ARC_DEG2RAD;
    const float p2 = b.lat * ARC_DEG2RAD, l2 = b.lon * ARC_DEG2RAD;
    const float dp = sinf((p2 - p1) * 0.5f), dl = sinf((l2 - l1) * 0.5f);
    float h = dp * dp + cosf(p1) * cosf(p2) * dl * dl;
    if (h < 0.0f) h = 0.0f;
    if (h > 1.0f) h = 1.0f;
    const float d = 2.0f * asinf(sqrtf(h));      // angular separation, radians
    const float sd = sinf(d);
    // Coincident (or antipodal, where the great circle is not unique): there is
    // nothing to interpolate along, so return an endpoint rather than dividing.
    if (!(sd > 1e-6f)) { outLat = a.lat; outLon = a.lon; return; }
    const float A = sinf((1.0f - f) * d) / sd;
    const float B = sinf(f * d) / sd;
    const float x = A * cosf(p1) * cosf(l1) + B * cosf(p2) * cosf(l2);
    const float y = A * cosf(p1) * sinf(l1) + B * cosf(p2) * sinf(l2);
    const float z = A * sinf(p1) + B * sinf(p2);
    outLat = atan2f(z, sqrtf(x * x + y * y)) * ARC_RAD2DEG;
    outLon = atan2f(y, x) * ARC_RAD2DEG;
}

/// Progress carried forward from the last fix by dead reckoning, for the
/// NO_COVERAGE estimate (§8).
///
/// ALONG THE ROUTE, NOT ALONG A HEADING, and the reason is not laziness: a
/// follow::Fix carries no track angle at all. It could be given one -- the feed
/// has it -- but it should not be, because what the arc renders is a PROGRESS
/// BAR, and a progress bar advanced by "he flew 400 km somewhere" is worse than
/// one advanced by "he flew 400 km onward". Mid-Atlantic, onward is also the
/// truth: the whole reason this state exists is that he is following a route
/// across a receiver gap.
///
/// Returns the unchanged input rather than an estimate whenever it cannot
/// honestly extrapolate -- a stationary ground speed, a garbage total, a
/// negative elapsed. The caller draws a hollow marker either way; what it must
/// never do is creep forward on numbers that do not support it.
inline float ProgressDeadReckoned(float progressAtFix, float totalKm,
                                  float groundSpeedKt, float elapsedSec)
{
    const float p = progressAtFix < 0.0f ? 0.0f
                                         : (progressAtFix > 1.0f ? 1.0f : progressAtFix);
    if (!std::isfinite(totalKm) || !std::isfinite(groundSpeedKt) ||
        !std::isfinite(elapsedSec))
        return p;
    if (!(totalKm > 1.0f) || elapsedSec <= 0.0f) return p;
    if (groundSpeedKt < 40.0f) return p;   // same floor as MinutesToArrival
    const float km = groundSpeedKt * 1.852f * (elapsedSec / 3600.0f);
    const float f = p + km / totalKm;
    return f > 1.0f ? 1.0f : f;
}

/// "How long since we last heard anything", for SIGNAL_LOST's primary readout.
///
/// The headline in that state is HOW STALE THE PICTURE IS, so this is a
/// glanceable magnitude rather than a precise duration: minutes below an hour,
/// hours and minutes below a day, whole days above. Formats into the caller's
/// buffer for the same reason AlertTitle does -- nothing on a draw path should
/// allocate, and String is what would drag Arduino into this header.
inline void FormatElapsed(uint32_t seconds, char* out, size_t n)
{
    if (!out || n == 0) return;
    if (seconds < 60u)              snprintf(out, n, "%us", (unsigned)seconds);
    else if (seconds < 3600u)       snprintf(out, n, "%um", (unsigned)(seconds / 60u));
    else if (seconds < 86400u)      snprintf(out, n, "%uh%02u", (unsigned)(seconds / 3600u),
                                             (unsigned)((seconds % 3600u) / 60u));
    else                            snprintf(out, n, "%ud", (unsigned)(seconds / 86400u));
}

} // namespace follow
