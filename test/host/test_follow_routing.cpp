// Which face, and whether the swipe can produce one.
//
// Both decisions were inline in AircraftManager until 2026-08-30, where the only
// way to check them was to flash a board and look. Extracting them is most of the
// value; the cases below are the rest.
//
// TWO OF THE ASSERTIONS HERE ARE MADE BY THE SIGNATURE, NOT BY A CASE.
// FaceForRoute takes no regime and no distance. Those are the two inputs it used
// to have and must never have again, and a parameter that does not exist cannot
// be consulted by accident. The cases below can only check what it does with the
// inputs it kept -- so the comments say which guarantee comes from where, since
// a future reader deleting "an untested parameter" would be deleting the point.

#include "../../include/FollowRouting.h"

#include <cstdio>

static int checks = 0;
static int failures = 0;

static void check(bool ok, const char* what)
{
    ++checks;
    if (!ok) { ++failures; std::printf("  FAIL  %s\n", what); }
}

using namespace follow;

int main()
{
    std::printf("== follow routing ==\n");

    // ---- THREE CASES, THREE FACES ------------------------------------------
    std::printf("  ---- the route decides, and only the route\n");

    check(FaceForRoute(true, true, true) == Face::Globe,
          "both codes place -> GLOBE");
    check(FaceForRoute(true, false, false) == Face::Arc,
          "neither code places -> ARC (the code-only face)");
    check(FaceForRoute(false, false, false) == Face::Local,
          "no codes at all -> LOCAL");

    // ---- THE HALF-PLACEABLE PAIR, which is the case that used to be wrong ---
    //
    // An earlier binary reading (routed / not routed) sent these to the local
    // face. That loses the codes -- the most informative true thing the device
    // holds about the flight -- and discards a composition already judged on
    // glass. Both directions, because "one endpoint missing" has two shapes and
    // only testing one of them is how the other stays broken.
    std::printf("  ---- half a route is still a route\n");
    check(FaceForRoute(true, true, false) == Face::Arc,
          "origin places, destination does not -> ARC, not LOCAL");
    check(FaceForRoute(true, false, true) == Face::Arc,
          "destination places, origin does not -> ARC, not LOCAL");
    check(FaceForRoute(true, true, false) != Face::Local,
          "CONTROL: ... and specifically NOT the local face (the old bug)");
    check(FaceForRoute(true, true, false) != Face::Globe,
          "CONTROL: ... and not an empty globe either");

    // ---- A GLOBE NEEDS BOTH, and nothing else can substitute ----------------
    //
    // Every input combination, so "globe" is provably reachable from exactly one
    // of them. A rule with three outcomes and eight inputs is small enough to
    // enumerate, and enumeration is the only form of this test that cannot be
    // satisfied by an implementation that happens to agree on the cases somebody
    // thought to write down.
    std::printf("  ---- exhaustive: 8 input combinations, 3 outcomes\n");
    {
        int globes = 0, arcs = 0, locals = 0;
        for (int i = 0; i < 8; ++i) {
            const bool codes = (i & 4) != 0;
            const bool o     = (i & 2) != 0;
            const bool d     = (i & 1) != 0;
            const Face f = FaceForRoute(codes, o, d);
            if (f == Face::Globe)      ++globes;
            else if (f == Face::Arc)   ++arcs;
            else                       ++locals;
            // A face that needs coordinates must never be chosen without them.
            if (f == Face::Globe)
                check(codes && o && d, "a GLOBE is only ever chosen with both endpoints placed");
        }
        check(globes == 1, "exactly one of the eight inputs yields a globe");
        check(arcs   == 3, "three yield the arc: codes present, not both placed");
        check(locals == 4, "four yield local: no codes, whatever the lookups said");
    }

    // ---- THE SWIPE ---------------------------------------------------------
    //
    // Every card is swipeable. The conjunction is the only refusal, and each half
    // of it alone must still follow -- which is the assertion that would have
    // caught a `||` typed where an `&&` belongs, and that is a one-character
    // mistake whose symptom (the feature refusing flights) is indistinguishable
    // from the old rule it replaces.
    std::printf("  ---- every card is swipeable; one conjunction declines\n");
    check(OutcomeForSwipe(true,  true)  == SwipeOutcome::Follow,
          "route and location -> follow");
    check(OutcomeForSwipe(true,  false) == SwipeOutcome::Follow,
          "route, no location -> follow (arc/globe need no home)");
    check(OutcomeForSwipe(false, true)  == SwipeOutcome::Follow,
          "no route, location -> follow (the local face is the picture)");
    check(OutcomeForSwipe(false, false) == SwipeOutcome::Decline,
          "no route AND no location -> decline: nothing can be drawn");

    // CONTROL: exactly one of the four declines. An implementation that declined
    // whenever a route was missing would pass three of the four assertions above
    // -- and it is precisely the implementation this change replaced.
    {
        int declines = 0;
        for (int i = 0; i < 4; ++i)
            if (OutcomeForSwipe((i & 2) != 0, (i & 1) != 0) == SwipeOutcome::Decline)
                ++declines;
        check(declines == 1,
              "CONTROL: exactly ONE of four inputs declines, not two");
    }

    std::printf("\n%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
