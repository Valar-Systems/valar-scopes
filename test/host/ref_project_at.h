#pragma once
// WD1's REFERENCE (docs/wx-device.md): AircraftManager::ProjectAt's body exactly as it stood at 788ad44,
// before the extraction into include/RadarProjection.h. The body between the braces below is a verbatim
// copy, cut from `git show 788ad44:src/AircraftManager.cpp` by script, not retyped; its sha256 is
// 018192a40cf53a7eedb6754830218923575abd23f095b653f20e6ef2ef49078b (LF line endings). The member names it uses (lat, lon,
// rotSin, rotCos, SCREEN_SIZE) are supplied by the struct, so not one character of the body changed.
#include <utility>

namespace refproj {

template <int SCREEN_SIZE>
struct Projector {
    double lat = 0.0;
    double lon = 0.0;
    float rotCos = 1.0f;
    float rotSin = 0.0f;

    std::pair<int, int> ProjectAt(float predLat, float predLon, double rLat, double rLon) const
{
    const float dLon = predLon - lon;
    const float dLat = predLat - lat;

    const float normLon = (dLon + rLon) / (2.0f * rLon);
    const float normLat = (dLat + rLat) / (2.0f * rLat);

    float x = normLon * SCREEN_SIZE;
    float y = SCREEN_SIZE - (normLat * SCREEN_SIZE);

    // Window-up: rotate the picture about the centre so bearing radarUpDeg reads
    // "up". Screen space is isotropic here (radLon is pre-scaled by cos(lat) so
    // the scan box is square on the ground), which makes a plain screen-space
    // rotation geometrically correct. Everything downstream of this projection
    // (blips, trails, taps, the sweep's paint-crossing test) rotates with it.
    if (rotSin != 0.0f || rotCos != 1.0f) {
        constexpr float C = SCREEN_SIZE / 2.0f;
        const float px = x - C, py = y - C;
        x = C + px * rotCos + py * rotSin;
        y = C - px * rotSin + py * rotCos;
    }

    return { static_cast<int>(x), static_cast<int>(y) };
}
};

}  // namespace refproj
