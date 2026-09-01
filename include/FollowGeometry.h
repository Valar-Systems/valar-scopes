#pragma once

// Follow Mode -- the local face's geometry.  Spec: docs/follow-mode-consolidated.md §10.
//
// =============================================================================
// WHY THIS IS ITS OWN FILE, AND PURE
//
// §10's local face auto-scales: "Range rings, auto-scaled to the track's
// bounding box, with live labels (1 / 2 / 5 km)". The scaling rule is the part
// that can be wrong in a way nobody notices -- a ring labelled 2 that is not at
// two, a scale that collapses to zero when the aeroplane sits still on the
// apron, a track that overflows its outermost ring. None of that is visible on a
// 240 px disc at a glance, and all of it is arithmetic.
//
// So the arithmetic lives here with no Arduino, no display and no state, and it
// is graded on the workstation by test/host/test_follow_state.cpp. The drawing
// code keeps only the parts a test could not have caught anyway.
//
// =============================================================================
// §9 AND §10 DO NOT CONFLICT, AND THIS IS THE FILE WHERE THAT MATTERS
//
// §9 forbids continuous zoom. §10 requires auto-fit. The rule that separates
// them: §9 is about SAMPLED geography -- a coastline decimated at 0.5 degrees,
// where zooming in past the decimation exposes the sampling and the picture
// starts lying. Here the reference is GENERATED: rings drawn at whatever radius
// the data needs, labelled with the radius they were drawn at. There is no
// sampling to expose, so continuous auto-fit has no failure mode. Scale the view
// to the track and label the rings honestly.

#include <cmath>
#include <cstdint>

namespace follow {

// ---------------------------------------------------------------------------
// THE RING LADDER
//
// A step is chosen from {1, 2, 5} x 10^k -- the ladder every chart and every
// instrument uses, for the reason that a person reads "2" and "5" instantly and
// reads "3.7" by doing arithmetic. §10's own example labels are 1 / 2 / 5.
//
// The ring at `divisions * step` is the outermost, so the whole track fits
// inside the face exactly when divisions * step >= the furthest point. Callers
// pass the furthest point ALREADY IN DISPLAY UNITS, because the labels have to
// be round in the unit the customer chose -- a step that is round in kilometres
// is 0.62, 1.24, 3.11 in miles, which is the failure this ladder exists to
// avoid. (One conversion, at the edge, per include/DisplayUnits.h.)
// ---------------------------------------------------------------------------

// The smallest ladder step whose `divisions`-th ring contains `maxValue`.
//
// FLOOR, and it is load-bearing: an aeroplane parked on the apron gives a track
// whose bounding box is zero, and a zero step is a division by zero in the
// projection and an unlabelled face. `minStep` is the smallest scale the face
// will ever draw -- the view stops zooming in rather than degenerating.
inline float NiceStep(float maxValue, int divisions, float minStep = 0.1f)
{
    if (divisions < 1) divisions = 1;
    if (!(maxValue > 0.0f) || !std::isfinite(maxValue))
        return minStep;

    const float want = maxValue / (float)divisions;
    if (want <= minStep) return minStep;

    // Walk the decade, then the mantissa. Bounded rather than closed-form so a
    // pathological input cannot spin: 1e-3 through 1e6 covers a parked aeroplane
    // through a transatlantic leg, and anything outside that is clamped and
    // visible rather than silently wrong.
    float decade = 1e-3f;
    for (int k = 0; k < 10; ++k) {
        const float ladder[3] = { 1.0f * decade, 2.0f * decade, 5.0f * decade };
        for (int i = 0; i < 3; ++i)
            if (ladder[i] >= want)
                return ladder[i] > minStep ? ladder[i] : minStep;
        decade *= 10.0f;
    }
    return 1e6f;
}

// The view the local face draws: centred on home, `rings` equally-spaced rings
// a `stepDisplay` apart, the outermost being the edge of the usable disc.
//
// `stepDisplay` is in the customer's unit (it is what the labels say);
// `radiusKm` is the same distance in kilometres, which is what the projection
// needs. Both are carried rather than one being re-derived at the draw site,
// because a second conversion is a second place for the two to disagree -- the
// exact failure include/DisplayUnits.h was written to end.
struct LocalView {
    float centreLat   = 0.0f;
    float centreLon   = 0.0f;
    float stepDisplay = 0.0f; // ring spacing, in the display unit -- the label
    float radiusKm    = 0.0f; // outermost ring, in km -- the projection scale
    int   rings       = 3;

    // ---- THE SUBJECT -------------------------------------------------------
    //
    // THE VIEW CARRIES THE POSITION; THE DRAW MUST NOT GO AND FETCH IT. This
    // face is the one the ABSENCE states land on, and NO_COVERAGE and
    // SIGNAL_LOST *mean* the aeroplane is not in the contact table -- so a draw
    // that asks FollowedAircraft() for it draws nothing in exactly the states
    // the face exists for. The fallback to the machine's last fix is the same
    // one FollowRouteView already makes for the arc; it was simply never made
    // here, because until now LocalView had nowhere to put the answer.
    float acLat       = 0.0f;
    float acLon       = 0.0f;
    bool  havePos     = false;
    float gsKt        = 0.0f;
    /// Degrees true. A Fix carries no heading -- only a live contact does -- so
    /// with no contact this is course made good over the track's last leg, and
    /// when even that is unavailable `haveHeading` stays false and the symbol
    /// declines to point rather than pointing at zero.
    float headingDeg  = 0.0f;
    bool  haveHeading = false;
};

// ---------------------------------------------------------------------------
// WINDOW-UP, EXPRESSED AS AN ANGLE RATHER THAN APPLIED TO A POSITION
//
// ProjectLocal below rotates a POSITION into the window-up frame. The bearing
// face has no position to project -- it draws a ray, and a ray is an angle --
// so it needs the same rotation in the form an angle can take, and this is it.
//
// THE TWO MUST AGREE, AND THAT IS NOT LEFT TO THIS COMMENT. A point projected
// through ProjectLocal at true bearing b lands at exactly ScreenBearingDeg(b)
// measured from the top of the disc; test/host/test_follow_state.cpp asserts it
// by MEASURING the angle off a projected point rather than by re-deriving the
// formula, so the two sides cannot drift apart while both look right alone.
//
// 0 is the TOP of the screen and it increases CLOCKWISE -- what a compass does,
// and not what screen-polar does. The arc face's bezel is screen-polar
// (0 = 3 o'clock) and subtracts a further 90 at its own call site; the WINDOW-UP
// half of the rule lives here, once, for both faces.
inline float ScreenBearingDeg(float trueBearingDeg, int radarUpDeg)
{
    float d = trueBearingDeg - (float)radarUpDeg;
    // Normalised the same way, and written the same way, as FollowArc.h's
    // BearingDeg: the sign of fmod differs across languages and this file's
    // numbers get transcribed (see CLAUDE.md on cross-language rounding).
    d = fmodf(d, 360.0f);
    if (d < 0.0f) d += 360.0f;
    return d;
}

// Screen position of a point, in the local face's frame.
//
// Equirectangular about home and then rotated for window-up, which is the same
// treatment AircraftManager::ProjectCoordinateToScreen gives the radar. It has
// to be the same or the two faces disagree about which way north is, and a
// device that rotates one picture and not the other reads as a bug in whichever
// one the customer is looking at second.
//
// `screenSize` and the 0.5 offsets keep this free of Layout.h, so the file stays
// host-compilable. Returns floats; the caller rounds.
inline void ProjectLocal(float pointLat, float pointLon, const LocalView& v,
                         float rotSin, float rotCos, float screenSize,
                         float usableRadiusPx, float& outX, float& outY)
{
    const float kmPerDegLat = 111.0f;
    const float kmPerDegLon = 111.0f * cosf(v.centreLat * 0.01745329252f);

    const float dNorthKm = (pointLat - v.centreLat) * kmPerDegLat;
    const float dEastKm  = (pointLon - v.centreLon) * kmPerDegLon;

    const float pxPerKm = (v.radiusKm > 0.0f) ? (usableRadiusPx / v.radiusKm) : 0.0f;
    const float c = screenSize * 0.5f;

    // Screen y grows downward; north is up before rotation.
    float px =  dEastKm  * pxPerKm;
    float py = -dNorthKm * pxPerKm;

    // Same rotation as the radar: bearing radarUpDeg reads "up".
    const float rx = px * rotCos + py * rotSin;
    const float ry = -px * rotSin + py * rotCos;

    outX = c + rx;
    outY = c + ry;
}

// -----------------------------------------------------------------------------
// CIRCUIT COUNTING IS NOT HERE, AND THAT IS DELIBERATE.
//
// §10 lists circuit count among the local face's readouts and §11 defers it in
// the same breath: "It is the most charming number in the feature and the one
// most likely to be wrong on first contact with real data. Build the state
// machine so the altitude history exists, then look at an actual logged lesson
// before deciding what a circuit is."
//
// §18.3 is the same instruction from the other end -- every constant in §5.3 is
// a guess until one real training flight has been logged. A circuit detector
// written now would be a fourth guess wearing a number, and a WRONG count is
// worse than no count: "6 circuits" is a claim, and the customer has no way to
// check it. The face draws what it can defend and leaves the slot empty.
// -----------------------------------------------------------------------------

} // namespace follow
