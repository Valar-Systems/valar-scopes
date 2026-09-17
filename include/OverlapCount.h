#pragma once

/* ============================================================================
 * WHAT IS ACTUALLY ON THE GLASS, COUNTED -- not what we assumed was on it.
 *
 * WHY THIS EXISTS. The first version of this counter measured LABEL BOXES and
 * nothing else, and it was built to answer one question: how many contacts can
 * the disc hold before the text becomes unreadable, so that #310's declutter
 * threshold comes off a curve rather than out of a guess.
 *
 * Then a saved frame off /diag/fb was read, and the heaviest collisions in it
 * were not label-on-label at all. They were the gold NEW flags -- four of them,
 * two stacked -- and a magenta NEAR tag lying across a neighbour's callsign.
 * The instrument was firing correctly and measuring the smaller half of the
 * problem, which is the worst thing an instrument can do: a number that is
 * right about its own definition and wrong about the question.
 *
 * So every rectangle that carries TEXT onto the radar enters the same count:
 * the label box, the NEW flag, the MIL/SPC/HELI tag, and the NEAR/HIGH/FAST
 * stack. One accumulator, one definition of a collision.
 *
 * THE NEW FLAG OVERLAPPING ITS OWN LABEL IS A REAL COLLISION, and the test
 * pins the arithmetic that says so. At text size 1 the first label row is drawn
 * at (x+5, y+5) and the NEW flag at (x+11, y+6) -- eighteen pixels of gold
 * printed straight through the first line of the label, on every claimable
 * contact, by construction. It is not a layout that happens to be tight; the
 * two strings share pixels every time. Counting it is the point.
 *
 * WHAT IS DELIBERATELY NOT COUNTED, said out loud so the omission is a decision
 * rather than a silence: the watchlist callsign, the pinned-contact callsign,
 * and the Follow HUD. They are opt-in, they are rare, and no frame has yet been
 * read where they were the problem. If one is, this is where they go.
 *
 * TOUCHING EDGES ARE NOT A COLLISION. The MIL/SPC/HELI tag sits at (x+11, y-3)
 * and is exactly eight pixels tall, so its bottom edge lands on y+5 -- the top
 * edge of the first label row, to the pixel. A `<=` here instead of a `<` would
 * report a collision for every special contact on the scope and none of them
 * would be real. There is a case in the host test whose only job is that.
 * ==========================================================================*/

#include <cstdint>

namespace overlapcount {

/// A drawn rectangle in screen pixels: [x0,x1) x [y0,y1), top-left origin.
struct Rect { int16_t x0, y0, x1, y1; };

/// Area of the overlap between two rectangles in px^2, and 0 when they miss.
///
/// AREA RATHER THAN A BOOLEAN, because a two-pixel clip and a badge printed
/// straight through a callsign are both "an overlap" and only one of them is
/// worth decluttering for. The pair count answers "how often"; this answers
/// "how badly", and #310 needs both.
inline uint32_t IntersectArea(const Rect& a, const Rect& b)
{
    const int l = a.x0 > b.x0 ? a.x0 : b.x0;
    const int r = a.x1 < b.x1 ? a.x1 : b.x1;
    const int t = a.y0 > b.y0 ? a.y0 : b.y0;
    const int u = a.y1 < b.y1 ? a.y1 : b.y1;
    if (r <= l || u <= t) return 0;   // strict: a shared edge is not a collision
    return (uint32_t)(r - l) * (uint32_t)(u - t);
}

/// One frame's worth of drawn rectangles and the collisions among them.
///
/// CAP is a template parameter so the host test can force saturation with a
/// two-slot frame. On the device it is sized from MAX_AIRCRAFT so that it
/// cannot saturate at all -- see the static_assert at the call site -- but
/// `Dropped()` exists anyway, because a counter that silently stops counting
/// under-reports exactly in the busy sky the number was collected to describe.
template <int CAP>
class Frame
{
  public:
    void Reset() { n_ = 0; pairs_ = 0; px_ = 0; dropped_ = 0; }

    /// Record one drawn rectangle, counting it against everything already
    /// recorded this frame. Counting happens BEFORE the capacity test, so a
    /// rectangle past the cap still contributes its collisions with what is
    /// stored -- it simply cannot be compared against later ones, and says so.
    void Add(const Rect& q)
    {
        for (int i = 0; i < n_; ++i) {
            const uint32_t a = IntersectArea(q, r_[i]);
            if (a != 0) { ++pairs_; px_ += a; }
        }
        if (n_ < CAP) r_[n_++] = q;
        else          ++dropped_;
    }

    int      Count()   const { return n_; }       ///< rectangles stored
    uint32_t Pairs()   const { return pairs_; }   ///< intersecting pairs
    uint32_t Px()      const { return px_; }      ///< summed intersection area
    uint32_t Dropped() const { return dropped_; } ///< rectangles past the cap

  private:
    Rect     r_[CAP];
    int      n_      = 0;
    uint32_t pairs_  = 0;
    uint32_t px_     = 0;
    uint32_t dropped_ = 0;
};

}  // namespace overlapcount
