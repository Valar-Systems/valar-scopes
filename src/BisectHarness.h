#pragma once

// Networking-off wedge-bisection harness (-DBISECT_TEST; env blipscope-kit-c3-128-bisect).
//
// Purpose: isolate the 2026-06-27 C3 touch-wedge regression. The wedge history says
// "more concurrent WiFi/TLS activity -> faster wedge", but the regression appeared
// right after the sweep-render work -- so which is the necessary condition? This
// build removes networking entirely (no WiFi init, no background task, no socket:
// see AircraftManager::kNoNet) and runs the OTHER suspect at full tilt for >= 2 h:
// the PPI sweep + paint-and-fade over a max-blip picture, under a continuous
// synthetic gesture storm.
//
//   - Fleet: BISECT_FLEET_N moving synthetic aircraft, re-injected every
//     BISECT_INJECT_MS through the REAL fetch-result queue + merge (trails, blend,
//     paint, eviction bookkeeping all exercised).
//   - Storm: synthetic touch samples driven through the production classification
//     pipeline (HandleTouch's poll still reads the real hardware every loop, so
//     genuine touch-bus traffic continues; its result is discarded). Aimed taps
//     open real detail cards (the classic wedge scene was "stuck on a card").
//   - Detection: the CST816 supervisor's bench probe (src/TouchWatchdog.h) actively
//     health-checks the unattended chip; [bisect] stats print every 30 s and a
//     verdict prints at the 2 h mark (the run keeps going -- longer is better data).
//
// Verdict semantics: wedges WITH networking off => render/electrical load alone
// suffices and the cloud feed won't save the C3. A clean window => the network
// path is implicated as necessary, and the C3 advances to the 24 h cloud soak
// (the un-retirement gate).

#ifdef BISECT_TEST

#if defined(FEATURE_EAM) || defined(FEATURE_SPACE) || defined(FEATURE_SEISMIC) || \
    defined(FEATURE_BIRDING) || defined(FEATURE_FISHING) || defined(FEATURE_CLAUDESCOPE) || \
    defined(FEATURE_SPEED)
#error "BISECT_TEST is a radar-app harness; build it on a radar env"
#endif

class AircraftManager;

namespace BisectHarness {

    // setup() tail: apply the deterministic test config, seed the fleet, arm the
    // watchdog bench probe. Call right after appManager.Initialise().
    void Setup(AircraftManager& mgr);

    // loop() head: advance + re-inject the fleet, keep the stats/verdict cadence.
    // Call every pass, before appManager.Update().
    void Tick(AircraftManager& mgr);

    // One synthetic gesture sample per touch poll; HandleTouch consumes this in
    // place of the (discarded) hardware sample.
    void NextTouchSample(bool& touched, int& x, int& y);

}

#endif // BISECT_TEST
