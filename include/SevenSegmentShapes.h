#pragma once

// The two segment shapes behind sevenseg::DrawSevenSeg, on their own so a host test can
// rasterize the geometry that ships (test/host/test_sevenseg_shapes.cpp). Templated on
// the canvas: the firmware passes a BandCanvas, the test a pixel grid. No dependencies.
//
// A segment is a flat hexagon: a body rectangle plus a triangular cap at each end, the
// LED-panel look. Along its length the near cap's base is at h2 = t/2 and the far cap's
// base at len - h2, so the body runs [h2, len - h2): len - 2*h2 pixels, flush with both.
//
// IT USED TO BE len - t PIXELS. That is the same thing only for an even t. For an odd t
// it stops ONE PIXEL short of the far cap, leaving a 1 px seam before every right and
// bottom tip -- the left and top caps stayed flush, so only those tips looked detached.
// The Missileer Clock on the 1.28" draws its digits at an odd thickness (digitH / 8), and
// the seam was plain in product photos (2026-10-12).
namespace sevenseg {

inline int SegBodyLen(int len, int t) { return len - 2 * (t / 2); }

// Horizontal segment spanning [x, x+len] at y, t thick.
template <typename Canvas, typename Col>
void SegH(Canvas& c, int x, int y, int len, int t, Col col)
{
    const int h2 = t / 2;
    if (len <= t) return;
    c.fillRect(x + h2, y, SegBodyLen(len, t), t, col);
    c.fillTriangle(x + h2, y, x + h2, y + t - 1, x, y + h2, col);
    c.fillTriangle(x + len - h2, y, x + len - h2, y + t - 1, x + len, y + h2, col);
}

// Vertical segment spanning [y, y+len] at x, t thick.
template <typename Canvas, typename Col>
void SegV(Canvas& c, int x, int y, int len, int t, Col col)
{
    const int h2 = t / 2;
    if (len <= t) return;
    c.fillRect(x, y + h2, t, SegBodyLen(len, t), col);
    c.fillTriangle(x, y + h2, x + t - 1, y + h2, x + h2, y, col);
    c.fillTriangle(x, y + len - h2, x + t - 1, y + len - h2, x + h2, y + len, col);
}

// The value DrawSevenSeg draws as a dash (the middle bar alone).
constexpr int DASH = 10;

// Segment thickness for a cell of height h.
inline int Thickness(int h)
{
    const int t = h / 8;
    return t < 2 ? 2 : t;
}

// Lit segments per digit, bit order a b c d e f g (bit0 = a ... bit6 = g); DASH is g alone.
//   aaa
//  f   b
//  f   b
//   ggg
//  e   c
//  e   c
//   ddd
inline unsigned SegMask(int digit)
{
    static const unsigned char kSegTable[10] = {
        0x3F, // 0: a b c d e f
        0x06, // 1: b c
        0x5B, // 2: a b d e g
        0x4F, // 3: a b c d g
        0x66, // 4: b c f g
        0x6D, // 5: a c d f g
        0x7D, // 6: a c d e f g
        0x07, // 7: a b c
        0x7F, // 8: all
        0x6F, // 9: a b c d f g
    };
    if (digit == DASH) return 0x40;
    return (digit >= 0 && digit <= 9) ? kSegTable[digit] : 0;
}

// THE 1's FLAG (2026-10-12). Segments b + c alone are a plain vertical bar, and under the
// Missileer Clock "1 ON WATCH" read as "I ON WATCH". A printed 1 has a flag: a short
// stroke from the top of its stem down to the left (the label font's own 1 has one --
// GLCD '1' is 00 42 7F 40 00). This is that stroke, in the segments' style: it leaves the
// bar's left edge where the top cap ends and runs down-left about 60 % of the free width
// at about 34 degrees, as a parallelogram t tall, and always ends above the middle bar.
// Drawn for a LIT 1 only, in the bloom and core passes -- never as ghost: no real
// seven-segment panel has it, and a ghosted diagonal in every cell would read as a
// broken digit.
template <typename Canvas, typename Col>
void Flag(Canvas& c, int x, int y, int w, int h, Col col)
{
    const int t = Thickness(h);
    const int barX = x + w - t;             // segment b's left edge
    const int reach = (w - t) * 6 / 10;     // how far left the flag goes
    if (reach < 1) return;
    const int x0 = barX, y0 = y + t / 2;    // where it leaves the bar
    const int midY = y + (h - t) / 2;       // the middle bar's top row
    int y1 = y0 + reach * 2 / 3;            // its far end's top
    if (y1 + t > midY) y1 = midY - t;       // never reaching the middle bar
    if (y1 < y0) y1 = y0;
    const int x1 = barX - reach;
    c.fillTriangle(x0, y0, x0, y0 + t - 1, x1, y1 + t - 1, col);
    c.fillTriangle(x0, y0, x1, y1 + t - 1, x1, y1, col);
}

// A digit cell's segment geometry.
struct DigitBox {
    int x, y, w, t;
    int midY;    // g's top; c and e start here too
    int rightX;  // b and c's left edge
    int halfH;   // vertical segment length (overlaps the middle bar)
    int botY;    // d's top
};
inline DigitBox Box(int x, int y, int w, int h)
{
    const int t = Thickness(h);
    DigitBox b = { x, y, w, t, y + (h - t) / 2, x + w - t, (h - t) / 2 + t, y + h - t };
    return b;
}

// Segment i (0..6 = a..g) of the cell. A plain function, not a lambda: the host
// toolchain's g++ rejects a capturing lambda inside a function template.
template <typename Canvas, typename Col>
void Seg(Canvas& c, const DigitBox& b, int i, Col col)
{
    switch (i) {
        case 0: SegH(c, b.x, b.y, b.w, b.t, col); break;                // a top
        case 1: SegV(c, b.rightX, b.y, b.halfH, b.t, col); break;       // b top-right
        case 2: SegV(c, b.rightX, b.midY, b.halfH, b.t, col); break;    // c bottom-right
        case 3: SegH(c, b.x, b.botY, b.w, b.t, col); break;             // d bottom
        case 4: SegV(c, b.x, b.midY, b.halfH, b.t, col); break;         // e bottom-left
        case 5: SegV(c, b.x, b.y, b.halfH, b.t, col); break;            // f top-left
        case 6: SegH(c, b.x, b.midY, b.w, b.t, col); break;             // g middle
    }
}

// One digit in the cell (x, y, w, h): every segment as ghost, then the lit ones as bloom
// and core, then (for a 1) the flag. Any value but 0..9 and DASH draws the ghost alone.
template <typename Canvas, typename Col>
void DrawDigit(Canvas& c, int x, int y, int w, int h, int digit, Col lit, Col ghost, Col bloom)
{
    const DigitBox b = Box(x, y, w, h);

    // Ghost every segment first so off segments stay faintly visible (the LED-panel look).
    for (int i = 0; i < 7; ++i) Seg(c, b, i, ghost);

    const unsigned mask = SegMask(digit);
    if (!mask) return;
    // Bloom pass, then the bright core.
    for (int i = 0; i < 7; ++i)
        if (mask & (1u << i)) Seg(c, b, i, bloom);
    if (digit == 1) Flag(c, x, y, w, h, bloom);
    for (int i = 0; i < 7; ++i)
        if (mask & (1u << i)) Seg(c, b, i, lit);
    if (digit == 1) Flag(c, x, y, w, h, lit);
}

} // namespace sevenseg
