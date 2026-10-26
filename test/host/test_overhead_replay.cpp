// Overhead replay host case: checks (e), (e2), the HA state case and a new unit, through the firmware's
// own rules (OverheadReplay.h). Frozen: docs/v17-overhead-card-predictions.md, addendum 2026-10-12 (2),
// d51bf9cc698a -- R1..R13 below are the numbered checks there, and the sabotage table predicts which
// of them each sabotage turns red.
#include <cstdio>
#include "OverheadReplay.h"

using namespace ovhreplay;

static int failures = 0;
static void check(bool ok, const char* what)
{
    std::printf("  %s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++failures;
}

static void show(const char* name, const Result& r)
{
    std::printf("  %-34s ovh-on=%-8s alert-after=%-8s edges=%d rings=%d cards=%d pushes=%d ha-state=%d ha-events=%d\n",
                name, r.ovhOn.c_str(), r.lookupAlertAfter.c_str(), r.edges, r.rings, r.cards, r.pushes, r.haStateRises, r.haEvents);
}

int main()
{
    std::printf("overhead replay (OverheadReplay.h)\n");
    // 8 edges: 6 idle, 2 touched. Cards when on: t=0 show, 20 s spacing, 70 s show, 100 s busy,
    // 200 s show, 230 s spacing, 400 s busy, 500 s show -> 4.
    const std::vector<Edge> E = { {0, true}, {20000, true}, {70000, true}, {100000, false},
                                  {200000, true}, {230000, true}, {400000, false}, {500000, true} };
    const int n = (int)E.size();

    const Result e = Replay({ "true", "false", true, false }, E);
    const Result eCtl = Replay({ "true", "true", true, false }, E);
    const Result e2 = Replay({ "false", "true", true, false }, E);
    const Result ha = Replay({ "false", "false", false, true }, E);
    const Result haCtl = Replay({ "true", "false", false, true }, E);
    const Result fresh = Replay({ nullptr, nullptr, true, false }, E);
    show("(e)  ring on,  push off, topic", e);
    show("(e)  CONTROL: push box on", eCtl);
    show("(e2) ring off, push on,  topic", e2);
    show("HA   ring off, push off, mqtt", ha);
    show("HA   CONTROL: ring on, mqtt", haCtl);
    show("new unit, never saved, topic", fresh);

    check(e.ovhOn == "true", "R1  (e) ovh-on written true");
    check(e.pushes == 0, "R2  (e) zero overhead pushes");
    check(eCtl.pushes == n && n >= 1, "R3  (e) CONTROL: with the push box on, pushes == edges");
    check(e2.ovhOn == "true", "R4  (e2) ovh-on written true (OR)");
    check(e2.pushes == n, "R5  (e2) keeps every push: pushes == edges");
    check(e2.rings == n, "R6  (e2) gains the ring: rings == edges (accepted)");
    check(e2.lookupAlertAfter == "true", "R7  (e2) lookup-alert still true after the migration");
    check(ha.ovhOn == "false", "R8  (HA) ovh-on written false");
    check(ha.haStateRises == 0, "R9  (HA) zero HA state rises over the replay");
    check(ha.haEvents == n, "R10 (HA) per-aircraft events == edges (independent of the switch)");
    check(haCtl.haStateRises == n && n >= 1, "R11 (HA) CONTROL: ring on -> HA state rises == edges");
    check(fresh.ovhOn == "(absent)" && fresh.rings == n && fresh.pushes == 0, "R12 new unit: switch ON by default, zero pushes (box defaults off)");
    check(e.cards == 4, "R13 (e) the card policy shows exactly 4 cards over the 8 edges");

    std::printf(failures ? "FAILED: %d\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
