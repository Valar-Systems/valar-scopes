#pragma once
// Where an airport's code may be printed so that it never lands on traffic (docs/v20-label-collision.md).
//
// DrawAirports used to print the code blind at (x+5, y-3), BEFORE the traffic pass. So a contact sitting on an airport
// mark printed its label straight through the airport's; the airfield preset puts NEAR at the centre, where the field
// is. The label now YIELDS: it is tried right, then left, else dropped. The MARK always stays, drawn first as before.
//
// PURE. The obstacles are what the traffic pass actually drew, recorded by the calls that drew it, never re-derived:
//   - TEXT: the overlap counter's label and badge boxes, plus the traffic text the counter does not hold (the
//     emergency descriptor and its dot, LOOK UP, the watchlist and pinned callsigns);
//   - SYMBOLS: each aircraft glyph's extent, as DrawAircraftTriangle drew it.
// The extra text and the symbols live in a SEPARATE list, so the counter (#310's series) is unchanged.
// THE RIM is a third obstacle: a candidate that does not fit on the glass is blocked (OnGlass), on either side.
#include <stdint.h>
#include "DiscGeometry.h"
#include "OverlapCount.h"

namespace airportlabel {

using overlapcount::Rect;

enum class Side : uint8_t { Right, Left, Dropped };

struct Placement {
    Side    side;
    int16_t x, y;   // the drawString origin (top-left); meaningless when Dropped
};

/// The box a code w x h px occupies when drawn with its top-left at (x, y).
inline Rect Box(int x, int y, int w, int h)
{
    return Rect{ (int16_t)x, (int16_t)y, (int16_t)(x + w), (int16_t)(y + h) };
}

/// A fixed-capacity list of drawn rectangles that answers one question: does a box land on any of them?
/// PAST CAPACITY IT ANSWERS YES. A list that silently stopped recording would say "clear" about exactly the busy sky
/// it exists for, so an overflowed list makes every label yield instead.
template <int CAP>
class Obstacles
{
public:
    void Reset() { n_ = 0; overflow_ = false; }
    void Add(const Rect& r)
    {
        if (n_ < CAP) r_[n_++] = r;
        else overflow_ = true;
    }
    bool Hits(const Rect& q) const
    {
        if (overflow_) return true;
        for (int i = 0; i < n_; ++i)
            if (overlapcount::IntersectArea(r_[i], q)) return true;
        return false;
    }
    int  Count()    const { return n_; }
    bool Overflow() const { return overflow_; }

private:
    Rect r_[CAP];
    int  n_       = 0;
    bool overflow_ = false;
};

/// On the glass: the box fits inside the disc's chord at its rows, in the chord's centred span, with the inset every
/// text caller keeps. It is discgeom::ChordWidthPx, the rule LabelLayout's traffic rows go through, applied the way
/// LabelLayout applies it (x within [c - avail/2, c + avail/2]).
/// A box that does not fit is BLOCKED, never slid and never cut: a code partly off the glass is a truncated string,
/// which the traffic labels' rule forbids ("drop, never truncate").
inline bool OnGlass(const Rect& b, int screenSize)
{
    const int avail = discgeom::ChordWidthPx(b.y0, b.y1 - b.y0, screenSize);
    if (avail <= 0) return false;
    const int c = screenSize / 2;
    return b.x0 >= c - avail / 2 && b.x1 <= c + avail / 2;
}

/// Where the code goes. `text(box)` answers "does this box land on traffic text?", and `symbol(box)` "on an aircraft
/// symbol?". The order is right first, at the old (mx+5, my-3); else left, with the label's right edge 5 px left of
/// the mark, on the same baseline; else dropped. A candidate is usable only ON THE GLASS (OnGlass) and clear of both.
template <class TextHits, class SymbolHits>
inline Placement Place(int mx, int my, int w, int h, int screenSize, const TextHits& text, const SymbolHits& symbol)
{
    const int y = my - 3;
    const int rx = mx + 5;
    const Rect right = Box(rx, y, w, h);
    if (OnGlass(right, screenSize) && !text(right) && !symbol(right))
        return Placement{ Side::Right, (int16_t)rx, (int16_t)y };
    const int lx = mx - 5 - w;
    const Rect left = Box(lx, y, w, h);
    if (OnGlass(left, screenSize) && !text(left) && !symbol(left))
        return Placement{ Side::Left, (int16_t)lx, (int16_t)y };
    return Placement{ Side::Dropped, 0, 0 };
}

}  // namespace airportlabel
