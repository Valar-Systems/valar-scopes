#pragma once
// The sweep's wedge, in ONE place: its three constants and the fill the device draws. Pure C++11 -- no Arduino, no
// LovyanGFX -- so every path that draws or marks the wedge reads the same numbers:
//   - DrawRadarSweep (include/DrawHelpers.h) draws the opaque wedge with FillWedge on the device's canvas;
//   - wxsweep::Rasterise (include/WxSweep.h) marks the same wedge for the weather layer, and MapSweep over the map;
//   - test/host/test_sweep_wedge.cpp draws FillWedge and requires Rasterise's marks, pixel for pixel.
// Until 2026-11-22 DrawRadarSweep kept its own copy of all three constants, so the afterglow's length lived in two
// places and changing one of them gave two looks. The owner's ruling that day: one constant. kTail below is the
// afterglow's length on every screen, and going back is one line.
#include <stdint.h>
#include <cmath>

namespace wxsweep {

constexpr float kTail = 1.0f;   // the afterglow behind the beam, radians: the one constant
constexpr int kSegs = 18;       // wedge slices; more = smoother fade, more fills
constexpr int kPeak = 120;      // peak trail green, kept under the blips' brightness

/** Slice i's green level (1 = next to the beam, kSegs = the tail), exactly FillWedge's expression. */
inline uint8_t SliceLevel(int i)
{
    const float frac = i / (float)kSegs;
    const float g = 1.0f - frac;
    return (uint8_t)(g * g * kPeak);
}

/** The wedge behind the beam: kSegs triangles from the beam backwards, slice i in SliceLevel(i)'s green, each drawn
 *  with `buf.fillTriangle(cx, cy, ..., colour(level))`. This is DrawRadarSweep's body until 2026-11-22, moved
 *  verbatim: only its three constants (now this file's) and its colour call (now `colour`) changed. */
template <typename Canvas, typename Colour>
inline void FillWedge(Canvas& buf, int cx, int cy, int radius, float angle, const Colour& colour)
{
    constexpr float TAIL = kTail;  // DrawRadarSweep's own names, kept so the loop below is its text byte for byte
    constexpr int   SEGS = kSegs;
    constexpr int   PEAK = kPeak;

    // Leading-edge point, then walk backwards in angle filling one triangle per
    // slice between successive rim points.
    const int xLead = cx + (int)(std::cos(angle) * radius);
    const int yLead = cy + (int)(std::sin(angle) * radius);

    int xPrev = xLead, yPrev = yLead;
    for (int i = 1; i <= SEGS; i++) {
        const float frac = i / (float)SEGS;            // 0 at the beam, 1 at the tail
        const float a = angle - frac * TAIL;
        const int x = cx + (int)(std::cos(a) * radius);
        const int y = cy + (int)(std::sin(a) * radius);

        // quadratic fade: bright next to the beam, dark at the tail
        const float g = 1.0f - frac;
        const uint8_t lvl = (uint8_t)(g * g * PEAK);
        buf.fillTriangle(cx, cy, xPrev, yPrev, x, y, colour(lvl));

        xPrev = x;
        yPrev = y;
    }
}

}  // namespace wxsweep
