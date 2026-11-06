#pragma once
// The radar's projection, in ONE place (docs/wx-device.md, WD1). AircraftManager::ProjectAt calls it, and
// the weather layer (WxLayer.h) inverts it, so weather and planes cannot register differently -- a second
// transcription would agree with this one until one of them changed (CLAUDE.md, the ChordWidthPx row).
//
// Moved out of AircraftManager::ProjectAt unchanged, types included: home lat/lon and the view radii are
// double, the target is float, and the arithmetic promotes exactly as it did there.
// test/host/test_radar_projection.cpp holds it bit-identical to the pre-extraction body over a sweep.
//
// Window-up: the picture is rotated about the centre so bearing radarUpDeg reads "up". Screen space is
// isotropic (rLon is pre-scaled by cos(lat) so the scan box is square on the ground), which makes a plain
// screen-space rotation geometrically correct. Everything downstream of this projection (blips, trails,
// taps, the sweep's paint-crossing test, and the weather layer) rotates with it.
#include <utility>

namespace radarproj {

inline std::pair<int, int> Project(float predLat, float predLon, double homeLat, double homeLon,
                                   double rLat, double rLon, float rotSin, float rotCos, int size)
{
    const float dLon = predLon - homeLon;
    const float dLat = predLat - homeLat;

    const float normLon = (dLon + rLon) / (2.0f * rLon);
    const float normLat = (dLat + rLat) / (2.0f * rLat);

    float x = normLon * size;
    float y = size - (normLat * size);

    if (rotSin != 0.0f || rotCos != 1.0f) {
        const float C = size / 2.0f;
        const float px = x - C, py = y - C;
        x = C + px * rotCos + py * rotSin;
        y = C - px * rotSin + py * rotCos;
    }

    return { static_cast<int>(x), static_cast<int>(y) };
}

}  // namespace radarproj
