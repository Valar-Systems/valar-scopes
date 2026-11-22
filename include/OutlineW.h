#pragma once
// P5 W (REG-W): the rain outline drawn ONCE. With the weather layer on, every contact gets a 1 px black edge, which
// was 8 full black copies of the marker at the 8 one-pixel offsets, then the marker. W rasterises the 8 copies, with
// exactly the integer arguments each copy's draw call received (include/MarkerDraw.h, the same geometry), into a
// 32 x 32 bitmask on the stack. It ORs them, paints the union as runs of black through the canvas's own clipped
// drawFastHLine, then draws the marker. The pixel set written black is the same, in the same colour, and every black
// pixel still lands before the marker, so the frame is byte-identical by construction.
//
// The mask's rasterisers are LovyanGFX 1.2.21's own, ported line for line (LGFXBase.cpp f1b00e921736: fillTriangle
// :600, drawLine :513, writeFastHLine :186, writeFastVLine :166, fillCircle and fillCircleHelper, drawCircle :230), with
// the clip off: drawLine's on-screen pixels do not depend on the clip, and the runs go through the canvas's clip.
// NO heap, NO PSRAM, no allocation of any kind: the mask is a local (REG-W section 3; run.sh's W heap lint).
// Pure C++11: test/host/test_p5_outline.cpp grades it (W-EQ, W-FIT, W-CALLS); sabotage_p5_outline.py plants it.
#include <stdint.h>
#include <string.h>

#include <algorithm>
#include <cstdlib>

namespace outlinew {

constexpr int kRows = 32;   // the mask's rows, and its 32 columns are one uint32_t each

/** The mask's pixel sink: the 8 copies' union around one contact. Row r, bit c is the pixel (x0 + c, y0 + r). No clip:
 *  drawLine's on-screen pixels do not depend on the clip, and the runs are painted through the canvas's own clip. */
struct MaskSink {
    uint32_t row[kRows];
    int32_t x0, y0;
    uint32_t outside;   // pixels a copy wrote outside the window: W-FIT reads 0
    int32_t cl = -1000000, ct = -1000000, cr = 1000000, cb = 1000000;   // the clip, off

    void Begin(int x, int y)
    {
        memset(row, 0, sizeof row);
        x0 = x - 16;
        y0 = y - kRows / 2;
        outside = 0;
    }
    template <typename T> void SetColor(const T&) {}   // one colour: the bits are the black set

    // LovyanGFX's writeFillRectPreclipped, into bits
    void fillPre(int32_t x, int32_t y, int32_t w, int32_t h)
    {
        for (int32_t j = 0; j < h; ++j) {
            const int32_t ly = y + j - y0;
            for (int32_t i = 0; i < w; ++i) {
                const int32_t lx = x + i - x0;
                if (lx < 0 || lx >= 32 || ly < 0 || ly >= kRows) { ++outside; continue; }
                row[ly] |= 1u << lx;
            }
        }
    }
    void writeFastHLine(int32_t x, int32_t y, int32_t w) { if (w < 1) return; fillPre(x, y, w, 1); }
    void writeFastVLine(int32_t x, int32_t y, int32_t h) { if (h < 1) return; fillPre(x, y, 1, h); }
    void writeFillRect(int32_t x, int32_t y, int32_t w, int32_t h) { if (w < 1 || h < 1) return; fillPre(x, y, w, h); }
};

/** LovyanGFX 1.2.21's algorithms, line for line, over any sink with fillPre / writeFastHLine / writeFastVLine /
 *  writeFillRect, SetColor and a clip (cl, ct, cr, cb). ONE port: the mask runs it unclipped, and the host test's canvas
 *  runs it clipped, so the two paths cannot run different algorithms. The four calls the marker makes take
 *  BandCanvas's parameter types (int32_t), so every float argument converts at the same boundary as on the canvas. */
template <typename Sink>
struct Raster : Sink {
    using Sink::fillPre;
    using Sink::writeFastHLine;
    using Sink::writeFastVLine;
    using Sink::writeFillRect;

    template <typename T>
    void drawLine(int32_t x0_, int32_t y0_, int32_t x1, int32_t y1, const T& color) { this->SetColor(color); Line(x0_, y0_, x1, y1); }
    template <typename T>
    void fillTriangle(int32_t xa, int32_t ya, int32_t xb, int32_t yb, int32_t xc, int32_t yc, const T& color)
    {
        this->SetColor(color);
        Triangle(xa, ya, xb, yb, xc, yc);
    }
    template <typename T>
    void fillCircle(int32_t x, int32_t y, int32_t r, const T& color)
    {
        this->SetColor(color);
        writeFastHLine(x - r, y, (r << 1) + 1);
        CircleHelper(x, y, r, 3, 0);
    }
    template <typename T>
    void drawCircle(int32_t x, int32_t y, int32_t r, const T& color) { this->SetColor(color); Circle(x, y, r); }
    template <typename T>
    void drawFastHLine(int32_t x, int32_t y, int32_t w, const T& color) { this->SetColor(color); HLine(x, y, w); }
    void HLine(int32_t x, int32_t y, int32_t w) { if (w < 0) { x += w + 1; w = -w; } writeFastHLine(x, y, w); }   // :178

    // ---- LovyanGFX's algorithms, line for line, reading the sink's clip (the mask's is off) ----
    void Line(int32_t xa, int32_t ya, int32_t xb, int32_t yb)
    {
        const bool steep = std::abs(yb - ya) > std::abs(xb - xa);
        int32_t xstart = this->cl, ystart = this->ct, xend = this->cr, yend = this->cb;
        if (steep) { std::swap(xstart, ystart); std::swap(xend, yend); std::swap(xa, ya); std::swap(xb, yb); }
        if (xa > xb) { std::swap(xa, xb); std::swap(ya, yb); }
        if (xa > xend || xb < xstart) return;
        xend = std::min(xb, xend);
        int32_t dy = std::abs(yb - ya), ystep = (yb > ya) ? 1 : -1, dx = xb - xa, err = dx >> 1;
        while (xa < xstart || ya < ystart || ya > yend) {
            err -= dy;
            if (err < 0) { err += dx; ya += ystep; }
            if (++xa > xend) return;
        }
        int32_t xs = xa, dlen = 0;
        if (ystep < 0) std::swap(ystart, yend);
        yend += ystep;
        if (steep) {
            do {
                ++dlen;
                if ((err -= dy) < 0) { fillPre(ya, xs, 1, dlen); err += dx; xs = xa + 1; dlen = 0; ya += ystep; if (ya == yend) break; }
            } while (++xa <= xend);
            if (dlen) fillPre(ya, xs, 1, dlen);
        } else {
            do {
                ++dlen;
                if ((err -= dy) < 0) { fillPre(xs, ya, dlen, 1); err += dx; xs = xa + 1; dlen = 0; ya += ystep; if (ya == yend) break; }
            } while (++xa <= xend);
            if (dlen) fillPre(xs, ya, dlen, 1);
        }
    }
    void Triangle(int32_t xa, int32_t ya, int32_t xb, int32_t yb, int32_t xc, int32_t yc)
    {
        int32_t a, b;
        if (ya > yb) { std::swap(ya, yb); std::swap(xa, xb); }
        if (yb > yc) { std::swap(yc, yb); std::swap(xc, xb); }
        if (ya > yb) { std::swap(ya, yb); std::swap(xa, xb); }
        if (ya == yc) {
            a = b = xa;
            if (xb < a) a = xb; else if (xb > b) b = xb;
            if (xc < a) a = xc; else if (xc > b) b = xc;
            HLine(a, ya, b - a + 1);
            return;
        }
        if ((xb - xa) * (yc - ya) == (xc - xa) * (yb - ya)) { Line(xa, ya, xc, yc); return; }
        int32_t dy1 = yb - ya, dy2 = yc - ya;
        const bool change = ((xb - xa) * dy2 > (xc - xa) * dy1);
        int32_t dx1 = std::abs(xb - xa), dx2 = std::abs(xc - xa);
        int32_t xstep1 = xb < xa ? -1 : 1, xstep2 = xc < xa ? -1 : 1;
        a = b = xa;
        if (change) { std::swap(dx1, dx2); std::swap(dy1, dy2); std::swap(xstep1, xstep2); }
        int32_t err1 = (std::max(dx1, dy1) >> 1) + (xstep1 < 0 ? std::min(dx1, dy1) : dx1);
        int32_t err2 = (std::max(dx2, dy2) >> 1) + (xstep2 > 0 ? std::min(dx2, dy2) : dx2);
        if (ya != yb) {
            do {
                err1 -= dx1;
                while (err1 < 0) { err1 += dy1; a += xstep1; }
                err2 -= dx2;
                while (err2 < 0) { err2 += dy2; b += xstep2; }
                writeFastHLine(a, ya, b - a + 1);
            } while (++ya < yb);
        }
        if (change) {
            b = xb; xstep2 = xc < xb ? -1 : 1; dx2 = std::abs(xc - xb); dy2 = yc - yb;
            err2 = (std::max(dx2, dy2) >> 1) + (xstep2 > 0 ? std::min(dx2, dy2) : dx2);
        } else {
            a = xb; dx1 = std::abs(xc - xb); dy1 = yc - yb; xstep1 = xc < xb ? -1 : 1;
            err1 = (std::max(dx1, dy1) >> 1) + (xstep1 < 0 ? std::min(dx1, dy1) : dx1);
        }
        do {
            err1 -= dx1;
            while (err1 < 0) { err1 += dy1; if ((a += xstep1) == xc) break; }
            err2 -= dx2;
            while (err2 < 0) { err2 += dy2; if ((b += xstep2) == xc) break; }
            writeFastHLine(a, ya, b - a + 1);
        } while (++ya <= yc);
    }
    void CircleHelper(int32_t x, int32_t y, int32_t r, unsigned corners, int32_t delta)
    {
        if (r <= 0) return;
        ++delta;
        int32_t f = 1 - r, ddF_y = -(r << 1), ddF_x = 1, i = 0;
        do {
            int32_t len = 0;
            while (f < 0) { f += (ddF_x += 2); ++len; }
            i += len;
            f += (ddF_y += 2);
            if (corners & 0x1) {
                if (len) writeFillRect(x - r, y + i - len + 1, (r << 1) + delta, len);
                writeFastHLine(x - i, y + r, (i << 1) + delta);
            }
            if (corners & 0x2) {
                writeFastHLine(x - i, y - r, (i << 1) + delta);
                if (len) writeFillRect(x - r, y - i, (r << 1) + delta, len);
            }
        } while (i < --r);
    }
    void Circle(int32_t x, int32_t y, int32_t r)
    {
        if (r <= 0) { writeFastHLine(x, y, 1); return; }
        int32_t f = 1 - r, ddF_y = -(r << 1), ddF_x = 1, i = 0, j = -1;
        do {
            while (f < 0) { ++i; f += (ddF_x += 2); }
            f += (ddF_y += 2);
            writeFastHLine(x - i, y + r, i - j); writeFastHLine(x - i, y - r, i - j);
            writeFastHLine(x + j + 1, y - r, i - j); writeFastHLine(x + j + 1, y + r, i - j);
            writeFastVLine(x + r, y + j + 1, i - j); writeFastVLine(x + r, y - i, i - j);
            writeFastVLine(x - r, y - i, i - j); writeFastVLine(x - r, y + j + 1, i - j);
            j = i;
        } while (i < --r);
    }
};

using Mask = Raster<MaskSink>;   // the outline's local raster: unclipped, on the stack

/** Paints the mask's set as horizontal runs of `color` through the canvas's own clipped drawFastHLine. */
template <typename Canvas, typename Color>
inline void Paint(const Mask& m, Canvas& c, const Color& color)
{
    for (int r = 0; r < kRows; ++r) {
        uint32_t bits = m.row[r];
        while (bits) {
            const int s = __builtin_ctz(bits);
            const uint32_t run = bits >> s;
            const int len = (~run == 0u) ? 32 : __builtin_ctz(~run);   // the run's trailing ones
            c.drawFastHLine(m.x0 + s, m.y0 + r, len, color);
            bits &= ~(((len == 32) ? 0xFFFFFFFFu : ((1u << len) - 1u)) << s);
        }
    }
}

/** One contact's outline, then its marker: `copy(mask, cx, cy)` rasterises one copy at (cx, cy) into the mask, exactly as
 *  the marker draws at (cx, cy); `marker()` draws the marker itself on the canvas, after the black. Returns the pixels a
 *  copy wrote outside the mask (0: W-FIT). */
template <typename Canvas, typename Color, typename Copy, typename Marker>
inline uint32_t Draw(Canvas& c, int x, int y, const Color& black, Copy&& copy, Marker&& marker)
{
    Mask m;
    m.Begin(x, y);
    for (int oy = -1; oy <= 1; ++oy)
        for (int ox = -1; ox <= 1; ++ox) {
            if (ox == 0 && oy == 0) continue;
            copy(m, x + ox, y + oy);
        }
    Paint(m, c, black);
    marker();
    return m.outside;
}

}  // namespace outlinew
