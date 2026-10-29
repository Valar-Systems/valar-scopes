#pragma once
// Day or night at a location, from the sun's elevation, with hysteresis so it cannot flicker at dusk. Pure C++11:
// no Arduino, no allocation, no clock and no config read here -- the caller passes everything in. Product-neutral by
// ruling: any scope's Auto theme or night dim can share it, and run.sh's SY scan refuses one product's symbols in it.
// Its plan and truth tables: docs/weatherscope-stage3.md A11.4, A12.43 and A12.45; held by test/host/test_daynight.cpp.
//
// The elevation is SolarDim.h's SunElevationDeg (one shared formula, not a second one). The threshold is -0.833 deg,
// the sun's upper limb on the horizon with refraction, as every edition's night dim already uses it.
//
//   - no valid clock, or no location: no verdict, so the previous phase stands (an unsynced clock is not evidence of
//     daytime);
//   - the first verdict (prev Unknown): Day at or above the threshold, Night below. No band, since nothing is held;
//   - after that: Night only below threshold - H, Day only above threshold + H; inside the band, prev stands.
#include <stdint.h>
#include <time.h>

#include "SolarDim.h"

namespace daynight {

enum class Phase : uint8_t { Unknown, Day, Night };

constexpr float kThresholdDeg = -0.833f;
constexpr float kHysteresisDeg = 1.0f;   // H (Q37): the band runs from -1.833 to +0.167 deg

inline Phase Decide(Phase prev, bool clockValid, bool locationValid, float elevDeg)
{
    if (!clockValid || !locationValid) return prev;
    if (prev == Phase::Unknown) return elevDeg >= kThresholdDeg ? Phase::Day : Phase::Night;
    if (elevDeg < kThresholdDeg - kHysteresisDeg) return Phase::Night;
    if (elevDeg > kThresholdDeg + kHysteresisDeg) return Phase::Day;
    return prev;
}

// Decide over SunElevationDeg. The elevation is computed only when both gates hold.
inline Phase Evaluate(Phase prev, bool clockValid, bool locationValid, double latDeg, double lonDeg, time_t utc)
{
    if (!(clockValid && locationValid)) return prev;
    return Decide(prev, true, true, SunElevationDeg(latDeg, lonDeg, utc));
}

}  // namespace daynight
