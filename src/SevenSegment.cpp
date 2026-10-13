#include "SevenSegment.h"
#include "SevenSegmentShapes.h"

namespace sevenseg {

// The digit itself -- segment table, hexagon segments, the 1's flag -- is
// include/SevenSegmentShapes.h, templated on the canvas so the host tests rasterize the
// exact geometry that ships (test/host/test_sevenseg_shapes.cpp, test_sevenseg_one.cpp).

int SevenSegThickness(int h)
{
    return Thickness(h);
}

void DrawSevenSeg(BandCanvas& c, int x, int y, int w, int h, int digit,
                  uint32_t lit, uint32_t ghost, uint32_t bloom, bool flagOne)
{
    DrawDigit(c, x, y, w, h, digit, lit, ghost, bloom, flagOne);
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

} // namespace sevenseg
