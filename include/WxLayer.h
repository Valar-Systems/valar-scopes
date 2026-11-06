#pragma once
// Which WXC1 crop cell each pixel of the radar shows (docs/wx-device.md, WD2): the exact inverse of
// radarproj::Project (include/RadarProjection.h), the projection the planes are drawn with. Pure C++11, no
// Arduino, so test/host/test_wx_layer.cpp can hold it to an independent double-precision reference.
//
// Why fixed point. Project's whole chain -- translate, scale, rotate about the centre -- is affine, so the
// fractional cell index is affine in the pixel: row = R0 + Rx*X + Ry*Y (and the same for the column). The
// coefficients are worked out ONCE per rebuild in double; each pixel is then two 64-bit integer adds at
// 2^-32 cells, which stays within ~1e-7 cells of exact across a 480 px row. The ESP32-S3 has no
// double-precision FPU (a 480 px rebuild in software doubles would take about half a second), and single
// precision alone would leave errors near 1e-4 cells -- the width of the reference's boundary band.
#include <stdint.h>
#include <math.h>

#include "WxCrop.h"

namespace wxlayer {

constexpr uint8_t kOffDisc = 255;     // not a WXC1 code: outside the round screen

/** What the radar is showing: the same values ProjectAt reads (home, the VIEW radii, the rotation). */
struct View {
    double homeLat = 0, homeLon = 0;  // degrees
    double rLat = 0, rLon = 0;        // degrees from the centre to the edge of the view (zoom included)
    float rotSin = 0.0f, rotCos = 1.0f;
    int size = 240;                   // the square screen's side in pixels
};

/** The round screen, as an integer test (WxRender's, generalised to any size): the pixel's centre is
 *  outside the circle of radius size/2 about the centre. Doubled, every term is exact. */
inline bool OffDisc(int x, int y, int size)
{
    const int64_t dx2 = 2 * x + 1 - size, dy2 = 2 * y + 1 - size;
    return dx2 * dx2 + dy2 * dy2 > (int64_t)size * size;
}

/** Calls fn(x, y, offDisc, row, col) for every pixel, row-major: the crop cell (row, col) whose area holds
 *  the pixel's centre, which may lie outside the crop. One loop for Build and for the tests, so the tests
 *  grade the indices Build uses. (>> on a negative int64 is arithmetic in GCC, host and xtensa.) */
template <typename Fn>
inline void ForEachPixel(const wxcrop::Header& h, const View& v, Fn&& fn)
{
    const double latNw = h.latNwMicro / 1e6, lonNw = h.lonNwMicro / 1e6;
    const double cellLat = h.cellLatMicro / 1e6, cellLon = h.cellLonMicro / 1e6;
    const double C = v.size / 2.0;
    // Project, read backwards. For a pixel centre at screen offset (X, Y) from the centre, undo the
    // rotation (the transpose of Project's): u = c*X - s*Y, w = s*X + c*Y. Then the unrotated offset maps to
    //   lon = homeLon + u * (2 rLon / size),   lat = homeLat - w * (2 rLat / size),
    // and to fractional cell indices row = (latNw - lat) / cellLat, col = (lon - lonNw) / cellLon.
    const double c = v.rotCos, s = v.rotSin;
    const double kRow = (2.0 * v.rLat / v.size) / cellLat, kCol = (2.0 * v.rLon / v.size) / cellLon;
    const double row0 = (latNw - v.homeLat) / cellLat, col0 = (v.homeLon - lonNw) / cellLon;
    // row = row0 + kRow * (s*X + c*Y), col = col0 + kCol * (c*X - s*Y): per +1 pixel in x the step is
    // (kRow*s, kCol*c).
    const double scale = 4294967296.0;                     // 2^32
    const int64_t dRowX = (int64_t)llround(kRow * s * scale), dColX = (int64_t)llround(kCol * c * scale);
    const int64_t half = (int64_t)1 << 31;
    for (int y = 0; y < v.size; ++y) {
        const double Y = y + 0.5 - C, X0 = 0.5 - C;
        int64_t rowF = (int64_t)llround((row0 + kRow * (s * X0 + c * Y)) * scale);
        int64_t colF = (int64_t)llround((col0 + kCol * (c * X0 - s * Y)) * scale);
        for (int x = 0; x < v.size; ++x, rowF += dRowX, colF += dColX)
            fn(x, y, OffDisc(x, y, v.size), (int32_t)((rowF + half) >> 32), (int32_t)((colF + half) >> 32));
    }
}

/** Fills out[size * size] (row-major screen) with the code each pixel shows: kOffDisc off the round screen,
 *  14 (no coverage -- never dry) where the pixel's cell falls outside the crop, else the crop's code.
 *  `codes` is the crop decoded row-major (h.rows x h.cols), e.g. by wxcrop::DecodeInto. */
inline void Build(const wxcrop::Header& h, const uint8_t* codes, const View& v, uint8_t* out)
{
    ForEachPixel(h, v, [&](int x, int y, bool off, int32_t r, int32_t k) {
        out[(size_t)y * v.size + x] = off ? kOffDisc
            : (r < 0 || r >= h.rows || k < 0 || k >= h.cols) ? wxcrop::kCodeNoCoverage
            : codes[(size_t)r * h.cols + (size_t)k];
    });
}

}  // namespace wxlayer
