// P5 W's host rows (REG-W section 2 and amendment 1): the rain outline drawn once (include/OutlineW.h) against the 8
// full black copies it replaces, over the marker's own geometry (include/MarkerDraw.h, the device's template, compiled
// here as it is there). The canvas is LovyanGFX 1.2.21's clip semantics, through OutlineW.h's ONE port of its
// algorithms, so the original and W cannot run different rasterisers. Whether that port is LovyanGFX's real raster is
// the device's question: REG-W's W-CHECK answers it on the board.
//
//   test_p5_outline      exit 0 every row ok, 1 a row failed, 2 the rig (also: not 4 rows)
//
// The population (REG-W section 2, the prototype's): the dot and the triangle in categories 0 (standard), 9 (glider),
// 4 (heavy), 8 (rotorcraft) and 10 (balloon), at radar-up 0, 37, 90, 180 and 271, 3,600 headings (0.1 deg), and 20 x
// positions each paired with a y from the same list, off-canvas and edge ones included: 2,160,000 cases, each over a
// NOISE canvas, so a pixel missed or added shows. Rows (sabotage_p5_outline.py reads the ids):
//   CMP control   the comparison sees one changed pixel
//   W-EQ          every case byte-identical, the whole canvas compared
//   W-FIT         0 pixels written outside a 32 x 32 mask
//   W-CALLS       the canvas's draw calls (hline + fill), the marker included in both: the prototype's exact counts
//   W-STAMP       REG-W-P1 L2: each fixed-circle table equals this header's own algorithm, bit for bit, at 9 centres
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

// REG-W-P1 L3: the mask counts the pixels a copy wrote outside it only where this is defined (W-FIT reads it).
#define OUTLINEW_COUNT_OUTSIDE 1
#include "MarkerDraw.h"
#include "OutlineW.h"

namespace {

constexpr int W = 240, H = 240;

// LovyanGFX's sprite as the outline's draws see it: clip 0..239 on both axes, one colour at a time.
struct CanvasSink {
    std::vector<uint32_t> px = std::vector<uint32_t>(W * H, 0);
    uint32_t color = 0;
    long hlines = 0, fills = 0;   // the cost comparison, as the prototype counted
    int32_t cl = 0, ct = 0, cr = W - 1, cb = H - 1;
    int32_t bx0 = W, by0 = H, bx1 = -1, by1 = -1;   // the box of every pixel written since the last Reset
    template <typename T> void SetColor(const T& c) { color = (uint32_t)c; }
    void fillPre(int32_t x, int32_t y, int32_t w, int32_t h)   // writeFillRectPreclipped
    {
        ++fills;
        if (w > 0 && h > 0) {
            bx0 = std::min(bx0, x); by0 = std::min(by0, y);
            bx1 = std::max(bx1, x + w - 1); by1 = std::max(by1, y + h - 1);
        }
        for (int32_t j = 0; j < h; ++j)
            for (int32_t i = 0; i < w; ++i) px[(size_t)(y + j) * W + (size_t)(x + i)] = color;
    }
    void writeFastHLine(int32_t x, int32_t y, int32_t w)   // :186
    {
        ++hlines;
        if (y < ct || y > cb) return;
        if (x < cl) { w += x - cl; x = cl; }
        const int32_t r = cr + 1 - x;
        if (w > r) w = r;
        if (w < 1) return;
        fillPre(x, y, w, 1);
    }
    void writeFastVLine(int32_t x, int32_t y, int32_t h)   // :166
    {
        if (x < cl || x > cr) return;
        if (y < ct) { h += y - ct; y = ct; }
        const int32_t b = cb + 1 - y;
        if (h > b) h = b;
        if (h < 1) return;
        fillPre(x, y, 1, h);
    }
    void writeFillRect(int32_t x, int32_t y, int32_t w, int32_t h)   // clip both axes, then fill
    {
        if (x < cl) { w += x - cl; x = cl; }
        if (w > cr + 1 - x) w = cr + 1 - x;
        if (y < ct) { h += y - ct; y = ct; }
        if (h > cb + 1 - y) h = cb + 1 - y;
        if (w < 1 || h < 1) return;
        fillPre(x, y, w, h);
    }
};
using Canvas = outlinew::Raster<CanvasSink>;

struct Marker {
    int cat;          // tracked.state.category
    float track;      // tracked.state.trueTrack
    int radarUp;      // radarUpDeg
    bool triangles;   // displayTriangles (false: the dot)
};

// One marker at (x, y) on any target, as AircraftManager draws it: the template, or the dot.
template <typename T>
void DrawMarker(T& t, int x, int y, const Marker& m, uint32_t col)
{
    if (m.triangles) markerdraw::Aircraft(t, x, y, m.cat, m.track, m.radarUp, col);
    else t.fillCircle(x, y, 3, col);
}

// TODAY'S CODE, the reference: 8 black copies, then the marker in its colour.
void Original(Canvas& c, int x, int y, const Marker& m, uint32_t col)
{
    for (int oy = -1; oy <= 1; ++oy)
        for (int ox = -1; ox <= 1; ++ox) {
            if (ox == 0 && oy == 0) continue;
            DrawMarker(c, x + ox, y + oy, m, 0u);
        }
    DrawMarker(c, x, y, m, col);
}

// W, as AircraftManager calls it.
uint32_t WOutline(Canvas& c, int x, int y, const Marker& m, uint32_t col)
{
    return outlinew::Draw(
        c, x, y, 0u, [&](outlinew::Mask& k, int cx, int cy) { DrawMarker(k, cx, cy, m, 0u); },
        [&]() { DrawMarker(c, x, y, m, col); });
}

int rows = 0, failures = 0;
void Row(bool ok, const char* id, const std::string& what)
{
    ++rows;
    if (!ok) ++failures;
    std::printf("  %s  %s  %s\n", ok ? "ok  " : "FAIL", id, what.c_str());
}

}  // namespace

int main()
{
    {
        std::vector<uint32_t> a(16, 7), b(16, 7);
        b[5] = 8;
        Row(a != b, "CMP control", std::string("one changed pixel ") + (a != b ? "reads a difference" : "reads the same"));
    }
    std::vector<uint32_t> noise(W * H);
    uint32_t s = 12345;
    for (auto& v : noise) { s = s * 1664525u + 1013904223u; v = (s >> 8) | 1u; }
    // Two canvases kept for the whole run. After each case only the union of their written boxes is compared and
    // then restored from the noise: outside that box both are untouched noise by construction, so this is the
    // whole-canvas comparison, without copying 2 x 230 KB per case.
    Canvas a, b;
    a.px = noise;
    b.px = noise;
    const int cats[] = {0, 9, 4, 8, 10};
    const int ups[] = {0, 37, 90, 180, 271};
    const int xs[] = {-14, -11, -9, -2, 0, 1, 2, 9, 10, 57, 118, 119, 120, 231, 237, 238, 239, 240, 248, 253};
    long cases = 0, same = 0, origCalls = 0, wCalls = 0;
    unsigned long outside = 0;
    for (int tri = 0; tri < 2; ++tri)
        for (int cat : cats) {
            if (!tri && cat != 0) continue;   // the dot ignores the category
            for (int up : ups)
                for (int h = 0; h < 3600; ++h) {
                    const Marker m{cat, h * 0.1f, up, tri == 1};
                    for (int xi : xs) {
                        const int yi = xs[(h + xi + 40) % 20];
                        Original(a, xi, yi, m, 0x00FF00u);
                        outside += WOutline(b, xi, yi, m, 0x00FF00u);
                        ++cases;
                        const int32_t x0 = std::min(a.bx0, b.bx0), y0 = std::min(a.by0, b.by0);
                        const int32_t x1 = std::max(a.bx1, b.bx1), y1 = std::max(a.by1, b.by1);
                        bool eq = true;
                        for (int32_t yy = y0; yy <= y1; ++yy)
                            for (int32_t xx = x0; xx <= x1; ++xx) {
                                const size_t i = (size_t)yy * W + (size_t)xx;
                                if (a.px[i] != b.px[i]) eq = false;
                                a.px[i] = noise[i];
                                b.px[i] = noise[i];
                            }
                        if (eq) ++same;
                        a.bx0 = b.bx0 = W; a.by0 = b.by0 = H; a.bx1 = b.bx1 = -1; a.by1 = b.by1 = -1;
                    }
                }
        }
    Row(same == cases && cases == 2160000, "W-EQ",
        std::to_string(same) + " of " + std::to_string(cases) + " cases byte-identical (want all 2160000)");
    Row(outside == 0, "W-FIT", std::to_string(outside) + " pixel(s) written outside a 32 x 32 mask (want 0)");
    origCalls = a.hlines + a.fills;
    wCalls = b.hlines + b.fills;
    // The instrument's own check, not a graded row: a restore that left a pixel behind would let one case's
    // drawing leak into the next one's comparison.
    if (a.px != noise || b.px != noise) {
        std::printf("the RIG: a canvas was not restored to the noise\n");
        return 2;
    }
    Row(origCalls == 239740224L && wCalls == 65787219L, "W-CALLS",
        "canvas calls (hline + fill): original " + std::to_string(origCalls) + ", W " + std::to_string(wCalls) +
            " (want the prototype's 239740224 and 65787219)");
    {
        // W-STAMP (REG-W-P1 L2): the Mask's stamp path against the algorithm's general path, into two masks, per circle
        // and centre; the rows compared whole. The general path is called by name, so the stamps cannot be compared
        // with themselves.
        int eqs = 0, tried = 0;
        const int cs[] = {14, 16, 18};
        for (int kind = 0; kind < 3; ++kind)
            for (int cx : cs)
                for (int cy : cs) {
                    outlinew::Mask g, t;
                    g.Begin(16, 16);
                    t.Begin(16, 16);
                    const int r = kind == 0 ? 2 : (kind == 1 ? 3 : 4);
                    if (kind < 2) {
                        g.writeFastHLine(cx - r, cy, (r << 1) + 1);
                        g.CircleHelper(cx, cy, r, 3, 0);
                        t.fillCircle(cx, cy, r, 0u);
                    } else {
                        g.Circle(cx, cy, r);
                        t.drawCircle(cx, cy, r, 0u);
                    }
                    bool same = true;
                    for (int k = 0; k < outlinew::kRows; ++k) same = same && g.row[k] == t.row[k];
                    eqs += same ? 1 : 0;
                    ++tried;
                }
        Row(eqs == tried && tried == 27, "W-STAMP",
            std::to_string(eqs) + " of " + std::to_string(tried) + " stamps equal the algorithm bit for bit (want 27)");
    }
    std::printf("%s\n", failures ? "P5 OUTLINE TESTS FAILED" : "all P5 outline checks passed");
    // The row count is pinned in the binary: a row dropped or compiled out is the RIG.
    if (rows != 5) {
        std::printf("the RIG: %d rows, not 5\n", rows);
        return 2;
    }
    return failures ? 1 : 0;
}
