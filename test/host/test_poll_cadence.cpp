// The poll cadence and the weekly reboot in a DEVICE build: BLIPSCOPE_WEB_RENDERER undefined (include/PollCadence.h;
// docs/web-kiosk-airfield.md). Rows KI1, KI3 (device) and KI5, one line each, so
// test/host/sabotage_web_kiosk.py can require exactly the sabotaged rows to go red.
//
//   test_poll_cadence.exe      exit 0 all pass, 1 a check failed
#include <cstdio>

#include "PollCadence.h"

static int failures = 0;
static void row(bool ok, const char* id, const char* what)
{
    if (!ok) ++failures;
    std::printf("  %s  %s %s\n", ok ? "ok  " : "FAIL", id, what);
}

// The rule as AircraftManager::CurrentPollIntervalMs wrote it before PollCadence.h, transcribed.
static unsigned long Today(unsigned long a, unsigned long i, unsigned long n, unsigned long after, unsigned long since,
                           bool clockValid, bool night)
{
    if (since <= after) return a;
    if (clockValid && night) return n;
    return i;
}

// The weekly reboot's condition as AircraftManager::Update wrote it before PollCadence.h, transcribed.
static bool TodayReboot(unsigned long now, unsigned long ms, bool inDetail, bool clockValid, unsigned long since, bool night)
{
    return now >= ms && !inDetail && clockValid && since >= 10UL * 60UL * 1000UL && night;
}

int main()
{
    const unsigned long A = 5000, I = 15000, N = 60000, AFTER = 600000;
    const unsigned long SINCE[] = {0, AFTER - 1, AFTER, AFTER + 1, 10UL * AFTER};
    bool ki1 = !pollcadence::KioskOn("true");
    for (unsigned long since : SINCE)
        for (int clock = 0; clock < 2; ++clock)
            for (int night = 0; night < 2; ++night)
                for (int kiosk = 0; kiosk < 2; ++kiosk)
                    ki1 &= pollcadence::IntervalMs(A, I, N, AFTER, since, clock, kiosk, [&] { return night != 0; }) ==
                           Today(A, I, N, AFTER, since, clock, night);
    row(ki1, "KI1", "the device rule is today's over the grid, kiosk ignored, and KioskOn(\"true\") is false");

    const unsigned long MS = 7UL * 24UL * 3600UL * 1000UL, TEN = 10UL * 60UL * 1000UL;
    const unsigned long NOW[] = {MS - 1, MS, MS + 1};
    const unsigned long IDLE[] = {0, TEN - 1, TEN, TEN + 1};
    bool ki3 = true;
    for (unsigned long now : NOW)
        for (unsigned long since : IDLE)
            for (int detail = 0; detail < 2; ++detail)
                for (int clock = 0; clock < 2; ++clock)
                    for (int night = 0; night < 2; ++night)
                        ki3 &= pollcadence::PreventiveRebootDue(now, MS, detail, clock, since, [&] { return night != 0; }) ==
                               TodayReboot(now, MS, detail, clock, since, night);
    row(ki3, "KI3", "the device's weekly reboot is today's condition over the grid");

    int calls = 0;
    auto counted = [&] { ++calls; return true; };
    bool ki5 = true;
    calls = 0; pollcadence::IntervalMs(A, I, N, AFTER, 0, true, false, counted); ki5 &= calls == 0;      // touched
    calls = 0; pollcadence::IntervalMs(A, I, N, AFTER, 10 * AFTER, false, false, counted); ki5 &= calls == 0;  // no clock
    calls = 0; pollcadence::IntervalMs(A, I, N, AFTER, 10 * AFTER, true, false, counted); ki5 &= calls == 1;
    calls = 0; pollcadence::PreventiveRebootDue(MS - 1, MS, false, true, 10 * TEN, counted); ki5 &= calls == 0;  // uptime
    calls = 0; pollcadence::PreventiveRebootDue(MS, MS, true, true, 10 * TEN, counted); ki5 &= calls == 0;      // a card
    calls = 0; pollcadence::PreventiveRebootDue(MS, MS, false, false, 10 * TEN, counted); ki5 &= calls == 0;     // no clock
    calls = 0; pollcadence::PreventiveRebootDue(MS, MS, false, true, TEN - 1, counted); ki5 &= calls == 0;      // touched
    calls = 0; pollcadence::PreventiveRebootDue(MS, MS, false, true, 10 * TEN, counted); ki5 &= calls == 1;
    row(ki5, "KI5", "the night test is called only where today's code calls it");

    std::printf("%s\n", failures ? "POLL CADENCE TESTS FAILED" : "all poll cadence checks passed");
    return failures ? 1 : 0;
}
