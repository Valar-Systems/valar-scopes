#pragma once

#include <Arduino.h>

#include "LGFX.h"

// CST816-family touch supervisor: detects the controller dropping off the I2C bus
// (the C3 "touch wedge") and attempts a measured recovery, counting everything so a
// soak run yields numbers instead of anecdotes.
//
// The June 2026 diagnosis (see CLAUDE.md and PR #8) killed two naive designs this
// module must never regress into:
//   - Probing on a timer false-fires: the CST816T auto-sleeps a few seconds after
//     the last touch and stops ACKing I2C entirely (a 0xFE DisAutoSleep write does
//     NOT stick on this chip), so an idle NACK is normal, not a wedge. We therefore
//     only probe at moments the chip is PROVABLY awake: right after a successful
//     touch read (release edge), or right after we woke it ourselves with a reset.
//   - Hammering RST thrashes: the chip needs ~300 ms after a reset before it will
//     talk; the old watchdog re-pulsed RST faster than that and made touch dead
//     from boot. Recovery here is ONE driver re-init (LovyanGFX owns pin_rst and
//     pulses it inside init()), a BOOT_WAIT judge delay, then a backoff ladder.
//
// All calls run on the loop task from inside HandleTouch's poll, and any I2C the
// watchdog issues happens only when the caller says the touch bus window is held
// (mayProbe) -- the same serialization against TLS the poll itself uses.
//
// Compiled for CST816-family variants (C3 1.28", S3 2.1"'s CST820); ACTIVE only
// where variant::TOUCH_WATCHDOG is set (the single-core C3, where the wedge lives).
namespace TouchWatchdog {

    struct Stats {
        uint32_t wedges = 0;          // confirmed dead-on-bus events
        uint32_t recoverAttempts = 0; // re-inits issued trying to clear a wedge
        uint32_t recoveries = 0;      // re-inits after which the chip ACKed again
        uint32_t benchWakes = 0;      // bench-mode wake resets (unattended health checks)
        uint32_t probesOk = 0;        // raw chip-id probe outcomes
        uint32_t probesFailed = 0;
    };

#if defined(BLIPSCOPE_TOUCH_CST816)
    // Call once per touch poll, right after tft.getTouch(). sawTouch = the poll
    // returned a point (proof the controller answered). mayProbe = the touch I2C
    // window is held right now, so the watchdog may issue its own transfer.
    void OnPoll(LGFX& tft, bool sawTouch, bool mayProbe);

    // Bench/bisection builds: nobody touches an unattended unit, so the passive
    // release-edge detector never runs. This adds an active check every intervalMs:
    // probe; if silent (usually just auto-sleep), wake with one re-init and judge
    // after the boot wait. A chip that stays silent after a proper reset + wait is
    // the wedge signature from the June diagnosis.
    void EnableBenchProbe(unsigned long intervalMs);

    const Stats& GetStats();
#else
    inline void OnPoll(LGFX&, bool, bool) {}
    inline void EnableBenchProbe(unsigned long) {}
    inline const Stats& GetStats() { static Stats s; return s; }
#endif

}
