#pragma once
// The sweep over the weather layer (docs/wx-device.md, WD6): looks 3 (see-through) and 4 (lit), as ruled after
// Daniel saw the mockup ("i like 3 or 4"). Pure C++11, no Arduino: test/host/test_wx_sweep.cpp grades it, and
// AircraftManager::DrawSweepOverWeather runs the same code on the canvas buffer.
//
// THE WEDGE IS TODAY'S, PIXEL FOR PIXEL. DrawRadarSweep (include/DrawHelpers.h) fills 18 triangles from the beam
// backwards with LovyanGFX's fillTriangle; this file ports that rasteriser line for line (LGFXBase.cpp fillTriangle
// and its collinear drawLine case) and gives each pixel the level of the LAST slice that covers it -- exactly what
// the opaque wedge leaves on the screen -- so a pixel on a shared edge is blended once, never twice.
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <cmath>

#include "WxS2.h"

namespace wxsweep {

constexpr float kTail = 1.0f;   // DrawRadarSweep's TAIL: the afterglow behind the beam, radians
constexpr int kSegs = 18;       // DrawRadarSweep's SEGS
constexpr int kPeak = 120;      // DrawRadarSweep's PEAK: the green level next to the beam

enum class Look : uint8_t { SeeThrough = 3, Lit = 4 };

/** Slice i's green level (1 = next to the beam, kSegs = the tail), exactly DrawRadarSweep's expression. */
inline uint8_t SliceLevel(int i)
{
    const float frac = i / (float)kSegs;
    const float g = 1.0f - frac;
    return (uint8_t)(g * g * kPeak);
}

/** The opaque wedge's colour at `lvl`: (0, lvl, 0) in RGB565, as fillTriangle stores color888(0, lvl, 0). */
inline uint16_t Green565(uint8_t lvl) { return (uint16_t)((lvl & 0xFC) << 3); }

/** Look 3: the wedge's level added to the pixel's green, clamped at full; red and blue untouched. */
inline uint16_t SeeThrough565(uint16_t c, uint8_t lvl)
{
    const int g8 = ((c >> 5) & 0x3F) << 2;
    const int up = g8 + lvl > 255 ? 255 : g8 + lvl;
    return (uint16_t)((c & ~(0x3F << 5)) | ((up >> 2) << 5));
}

/** Look 4's percent: the look's own at the tail (lvl 0), 100 at the beam (lvl kPeak). */
inline int LitPercent(int pct, uint8_t lvl) { return pct + ((100 - pct) * lvl + kPeak / 2) / kPeak; }

/** Look 4: an echo code (1-13) at its colour raised toward 100 %; anything else the wedge's green, as today. */
inline uint16_t Lit565(uint8_t code, int pct, uint8_t lvl)
{
    // From the code's OWN percent (WD7: light rain's x 55/40), so the tail is exactly the layer's colour.
    if (code >= 1 && code <= 13) return wxs2::To565(wxs2::Pack(wxs2::EchoAt(code, LitPercent(wxs2::CodePercent(code, pct), lvl))));
    return Green565(lvl);
}

namespace detail {
// LGFXBase::writeFillRectPreclipped through a clip of [0, w) x [0, h).
template <typename Put> inline void Span(int x, int y, int len, int w, int h, Put& put)
{
    if (y < 0 || y >= h) return;
    for (int k = 0; k < len; ++k) {
        const int px = x + k;
        if (px >= 0 && px < w) put(px, y);
    }
}

// LGFXBase::drawLine (LGFXBase.cpp:513-588), ported: the clip is the screen.
template <typename Put> inline void Line(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int w, int h, Put& put)
{
    const bool steep = abs(y1 - y0) > abs(x1 - x0);
    int32_t xstart = 0, ystart = 0, xend = w - 1, yend = h - 1;
    if (steep) { int32_t t; t = xstart; xstart = ystart; ystart = t; t = xend; xend = yend; yend = t;
                 t = x0; x0 = y0; y0 = t; t = x1; x1 = y1; y1 = t; }
    if (x0 > x1) { int32_t t = x0; x0 = x1; x1 = t; t = y0; y0 = y1; y1 = t; }
    if (x0 > xend || x1 < xstart) return;
    xend = x1 < xend ? x1 : xend;
    const int32_t dy = abs(y1 - y0);
    const int32_t ystep = (y1 > y0) ? 1 : -1;
    const int32_t dx = x1 - x0;
    int32_t err = dx >> 1;
    while (x0 < xstart || y0 < ystart || y0 > yend) {
        err -= dy;
        if (err < 0) { err += dx; y0 += ystep; }
        if (++x0 > xend) return;
    }
    int32_t xs = x0, dlen = 0;
    if (ystep < 0) { int32_t t = ystart; ystart = yend; yend = t; }
    yend += ystep;
    do {
        ++dlen;
        if ((err -= dy) < 0) {
            if (steep) for (int32_t k = 0; k < dlen; ++k) Span(y0, xs + k, 1, w, h, put);
            else Span(xs, y0, dlen, w, h, put);
            err += dx;
            xs = x0 + 1; dlen = 0; y0 += ystep;
            if (y0 == yend) break;
        }
    } while (++x0 <= xend);
    if (dlen) {
        if (steep) for (int32_t k = 0; k < dlen; ++k) Span(y0, xs + k, 1, w, h, put);
        else Span(xs, y0, dlen, w, h, put);
    }
}

// LGFXBase::fillTriangle (LGFXBase.cpp:600-683), ported; writeFastHLine draws nothing for a width below 1.
template <typename Put> inline void Triangle(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t x2, int32_t y2,
                                             int w, int h, Put& put)
{
    int32_t a, b, t;
    if (y0 > y1) { t = y0; y0 = y1; y1 = t; t = x0; x0 = x1; x1 = t; }
    if (y1 > y2) { t = y2; y2 = y1; y1 = t; t = x2; x2 = x1; x1 = t; }
    if (y0 > y1) { t = y0; y0 = y1; y1 = t; t = x0; x0 = x1; x1 = t; }
    if (y0 == y2) {
        a = b = x0;
        if (x1 < a) a = x1; else if (x1 > b) b = x1;
        if (x2 < a) a = x2; else if (x2 > b) b = x2;
        Span(a, y0, b - a + 1, w, h, put);
        return;
    }
    if ((x1 - x0) * (y2 - y0) == (x2 - x0) * (y1 - y0)) { Line(x0, y0, x2, y2, w, h, put); return; }
    int32_t dy1 = y1 - y0, dy2 = y2 - y0;
    const bool change = ((x1 - x0) * dy2 > (x2 - x0) * dy1);
    int32_t dx1 = abs(x1 - x0), dx2 = abs(x2 - x0);
    int32_t xstep1 = x1 < x0 ? -1 : 1, xstep2 = x2 < x0 ? -1 : 1;
    a = b = x0;
    if (change) { t = dx1; dx1 = dx2; dx2 = t; t = dy1; dy1 = dy2; dy2 = t; t = xstep1; xstep1 = xstep2; xstep2 = t; }
    int32_t err1 = ((dx1 > dy1 ? dx1 : dy1) >> 1) + (xstep1 < 0 ? (dx1 < dy1 ? dx1 : dy1) : dx1);
    int32_t err2 = ((dx2 > dy2 ? dx2 : dy2) >> 1) + (xstep2 > 0 ? (dx2 < dy2 ? dx2 : dy2) : dx2);
    if (y0 != y1) {
        do {
            err1 -= dx1;
            while (err1 < 0) { err1 += dy1; a += xstep1; }
            err2 -= dx2;
            while (err2 < 0) { err2 += dy2; b += xstep2; }
            Span(a, y0, b - a + 1, w, h, put);
        } while (++y0 < y1);
    }
    if (change) {
        b = x1;
        xstep2 = x2 < x1 ? -1 : 1;
        dx2 = abs(x2 - x1);
        dy2 = y2 - y1;
        err2 = ((dx2 > dy2 ? dx2 : dy2) >> 1) + (xstep2 > 0 ? (dx2 < dy2 ? dx2 : dy2) : dx2);
    } else {
        a = x1;
        dx1 = abs(x2 - x1);
        dy1 = y2 - y1;
        xstep1 = x2 < x1 ? -1 : 1;
        err1 = ((dx1 > dy1 ? dx1 : dy1) >> 1) + (xstep1 < 0 ? (dx1 < dy1 ? dx1 : dy1) : dx1);
    }
    do {
        err1 -= dx1;
        while (err1 < 0) { err1 += dy1; if ((a += xstep1) == x2) break; }
        err2 -= dx2;
        while (err2 < 0) { err2 += dy2; if ((b += xstep2) == x2) break; }
        Span(a, y0, b - a + 1, w, h, put);
    } while (++y0 <= y2);
}
}  // namespace detail

/** Marks every pixel of the wedge with the LAST slice (1-kSegs) that covers it, in `slice` (w x h, row-major; 0 =
 *  outside). The slices and their rim points are DrawRadarSweep's, in its order. Returns the pixels marked, and their
 *  bounding rows in *y0 / *y1 so the caller can clear only those.
 *
 *  `rowLo` / `rowHi` (h entries each) receive each row's first and last marked column, or (w, -1) for a row with none,
 *  so Blend visits only that span (the B1 fix, docs/wx-device.md). The wedge is narrower than a half-disc, so each of
 *  its rows is ONE run and the span holds no unmarked byte (sweep_geom: 7,279 span bytes = 7,279 marked, against
 *  27,542 B in whole rows). The span is tracked per run of consecutive pixels, not per pixel. */
inline uint32_t Rasterise(int cx, int cy, int radius, float angle, uint8_t* slice, int w, int h, int* yLo, int* yHi,
                          int16_t* rowLo, int16_t* rowHi)
{
    uint32_t marked = 0;
    int lo = h, hi = -1;
    int cur = 0;
    for (int y = 0; y < h; ++y) { rowLo[y] = (int16_t)w; rowHi[y] = -1; }
    int runY = -1, runLo = 0, runHi = -1;
    auto flush = [&]() {
        if (runY < 0) return;
        if (runLo < rowLo[runY]) rowLo[runY] = (int16_t)runLo;
        if (runHi > rowHi[runY]) rowHi[runY] = (int16_t)runHi;
    };
    auto put = [&](int x, int y) {
        uint8_t& s = slice[(size_t)y * w + x];
        if (s == 0) ++marked;
        s = (uint8_t)cur;
        if (y < lo) lo = y;
        if (y > hi) hi = y;
        if (y != runY || x != runHi + 1) { flush(); runY = y; runLo = x; }
        runHi = x;
    };
    const int xLead = cx + (int)(std::cos(angle) * radius);
    const int yLead = cy + (int)(std::sin(angle) * radius);
    int xPrev = xLead, yPrev = yLead;
    for (int i = 1; i <= kSegs; i++) {
        const float frac = i / (float)kSegs;
        const float a = angle - frac * kTail;
        const int x = cx + (int)(std::cos(a) * radius);
        const int y = cy + (int)(std::sin(a) * radius);
        cur = i;
        detail::Triangle(cx, cy, xPrev, yPrev, x, y, w, h, put);
        xPrev = x;
        yPrev = y;
    }
    flush();
    *yLo = lo;
    *yHi = hi;
    return marked;
}

namespace detail {
// One wedge pixel's colour, shared by both loops below so they cannot disagree about it.
template <typename Get, typename Set, typename Code>
inline void BlendOne(Look look, int pct, uint8_t mark, int x, int y, Get& get, Set& set, Code& code)
{
    const uint8_t lvl = SliceLevel(mark);
    if (look == Look::SeeThrough) set(x, y, SeeThrough565(get(x, y), lvl));
    else set(x, y, Lit565(code(x, y), pct, lvl));
}
}  // namespace detail

/** Blends the wedge marked in `slice` over the frame, once per pixel, then clears those marks, visiting only each
 *  row's span from Rasterise (`rowLo` / `rowHi`). `get(x, y)` and `set(x, y, c)` read and write logical RGB565 at screen
 *  coordinates; `code(x, y)` is the layer's code there. `pct` is the layer's look percent (WxS2). Returns the pixels
 *  blended. The device's path. */
template <typename Get, typename Set, typename Code>
inline uint32_t Blend(Look look, int pct, uint8_t* slice, int w, int yLo, int yHi, const int16_t* rowLo,
                      const int16_t* rowHi, Get&& get, Set&& set, Code&& code)
{
    uint32_t n = 0;
    for (int y = yLo; y <= yHi; ++y) {
        uint8_t* row = slice + (size_t)y * w;
        for (int x = rowLo[y]; x <= rowHi[y]; ++x) {
            if (row[x] == 0) continue;
            detail::BlendOne(look, pct, row[x], x, y, get, set, code);
            row[x] = 0;
            ++n;
        }
    }
    return n;
}

/** The same blend over WHOLE rows, as it was before the B1 fix: the host test's reference for Blend, and the bench's
 *  measure of what scanning an empty byte costs. Not the device's path. */
template <typename Get, typename Set, typename Code>
inline uint32_t BlendRows(Look look, int pct, uint8_t* slice, int w, int yLo, int yHi, Get&& get, Set&& set, Code&& code)
{
    uint32_t n = 0;
    for (int y = yLo; y <= yHi; ++y) {
        uint8_t* row = slice + (size_t)y * w;
        for (int x = 0; x < w; ++x) {
            if (row[x] == 0) continue;
            detail::BlendOne(look, pct, row[x], x, y, get, set, code);
            row[x] = 0;
            ++n;
        }
    }
    return n;
}

}  // namespace wxsweep
