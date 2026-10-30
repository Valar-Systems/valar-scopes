#pragma once
// The sweep over the weather layer (docs/wx-device.md, WD6): look 4, lit, Daniel's pick ("i like 4: lit"). Look 3,
// see-through, was retired on 2026-10-30: on the glass Daniel found no difference, and it cost the same within 0.1 ms.
// Pure C++11, no Arduino: test/host/test_wx_sweep.cpp grades it, and AircraftManager::DrawSweepOverWeather runs the
// same code on the canvas buffer.
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

/** Slice i's green level (1 = next to the beam, kSegs = the tail), exactly DrawRadarSweep's expression. */
inline uint8_t SliceLevel(int i)
{
    const float frac = i / (float)kSegs;
    const float g = 1.0f - frac;
    return (uint8_t)(g * g * kPeak);
}

/** The opaque wedge's colour at `lvl`: (0, lvl, 0) in RGB565, as fillTriangle stores color888(0, lvl, 0). */
inline uint16_t Green565(uint8_t lvl) { return (uint16_t)((lvl & 0xFC) << 3); }

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
// LGFXBase::writeFillRectPreclipped through a clip of [0, w) x [0, h). The same pixels in the same order as a per-pixel
// clip; then, once per span, `put.Spanned(x0, x1, y)` with the clipped range (the span change, docs/wx-device.md). Every
// marked pixel comes through here, the degenerate-line case included.
template <typename Put> inline void Span(int x, int y, int len, int w, int h, Put& put)
{
    if (y < 0 || y >= h) return;
    const int x0 = x < 0 ? 0 : x;
    const int x1 = x + len - 1 < w ? x + len - 1 : w - 1;
    for (int px = x0; px <= x1; ++px) put(px, y);
    if (x1 >= x0) put.Spanned(x0, x1, y);
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
// Rasterise's marker: per pixel, exactly what it did before any span was recorded (mark, count, the rows); once per
// span, the row's first and last marked column.
struct Marker {
    uint8_t* slice;
    int w;
    int16_t* rowLo;
    int16_t* rowHi;
    uint32_t marked;
    int lo, hi;
    uint8_t cur;
    void operator()(int x, int y)
    {
        uint8_t& s = slice[(size_t)y * w + x];
        if (s == 0) ++marked;
        s = cur;
        if (y < lo) lo = y;
        if (y > hi) hi = y;
    }
    void Spanned(int x0, int x1, int y)
    {
        if (x0 < rowLo[y]) rowLo[y] = (int16_t)x0;
        if (x1 > rowHi[y]) rowHi[y] = (int16_t)x1;
    }
};
}  // namespace detail

/** Marks every pixel of the wedge with the LAST slice (1-kSegs) that covers it, in `slice` (w x h, row-major; 0 =
 *  outside). The slices and their rim points are DrawRadarSweep's, in its order. Returns the pixels marked, and their
 *  bounding rows in *y0 / *y1 so the caller can clear only those.
 *
 *  `rowLo` / `rowHi` (h entries each) receive each row's first and last marked column, or (w, -1) for a row with none,
 *  so Blend visits only that span (the B1 fix, docs/wx-device.md). The wedge is narrower than a half-disc, so each of
 *  its rows is ONE run and the span holds no unmarked byte (sweep_geom: 7,279 span bytes = 7,279 marked, against
 *  27,542 B in whole rows). The span is recorded once per Span call (the span change), not per pixel: the per-pixel
 *  form cost about 1 ms a frame on the board. */
inline uint32_t Rasterise(int cx, int cy, int radius, float angle, uint8_t* slice, int w, int h, int* yLo, int* yHi,
                          int16_t* rowLo, int16_t* rowHi)
{
    for (int y = 0; y < h; ++y) { rowLo[y] = (int16_t)w; rowHi[y] = -1; }
    detail::Marker put{slice, w, rowLo, rowHi, 0u, h, -1, 0};
    const int xLead = cx + (int)(std::cos(angle) * radius);
    const int yLead = cy + (int)(std::sin(angle) * radius);
    int xPrev = xLead, yPrev = yLead;
    for (int i = 1; i <= kSegs; i++) {
        const float frac = i / (float)kSegs;
        const float a = angle - frac * kTail;
        const int x = cx + (int)(std::cos(a) * radius);
        const int y = cy + (int)(std::sin(a) * radius);
        put.cur = (uint8_t)i;
        detail::Triangle(cx, cy, xPrev, yPrev, x, y, w, h, put);
        xPrev = x;
        yPrev = y;
    }
    *yLo = put.lo;
    *yHi = put.hi;
    return put.marked;
}

namespace detail {
// One wedge pixel's colour, shared by both loops below so they cannot disagree about it.
template <typename Set, typename Code>
inline void BlendOne(int pct, uint8_t mark, int x, int y, Set& set, Code& code)
{
    const uint8_t lvl = SliceLevel(mark);
    set(x, y, Lit565(code(x, y), pct, lvl));
}
}  // namespace detail

/** Blends the wedge marked in `slice` over the frame, once per pixel, then clears those marks, visiting only each
 *  row's span from Rasterise (`rowLo` / `rowHi`). `set(x, y, c)` writes logical RGB565 at screen coordinates;
 *  `code(x, y)` is the layer's code there. `pct` is the layer's look percent (WxS2). Returns the pixels blended. The
 *  device's path. */
template <typename Set, typename Code>
inline uint32_t Blend(int pct, uint8_t* slice, int w, int yLo, int yHi, const int16_t* rowLo, const int16_t* rowHi,
                      Set&& set, Code&& code)
{
    uint32_t n = 0;
    for (int y = yLo; y <= yHi; ++y) {
        uint8_t* row = slice + (size_t)y * w;
        for (int x = rowLo[y]; x <= rowHi[y]; ++x) {
            if (row[x] == 0) continue;
            detail::BlendOne(pct, row[x], x, y, set, code);
            row[x] = 0;
            ++n;
        }
    }
    return n;
}

/** The same blend over WHOLE rows, as it was before the B1 fix: the host test's reference for Blend, and the bench's
 *  measure of what scanning an empty byte costs. Not the device's path. */
template <typename Set, typename Code>
inline uint32_t BlendRows(int pct, uint8_t* slice, int w, int yLo, int yHi, Set&& set, Code&& code)
{
    uint32_t n = 0;
    for (int y = yLo; y <= yHi; ++y) {
        uint8_t* row = slice + (size_t)y * w;
        for (int x = 0; x < w; ++x) {
            if (row[x] == 0) continue;
            detail::BlendOne(pct, row[x], x, y, set, code);
            row[x] = 0;
            ++n;
        }
    }
    return n;
}

}  // namespace wxsweep
