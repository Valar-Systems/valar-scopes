#pragma once

#include <Arduino.h>

#include "LGFX.h"

// CST816-family touch supervisor: detects the controller dropping off the I2C bus
// (the C3 "touch wedge") and recovers it through an escalating ladder, counting
// everything so bench/soak runs yield numbers instead of anecdotes.
//
// v2 design, tuned from the 2026-07-09 bisection run1 (10 wedges / 10.7 h storm,
// 10/10 recovered; onset uncorrelated with gesture or render load, memoryless
// arrivals; 1312 probes + 22 wake resets over the run were harmless):
//
//   DETECTION -- two paths, both probing only when a NACK is meaningful:
//   - Release-edge probe: right after a successful touch read (chip provably
//     awake), a NACK cannot be auto-sleep. Catches mid-touch death instantly.
//   - Standing probe (default ON, every PROBE_INTERVAL_MS = 10 s): the loop's
//     getTouch cannot see a wedge while nobody touches (a dead chip and an idle
//     chip both read "no touch"), and run1's memoryless arrivals mean wedges DO
//     happen while idle -- so an active check is mandatory, not bench-only. A
//     silent chip gets ONE wake reset + boot wait; only a no-show after that is
//     a wedge. Run1 evidence: wake resets ran ~1/h under storm with zero harm.
//     If a future soak shows an auto-sleeping chip turning this into wake churn
//     (watch the wakes counter), rate-limit idle wakes here -- do not widen the
//     probe interval, detection latency is the gate's budget.
//
//   RECOVERY ladder (cool-off is the active ingredient -- run1's immediate
//   resets failed, later ones took):
//     +5 s   soft: driver init() (LovyanGFX pulses pin_rst + rewrites setup)
//     +15 s  soft
//     +40 s  hard: I2C bus unstick (9 SCL clocks + STOP), long TP_RST hold
//            (250 ms low, 650 ms boot), then driver init()
//     +120 s hard, repeating
//   Rung spacing keeps a rung-3 recovery inside the 90 s outage bound; wedges
//   that outlive 90 s set RebootRecommended() and the app reboots at the next
//   idle window (the last rung a stuck chip historically responded to).
//
// The two failure modes that sank the old (June) watchdog stay designed out:
// auto-sleep NACKs never count as wedges without a failed wake reset first, and
// no reset is ever issued before the previous one's ~450 ms boot wait elapsed.
//
// All calls run on the loop task from inside HandleTouch's poll; any I2C or RST
// work happens only while the touch bus window is held (mayProbe), serialized
// against TLS exactly like the poll itself.
//
// Compiled for CST816-family variants (C3 1.28", S3 2.1"'s CST820); ACTIVE only
// where variant::TOUCH_WATCHDOG is set (the single-core C3, where the wedge lives).
namespace TouchWatchdog {

    struct Stats {
        uint32_t wedges = 0;          // confirmed dead-on-bus events
        uint32_t recoverAttempts = 0; // ladder re-inits issued while wedged
        uint32_t recoveries = 0;      // ladder re-inits after which the chip ACKed
        uint32_t softRecoveries = 0;  // ...on a soft rung (driver init only)
        uint32_t hardRecoveries = 0;  // ...on a hard rung (bus unstick + long RST)
        uint32_t wakes = 0;           // standing-probe wake resets that found a healthy chip
        uint32_t probesOk = 0;        // raw chip-id probe outcomes
        uint32_t probesFailed = 0;
        uint32_t rebootsRecommended = 0; // episodes that outlived the 90 s bound
        // Outage bookkeeping, measured from wedge DECLARATION to recovery (add
        // up to one probe interval of detection latency for the user-perceived
        // figure). Buckets feed the soak gate ("all recoveries <= 90 s").
        uint32_t lastOutageMs = 0;
        uint32_t maxOutageMs = 0;
        uint32_t outageLe30s = 0;
        uint32_t outageLe90s = 0;
        uint32_t outageGt90s = 0;
    };

#if defined(BLIPSCOPE_TOUCH_CST816)
    // Call once per touch poll, right after tft.getTouch(). sawTouch = the poll
    // returned a point (proof the controller answered). mayProbe = the touch I2C
    // window is held right now, so the watchdog may issue its own transfers.
    void OnPoll(LGFX& tft, bool sawTouch, bool mayProbe);

    // Standing-probe cadence override (0 disables it -- bench experiments only;
    // production relies on it for idle-onset detection). Default 10 s.
    void SetProbeIntervalMs(unsigned long intervalMs);

    // True while a wedge has outlived the 90 s outage bound and the ladder still
    // hasn't recovered it: the app should reboot at its next idle window. Stays
    // true until recovery or reboot.
    bool RebootRecommended();

    const Stats& GetStats();
#else
    inline void OnPoll(LGFX&, bool, bool) {}
    inline void SetProbeIntervalMs(unsigned long) {}
    inline bool RebootRecommended() { return false; }
    inline const Stats& GetStats() { static Stats s; return s; }
#endif

}
