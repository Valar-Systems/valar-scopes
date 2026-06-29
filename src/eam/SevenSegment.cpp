#include "SevenSegment.h"

namespace eam {

namespace {

// Segment bitmask per digit, bit order: a b c d e f g (bit0 = a ... bit6 = g).
//   aaa
//  f   b
//  f   b
//   ggg
//  e   c
//  e   c
//   ddd
const uint8_t kSegTable[10] = {
    0b0111111, // 0: a b c d e f
    0b0000110, // 1: b c
    0b1011011, // 2: a b d e g
    0b1001111, // 3: a b c d g
    0b1100110, // 4: b c f g
    0b1101101, // 5: a c d f g
    0b1111101, // 6: a c d e f g
    0b0000111, // 7: a b c
    0b1111111, // 8: all
    0b1101111, // 9: a b c d f g
};

// Horizontal segment as a flat hexagon (rect body + triangular caps) spanning [x, x+len] at y.
void SegH(BandCanvas& c, int x, int y, int len, int t, uint32_t col)
{
    const int h2 = t / 2;
    if (len <= t) return;
    c.fillRect(x + h2, y, len - t, t, col);
    c.fillTriangle(x + h2, y, x + h2, y + t - 1, x, y + h2, col);
    c.fillTriangle(x + len - h2, y, x + len - h2, y + t - 1, x + len, y + h2, col);
}

// Vertical segment hexagon spanning [y, y+len] at x.
void SegV(BandCanvas& c, int x, int y, int len, int t, uint32_t col)
{
    const int h2 = t / 2;
    if (len <= t) return;
    c.fillRect(x, y + h2, t, len - t, col);
    c.fillTriangle(x, y + h2, x + t - 1, y + h2, x + h2, y, col);
    c.fillTriangle(x, y + len - h2, x + t - 1, y + len - h2, x + h2, y + len, col);
}

} // namespace

int SevenSegThickness(int h)
{
    int t = h / 8;
    if (t < 2) t = 2;
    return t;
}

void DrawSevenSeg(BandCanvas& c, int x, int y, int w, int h, int digit,
                  uint32_t lit, uint32_t ghost, uint32_t bloom)
{
    const int t = SevenSegThickness(h);
    const int midY = y + (h - t) / 2;
    const int rightX = x + w - t;
    const int halfH = (h - t) / 2 + t; // vertical seg length (overlaps the middle bar)
    const int botY = y + h - t;
    const int lowerY = midY;

    // Per-segment geometry as a draw lambda, so we can paint ghost (all) then lit (on) cheaply.
    auto seg = [&](int i, uint32_t col) {
        switch (i) {
            case 0: SegH(c, x, y, w, t, col); break;            // a top
            case 1: SegV(c, rightX, y, halfH, t, col); break;   // b top-right
            case 2: SegV(c, rightX, lowerY, halfH, t, col); break; // c bottom-right
            case 3: SegH(c, x, botY, w, t, col); break;         // d bottom
            case 4: SegV(c, x, lowerY, halfH, t, col); break;   // e bottom-left
            case 5: SegV(c, x, y, halfH, t, col); break;        // f top-left
            case 6: SegH(c, x, midY, w, t, col); break;         // g middle
        }
    };

    // Ghost every segment first so off segments stay faintly visible (the LED-panel look).
    for (int i = 0; i < 7; ++i) seg(i, ghost);

    if (digit < 0 || digit > 9) return;
    const uint8_t mask = kSegTable[digit];
    // Bloom pass (slightly offset, dimmer) then the bright core.
    for (int i = 0; i < 7; ++i)
        if (mask & (1 << i)) seg(i, bloom);
    for (int i = 0; i < 7; ++i)
        if (mask & (1 << i)) seg(i, lit);
}

void DrawColon(BandCanvas& c, int x, int y, int w, int h, bool lit,
               uint32_t litCol, uint32_t ghost)
{
    const int r = SevenSegThickness(h) / 2 + 1;
    const int cx = x + w / 2;
    const int y1 = y + h / 3;
    const int y2 = y + (2 * h) / 3;
    const uint32_t col = lit ? litCol : ghost;
    c.fillCircle(cx, y1, r, col);
    c.fillCircle(cx, y2, r, col);
}

} // namespace eam
