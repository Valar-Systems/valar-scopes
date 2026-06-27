#pragma once

#include <cmath>
#include "LGFX.h"
#include "BandCanvas.h"

// Radar sweep: a bright leading beam trailed by a soft phosphor afterglow that
// fans out behind it, the way a PPI scope's persistence lags the rotating beam.
//
// The afterglow is built from a fan of filled triangles (gapless, unlike a fan
// of lines, which leaves widening gaps toward the rim) whose green brightness
// decays quadratically toward the tail. It's drawn beneath the range rings and
// blips, so contacts stay readable as the beam passes over them and momentarily
// lights up their sector.
//
// All geometry derives from `angle`/`radius` (computed once per frame in the
// draw loop), so the two render bands paint an identical wedge with no seam.
inline void DrawRadarSweep(BandCanvas& buf, int cx, int cy, int radius, float angle)
{
    constexpr float TAIL = 1.0f;   // afterglow span behind the beam, radians (~57 deg)
    constexpr int   SEGS = 18;     // wedge slices; more = smoother fade, more fills
    constexpr int   PEAK = 120;    // peak trail green, kept under the blips' brightness

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
        buf.fillTriangle(cx, cy, xPrev, yPrev, x, y, lgfx::color888(0, lvl, 0));

        xPrev = x;
        yPrev = y;
    }

    // bright leading beam with a phosphor-white head where it meets the bezel
    buf.drawLine(cx, cy, xLead, yLead, lgfx::color888(80, 255, 80));
    buf.fillCircle(xLead, yLead, 2, lgfx::color888(170, 255, 170));
}
