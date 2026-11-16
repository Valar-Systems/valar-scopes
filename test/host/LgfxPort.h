#pragma once
// LovyanGFX 1.2.21's integer raster, ported line for line from the pinned copy in .pio/libdeps
// (src/lgfx/v1/LGFXBase.cpp: writeFastHLine 186-196, writeFastVLine 166-176, writeFillRect 207-213 with _clipping
// LGFXBase.hpp:1069-1083, drawFastHLine 178-184, drawCircle 230-262, fillCircle 302-307, fillCircleHelper 309-339,
// drawLine 513-589, fillTriangle 600-681). Host only: the stand-in for the device's LovyanGFX in the P5 1a host rows
// (test_p5_outline.cpp). Whether it equals the device is P5 1a's DEVICE ANCHOR row (docs/speed-p5p6.md), not this file.
//
// A Port<Px> draws onto any pixel store Px with Px::Set(x, y, color), clipped to [cl, cr] x [ct, cb] as LovyanGFX
// clips. Colours are passed through unchanged.
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <utility>

namespace lgfxport {

template <class Px>
struct Port {
    Px& px;
    int32_t _clip_l, _clip_t, _clip_r, _clip_b;
    uint32_t color = 0;

    Port(Px& p, int32_t l, int32_t t, int32_t r, int32_t b) : px(p), _clip_l(l), _clip_t(t), _clip_r(r), _clip_b(b) {}

    static bool _adjust_abs(int32_t& x, int32_t& w) { if (w < 0) { x += w; w = -w; } return !w; }

    void writeFillRectPreclipped(int32_t x, int32_t y, int32_t w, int32_t h)
    {
        for (int32_t j = 0; j < h; ++j)
            for (int32_t i = 0; i < w; ++i) px.Set(x + i, y + j, color);
    }

    bool _clipping(int32_t& x, int32_t& y, int32_t& w, int32_t& h)
    {
        auto cl = _clip_l;
        if (x < cl) { w += x - cl; x = cl; }
        auto cr = _clip_r + 1 - x;
        if (w > cr) w = cr;
        if (w < 1) return false;

        auto ct = _clip_t;
        if (y < ct) { h += y - ct; y = ct; }
        auto cb = _clip_b + 1 - y;
        if (h > cb) h = cb;
        if (h < 1) return false;
        return true;
    }

    void drawPixel(int32_t x, int32_t y)
    {
        if (x < _clip_l || x > _clip_r || y < _clip_t || y > _clip_b) return;
        writeFillRectPreclipped(x, y, 1, 1);
    }

    void writeFastVLine(int32_t x, int32_t y, int32_t h)
    {
        if (x < _clip_l || x > _clip_r) return;
        auto ct = _clip_t;
        if (y < ct) { h += y - ct; y = ct; }
        auto cb = _clip_b + 1 - y;
        if (h > cb) h = cb;
        if (h < 1) return;

        writeFillRectPreclipped(x, y, 1, h);
    }

    void drawFastHLine(int32_t x, int32_t y, int32_t w)
    {
        _adjust_abs(x, w);
        writeFastHLine(x, y, w);
    }

    void writeFastHLine(int32_t x, int32_t y, int32_t w)
    {
        if (y < _clip_t || y > _clip_b) return;
        auto cl = _clip_l;
        if (x < cl) { w += x - cl; x = cl; }
        auto cr = _clip_r + 1 - x;
        if (w > cr) w = cr;
        if (w < 1) return;

        writeFillRectPreclipped(x, y, w, 1);
    }

    void writeFillRect(int32_t x, int32_t y, int32_t w, int32_t h)
    {
        if (_clipping(x, y, w, h))
        {
            writeFillRectPreclipped(x, y, w, h);
        }
    }

    void drawCircle(int32_t x, int32_t y, int32_t r)
    {
        if ( r <= 0 ) {
            drawPixel(x, y);
            return;
        }

        int32_t f = 1 - r;
        int32_t ddF_y = - (r << 1);
        int32_t ddF_x = 1;
        int32_t i = 0;
        int32_t j = -1;
        do {
            while (f < 0) {
                ++i;
                f += (ddF_x += 2);
            }
            f += (ddF_y += 2);

            writeFastHLine(x - i    , y + r, i - j);
            writeFastHLine(x - i    , y - r, i - j);
            writeFastHLine(x + j + 1, y - r, i - j);
            writeFastHLine(x + j + 1, y + r, i - j);

            writeFastVLine(x + r, y + j + 1, i - j);
            writeFastVLine(x + r, y - i    , i - j);
            writeFastVLine(x - r, y - i    , i - j);
            writeFastVLine(x - r, y + j + 1, i - j);
            j = i;
        } while (i < --r);
    }

    void fillCircle(int32_t x, int32_t y, int32_t r) {
        writeFastHLine(x - r, y, (r << 1) + 1);
        fillCircleHelper(x, y, r, 3, 0);
    }

    void fillCircleHelper(int32_t x, int32_t y, int32_t r, uint_fast8_t corners, int32_t delta)
    {
        if (r <= 0) return;

        ++delta;

        int32_t f     = 1 - r;
        int32_t ddF_y = - (r << 1);
        int32_t ddF_x = 1;
        int32_t i     = 0;

        do {
            int32_t len = 0;
            while (f < 0) {
                f += (ddF_x += 2);
                ++len;
            }
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

    void drawLine(int32_t x0, int32_t y0, int32_t x1, int32_t y1)
    {
        bool steep = abs(y1 - y0) > abs(x1 - x0);

        int32_t xstart = _clip_l;
        int32_t ystart = _clip_t;
        int32_t xend   = _clip_r;
        int32_t yend   = _clip_b;

        if (steep)
        {
            std::swap(xstart, ystart);
            std::swap(xend, yend);
            std::swap(x0, y0);
            std::swap(x1, y1);
        }
        if (x0 > x1)
        {
            std::swap(x0, x1);
            std::swap(y0, y1);
        }
        if (x0 > xend || x1 < xstart) return;
        xend = std::min(x1, xend);

        int32_t dy = abs(y1 - y0);
        int32_t ystep = (y1 > y0) ? 1 : -1;
        int32_t dx = x1 - x0;
        int32_t err = dx >> 1;

        while (x0 < xstart || y0 < ystart || y0 > yend)
        {
            err -= dy;
            if (err < 0)
            {
                err += dx;
                y0 += ystep;
            }
            if (++x0 > xend) return;
        }
        int32_t xs = x0;
        int32_t dlen = 0;
        if (ystep < 0) std::swap(ystart, yend);
        yend += ystep;

        if (steep)
        {
            do
            {
                ++dlen;
                if ((err -= dy) < 0)
                {
                    writeFillRectPreclipped(y0, xs, 1, dlen);
                    err += dx;
                    xs = x0 + 1; dlen = 0; y0 += ystep;
                    if (y0 == yend) break;
                }
            } while (++x0 <= xend);
            if (dlen) writeFillRectPreclipped(y0, xs, 1, dlen);
        }
        else
        {
            do
            {
                ++dlen;
                if ((err -= dy) < 0)
                {
                    writeFillRectPreclipped(xs, y0, dlen, 1);
                    err += dx;
                    xs = x0 + 1; dlen = 0; y0 += ystep;
                    if (y0 == yend) break;
                }
            } while (++x0 <= xend);
            if (dlen) writeFillRectPreclipped(xs, y0, dlen, 1);
        }
    }

    void fillTriangle(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t x2, int32_t y2)
    {
        int32_t a, b;

        // Sort coordinates by Y order (y2 >= y1 >= y0)
        if (y0 > y1) { std::swap(y0, y1); std::swap(x0, x1); }
        if (y1 > y2) { std::swap(y2, y1); std::swap(x2, x1); }
        if (y0 > y1) { std::swap(y0, y1); std::swap(x0, x1); }

        if (y0 == y2) { // Handle awkward all-on-same-line case as its own thing
            a = b = x0;
            if (x1 < a)      a = x1;
            else if (x1 > b) b = x1;
            if (x2 < a)      a = x2;
            else if (x2 > b) b = x2;
            drawFastHLine(a, y0, b - a + 1);
            return;
        }
        if ((x1-x0) * (y2-y0) == (x2-x0) * (y1-y0)) {
            drawLine(x0,y0,x2,y2);
            return;
        }

        int32_t dy1 = y1 - y0;
        int32_t dy2 = y2 - y0;
        bool change = ((x1 - x0) * dy2 > (x2 - x0) * dy1);
        int32_t dx1 = abs(x1 - x0);
        int32_t dx2 = abs(x2 - x0);
        int32_t xstep1 = x1 < x0 ? -1 : 1;
        int32_t xstep2 = x2 < x0 ? -1 : 1;
        a = b = x0;
        if (change) {
            std::swap(dx1, dx2);
            std::swap(dy1, dy2);
            std::swap(xstep1, xstep2);
        }
        int32_t err1 = (std::max(dx1, dy1) >> 1)
                     + (xstep1 < 0
                       ? std::min(dx1, dy1)
                       : dx1);
        int32_t err2 = (std::max(dx2, dy2) >> 1)
                     + (xstep2 > 0
                       ? std::min(dx2, dy2)
                       : dx2);
        if (y0 != y1) {
            do {
                err1 -= dx1;
                while (err1 < 0) { err1 += dy1; a += xstep1; }
                err2 -= dx2;
                while (err2 < 0) { err2 += dy2; b += xstep2; }
                writeFastHLine(a, y0, b - a + 1);
            } while (++y0 < y1);
        }

        if (change) {
            b = x1;
            xstep2 = x2 < x1 ? -1 : 1;
            dx2 = abs(x2 - x1);
            dy2 = y2 - y1;
            err2 = (std::max(dx2, dy2) >> 1)
                 + (xstep2 > 0
                   ? std::min(dx2, dy2)
                   : dx2);
        } else {
            a = x1;
            dx1 = abs(x2 - x1);
            dy1 = y2 - y1;
            xstep1 = x2 < x1 ? -1 : 1;
            err1 = (std::max(dx1, dy1) >> 1)
                 + (xstep1 < 0
                   ? std::min(dx1, dy1)
                   : dx1);
        }
        do {
            err1 -= dx1;
            while (err1 < 0) { err1 += dy1; if ((a += xstep1) == x2) break; }
            err2 -= dx2;
            while (err2 < 0) { err2 += dy2; if ((b += xstep2) == x2) break; }
            writeFastHLine(a, y0, b - a + 1);
        } while (++y0 <= y2);
    }

    // The colour-taking forms BandCanvas and the scratch expose (LovyanGFX's templates set the colour, then draw).
    template <class T> void fillTriangle(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t x2, int32_t y2, const T& c) { color = (uint32_t)c; fillTriangle(x0, y0, x1, y1, x2, y2); }
    template <class T> void drawLine(int32_t x0, int32_t y0, int32_t x1, int32_t y1, const T& c) { color = (uint32_t)c; drawLine(x0, y0, x1, y1); }
    template <class T> void drawCircle(int32_t x, int32_t y, int32_t r, const T& c) { color = (uint32_t)c; drawCircle(x, y, r); }
    template <class T> void fillCircle(int32_t x, int32_t y, int32_t r, const T& c) { color = (uint32_t)c; fillCircle(x, y, r); }
    template <class T> void drawFastHLine(int32_t x, int32_t y, int32_t w, const T& c) { color = (uint32_t)c; drawFastHLine(x, y, w); }
};

}  // namespace lgfxport
