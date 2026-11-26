#pragma once

#include <cmath>
#include "LGFX.h"
#include "BandCanvas.h"
#include "SweepWedge.h"

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

// The bright leading beam and its phosphor-white head, drawn last over the wedge. Shared by the opaque wedge below and
// by the sweep over the weather layer (AircraftManager::DrawSweepOverWeather, WD6), so the beam is today's in both.
inline void DrawRadarSweepBeam(BandCanvas& buf, int cx, int cy, int radius, float angle)
{
    const int xLead = cx + (int)(std::cos(angle) * radius);
    const int yLead = cy + (int)(std::sin(angle) * radius);
    buf.drawLine(cx, cy, xLead, yLead, lgfx::color888(80, 255, 80));
    buf.fillCircle(xLead, yLead, 2, lgfx::color888(170, 255, 170));
}

inline void DrawRadarSweep(BandCanvas& buf, int cx, int cy, int radius, float angle)
{
    // The wedge and its three constants live in include/SweepWedge.h, shared with the weather and map sweeps
    // (wxsweep::Rasterise); test/host/test_sweep_wedge.cpp holds the two to the same pixels. The afterglow's length
    // is wxsweep::kTail there, and nowhere else.
    wxsweep::FillWedge(buf, cx, cy, radius, angle, [](uint8_t lvl) { return lgfx::color888(0, lvl, 0); });

    // bright leading beam with a phosphor-white head where it meets the bezel
    DrawRadarSweepBeam(buf, cx, cy, radius, angle);
}
