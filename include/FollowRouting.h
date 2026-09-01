#pragma once

/**
 * WHICH FACE, AND WHETHER THE GESTURE CAN PRODUCE ONE.
 *
 * Two decisions, extracted from AircraftManager so they can be graded on the
 * host. Both used to live inline in a 9,000-line Arduino TU, where the only way
 * to check them was to flash a board and look at it -- which is how the routing
 * rule below spent a fortnight being about the wrong thing.
 *
 * THE SIGNATURE IS THE ASSERTION. FaceForRoute takes no regime, no distance and
 * no home position, and that is the whole point of the file rather than an
 * incidental fact about it:
 *
 *   - THE REGIME IS GONE (demoted 2026-08-30, docs/follow-mode-consolidated.md
 *     7.1). Routing on it put a decision about WHICH PICTURE TO DRAW behind an
 *     inference about HOW FAR FROM HOME THE FLIGHT GOT, so an airliner on
 *     climb-out got the local face despite having a route, and then the face
 *     changed under the customer at an invisible circle. A test can assert the
 *     regime is not consulted; a parameter that does not exist cannot be
 *     consulted by accident, which is strictly better.
 *
 *   - THE DISTANCE IS GONE (#274). The 4,000 km globe threshold was a
 *     legibility argument and a coastline-resolution argument wearing one
 *     number, and route framing plus denser data removed both.
 *
 * What is left is the only question the faces actually differ on: WHAT THE
 * INPUTS CAN FILL. The globe is built from coordinates, the arc from strings,
 * the local face from neither.
 */

#include <stdint.h>

#include "FollowArc.h"
#include "GlobeProjection.h"   // the fitted view is the test -- see AircraftOutOfFittedView   // GreatCircleKm, for the contradiction test below

namespace follow {

enum class Face : uint8_t {
    Local,   // rings around home, and a track (spec 10)
    Arc,     // the CODE-ONLY face (spec 8) -- draws from strings alone
    Globe,   // orthographic, route-framed (spec 9) -- needs both endpoints placed
};

/**
 * @param haveCodes    both route codes are present as strings
 * @param originPlaces the origin code resolved to coordinates
 * @param destPlaces   the destination code resolved to coordinates
 *
 * THE ARC IS THE CODE-ONLY FACE, NOT THE UNROUTED FACE. An earlier reading of
 * this rule was binary -- routed or not -- and sent an unplaceable pair to the
 * local face. That loses the codes, which are the most informative true thing
 * the device holds about that flight, and throws away a composition already
 * judged on glass: "ZQX -> QZY" under "ROUTE ONLY" was deliberately made to
 * read as a lesser state rather than as a broken one.
 */
inline Face FaceForRoute(bool haveCodes, bool originPlaces, bool destPlaces)
{
    if (!haveCodes) return Face::Local;
    return (originPlaces && destPlaces) ? Face::Globe : Face::Arc;
}

// =============================================================================
// DOES THIS ROUTE BELONG TO THIS AIRCRAFT?
//
// SWA986 drew Orlando -> Baltimore on the globe while the aircraft was overhead
// in central Oregon, at 100% progress. Nothing on the face was wrong about
// itself: the codes resolved, the arc drew, the progress clamped to 1.0. The
// route simply was not this aeroplane's.
//
// The mechanism is upstream and worth naming so this check is not mistaken for
// the fix: routes are keyed on CALLSIGN (`rt:<cs>` in the Worker), and a flight
// number is reused across legs and days. The Worker already knows this -- it
// runs a plausibility test against the aircraft's position and refuses an
// implausible route -- but ONLY on a cache miss. The cached branch returns the
// stored route before that test is reached, so a leg that was plausible when it
// was cached is served unchecked to a different aeroplane flying the same
// number later. A guard with a path around it, which is the recurring shape.
//
// So this is the DEVICE refusing to draw a claim it can see is false, which is
// worth having whatever the upstream does -- it is the last place the position
// and the route are both in hand.
//
// THE FIRST VERSION OF THIS TEST WAS A DISTANCE RULE, AND IT DID NOT FIRE.
//
//     is the aircraft FURTHER FROM BOTH ENDPOINTS than they are from each other?
//
// The reasoning was that real flights leave the geodesic by tens to low hundreds
// of km as a matter of course, so a cross-track threshold tight enough to catch
// a wrong route is tight enough to reject a correct one -- and that a question
// with no plausible innocent answer was safer than a tuned number.
//
// It was too weak by a factor nobody estimated. SWA4083 drew DEN -> RDU on a
// device at Bend, Oregon, with ADS-B line-of-sight at cruise around 400 km:
//
//     route DEN-RDU   d = 2306.5 km
//     Bend -> DEN     a = 1450.0 km      <- the term that saved the bad route
//     Bend -> RDU     b = 3692.9 km
//     needed a > d + 100 = 2406.5, so the aircraft had to be 957 km FURTHER OUT
//
// It had also never been observed rejecting anything in production -- there is no
// telemetry on it at all -- so it was a constraint nobody had seen fire, guarding
// a case it turns out not to catch.
//
// THE REPLACEMENT HAS NO THRESHOLD, because a tighter number would just be a
// differently-untested constant. The globe face already fits its zoom to the
// route: R = usable / sin(theta/2), clamped. So ask the question the picture
// already answers --
//
//     does the aircraft land inside the view fitted to its own route?
//
// -- and a route whose aircraft is not in frame is wrong by construction. In the
// SWA4083 photograph both endpoint markers were drawn and there was no aircraft
// anywhere on the line, because the fit framed Denver-to-Raleigh and the aircraft
// was 900 miles west of the left edge, clipped. The evidence was already on the
// glass; nothing was asking about it.
//
// NO NEW DATA AND NO NEW CALL. The position and the fitted radius both already
// exist at the call site. R IS PASSED IN rather than recomputed here, and that is
// the load-bearing part of the signature: the caller obtains it from the same
// helper the globe draws with, so the check cannot drift from the picture it is
// checking. Re-deriving it here would be a second path to the same quantity,
// which is the mistake this file's own history is made of.
//
// Returns TRUE when the route must be refused.
inline bool AircraftOutOfFittedView(const Endpoint& origin, const Endpoint& dest,
                                    float acLat, float acLon,
                                    float fittedR, int screenSize)
{
    if (!origin.known || !dest.known) return false;   // cannot be checked
    if (!(fittedR > 0.0f) || screenSize <= 0) return false;

    // The same basis the globe builds: centred on the route's great-circle
    // midpoint, north up, tilt 0.
    const globeproj::Basis b =
        globeproj::MakeBasis(origin.lon, origin.lat, dest.lon, dest.lat, 0.0f);

    float w[3], x = 0.0f, y = 0.0f;
    globeproj::UnitVec(acLon, acLat, w);
    const float c = (float)screenSize * 0.5f;
    // Project() returns false for the FAR HEMISPHERE, which is itself decisive:
    // an aircraft on the other side of the planet from its own route's midpoint
    // is not on that route.
    if (!globeproj::Project(b, w[0], w[1], w[2], c, c, fittedR, x, y)) return true;

    // In frame means inside the DISC, not the square. The face is a circle and
    // the corners are behind the bezel, so a marker there is not visible to the
    // customer whatever the framebuffer says.
    //
    // A margin of one radius-tenth INWARD would reject aircraft that are merely
    // near the rim, which is where a legitimately-just-departed flight sits. So
    // the test is the full disc: this exists to catch the wrong continent, not to
    // police the edge.
    const float dx = x - c, dy = y - c;
    return (dx * dx + dy * dy) > (c * c);
}

enum class SwipeOutcome : uint8_t {
    Follow,   // set the session target
    Decline,  // say so on screen; nothing can be drawn
};

/**
 * Swipe down on a detail card.
 *
 * EVERY CARD IS SWIPEABLE, which reverses "no route, no affordance". That rule
 * was aimed at a real risk -- an affordance which sometimes does nothing teaches
 * people it does nothing -- and at the wrong behaviour: a route-less contact is
 * not a flight with nothing to draw, it is a flight whose picture is the local
 * face, and refusing the gesture there silently declined the commonest aircraft
 * on a domestic scope.
 *
 * EXACTLY ONE COMBINATION CANNOT PRODUCE A FACE, and it is the conjunction:
 * without codes there is no arc and no globe, and without a location the local
 * face has no centre, no HOME marker and nothing to auto-scale against. Either
 * one alone is fine. That case declines VISIBLY -- silence there is the old
 * rule in miniature, and its cost is that the customer learns the gesture does
 * nothing instead of learning what to do about it.
 */
inline SwipeOutcome OutcomeForSwipe(bool haveRoute, bool hasLocation)
{
    return (!haveRoute && !hasLocation) ? SwipeOutcome::Decline
                                        : SwipeOutcome::Follow;
}

} // namespace follow
