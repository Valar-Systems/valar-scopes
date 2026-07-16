#include "TouchWatchdog.h"

#if defined(BLIPSCOPE_TOUCH_CST816)

#include "variants/Variant.h"

namespace {

    enum class State : uint8_t {
        Healthy,     // nothing suspicious; watch release edges + the standing probe
        Suspect,     // a release-edge probe NACKed on a provably-awake chip; confirming
        WakeBoot,    // standing probe found a silent chip; ONE wake reset issued, waiting to judge
        SoftBoot,    // a soft ladder re-init was issued; waiting to judge
        HardRstLow,  // hard rung: TP_RST held low
        HardRstHigh, // hard rung: RST released, chip boot wait before driver init()
        HardBoot,    // hard rung: driver init() issued, waiting to judge
        Backoff,     // last rung failed; cooling off until the next one
    };

    State state = State::Healthy;
    TouchWatchdog::Stats stats;

    bool prevSaw = false;
    bool releaseProbePending = false; // a release edge happened while the bus wasn't held
    unsigned long lastSawMs = 0;      // last poll that returned a touch point
    uint8_t suspectFails = 0;
    unsigned long nextActionMs = 0;   // per-state deadline (confirm probe / judge / next rung)
    uint8_t rung = 0;                 // ladder position for the CURRENT wedge episode
    unsigned long probeIntervalMs = 10000; // standing probe; 0 = off (bench experiments only)
    unsigned long probeNextMs = 0;
    unsigned long wedgeSinceMs = 0;   // declaration time of the current episode (0 = none)
    bool rebootWanted = false;

    // Chip provably awake this long after a successful touch read. Kept short: the
    // auto-sleep timeout is nominally ~5 s but this chip has been observed sleeping
    // regardless of register pokes, so only the window right after seen traffic counts.
    constexpr unsigned long AWAKE_WINDOW_MS = 1200;
    constexpr uint8_t SUSPECT_PROBES = 3;            // consecutive NACKs to declare a wedge
    constexpr unsigned long SUSPECT_SPACING_MS = 150;
    // CST816 needs ~300 ms+ after any reset before it ACKs; judging earlier is what
    // made the old watchdog thrash. Never re-enter a reset before this elapses.
    constexpr unsigned long BOOT_WAIT_MS = 450;
    // Ladder rungs (delay from the previous failure; run1: cool-off, not the reset
    // itself, is what recovers the chip). Rungs 0-1 soft, 2+ hard. Rung 2 at +40 s
    // keeps a rung-2 recovery inside the soak's 90 s outage bound.
    constexpr unsigned long RUNG_DELAY_MS[] = { 5000, 15000, 40000, 120000 }; // last repeats
    constexpr uint8_t RUNG_COUNT = sizeof(RUNG_DELAY_MS) / sizeof(RUNG_DELAY_MS[0]);
    constexpr uint8_t FIRST_HARD_RUNG = 2;
    constexpr unsigned long HARD_RST_LOW_MS = 250;  // long TP_RST hold (driver init uses ~10 ms)
    constexpr unsigned long HARD_RST_BOOT_MS = 650; // post-release settle before driver init()
    constexpr unsigned long REBOOT_AFTER_MS = 90000; // the soak gate's outage bound

#ifdef CST816_DISABLE_AUTOSLEEP
    // Wedge-program step 1 (2026-07-10): hold the CST816 out of its auto-sleep engine.
    // Both wedge classes implicate the sleep engine, and the device is USB-powered so
    // controller sleep saves nothing worth having. June's ONE-SHOT 0xFE write "didn't
    // stick", so this maintains it instead: on every successful probe (the chip is
    // provably awake and post-boot-wait) read DisAutoSleep (0xFE) and re-write 0x01
    // whenever the chip has dropped it (a silent chip-internal reset clears it). The
    // re-application count doubles as the stickiness meter for the overnight A/B.
    unsigned long noSleepApplied = 0;
    void MaintainNoSleep()
    {
        const auto cur = lgfx::i2c::readRegister8(BLIPSCOPE_TOUCH_I2C_PORT,
                                                  BLIPSCOPE_TOUCH_I2C_ADDR,
                                                  0xFE, BLIPSCOPE_TOUCH_FREQ);
        if (cur.has_value() && cur.value() != 0)
            return; // still holding
        const bool wrote = lgfx::i2c::writeRegister8(BLIPSCOPE_TOUCH_I2C_PORT,
                                                     BLIPSCOPE_TOUCH_I2C_ADDR,
                                                     0xFE, 0x01, 0,
                                                     BLIPSCOPE_TOUCH_FREQ).has_value();
        const auto back = lgfx::i2c::readRegister8(BLIPSCOPE_TOUCH_I2C_PORT,
                                                   BLIPSCOPE_TOUCH_I2C_ADDR,
                                                   0xFE, BLIPSCOPE_TOUCH_FREQ);
        noSleepApplied++;
        // Log the first few and then every 50th: if 0xFE reads back 0 despite a clean
        // write (write-only register or a chip that ignores it), this fires every
        // probe and the throttle keeps it to ~1 line / 8 min at the 10 s cadence.
        if (noSleepApplied <= 5 || noSleepApplied % 50 == 0)
            Serial.printf("[touch-wd] no-autosleep applied #%lu (write %s, readback=%d)\n",
                          noSleepApplied, wrote ? "ok" : "NACK",
                          back.has_value() ? (int)back.value() : -1);
    }
#endif // CST816_DISABLE_AUTOSLEEP

#ifdef TOUCHWD_PROPHYLACTIC
    // Wedge-program step 2 (2026-07-10): preventive controller re-init every 10 min
    // at idle. If the wedge hazard is state accumulation inside the CST816, a standing
    // reset collapses incidence; if it's memoryless (run1's cv=0.83 says so), it won't
    // move. Either result is diagnostic. Step 1 (disable auto-sleep) was DOA: this
    // CST816T revision NACKs register 0xFE on read AND write, awake or 450 ms
    // post-reset, so auto-sleep cannot be turned off in software.
    constexpr unsigned long PROPHYLACTIC_EVERY_MS = 10UL * 60UL * 1000UL;
    constexpr unsigned long PROPHYLACTIC_IDLE_MS  = 60000; // defer around live fingers
    unsigned long lastProphylacticMs = 0;
    unsigned long prophylacticResets = 0;
#endif // TOUCHWD_PROPHYLACTIC

    // One-byte chip-id read (0xA7; the C3 kit's CST816T answers 0xB6). ACK/NACK is
    // the whole signal. Same lgfx::i2c owner as the touch driver, per convention.
    bool Probe()
    {
        const auto r = lgfx::i2c::readRegister8(BLIPSCOPE_TOUCH_I2C_PORT,
                                                BLIPSCOPE_TOUCH_I2C_ADDR,
                                                0xA7, BLIPSCOPE_TOUCH_FREQ);
        if (r.has_value()) {
            stats.probesOk++;
#ifdef CST816_DISABLE_AUTOSLEEP
            MaintainNoSleep(); // chip provably awake right now: safe write window
#endif
            return true;
        }
        stats.probesFailed++;
        return false;
    }

    void DriverInit(LGFX& tft)
    {
        auto* touch = tft.touch();
        if (touch != nullptr)
            touch->init(); // pulses pin_rst + rewrites the chip setup
    }

#ifdef TOUCHWD_BUS_UNSTICK
    // Classic I2C bus recovery: if the chip crashed mid-transfer holding SDA low,
    // no reset of OUR peripheral helps until the slave releases the line -- clock
    // it out. Tear down the I2C driver, pulse SCL 9 times, generate a STOP, and
    // re-init the driver.
    //
    // DEFAULT OFF (2026-07-09 finding): on the C3, this lgfx::i2c::release/init
    // cycle killed the concurrent fetch task's TLS client -- the cloud soak's
    // fetches went permanently silent at the exact first hard rung, and ran
    // perfectly all night with the watchdog disabled (A/B on hardware). Mechanism
    // not yet isolated (suspect: driver/interrupt re-allocation colliding with the
    // network stack on the single core). Re-enable only for a bench session with
    // a live chip and no networking, until the interaction is understood.
    void BusUnstick()
    {
        lgfx::i2c::release(BLIPSCOPE_TOUCH_I2C_PORT);
        pinMode(BLIPSCOPE_TOUCH_PIN_SDA, INPUT_PULLUP);
        pinMode(BLIPSCOPE_TOUCH_PIN_SCL, OUTPUT_OPEN_DRAIN);
        for (int i = 0; i < 9; ++i) {
            digitalWrite(BLIPSCOPE_TOUCH_PIN_SCL, LOW);
            delayMicroseconds(5);
            digitalWrite(BLIPSCOPE_TOUCH_PIN_SCL, HIGH);
            delayMicroseconds(5);
        }
        // STOP condition: SDA low->high while SCL high
        pinMode(BLIPSCOPE_TOUCH_PIN_SDA, OUTPUT_OPEN_DRAIN);
        digitalWrite(BLIPSCOPE_TOUCH_PIN_SDA, LOW);
        delayMicroseconds(5);
        digitalWrite(BLIPSCOPE_TOUCH_PIN_SDA, HIGH);
        delayMicroseconds(5);
        lgfx::i2c::setPins(BLIPSCOPE_TOUCH_I2C_PORT, BLIPSCOPE_TOUCH_PIN_SDA, BLIPSCOPE_TOUCH_PIN_SCL);
        lgfx::i2c::init(BLIPSCOPE_TOUCH_I2C_PORT);
    }
#endif // TOUCHWD_BUS_UNSTICK

    // Launch the CURRENT rung's reset (assumes its cool-off already elapsed).
    void StartRung(LGFX& tft, unsigned long now)
    {
        stats.recoverAttempts++;
        if (rung >= FIRST_HARD_RUNG) {
#ifdef TOUCHWD_BUS_UNSTICK
            BusUnstick(); // see the finding above: off by default, it kills the fetch TLS client
#endif
            pinMode(BLIPSCOPE_TOUCH_PIN_RST, OUTPUT);
            digitalWrite(BLIPSCOPE_TOUCH_PIN_RST, LOW);
            state = State::HardRstLow;
            nextActionMs = now + HARD_RST_LOW_MS;
        } else {
            DriverInit(tft);
            state = State::SoftBoot;
            nextActionMs = now + BOOT_WAIT_MS;
        }
    }

    void DeclareWedge(unsigned long now)
    {
        stats.wedges++;
        wedgeSinceMs = now;
        rung = 0;
        Serial.printf("[touch-wd] WEDGE #%lu: controller dead on the bus; ladder engaged\n",
                      (unsigned long)stats.wedges);
        state = State::Backoff;
        nextActionMs = now + RUNG_DELAY_MS[0];
    }

    void Recovered(unsigned long now, bool hardRung)
    {
        stats.recoveries++;
        if (hardRung) stats.hardRecoveries++;
        else          stats.softRecoveries++;
        const uint32_t outage = (uint32_t)(now - wedgeSinceMs);
        stats.lastOutageMs = outage;
        if (outage > stats.maxOutageMs) stats.maxOutageMs = outage;
        if      (outage <= 30000) stats.outageLe30s++;
        else if (outage <= 90000) stats.outageLe90s++;
        else                      stats.outageGt90s++;
        Serial.printf("[touch-wd] RECOVERED on %s rung after %lums (%lu/%lu re-inits have worked; soft=%lu hard=%lu)\n",
                      hardRung ? "HARD" : "soft", (unsigned long)outage,
                      (unsigned long)stats.recoveries, (unsigned long)stats.recoverAttempts,
                      (unsigned long)stats.softRecoveries, (unsigned long)stats.hardRecoveries);
        wedgeSinceMs = 0;
        rebootWanted = false;
        rung = 0;
        state = State::Healthy;
    }

    void RungFailed(unsigned long now)
    {
        if (rung + 1 < RUNG_COUNT)
            rung++;
        Serial.printf("[touch-wd] still dead; next re-init (%s) in %lus\n",
                      rung >= FIRST_HARD_RUNG ? "hard" : "soft", RUNG_DELAY_MS[rung] / 1000);
        state = State::Backoff;
        nextActionMs = now + RUNG_DELAY_MS[rung];
    }

} // namespace

void TouchWatchdog::OnPoll(LGFX& tft, bool sawTouch, bool mayProbe)
{
#ifdef TOUCHWD_DISABLED
    // Bench escape hatch (-DTOUCHWD_DISABLED): run a board whose touch hardware is
    // known-dead without the supervisor declaring wedges and the reboot escalation
    // cycling the device -- e.g. a cloud/heap soak while an FPC repair is pending.
    (void)tft; (void)sawTouch; (void)mayProbe;
    return;
#endif
    if constexpr (!variant::TOUCH_WATCHDOG)
        return; // capability-gated: today only the C3's CST816T is marginal

    const unsigned long now = millis();

    if (sawTouch) {
        lastSawMs = now;
        if (state == State::Suspect) {
            // touch reads are landing again: whatever NACKed was transient
            state = State::Healthy;
            suspectFails = 0;
        }
    }
    const bool releaseEdge = prevSaw && !sawTouch;
    prevSaw = sawTouch;

    // Escalate to the app: this episode has outlived the outage bound.
    if (wedgeSinceMs != 0 && !rebootWanted && now - wedgeSinceMs > REBOOT_AFTER_MS) {
        rebootWanted = true;
        stats.rebootsRecommended++;
        Serial.printf("[touch-wd] wedge #%lu has outlived %lus; recommending reboot at next idle\n",
                      (unsigned long)stats.wedges, REBOOT_AFTER_MS / 1000);
    }

    switch (state) {
    case State::Healthy:
        // A release edge is the one moment the chip is PROVABLY awake (its touch
        // reads just succeeded), so a NACK now cannot be auto-sleep -- the false
        // trigger that sank the old probe watchdog. One probe per release.
        if (releaseEdge)
            releaseProbePending = true;
        if (releaseProbePending && mayProbe) {
            releaseProbePending = false;
            if (now - lastSawMs <= AWAKE_WINDOW_MS && !Probe()) {
                suspectFails = 1;
                nextActionMs = now + SUSPECT_SPACING_MS;
                state = State::Suspect;
                Serial.printf("[touch-wd] post-release probe NACK (1/%u); confirming\n", SUSPECT_PROBES);
            }
        } else if (probeIntervalMs != 0 && mayProbe && now >= probeNextMs) {
            probeNextMs = now + probeIntervalMs;
            if (!Probe()) {
                // Silent chip: usually just auto-sleep. ONE wake reset, judge after
                // the boot wait; only a no-show then is a wedge.
                DriverInit(tft);
                state = State::WakeBoot;
                nextActionMs = now + BOOT_WAIT_MS;
            }
        }
#ifdef TOUCHWD_PROPHYLACTIC
        else if (mayProbe && now - lastProphylacticMs >= PROPHYLACTIC_EVERY_MS
                 && (lastSawMs == 0 || now - lastSawMs >= PROPHYLACTIC_IDLE_MS)) {
            // Preventive reset, judged exactly like a wake: an ACK 450 ms later is a
            // clean cycle (counted under wakes), a no-show is a real wedge caught early.
            lastProphylacticMs = now;
            prophylacticResets++;
            Serial.printf("[touch-wd] prophylactic re-init #%lu (idle)\n", prophylacticResets);
            DriverInit(tft);
            state = State::WakeBoot;
            nextActionMs = now + BOOT_WAIT_MS;
        }
#endif
        break;

    case State::Suspect:
        if (mayProbe && now >= nextActionMs) {
            if (Probe()) {
                Serial.printf("[touch-wd] probe recovered after %u NACK(s) -- transient\n", suspectFails);
                suspectFails = 0;
                state = State::Healthy;
            } else if (++suspectFails >= SUSPECT_PROBES) {
                DeclareWedge(now);
            } else {
                nextActionMs = now + SUSPECT_SPACING_MS;
            }
        }
        break;

    case State::WakeBoot:
        if (mayProbe && now >= nextActionMs) {
            if (Probe()) {
                stats.wakes++; // healthy chip, was just silent/asleep
                state = State::Healthy;
            } else {
                // a real reset + full boot wait and still no ACK: the wedge signature
                DeclareWedge(now);
            }
        }
        break;

    case State::SoftBoot:
        if (mayProbe && now >= nextActionMs) {
            if (Probe()) Recovered(now, false);
            else         RungFailed(now);
        }
        break;

    case State::HardRstLow:
        if (now >= nextActionMs) { // RST release needs no bus
            digitalWrite(BLIPSCOPE_TOUCH_PIN_RST, HIGH);
            state = State::HardRstHigh;
            nextActionMs = now + HARD_RST_BOOT_MS;
        }
        break;

    case State::HardRstHigh:
        if (mayProbe && now >= nextActionMs) {
            DriverInit(tft); // reconfigure registers now that the chip (re)booted
            state = State::HardBoot;
            nextActionMs = now + BOOT_WAIT_MS;
        }
        break;

    case State::HardBoot:
        if (mayProbe && now >= nextActionMs) {
            if (Probe()) Recovered(now, true);
            else         RungFailed(now);
        }
        break;

    case State::Backoff:
        if (mayProbe && now >= nextActionMs)
            StartRung(tft, now);
        break;
    }
}

void TouchWatchdog::SetProbeIntervalMs(unsigned long intervalMs)
{
    if constexpr (!variant::TOUCH_WATCHDOG)
        return;
    probeIntervalMs = intervalMs;
    probeNextMs = millis() + 3000; // first check soon after boot (chip is freshly init'd)
}

bool TouchWatchdog::RebootRecommended()
{
    if constexpr (!variant::TOUCH_WATCHDOG)
        return false;
    return rebootWanted;
}

const TouchWatchdog::Stats& TouchWatchdog::GetStats()
{
    return stats;
}

#endif // BLIPSCOPE_TOUCH_CST816
