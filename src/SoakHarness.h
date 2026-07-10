#pragma once

// Realistic-duty soak harness (-DSOAK_TEST; env blipscope-kit-c3-128-soak) -- the
// C3 launch gate. Unlike the bisection storm, NOTHING is faked but the fingers:
// networking is fully on (cloud mode against staging, real blip traffic, real
// TLS), and a sparse human-scale gesture script plays short tap bursts every
// 5-15 minutes (~150 presses/day). Real touches pass through between bursts.
//
// The 24 h gate (evaluated + printed on-device, run keeps going after):
//   - wedge incidence <= 1
//   - every recovery bounded <= 90 s (and no reboot ever recommended)
//   - display never interrupted (reaching the 24 h line at all proves no reboot)
//   - zero BUDGET BROKEN lines (frame p95 <= 60 ms -- recalibrated 2026-07-10
//     from the s3-128 ship-config soak's measured 51-53 ms sustained p95, which
//     grazed the old Phase-0 50 ms line 42x/10 h with zero functional impact;
//     largest block >= 20 KB)
//   - heap flat (min largest block >= 20 KB across the run)
//
// [soak] stats print every 60 s so the gate is auditable from the serial log.

#ifdef SOAK_TEST

#ifdef BISECT_TEST
#error "SOAK_TEST and BISECT_TEST are mutually exclusive harnesses"
#endif
#if defined(FEATURE_EAM) || defined(FEATURE_SPACE) || defined(FEATURE_SEISMIC) || \
    defined(FEATURE_BIRDING) || defined(FEATURE_FISHING) || defined(FEATURE_CLAUDESCOPE) || \
    defined(FEATURE_SPEED)
#error "SOAK_TEST is a radar-app harness; build it on a radar env"
#endif

class AircraftManager;

namespace SoakHarness {

    // setup() tail, AFTER the normal WiFi/config bring-up and appManager.Initialise():
    // arms the gesture script and prints the banner. Touches no configuration --
    // the device's real NVS settings (location, cloud key) govern the run.
    void Setup(AircraftManager& mgr);

    // loop() head: script scheduling, the 60 s stats line, the 24 h gate verdict.
    void Tick(AircraftManager& mgr);

    // One synthetic sample per touch poll WHILE a scripted gesture is in flight;
    // returns false between bursts so HandleTouch passes real touches through.
    bool NextTouchSample(bool& touched, int& x, int& y);

}

#endif // SOAK_TEST
