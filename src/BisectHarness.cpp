#include "BisectHarness.h"

#ifdef BISECT_TEST

#include <Arduino.h>
#include <cmath>
#include <vector>

#include "AircraftManager.h"
#include "TouchWatchdog.h"
#include "Layout.h"
#include "models/Aircraft.h"

// Tunables (override with -D in the env for a harsher run).
#ifndef BISECT_FLEET_N
#define BISECT_FLEET_N 25 // the cloud C3 blips ceiling (/v1/blips limit=25); 60 = firmware MAX_AIRCRAFT
#endif
#ifndef BISECT_INJECT_MS
#define BISECT_INJECT_MS 2000 // merge cadence; harsher than the C3 cloud active poll (5 s)
#endif
#ifndef BISECT_PROBE_INTERVAL_MS
#define BISECT_PROBE_INTERVAL_MS 30000 // unattended CST816 health check (wake reset + chip-id probe)
#endif
#ifndef BISECT_WINDOW_MS
#define BISECT_WINDOW_MS (2UL * 60UL * 60UL * 1000UL) // the >= 2 h verdict window
#endif

namespace {

    AircraftManager* mgrPtr = nullptr;

    // Deterministic PRNG (xorshift32, fixed seed): every run storms identically, so
    // a wedge timestamp is comparable across flashes and boards.
    uint32_t rngState = 0xB15EC7u;
    uint32_t Rand()
    {
        uint32_t x = rngState;
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        return rngState = x;
    }
    long RandIn(long lo, long hi) { return lo + (long)(Rand() % (uint32_t)(hi - lo + 1)); }

    // ---- fleet ground truth (the harness owns the "sky"; the app only ever sees
    //      it through the real fetch-result merge) ----
    struct Truth {
        char icao[8];
        char cs[10];
        float lat, lon;
        float trackDeg;
        float speedMs;
        float altM;
        float vrateMs;
        int category;
    };
    Truth fleet[BISECT_FLEET_N];

    constexpr float HOME_LAT = 47.6f, HOME_LON = -122.3f; // must match BisectApplyTestConfig
    constexpr float LAT_M_PER_DEG = 111320.0f;

    unsigned long startMs = 0;
    unsigned long lastInjectMs = 0;
    unsigned long lastStatsMs = 0;
    bool verdictPrinted = false;

    unsigned long gestures = 0, taps = 0, swipes = 0, cardOpens = 0;
    unsigned long injects = 0, injectDrops = 0;
    bool prevCardOpen = false;

    // ---- the storm: a tiny gesture player. IDLE (gap) -> PRESS (hold/drag) ->
    //      release sample -> IDLE. One sample per loop pass; gesture shape is
    //      time-based so it's frame-rate independent. ----
    enum class Phase : uint8_t { Idle, Press };
    Phase phase = Phase::Idle;
    unsigned long phaseEndMs = 0;
    unsigned long pressStartMs = 0;
    int fromX = 0, fromY = 0, toX = 0, toY = 0; // drag path (tap: from == to)
    uint8_t queuedLeftSwipes = 0;               // remainder of a view-cycle burst (3 lefts = back to Radar)

    unsigned long NextGapMs()
    {
        if (queuedLeftSwipes > 0)
            return (unsigned long)RandIn(140, 300); // mid-burst: quick succession
        const uint32_t roll = Rand() % 100;
        if (roll < 70) return (unsigned long)RandIn(120, 450);   // busy user
        if (roll < 95) return (unsigned long)RandIn(800, 2500);  // brief pause
        return (unsigned long)RandIn(3000, 6000);                // calm stretch: clean sweep frames
    }

    void BeginPress(int fx, int fy, int tx, int ty, long durMs, unsigned long now)
    {
        fromX = fx; fromY = fy;
        toX = tx;   toY = ty;
        pressStartMs = now;
        phaseEndMs = now + (unsigned long)durMs;
        phase = Phase::Press;
        gestures++;
    }

    void PlanNextGesture(unsigned long now)
    {
        constexpr int C = SCREEN_SIZE / 2;
        constexpr int H = SCREEN_SIZE / 4; // horizontal swipe half-stroke (2H > SWIPE_MIN everywhere)

        if (queuedLeftSwipes > 0) {
            queuedLeftSwipes--;
            swipes++;
            BeginPress(C + H, C, C - H, C, RandIn(140, 260), now);
            return;
        }

        const bool cardOpen = mgrPtr->BisectCardOpen();
        const uint32_t roll = Rand() % 100;

        if (cardOpen) {
            if (roll < 60) { // close tap (no photo in this build, so one tap closes)
                taps++;
                const int x = C + (int)RandIn(-30, 30), y = C + (int)RandIn(-30, 30);
                BeginPress(x, y, x, y, RandIn(60, 140), now);
            } else if (roll < 80) { // vertical swipe: Up pins + closes, Down closes
                swipes++;
                const bool up = (Rand() & 1) != 0;
                BeginPress(C, up ? C + 70 : C - 70, C, up ? C - 70 : C + 70, RandIn(140, 260), now);
            } else { // dwell: hold the card open (its render load; the classic wedge scene)
                phase = Phase::Idle;
                phaseEndMs = now + (unsigned long)RandIn(2500, 6000);
            }
            return;
        }

        if (roll < 45) { // aimed tap at a live contact (opens real detail cards)
            taps++;
            const Truth& t = fleet[Rand() % BISECT_FLEET_N];
            auto [px, py] = mgrPtr->BisectProject(t.lat, t.lon);
            const int x = constrain(px + (int)RandIn(-8, 8), 4, SCREEN_SIZE - 5);
            const int y = constrain(py + (int)RandIn(-8, 8), 4, SCREEN_SIZE - 5);
            BeginPress(x, y, x, y, RandIn(60, 140), now);
        } else if (roll < 60) { // random tap in the disc: misses, tap-cycling, list rows
            taps++;
            const float ang = (float)(Rand() % 628) / 100.0f;
            const int r = (int)RandIn(0, C - 12);
            const int x = C + (int)(r * cosf(ang)), y = C + (int)(r * sinf(ang));
            BeginPress(x, y, x, y, RandIn(60, 140), now);
        } else if (roll < 80) { // view-cycle burst: 3 lefts -> List + Stats render, land on Radar
            queuedLeftSwipes = 2;
            swipes++;
            BeginPress(C + H, C, C - H, C, RandIn(140, 260), now);
        } else if (roll < 88) { // single horizontal swipe either way (some time off-radar)
            swipes++;
            const bool left = (Rand() & 1) != 0;
            BeginPress(left ? C + H : C - H, C, left ? C - H : C + H, C, RandIn(140, 260), now);
        } else { // vertical swipe: list scroll; a no-op on the radar
            swipes++;
            const bool up = (Rand() & 1) != 0;
            BeginPress(C, up ? C + 70 : C - 70, C, up ? C - 70 : C + 70, RandIn(140, 260), now);
        }
    }

    // ---- fleet dynamics (same flat-earth step the app's dead-reckoning uses) ----
    void SeedFleet()
    {
        for (int i = 0; i < BISECT_FLEET_N; ++i) {
            Truth& t = fleet[i];
            const bool mil = (i % 8) == 0; // a few military-pattern contacts: MIL ring + tag draw load
            snprintf(t.icao, sizeof(t.icao), mil ? "ae15%02x" : "b15e%02x", i);
            snprintf(t.cs, sizeof(t.cs), mil ? "RCH%03d" : "BSCT%03d", i);
            // two contacts near the centre (overhead-ring load), the rest across the disc
            const float rKm = (i < 2) ? 3.0f + i * 2.0f : 8.0f + (float)(Rand() % 80);
            const float bearing = (float)(Rand() % 360) * DEG_TO_RAD;
            t.lat = HOME_LAT + (rKm * 1000.0f * cosf(bearing)) / LAT_M_PER_DEG;
            t.lon = HOME_LON + (rKm * 1000.0f * sinf(bearing)) / (LAT_M_PER_DEG * cosf(HOME_LAT * DEG_TO_RAD));
            t.trackDeg = (float)(Rand() % 360);
            t.speedMs = 60.0f + (float)(Rand() % 190);
            t.altM = 500.0f + (float)(Rand() % 10500);
            t.vrateMs = (float)((int)(Rand() % 11) - 5);
            t.category = 1 + (int)(Rand() % 14);
        }
    }

    void AdvanceFleet(float dt)
    {
        for (auto& t : fleet) {
            const float hdg = t.trackDeg * DEG_TO_RAD;
            t.lat += (t.speedMs * dt * cosf(hdg)) / LAT_M_PER_DEG;
            t.lon += (t.speedMs * dt * sinf(hdg)) / (LAT_M_PER_DEG * cosf(t.lat * DEG_TO_RAD));
            // steer home past ~80% of the radar radius so the picture stays full for
            // the whole run (nothing ever leaves range, nothing evicts)
            const float dLatM = (t.lat - HOME_LAT) * LAT_M_PER_DEG;
            const float dLonM = (t.lon - HOME_LON) * LAT_M_PER_DEG * cosf(HOME_LAT * DEG_TO_RAD);
            if (sqrtf(dLatM * dLatM + dLonM * dLonM) > 80000.0f) {
                const float homeTrack = atan2f(-dLonM, -dLatM) / DEG_TO_RAD; // deg CW from north
                t.trackDeg = fmodf(homeTrack + 360.0f + (float)RandIn(-20, 20), 360.0f);
            }
        }
    }

    std::vector<Aircraft> BuildAircraftVector()
    {
        std::vector<Aircraft> v;
        v.reserve(BISECT_FLEET_N);
        // Any monotonic epoch works: timePosition == lastContact makes the merge's
        // age-on-arrival zero, so dead-reckoning runs purely on device elapsed time.
        const long fakeEpoch = 1750000000L + (long)(millis() / 1000);
        for (const auto& t : fleet) {
            Aircraft a{};
            a.icao24 = t.icao;
            a.callsign = t.cs;
            a.originCountry = "Bisection";
            a.timePosition = fakeEpoch;
            a.lastContact = fakeEpoch;
            a.longitude = t.lon;
            a.latitude = t.lat;
            a.baroAltitude = t.altM;
            a.onGround = false;
            a.velocity = t.speedMs;
            a.trueTrack = t.trackDeg;
            a.verticalRate = t.vrateMs;
            a.geoAltitude = t.altM;
            a.squawk = "1200";
            a.spi = false;
            a.positionSource = 0;
            a.category = t.category;
            v.push_back(std::move(a));
        }
        return v;
    }

} // namespace

void BisectHarness::Setup(AircraftManager& mgr)
{
    mgrPtr = &mgr;
    startMs = millis();
    lastInjectMs = startMs - BISECT_INJECT_MS; // first inject on the first Tick
    lastStatsMs = startMs;
    phaseEndMs = startMs + 2000; // let the first picture land before the storm starts

    mgr.BisectApplyTestConfig();
    TouchWatchdog::EnableBenchProbe(BISECT_PROBE_INTERVAL_MS);
    SeedFleet();

    Serial.println("[bisect] ==================================================");
    Serial.printf("[bisect] wedge-bisection harness: fleet=%d inject=%dms probe=%dms window=%lumin\n",
                  (int)BISECT_FLEET_N, (int)BISECT_INJECT_MS, (int)BISECT_PROBE_INTERVAL_MS,
                  (unsigned long)(BISECT_WINDOW_MS / 60000UL));
    Serial.println("[bisect] networking OFF: no WiFi, no background task, no socket. Sweep + storm running.");
    Serial.println("[bisect] ==================================================");
}

void BisectHarness::Tick(AircraftManager& mgr)
{
    const unsigned long now = millis();

    // advance ground truth + re-inject through the real fetch-result queue/merge
    if (now - lastInjectMs >= BISECT_INJECT_MS) {
        const float dt = (float)(now - lastInjectMs) / 1000.0f;
        lastInjectMs = now;
        AdvanceFleet(dt);
        if (mgr.BisectInjectFleet(BuildAircraftVector()))
            injects++;
        else
            injectDrops++; // result slot busy (an open card pauses merging) -- expected under the storm
    }

    const bool cardOpen = mgr.BisectCardOpen();
    if (cardOpen && !prevCardOpen)
        cardOpens++;
    prevCardOpen = cardOpen;

    if (now - lastStatsMs >= 30000) {
        lastStatsMs = now;
        const auto& wd = TouchWatchdog::GetStats();
        const unsigned long up = (now - startMs) / 1000UL;
        Serial.printf("[bisect] up=%02lu:%02lu:%02lu gestures=%lu (taps=%lu swipes=%lu cards=%lu) injects=%lu drops=%lu | wd wedges=%lu recov=%lu/%lu wakes=%lu probes=%lu/%lu\n",
                      up / 3600, (up / 60) % 60, up % 60,
                      gestures, taps, swipes, cardOpens, injects, injectDrops,
                      (unsigned long)wd.wedges, (unsigned long)wd.recoveries,
                      (unsigned long)wd.recoverAttempts, (unsigned long)wd.benchWakes,
                      (unsigned long)wd.probesOk, (unsigned long)wd.probesFailed);
    }

    if (!verdictPrinted && now - startMs >= BISECT_WINDOW_MS) {
        verdictPrinted = true;
        const auto& wd = TouchWatchdog::GetStats();
        const bool pass = (wd.wedges == 0);
        Serial.println("[bisect] ==================================================");
        Serial.printf("[bisect] %lu h WINDOW COMPLETE: %s\n",
                      (unsigned long)(BISECT_WINDOW_MS / 3600000UL),
                      pass ? "PASS -- no wedge with networking off" : "FAIL -- wedge(s) with networking off");
        Serial.printf("[bisect] wedges=%lu recoveries=%lu/%lu wakes=%lu probes=%lu/%lu gestures=%lu cards=%lu\n",
                      (unsigned long)wd.wedges, (unsigned long)wd.recoveries,
                      (unsigned long)wd.recoverAttempts, (unsigned long)wd.benchWakes,
                      (unsigned long)wd.probesOk, (unsigned long)wd.probesFailed,
                      gestures, cardOpens);
        Serial.println(pass
            ? "[bisect] => render+touch load alone did NOT wedge; the network path is implicated -> proceed to the 24 h cloud soak"
            : "[bisect] => wedged WITHOUT any networking: render/electrical load suffices; the cloud feed will not save the C3");
        Serial.println("[bisect] ==================================================");
        Serial.println("[bisect] harness keeps running; longer is better data");
    }
}

void BisectHarness::NextTouchSample(bool& touched, int& x, int& y)
{
    touched = false;
    x = y = 0;
    if (mgrPtr == nullptr)
        return;

    const unsigned long now = millis();

    if (phase == Phase::Idle) {
        if (now < phaseEndMs)
            return; // gap: finger up
        PlanNextGesture(now);
        if (phase == Phase::Idle)
            return; // planned a dwell: still finger-up
    }

    // Press: release exactly once past the end (the release sample is what fires
    // the tap/swipe classification), else interpolate along the stroke.
    if (now >= phaseEndMs) {
        phase = Phase::Idle;
        phaseEndMs = now + NextGapMs();
        return; // touched=false -> release edge
    }
    const float span = (float)(phaseEndMs - pressStartMs);
    const float tt = span > 0.0f ? (float)(now - pressStartMs) / span : 1.0f;
    x = fromX + (int)((float)(toX - fromX) * tt);
    y = fromY + (int)((float)(toY - fromY) * tt);
    touched = true;
}

#endif // BISECT_TEST
