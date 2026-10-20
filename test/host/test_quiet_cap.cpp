// #377: the quiet-hour reboot vs the deferred-reboot cap (include/RebootCap.h).
//
// Drives the REAL quiet::Step (include/QuietHourPolicy.h) across consecutive nights and feeds each
// proposal to the SHIPPED rebootcap::Allows -- the same function DeferRebootWithCause calls. Before
// #377 this test carried a verbatim transcription of the cap line, because OtaUpdater.h pulls in
// Arduino and LGFX; moving the comparison into a pure header is what lets the test call the real one.
//
// Predictions (docs/v18-reboot-cap.md, frozen before code): all six pass. With the sabotage (a
// scheduled deferral held to the full 24 h cap -- today's rule), exactly A and B fail.
#include <cstdio>
#include <cstdint>
#include "../../include/QuietHourPolicy.h"
#include "../../include/RebootCap.h"

static int failures = 0;
static void check(bool ok, const char* what) { std::printf("  %s  %s\n", ok ? "ok  " : "FAIL", what); if (!ok) ++failures; }

constexpr uint32_t CAP_S = 24UL * 60UL * 60UL;   // src/OtaUpdater.h REBOOT_MIN_INTERVAL_S (shipping)

// One boot that runs from `bootEpoch` until the first proposal (or `untilEpoch`), stepping once a
// second as loop() would, with the decision delayed by `lagS` seconds on the night it fires.
// Returns the epoch the cap stamped (the reboot), or 0 if no reboot happened.
static uint32_t RunBoot(uint32_t bootEpoch, uint32_t untilEpoch, long offsetSec, uint32_t& lastReb, int lagS, bool* refused)
{
    quiet::State qs;                       // RAM: fresh every boot
    *refused = false;
    for (uint32_t t = bootEpoch; t < untilEpoch; ++t) {
        const quiet::Decision d = quiet::Step(qs, t, offsetSec, (t - bootEpoch) * 1000UL);
        if (d == quiet::Decision::None) continue;
        const uint32_t now = t + (uint32_t)lagS;   // a loop pass that ran late by lagS seconds
        if (!rebootcap::Allows(now, lastReb, /*scheduled=*/true, CAP_S)) { *refused = true; return 0; }
        lastReb = now;
        return now;
    }
    return 0;
}

int main()
{
    std::printf("quiet-hour reboot vs the reboot cap (include/RebootCap.h)\n");
    const long PST = -28800, PDT = -25200;
    const uint32_t D1 = 1791457200UL;   // 2026-10-08T11:00:00Z = 03:00 at -8 h

    // ---- A: one second of jitter. Night 1's loop pass ran 1 s late; night 2's did not. ----
    {
        uint32_t last = 0; bool ref = false;
        const uint32_t r1 = RunBoot(D1 - 3600, D1 + 3600, PST, last, /*lag*/1, &ref);
        const uint32_t r2 = RunBoot(r1 + 15, r1 + 86400 + 3600, PST, last, /*lag*/0, &ref);
        std::printf("  A: night-1 stamp %lu, night-2 %s (gap %ld s)\n", (unsigned long)r1,
                    r2 ? "rebooted" : (ref ? "REFUSED" : "no proposal"), r2 ? (long)(r2 - r1) : 86399L);
        check(r1 != 0 && r2 != 0, "A: a 1 s late loop pass last night does not cost tonight's reboot");
    }
    // ---- B: the offset moves an hour EARLIER between two nights (DST spring-forward, #379). ----
    {
        uint32_t last = 0; bool ref = false;
        const uint32_t r1 = RunBoot(D1 - 3600, D1 + 3600, PST, last, 0, &ref);
        const uint32_t r2 = RunBoot(r1 + 15, r1 + 86400 + 3600, PDT, last, 0, &ref);   // local 03:00 is now 23 h later
        std::printf("  B: night-2 %s (gap %ld s)\n", r2 ? "rebooted" : (ref ? "REFUSED" : "no proposal"), r2 ? (long)(r2 - r1) : 82800L);
        check(r1 != 0 && r2 != 0, "B: a one-hour offset change does not cost the next night's reboot");
    }
    // ---- C (CONTROL): what the cap is FOR still holds. A reboot one hour before the quiet hour leaves
    // the quiet hour refused -- never two reboots within 20 h. ----
    {
        uint32_t last = D1 - 3600;   // something rebooted the board at 02:00 local
        bool ref = false;
        const uint32_t r = RunBoot(D1 - 3600 + 15, D1 + 3600, PST, last, 0, &ref);
        check(r == 0 && ref, "CONTROL C: a quiet hour one hour after another reboot is still refused");
    }
    // ---- D (CONTROL): the steady state is unchanged -- 14 nights, no jitter, 14 reboots. ----
    {
        uint32_t last = 0; bool ref = false; int n = 0; uint32_t boot = D1 - 3600;
        for (int night = 0; night < 14; ++night) {
            const uint32_t r = RunBoot(boot, boot + 2 * 86400, PST, last, 0, &ref);
            if (r) { ++n; boot = r + 15; } else break;
        }
        std::printf("  D: %d reboots in 14 nights\n", n);
        check(n == 14, "CONTROL D: no jitter, 14 nights -> 14 reboots");
    }
    // ---- E (CONTROL): an UNSCHEDULED deferral (net wedge, fleet floor) keeps the full cap. ----
    {
        const uint32_t last = D1;
        check(!rebootcap::Allows(last + 82800, last, /*scheduled=*/false, CAP_S),
              "CONTROL E: an unscheduled deferral 23 h after the last reboot is refused");
        check(rebootcap::Allows(last + 86400, last, /*scheduled=*/false, CAP_S),
              "CONTROL E: an unscheduled deferral 24 h after the last reboot is allowed");
    }
    // ---- F (CONTROL): the bench override (BLIPSCOPE_REBOOT_MIN_S) still governs a scheduled deferral
    // when it is shorter than 20 h, so the gate test exercises this exact comparison. ----
    {
        const uint32_t last = D1, cap = 60;
        check(rebootcap::Allows(last + 61, last, /*scheduled=*/true, cap),
              "CONTROL F: with a 60 s override, a scheduled deferral 61 s later is allowed");
        check(!rebootcap::Allows(last + 59, last, /*scheduled=*/true, cap),
              "CONTROL F: with a 60 s override, a scheduled deferral 59 s later is refused");
    }
    std::printf(failures ? "FAILED (%d)\n" : "ok\n", failures);
    return failures ? 1 : 0;
}
