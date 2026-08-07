#include "SoakHarness.h"

#ifdef SOAK_TEST

#include <Arduino.h>
#include <cmath>

#include "HeapHealth.h"
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
    uint32_t trialRejectBase = 0; // same idea for handshake refusals (boot transients out)
    bool     gatePassed = false;  // latched at the 24 h print; a regression after it prints once
    uint32_t warmupFreeHeap = 0; // free heap at warmup end; the outcome criterion is trend, not floor

    unsigned long bursts = 0, presses = 0;
    uint32_t minLargest = UINT32_MAX;

    // ---- detail-card dwell ---------------------------------------------------
    // A single tap that lands on an aircraft opens its detail card, and NOTHING in
    // the product closes it: ExitDetail() is only ever reached from another tap,
    // there is no timeout. So before this existed, a card opened by one burst
    // stayed open until the NEXT burst 6-16 minutes later.
    //
    // That is not a cosmetic detail, it silently aims the whole run at the wrong
    // code path. The gate's criteria are steady-state RADAR criteria, and
    // IsRadarView() is `screen == Radar && !inDetail`:
    //
    //   - touch is serialized against TLS only on the radar view; on the detail
    //     card HandleTouch is deliberately UNGATED so close-taps stay instant. The
    //     touch-vs-TLS contention this soak lineage exists to hunt therefore is
    //     not under test at all while a card is up.
    //   - the sweep is not rendering, so frame-budget breaches are not sampled
    //     against the load the budget was set for.
    //   - the card holds a ~15 KB photo sprite that ExitDetail() frees, so
    //     minLargest is measured against an allocation profile the device does not
    //     spend most of its life in.
    //
    // A human who opens a card reads it for a few seconds and closes it, which is
    // what "human-scale duty" is supposed to mean. So when a card is open the wait
    // collapses to a plausible dwell and the next press is a close tap. Costs
    // ~30-40 extra presses/day (~155 -> ~190), which is if anything closer to a
    // real owner than the original figure.
    constexpr long DWELL_MIN_S = 8, DWELL_MAX_S = 25;
    bool dwelling = false;      // the shortened wait is a card dwell, not a burst gap
    bool forceCloseTap = false; // next press must dismiss the open card
    unsigned long cardDismissals = 0;

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
        constexpr int C = SCREEN_SIZE / 2;

        // Dismissing an open card is not a burst -- it is the tail of the burst
        // that opened it, so it must not inflate the burst count the duty figure
        // is read from. A short press near centre lands on the card and closes it.
        if (forceCloseTap) {
            forceCloseTap = false;
            dwelling = false;
            cardDismissals++;
            BeginPress(C + (int)RandIn(-20, 20), C + (int)RandIn(-20, 20),
                       C + (int)RandIn(-20, 20), C + (int)RandIn(-20, 20),
                       RandIn(70, 130), now);
            return;
        }

        bursts++;
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

    // Arm the standing probe explicitly (as the bisect harness does). Without this the
    // watchdog's default fires its first probe on the first loop iteration, before the
    // system settles; SetProbeIntervalMs defers it a few seconds past Initialise.
    TouchWatchdog::SetProbeIntervalMs(10000);

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
        trialRejectBase = heaphealth::TrialRejectionCount();
        warmupFreeHeap = ESP.getFreeHeap(); // trend baseline for the outcome criterion
        minLargest = UINT32_MAX; // steady-state floor only; boot transients excluded
        Serial.println("[soak] warmup complete; gate counters armed (boot transients excluded)");
    }

    if (warmedUp) {
        const uint32_t largest = ESP.getMaxAllocHeap();
        if (largest < minLargest)
            minLargest = largest;
    }

    // Card left open by the burst that just ended: collapse the 6-16 min gap to a
    // reading dwell and make the next press dismiss it (see DWELL_MIN_S above).
    // Only from Wait, so a burst mid-flight is never cut short, and only once per
    // card -- `dwelling` keeps this from re-arming every loop while the dwell runs.
    if (phase == Phase::Wait && !dwelling && !forceCloseTap && mgr.DetailCardOpen()) {
        dwelling = true;
        forceCloseTap = true;
        phaseEndMs = now + (unsigned long)RandIn(DWELL_MIN_S, DWELL_MAX_S) * 1000UL;
    }
    // The card can also close without us -- a real finger during bench testing, or
    // the second press of a double-tap burst. Disarm rather than fire a close tap
    // at a radar that no longer has a card on it, which would OPEN one instead and
    // invert the fix.
    if (dwelling && !mgr.DetailCardOpen()) {
        dwelling = false;
        forceCloseTap = false;
        ScheduleNextBurst(now);
    }

    if (now - lastStatsMs >= STATS_EVERY_MS) {
        lastStatsMs = now;
        const auto& wd = TouchWatchdog::GetStats();
        const unsigned long up = (now - startMs) / 1000UL;
        const uint32_t gateBreaches = warmedUp ? mgr.BudgetBreachCount() - breachBase : 0;
        Serial.printf("[soak] up=%02lu:%02lu:%02lu presses=%lu bursts=%lu cards=%lu%s | wd wedges=%lu recov=%lu/%lu (s%lu/h%lu) wakes=%lu maxOutage=%lums rebootRec=%lu | breaches=%lu heap=%u minLargest=%u trend=%+ld allocFail=%lu hardFail=%lu%s\n",
                      up / 3600, (up / 60) % 60, up % 60, presses, bursts,
                      cardDismissals, mgr.DetailCardOpen() ? " CARD-OPEN" : "",
                      (unsigned long)wd.wedges, (unsigned long)wd.recoveries,
                      (unsigned long)wd.recoverAttempts, (unsigned long)wd.softRecoveries,
                      (unsigned long)wd.hardRecoveries, (unsigned long)wd.wakes,
                      (unsigned long)wd.maxOutageMs, (unsigned long)wd.rebootsRecommended,
                      (unsigned long)gateBreaches,
                      (unsigned)ESP.getFreeHeap(),
                      minLargest == UINT32_MAX ? 0u : (unsigned)minLargest,
                      warmedUp ? (long)ESP.getFreeHeap() - (long)warmupFreeHeap : 0L,
                      (unsigned long)mgr.AllocFailureCount(),
                      (unsigned long)mgr.FetchHardFailCount(),
                      warmedUp ? "" : " (warming up)");
    }

    // RE-SCORED CONTINUOUSLY, not once. The 2026-08-04 run passed at 24:00 with
    // minLargest=25588 and then dropped to 18420 at 24:43 -- below the gate's own
    // floor, 43 minutes after the only time it was ever measured. A gate that
    // samples once and never looks again is not measuring what it claims to, so
    // the criteria are evaluated every tick and the first regression AFTER a pass
    // gets its own line. The 24 h print stays: it is the published milestone.
    {
        const auto& wd = TouchWatchdog::GetStats();
        const uint32_t gateBreaches = warmedUp ? mgr.BudgetBreachCount() - breachBase : 0;
        const bool okWedges  = wd.wedges <= 1;
        const bool okOutage  = wd.maxOutageMs <= 90000 && wd.rebootsRecommended == 0;
        const bool okBudget  = gateBreaches == 0;
        // THE HEAP CRITERION IS NO LONGER A THRESHOLD ON largest. It was
        // minLargest >= 20000, read off ESP.getMaxAllocHeap() -- the metric #163
        // proved is pinned to an untouched reserve region and took five distinct
        // values in 54 h. Re-scoring that number more often would only have
        // sampled a constant faster. What the criterion always MEANT is "was there
        // ever a moment a TLS handshake could not have been served", and the trial
        // allocator answers exactly that, so: zero refusals post-warmup.
        const bool okHeap = warmedUp
            ? (heaphealth::TrialRejectionCount() - trialRejectBase) == 0
            : true;
        const bool pass = okWedges && okOutage && okBudget && okHeap;

        if (gatePrinted && gatePassed && !pass) {
            gatePassed = false;   // latched: report the first regression, once
            Serial.printf("[soak] !! GATE REGRESSED AFTER PASS at up=%02lu:%02lu:%02lu -- "
                          "wedges=%d outage=%d budget=%d heap=%d (rejections=%lu)\n",
                          (now - startMs) / 3600000UL, ((now - startMs) / 60000UL) % 60,
                          ((now - startMs) / 1000UL) % 60,
                          (int)okWedges, (int)okOutage, (int)okBudget, (int)okHeap,
                          (unsigned long)(heaphealth::TrialRejectionCount() - trialRejectBase));
        }
        if (!gatePrinted && now - startMs >= GATE_AT_MS) {
        gatePrinted = true;
        gatePassed = pass;
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
        Serial.printf("[soak]   handshake refusals %lu post-warmup (==0)  %s\n",
                      (unsigned long)(heaphealth::TrialRejectionCount() - trialRejectBase),
                      okHeap ? "ok" : "FAIL");
        Serial.printf("[soak]   heap floor        minLargest=%u (REPORT ONLY -- plateaued metric, see #163)\n",
                      (unsigned)minLargest);
        Serial.printf("[soak]   outcome (report-only, criteria rewrite pending): heapTrend=%+ld B allocFails=%lu hardFetchFails=%lu\n",
                      (long)ESP.getFreeHeap() - (long)warmupFreeHeap,
                      (unsigned long)mgr.AllocFailureCount(),
                      (unsigned long)mgr.FetchHardFailCount());
        Serial.println("[soak]   display           uninterrupted (this line printing at 24 h proves no reboot)");
        Serial.println("[soak] ==================================================");
        Serial.println("[soak] harness keeps running, and the gate keeps re-scoring");
        }
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
