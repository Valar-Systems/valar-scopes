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
