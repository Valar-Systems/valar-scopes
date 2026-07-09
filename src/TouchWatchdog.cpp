#include "TouchWatchdog.h"

#if defined(BLIPSCOPE_TOUCH_CST816)

#include "variants/Variant.h"

namespace {

    enum class State : uint8_t {
        Healthy,  // nothing suspicious; watch release edges (+ bench timer)
        Suspect,  // a probe NACKed while the chip was provably awake; confirming
        BootWait, // a re-init was issued; the chip gets BOOT_WAIT_MS before judgment
        Backoff,  // re-init + wait didn't bring it back; retry on the ladder
    };
    enum class InitReason : uint8_t { Recovery, BenchWake };

    State state = State::Healthy;
    InitReason initReason = InitReason::Recovery;
    TouchWatchdog::Stats stats;

    bool prevSaw = false;
    bool releaseProbePending = false; // a release edge happened while the bus wasn't held
    unsigned long lastSawMs = 0;      // last poll that returned a touch point
    uint8_t suspectFails = 0;
    unsigned long nextActionMs = 0;   // Suspect: next confirm probe; BootWait: judge time; Backoff: next re-init
    uint8_t backoffStep = 0;
    unsigned long benchIntervalMs = 0; // 0 = bench probing off (production default)
    unsigned long benchNextMs = 0;

    // The chip is treated as provably awake this long after a successful touch
    // read. Kept short: the auto-sleep timeout is nominally ~5 s but this chip has
    // been observed sleeping regardless of register pokes, so we only trust the
    // window right after traffic we saw succeed.
    constexpr unsigned long AWAKE_WINDOW_MS = 1200;
    constexpr uint8_t SUSPECT_PROBES = 3;          // consecutive NACKs to declare a wedge
    constexpr unsigned long SUSPECT_SPACING_MS = 150;
    // CST816 needs ~300 ms+ after a reset before it ACKs; judging earlier is what
    // made the old watchdog thrash. 450 ms adds margin.
    constexpr unsigned long BOOT_WAIT_MS = 450;
    constexpr unsigned long BACKOFF_MS[] = { 5000, 15000, 60000, 120000 }; // last repeats
    constexpr uint8_t BACKOFF_STEPS = sizeof(BACKOFF_MS) / sizeof(BACKOFF_MS[0]);

    // One-byte chip-id read (0xA7; the C3 kit's CST816T answers 0xB6). ACK/NACK is
    // the whole signal -- the value doesn't matter. Same lgfx::i2c owner as the
    // touch driver itself, per the board-code convention.
    bool Probe()
    {
        const auto r = lgfx::i2c::readRegister8(BLIPSCOPE_TOUCH_I2C_PORT,
                                                BLIPSCOPE_TOUCH_I2C_ADDR,
                                                0xA7, BLIPSCOPE_TOUCH_FREQ);
        if (r.has_value()) {
            stats.probesOk++;
            return true;
        }
        stats.probesFailed++;
        return false;
    }

    // ONE driver re-init per event: LovyanGFX owns pin_rst and pulses it inside
    // init(), then rewrites the chip setup. Judgment waits BOOT_WAIT_MS -- never
    // re-enter init before that elapses (the old thrash).
    void Reinit(LGFX& tft, InitReason reason)
    {
        auto* touch = tft.touch();
        if (touch != nullptr)
            touch->init();
        if (reason == InitReason::Recovery)
            stats.recoverAttempts++;
        else
            stats.benchWakes++;
        initReason = reason;
        state = State::BootWait;
        nextActionMs = millis() + BOOT_WAIT_MS;
    }

} // namespace

void TouchWatchdog::OnPoll(LGFX& tft, bool sawTouch, bool mayProbe)
{
    if constexpr (!variant::TOUCH_WATCHDOG)
        return; // compiled for every CST816-family SKU, active only where the wedge lives

    const unsigned long now = millis();

    if (sawTouch) {
        lastSawMs = now;
        backoffStep = 0;
        if (state == State::Suspect) {
            // touch reads are landing again: whatever NACKed was transient
            state = State::Healthy;
            suspectFails = 0;
        }
    }
    const bool releaseEdge = prevSaw && !sawTouch;
    prevSaw = sawTouch;

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
        } else if (benchIntervalMs != 0 && mayProbe && now >= benchNextMs) {
            benchNextMs = now + benchIntervalMs;
            if (!Probe()) {
                // silent chip: usually just auto-sleep. Wake it with one reset and
                // judge after the boot wait; only a no-show THEN is a wedge.
                Reinit(tft, InitReason::BenchWake);
            }
        }
        break;

    case State::Suspect:
        if (mayProbe && now >= nextActionMs) {
            if (Probe()) {
                Serial.printf("[touch-wd] probe recovered after %u NACK(s) -- transient\n", suspectFails);
                suspectFails = 0;
                state = State::Healthy;
            } else if (++suspectFails >= SUSPECT_PROBES) {
                stats.wedges++;
                Serial.printf("[touch-wd] WEDGE #%lu: controller dead on the bus while awake; re-initialising\n",
                              (unsigned long)stats.wedges);
                Reinit(tft, InitReason::Recovery);
            } else {
                nextActionMs = now + SUSPECT_SPACING_MS;
            }
        }
        break;

    case State::BootWait:
        if (mayProbe && now >= nextActionMs) {
            if (Probe()) {
                if (initReason == InitReason::Recovery) {
                    stats.recoveries++;
                    Serial.printf("[touch-wd] RECOVERED (%lu of %lu re-inits have worked)\n",
                                  (unsigned long)stats.recoveries, (unsigned long)stats.recoverAttempts);
                }
                backoffStep = 0;
                state = State::Healthy;
            } else {
                if (initReason == InitReason::BenchWake) {
                    // a real reset + full boot wait and still no ACK: that is the
                    // wedge signature from the June diagnosis
                    stats.wedges++;
                    Serial.printf("[touch-wd] WEDGE #%lu: no ACK after RST wake + %lums\n",
                                  (unsigned long)stats.wedges, BOOT_WAIT_MS);
                }
                const unsigned long wait = BACKOFF_MS[backoffStep];
                if (backoffStep + 1 < BACKOFF_STEPS)
                    backoffStep++;
                nextActionMs = now + wait;
                state = State::Backoff;
                Serial.printf("[touch-wd] still dead; next re-init in %lus\n", wait / 1000);
            }
        }
        break;

    case State::Backoff:
        if (mayProbe && now >= nextActionMs)
            Reinit(tft, InitReason::Recovery);
        break;
    }
}

void TouchWatchdog::EnableBenchProbe(unsigned long intervalMs)
{
    if constexpr (!variant::TOUCH_WATCHDOG)
        return;
    benchIntervalMs = intervalMs;
    // First check soon after boot (tft.init() just brought the chip up, so it
    // should be awake and ACK without needing a wake reset).
    benchNextMs = millis() + 3000;
}

const TouchWatchdog::Stats& TouchWatchdog::GetStats()
{
    return stats;
}

#endif // BLIPSCOPE_TOUCH_CST816
