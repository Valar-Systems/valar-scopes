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

/// A rectangle that text must not run into. Screen pixels, x1/y1 exclusive.
struct Box { int x0, y0, x1, y1; };

/// Widest CENTRED line at this row that clears the curve AND any obstacle boxes
/// overlapping the row band.
///
/// WHY THE CHORD IS NOT ENOUGH, which is the whole reason this exists. The arc
/// face draws its two airport codes INSIDE the disc, at radius 84 -- so at the
/// explanation row the glass is 195 px wide and only the middle 100 px is
/// actually free. Fitting to the chord put "Expected at this range." straight
/// through "BCN" (found on glass 2026-08-29, in two states).
///
/// The failure looked string-length dependent, which is why it survived review:
/// short explanations merely came CLOSE to the label and long ones crossed it,
/// so whether it looked broken depended on which sentence happened to be on
/// screen. It was never a copy problem. Every explanation string is wider than
/// the free span at that row -- the row was in the wrong place, and shortening
/// the copy would only have moved the trap to whoever wrote the next string.
/// Clearance kept between text and an obstacle, in pixels.
///
/// NOT ZERO, and the difference is the whole fix. The first version tested bare
/// intersection, so a line sitting THREE PIXELS under an airport code counted as
/// clear and returned the full chord -- geometrically true, visibly crowded, and
/// it moved the collision to the other label rather than resolving it. An
/// obstacle is something text keeps AWAY from, not something it merely fails to
/// intersect.
constexpr int DISC_OBSTACLE_MARGIN_PX = 5;

inline int ClearCentredWidthPx(int yTop, int lineH, int screenSize,
                               const Box* boxes, int nBoxes,
                               int inset = DISC_TEXT_INSET_PX,
                               int margin = DISC_OBSTACLE_MARGIN_PX)
{
    int w = ChordWidthPx(yTop, lineH, screenSize, inset);
    if (w <= 0) return 0;
    const int cx = screenSize / 2;
    const int yBot = yTop + lineH;
    for (int i = 0; i < nBoxes; ++i) {
        Box b = boxes[i];
        b.x0 -= margin; b.y0 -= margin; b.x1 += margin; b.y1 += margin;
        if (b.y1 <= yTop || b.y0 >= yBot) continue;   // clear by more than margin
        int limit;
        if (b.x1 <= cx)       limit = 2 * (cx - b.x1);   // wholly left of centre
        else if (b.x0 >= cx)  limit = 2 * (b.x0 - cx);   // wholly right of centre
        else                  limit = 0;                 // straddles the centre
        if (limit < w) w = limit;
    }
    return w > 0 ? w : 0;
}


/// Move a text band vertically until it clears every reserved band, or report
/// that it cannot.
///
/// WHY A NUDGE AND NOT A SHRINK. ClearCentredWidthPx above answers the other
/// half of this problem -- a CENTRED row yielding width to an obstacle beside
/// it. That is right for a sentence and wrong for a label: an airport code is
/// three characters that mean one thing, so narrowing it produces "JF..." which
/// is not a shorter code, it is a wrong one. A code has somewhere else to go
/// (the marker it names is a point, and any adjacent pixel still reads as
/// adjacent), so it moves.
///
/// THE TWO ARE COMPLEMENTARY, AND THE ORDER MATTERS: the label moves first and
/// is then RESERVED, and the centred rows shrink around where it ended up. Doing
/// it the other way round would let a row claim a span the label is about to
/// need.
///
/// @return the adjusted yTop, or `noFit` if no position on the glass clears the
///         bands. Callers must treat that as "do not draw" rather than clamping,
///         for the reason above -- a label drawn in the wrong place is worse than
///         one that is absent, because the marker underneath it is still there
///         and still correct.
inline int NudgeClearOfBands(int yTop, int lineH, const Box* bands, int nBands,
                             int screenSize, int noFit = -1,
                             int margin = DISC_OBSTACLE_MARGIN_PX)
{
    const int top = 2, bot = screenSize - 2;
    const auto onGlass = [&](int y) { return y >= top && y + lineH <= bot; };
    const auto clearAt = [&](int y) {
        for (int i = 0; i < nBands; ++i)
            if (y + lineH > bands[i].y0 - margin && y < bands[i].y1 + margin)
                return false;
        return true;
    };

    if (clearAt(yTop) && onGlass(yTop)) return yTop;

    // ENUMERATE, DO NOT WALK. The first version shoved the band to whichever
    // side of the offending row was nearer and repeated. That thrashes wherever
    // rows are close together -- and on this face they are: at the bottom of the
    // globe the state, clock and readout rows leave gaps of 6 px and 1 px, so a
    // label starting in the clock row was pushed up into the state row, back
    // down into the clock row, and out of the loop as "no fit". The sweep test
    // measured it: 13 placements against 40 refusals across the crowded half.
    //
    // A greedy local move cannot see past the row it is standing in. There are
    // only 2n+1 positions that can ever be the answer -- flush above or below
    // each band, or where it started -- so score them all and take the nearest
    // valid one. Deterministic, no iteration count to tune, and it cannot
    // oscillate.
    int best = noFit, bestDist = 0;
    const auto consider = [&](int y) {
        if (!onGlass(y) || !clearAt(y)) return;
        const int d = (y > yTop) ? (y - yTop) : (yTop - y);
        if (best == noFit || d < bestDist) { best = y; bestDist = d; }
    };
    for (int i = 0; i < nBands; ++i) {
        consider(bands[i].y0 - margin - lineH);   // flush above, with clearance
        consider(bands[i].y1 + margin);           // flush below
    }
    return best;
}

} // namespace discgeom
