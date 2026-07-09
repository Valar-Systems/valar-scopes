#include "SoakHarness.h"

#ifdef SOAK_TEST

#include <Arduino.h>
#include <cmath>

#include "AircraftManager.h"
#include "TouchWatchdog.h"
#include "Layout.h"

namespace {

    // Deterministic PRNG (xorshift32, fixed seed) so soak runs are comparable.
    uint32_t rngState = 0x50AC1234u;
    uint32_t Rand()
    {
        uint32_t x = rngState;
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        return rngState = x;
    }
    long RandIn(long lo, long hi) { return lo + (long)(Rand() % (uint32_t)(hi - lo + 1)); }

    constexpr unsigned long GATE_AT_MS = 24UL * 60UL * 60UL * 1000UL;
    constexpr unsigned long STATS_EVERY_MS = 60000;
    // Gate counters arm only after the boot settles: WiFi + first TLS bring-up
    // legitimately spikes frame times and craters the largest block for a couple
    // of minutes, and the gate is a steady-state criterion, not a boot one.
    constexpr unsigned long WARMUP_MS = 3UL * 60UL * 1000UL;

    unsigned long startMs = 0;
    unsigned long lastStatsMs = 0;
    bool gatePrinted = false;
    bool warmedUp = false;
    uint32_t breachBase = 0; // BudgetBreachCount() at warmup end; gate counts the delta

    unsigned long bursts = 0, presses = 0;
    uint32_t minLargest = UINT32_MAX;

    // ---- the script: one burst every 6-16 min (mean ~11 -> ~131 bursts/day),
    //      each 60% single tap / 20% double-tap / 20% swipe (~1.2 presses/burst
    //      -> ~155 presses/day, the "human-scale" duty the gate specifies). ----
    enum class Phase : uint8_t { Wait, Press, Between };
    Phase phase = Phase::Wait;
    unsigned long phaseEndMs = 0;
    unsigned long pressStartMs = 0;
    int fromX = 0, fromY = 0, toX = 0, toY = 0;
    uint8_t pendingPresses = 0; // extra presses left in this burst (double-tap)

    void ScheduleNextBurst(unsigned long now)
    {
        phase = Phase::Wait;
        phaseEndMs = now + (unsigned long)RandIn(6, 16) * 60000UL;
    }

    void BeginPress(int fx, int fy, int tx, int ty, long durMs, unsigned long now)
    {
        fromX = fx; fromY = fy;
        toX = tx;   toY = ty;
        pressStartMs = now;
        phaseEndMs = now + (unsigned long)durMs;
        phase = Phase::Press;
        presses++;
    }

    void StartBurst(unsigned long now)
    {
        bursts++;
        constexpr int C = SCREEN_SIZE / 2;
        const uint32_t roll = Rand() % 100;
        if (roll < 60) { // single tap somewhere plausible on the scope
            const float ang = (float)(Rand() % 628) / 100.0f;
            const int r = (int)RandIn(0, C - 16);
            BeginPress(C + (int)(r * cosf(ang)), C + (int)(r * sinf(ang)),
                       C + (int)(r * cosf(ang)), C + (int)(r * sinf(ang)),
                       RandIn(70, 130), now);
        } else if (roll < 80) { // double-tap: open a card, then close/flip it
            pendingPresses = 1;
            const float ang = (float)(Rand() % 628) / 100.0f;
            const int r = (int)RandIn(0, C - 16);
            BeginPress(C + (int)(r * cosf(ang)), C + (int)(r * sinf(ang)),
                       C + (int)(r * cosf(ang)), C + (int)(r * sinf(ang)),
                       RandIn(70, 130), now);
        } else { // swipe: mostly horizontal view cycling, sometimes vertical
            const int H = SCREEN_SIZE / 4;
            if (Rand() % 4 == 0) {
                const bool up = (Rand() & 1) != 0;
                BeginPress(C, up ? C + 70 : C - 70, C, up ? C - 70 : C + 70, RandIn(150, 260), now);
            } else {
                const bool left = (Rand() & 1) != 0;
                BeginPress(left ? C + H : C - H, C, left ? C - H : C + H, C, RandIn(150, 260), now);
            }
        }
    }

} // namespace

void SoakHarness::Setup(AircraftManager& mgr)
{
    (void)mgr; // real NVS config governs; the harness only scripts fingers
    startMs = millis();
    lastStatsMs = startMs;
    ScheduleNextBurst(startMs);
    phaseEndMs = startMs + 120000; // first burst ~2 min in, after the first fetches settle

    Serial.println("[soak] ==================================================");
    Serial.println("[soak] realistic-duty soak: cloud mode, real traffic, human-scale gestures");
    Serial.println("[soak] bursts every 6-16 min (~155 presses/day); real touches pass through");
    Serial.println("[soak] 24 h GATE: wedges<=1, recoveries<=90s, no reboot, no BUDGET BROKEN, heap flat");
    Serial.println("[soak] ==================================================");
}

void SoakHarness::Tick(AircraftManager& mgr)
{
    const unsigned long now = millis();

    if (!warmedUp && now - startMs >= WARMUP_MS) {
        warmedUp = true;
        breachBase = mgr.BudgetBreachCount();
        minLargest = UINT32_MAX; // steady-state floor only; boot transients excluded
        Serial.println("[soak] warmup complete; gate counters armed (boot transients excluded)");
    }

    if (warmedUp) {
        const uint32_t largest = ESP.getMaxAllocHeap();
        if (largest < minLargest)
            minLargest = largest;
    }

    if (now - lastStatsMs >= STATS_EVERY_MS) {
        lastStatsMs = now;
        const auto& wd = TouchWatchdog::GetStats();
        const unsigned long up = (now - startMs) / 1000UL;
        const uint32_t gateBreaches = warmedUp ? mgr.BudgetBreachCount() - breachBase : 0;
        Serial.printf("[soak] up=%02lu:%02lu:%02lu presses=%lu bursts=%lu | wd wedges=%lu recov=%lu/%lu (s%lu/h%lu) wakes=%lu maxOutage=%lums rebootRec=%lu | breaches=%lu heap=%u minLargest=%u%s\n",
                      up / 3600, (up / 60) % 60, up % 60, presses, bursts,
                      (unsigned long)wd.wedges, (unsigned long)wd.recoveries,
                      (unsigned long)wd.recoverAttempts, (unsigned long)wd.softRecoveries,
                      (unsigned long)wd.hardRecoveries, (unsigned long)wd.wakes,
                      (unsigned long)wd.maxOutageMs, (unsigned long)wd.rebootsRecommended,
                      (unsigned long)gateBreaches,
                      (unsigned)ESP.getFreeHeap(),
                      minLargest == UINT32_MAX ? 0u : (unsigned)minLargest,
                      warmedUp ? "" : " (warming up)");
    }

    if (!gatePrinted && now - startMs >= GATE_AT_MS) {
        gatePrinted = true;
        const auto& wd = TouchWatchdog::GetStats();
        const uint32_t gateBreaches = mgr.BudgetBreachCount() - breachBase;
        const bool okWedges  = wd.wedges <= 1;
        const bool okOutage  = wd.maxOutageMs <= 90000 && wd.rebootsRecommended == 0;
        const bool okBudget  = gateBreaches == 0;
        const bool okHeap    = minLargest >= 20000;
        const bool pass = okWedges && okOutage && okBudget && okHeap;
        Serial.println("[soak] ==================================================");
        Serial.printf("[soak] 24 h GATE: %s\n", pass ? "PASS" : "FAIL");
        Serial.printf("[soak]   wedge incidence   %lu (<=1)          %s\n",
                      (unsigned long)wd.wedges, okWedges ? "ok" : "FAIL");
        Serial.printf("[soak]   recovery bound    max %lums (<=90s), rebootRec=%lu  %s\n",
                      (unsigned long)wd.maxOutageMs, (unsigned long)wd.rebootsRecommended,
                      okOutage ? "ok" : "FAIL");
        Serial.printf("[soak]   outage buckets    <=30s:%lu 30-90s:%lu >90s:%lu (from declaration; +<=10s detection)\n",
                      (unsigned long)wd.outageLe30s, (unsigned long)wd.outageLe90s,
                      (unsigned long)wd.outageGt90s);
        Serial.printf("[soak]   budget breaches   %lu post-warmup (==0)   %s\n",
                      (unsigned long)gateBreaches, okBudget ? "ok" : "FAIL");
        Serial.printf("[soak]   heap floor        minLargest=%u post-warmup (>=20000)  %s\n",
                      (unsigned)minLargest, okHeap ? "ok" : "FAIL");
        Serial.println("[soak]   display           uninterrupted (this line printing at 24 h proves no reboot)");
        Serial.println("[soak] ==================================================");
        Serial.println("[soak] harness keeps running; longer is better data");
    }
}

bool SoakHarness::NextTouchSample(bool& touched, int& x, int& y)
{
    touched = false;
    x = y = 0;

    const unsigned long now = millis();

    switch (phase) {
    case Phase::Wait:
        if (now < phaseEndMs)
            return false; // between bursts: real touches pass through
        StartBurst(now);
        break; // fall into Press sampling below

    case Phase::Between: // the double-tap's inter-press pause: hold the script active
        if (now < phaseEndMs) {
            return true; // synthetic finger-up (a release/idle sample)
        }
        // second press, same spot give-or-take a wobble
        BeginPress(fromX + (int)RandIn(-8, 8), fromY + (int)RandIn(-8, 8),
                   fromX + (int)RandIn(-8, 8), fromY + (int)RandIn(-8, 8),
                   RandIn(70, 130), now);
        break;

    case Phase::Press:
        break;
    }

    if (now >= phaseEndMs) {
        // release sample (fires the classification), then pause or finish
        if (pendingPresses > 0) {
            pendingPresses--;
            phase = Phase::Between;
            phaseEndMs = now + (unsigned long)RandIn(350, 600);
        } else {
            ScheduleNextBurst(now);
        }
        return true; // touched=false -> the release edge
    }

    const float span = (float)(phaseEndMs - pressStartMs);
    const float tt = span > 0.0f ? (float)(now - pressStartMs) / span : 1.0f;
    x = fromX + (int)((float)(toX - fromX) * tt);
    y = fromY + (int)((float)(toY - fromY) * tt);
    touched = true;
    return true;
}

#endif // SOAK_TEST
