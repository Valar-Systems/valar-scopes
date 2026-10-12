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

} // namespace sevenseg
