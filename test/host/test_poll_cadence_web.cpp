// The poll cadence and the weekly reboot in the WEB build: BLIPSCOPE_WEB_RENDERER defined (include/PollCadence.h;
// docs/web-kiosk-airfield.md). Rows KI2 and KI3 (web), one line each, for test/host/sabotage_web_kiosk.py.
//
//   test_poll_cadence_web.exe      exit 0 all pass, 1 a check failed
#define BLIPSCOPE_WEB_RENDERER
#include <cstdio>

#include "PollCadence.h"

static int failures = 0;
static void row(bool ok, const char* id, const char* what)
{
    if (!ok) ++failures;
    std::printf("  %s  %s %s\n", ok ? "ok  " : "FAIL", id, what);
}

static unsigned long Today(unsigned long a, unsigned long i, unsigned long n, unsigned long after, unsigned long since,
                           bool clockValid, bool night)
{
    if (since <= after) return a;
    if (clockValid && night) return n;
    return i;
}

int main()
{
    const unsigned long A = 5000, I = 15000, N = 60000, AFTER = 600000;
    const unsigned long SINCE[] = {0, AFTER - 1, AFTER, AFTER + 1, 10UL * AFTER};
    bool ki2 = pollcadence::KioskOn("true") && !pollcadence::KioskOn("") && !pollcadence::KioskOn("false") &&
               !pollcadence::KioskOn("1") && !pollcadence::KioskOn("TRUE") && !pollcadence::KioskOn(nullptr);
    for (unsigned long since : SINCE)
        for (int clock = 0; clock < 2; ++clock)
            for (int night = 0; night < 2; ++night) {
                auto nf = [&] { return night != 0; };
                ki2 &= pollcadence::IntervalMs(A, I, N, AFTER, since, clock, true, nf) == A;
                ki2 &= pollcadence::IntervalMs(A, I, N, AFTER, since, clock, false, nf) == Today(A, I, N, AFTER, since, clock, night);
            }
    row(ki2, "KI2", "kiosk on: the active rate in every case; off: today's rule; KioskOn is true for exactly \"true\"");

    const unsigned long MS = 7UL * 24UL * 3600UL * 1000UL, TEN = 10UL * 60UL * 1000UL;
    const unsigned long NOW[] = {MS - 1, MS, MS + 1, 10UL * MS};
    const unsigned long IDLE[] = {0, TEN - 1, TEN, TEN + 1, 100UL * TEN};
    bool ki3 = true;
    for (unsigned long now : NOW)
        for (unsigned long since : IDLE)
            for (int detail = 0; detail < 2; ++detail)
                for (int clock = 0; clock < 2; ++clock)
                    for (int night = 0; night < 2; ++night)
                        ki3 &= !pollcadence::PreventiveRebootDue(now, MS, detail, clock, since, [&] { return night != 0; });
    row(ki3, "KI3", "the web build's weekly reboot is never due");

    std::printf("%s\n", failures ? "POLL CADENCE WEB TESTS FAILED" : "all poll cadence web checks passed");
    return failures ? 1 : 0;
}
