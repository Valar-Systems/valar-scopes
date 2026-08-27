#pragma once

// How wide is the glass at this row?
//
// A ROUND SCREEN HAS NO EDGE TO CLIP AGAINST. There is no rectangle for glyphs
// to stop at: they simply run off the curve, and text clipped by the bezel looks
// identical to text clipped by a bug. Near the top and bottom of the disc the
// usable width collapses fast -- at y=214 on a 240 px panel the chord is 118 px,
// nineteen characters, half what the same row would hold on a square panel.
//
// This started as a lambda inside DrawStats, fitting the one string a customer
// can make arbitrarily long (their Wi-Fi SSID). The Follow local face's bottom
// readout then ran off BOTH ends of the curve for exactly the same reason, on a
// row even closer to the edge -- a second surface with the same defect. So the
// rule lives here, once, and every caller goes through it.
//
// SEPARATE FROM Layout.h ON PURPOSE. Layout.h resolves the variant, which needs
// the board; this is pure integer geometry with no hardware in it, so it can be
// graded on the workstation (test/host/test_follow_state.cpp) with the same
// narrow include path as everything else in that suite. Layout.h wraps it with
// the SCREEN_SIZE default so call sites stay short.

#include <cmath>

namespace discgeom {

/// Keep glyphs off the bezel itself rather than exactly touching it.
constexpr int DISC_TEXT_INSET_PX = 8;

/// Usable width, in pixels, of a `screenSize` round panel for a line of text
/// occupying rows `yTop .. yTop + lineH`.
///
/// Measured at whichever edge of the glyph band sits FARTHER from the centre
/// line -- the narrower end is the one that decides whether the line fits.
/// Returns 0 for a row that is off the glass, never a negative width a caller
/// might quietly use as a length.
inline int ChordWidthPx(int yTop, int lineH, int screenSize,
                        int inset = DISC_TEXT_INSET_PX)
{
    const float r = (float)screenSize * 0.5f;
    const int   c = screenSize / 2;
    const int   d0 = yTop - c;
    const int   d1 = yTop + lineH - c;
    const int   a0 = d0 < 0 ? -d0 : d0;
    const int   a1 = d1 < 0 ? -d1 : d1;
    const float dy = (float)(a0 > a1 ? a0 : a1);
    if (dy >= r) return 0;
    const int avail = (int)(2.0f * sqrtf(r * r - dy * dy)) - inset;
    return avail > 0 ? avail : 0;
}

} // namespace discgeom
