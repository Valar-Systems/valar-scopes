// Host test for the bench override of the quiet hour.
//
// Compiled with -DBLIPSCOPE_QUIET_HOUR=20 by test/host/run.sh, so it tests the
// OVERRIDE MECHANISM rather than the schedule -- which the default-built
// test_quiet_hour_policy.cpp already covers in full.
//
// WHY THIS IS A SEPARATE BINARY. The value is a compile-time constant, so the
// only way to test both settings is to build twice. A runtime knob would have
// been testable in one file and is the wrong shape: a shipping build must not
// contain a path that can move the reboot to 20:00, however it is guarded.
//
// The risk being managed is not that the override fails to work. It is that a
// bench build ESCAPES -- reaching a customer, or (far likelier) a capture from
// one being read weeks later and its 20:00 reboot taken for a shipping defect.
// So the assertions below are as much about the ANNOUNCEMENT as the behaviour.
#include <cstdio>
#include "../../include/QuietHourPolicy.h"

static int failures = 0;
static void check(bool ok, const char* what)
{
    if (!ok) { std::printf("  FAIL: %s\n", what); ++failures; }
}

using namespace quiet;

static constexpr uint32_t T0 = 1788912000UL; // 2026-09-08T00:00:00Z

int main()
{
    std::printf("quiet-hour override\n");

    check(QUIET_HOUR == 20, "the build flag sets the hour (20, not the default 3)");
    check(QUIET_HOUR_IS_OVERRIDE, "and the build ANNOUNCES itself as an override");

    // The schedule must actually use it -- a flag that changes a constant nothing
    // reads is the failure this file exists to exclude.
    {
        State s;
        int fired = 0, firedHour = -1;
        for (uint32_t m = 0; m < 24 * 60; ++m) {
            const uint32_t epoch = T0 + m * 60;
            if (Step(s, epoch, 0, m * 60000u) != Decision::None) {
                ++fired;
                firedHour = LocalHour(epoch, 0);
            }
        }
        check(fired == 1, "fires exactly once in the day");
        check(firedHour == 20, "...at the OVERRIDDEN hour, not at 03:00");
    }

    // CONTROL: it must not ALSO fire at the default hour. A naive
    // implementation that kept 3 alongside the override would pass the count
    // above only by luck of ordering.
    {
        State s;
        bool firedAtThree = false;
        for (uint32_t m = 0; m < 3 * 24 * 60; ++m) {
            const uint32_t epoch = T0 + m * 60;
            if (Step(s, epoch, 0, m * 60000u) != Decision::None
                && LocalHour(epoch, 0) == 3)
                firedAtThree = true;
        }
        check(!firedAtThree, "CONTROL: it never fires at 03:00 as well, across three days");
    }

    if (failures == 0) std::printf("PASS\n");
    else               std::printf("%d FAILURE(S)\n", failures);
    return failures == 0 ? 0 : 1;
}
