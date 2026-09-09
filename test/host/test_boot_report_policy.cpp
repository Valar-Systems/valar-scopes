// Host test for the boot-reason delivery rule.
//
// WRITTEN AFTER A LIVE ATTEMPT EXPOSED THE DEFECT, 2026-09-08. The first version
// of this path set `taken = true` before the request was even built, so the
// reason was consumed by whichever check-in happened first -- succeed or fail.
//
// That is worse than it sounds, because the failure is CORRELATED with the
// measurement: the boots most worth reporting are the ones following a network
// problem, and their first check-in is the one most likely to fail. A
// best-effort field here loses precisely the population it exists to measure,
// and reports a healthy-looking fleet while doing it.
//
// The retry then introduces its own hazard, which is why there are two flags and
// why half the cases below are about NOT double-reporting.
#include <cstdio>
#include "../../include/BootReportPolicy.h"

static int failures = 0;
static void check(bool ok, const char* what)
{
    if (!ok) { std::printf("  FAIL: %s\n", what); ++failures; }
}

using namespace bootreport;

int main()
{
    std::printf("boot-report delivery\n");

    // ---- THE CASE THE LIVE ATTEMPT WOULD HAVE HIT -------------------------
    {
        // First check-in fails (traffic still blocked at the router), second
        // succeeds after the unpause. The reason must survive the first and be
        // sent exactly once on the second.
        State s;
        check(Take(s), "the first check-in carries the reason");
        Ack(s, /*delivered=*/false);
        check(s.pending, "a FAILED check-in does not retire the reason");
        check(Take(s), "so the next check-in carries it again");
        Ack(s, /*delivered=*/true);
        check(!s.pending, "a DELIVERED check-in retires it");
        check(!Take(s), "and it is never sent again");
    }

    // ---- and it must not double-report ------------------------------------
    {
        // A second request built while the first is still in flight must NOT
        // also carry it -- that turns a dropped sample into a double-counted
        // one, which is worse than the bug being fixed.
        State s;
        check(Take(s), "first request takes it");
        check(!Take(s), "a second request built while in flight does NOT");
        check(!Take(s), "...nor a third");
        Ack(s, true);
        check(!Take(s), "and none afterwards");
    }
    {
        // The same, across a failure: the retry is allowed, a concurrent
        // duplicate is not.
        State s;
        Take(s);
        Ack(s, false);
        check(Take(s), "retry allowed after a failure");
        check(!Take(s), "but still only one carrier at a time");
    }

    // ---- many failures in a row -------------------------------------------
    {
        // A board on a dead network retries every check-in and reports once,
        // when the network returns. It must not give up, and must not
        // accumulate.
        State s;
        int carried = 0;
        for (int i = 0; i < 50; ++i) {
            if (Take(s)) ++carried;
            Ack(s, false);
        }
        check(carried == 50, "it is offered on every check-in while undelivered");
        check(s.pending, "and is still pending after 50 failures");
        if (Take(s)) { Ack(s, true); }
        check(!s.pending, "then retires on the first success");
        int after = 0;
        for (int i = 0; i < 50; ++i) { if (Take(s)) ++after; Ack(s, true); }
        check(after == 0, "and is offered ZERO times afterwards -- exactly one report per boot");
    }

    // ---- Ack is safe to call unconditionally -------------------------------
    {
        // The caller wires it on every fetch result rather than tracking which
        // request was the carrier, so an Ack with nothing in flight must not
        // retire anything.
        State s;
        Ack(s, true);
        check(s.pending, "an Ack with nothing in flight does NOT retire the report");
        check(Take(s), "the report is still available");
    }
    {
        // ...including after delivery: a later unrelated result must not
        // resurrect it.
        State s;
        Take(s); Ack(s, true);
        Ack(s, true); Ack(s, false);
        check(!s.pending && !s.inFlight, "state is stable under stray Acks");
        check(!Take(s), "and nothing is resurrected");
    }

    // ---- CONTROL: the rule is not vacuous ----------------------------------
    {
        // Every assertion above is about refusing. If Take() simply always
        // returned false they would nearly all pass, so pin that a fresh boot
        // does offer exactly one.
        State s;
        int n = 0;
        for (int i = 0; i < 10; ++i) { if (Take(s)) { ++n; Ack(s, true); } }
        check(n == 1, "CONTROL: a fresh boot delivers exactly one report, not zero");
    }

    if (failures == 0) std::printf("PASS\n");
    else               std::printf("%d FAILURE(S)\n", failures);
    return failures == 0 ? 0 : 1;
}
