// Weatherscope's basemap rasteriser (docs/weatherscope-stage3.md A10.2, A10.3; docs/basemap-package-format.md section 9):
// a validated BMPK package (include/BasemapPackage.h) drawn into the BASEMAP IMAGE, 240 x 240 palette indices, which
// the map pass folds under every dry pixel (WxScreens.h MapPixel). Pure C++11; host-tested by test_wx_basemap.cpp
// against A10.11's frozen tables. No coordinate is stored here.
//
//   index  0       nothing (black)
//   index  1-37    terrain level v -> grey g = 10 + (index - 1), drawn (g, g, g + 2)
//   index  38      water area (10, 34, 72)        39  river (26, 70, 128)        40  road (62, 62, 62)
//
// The build order is bendmock.py's and the last write wins: terrain (if on), water rings filled, rivers, roads.
// Every expression is written in the format doc's order, so the doubles match ref35.py's independent rendering.

#pragma once

#include <math.h>
#include <stdint.h>
#include <string.h>

#include "BasemapPackage.h"
#include "WxRaster.h"
#include "WxRender.h"

namespace wxbase {

constexpr int kSize = wxraster::kSize;
constexpr double kKmPerDeg = 111.0;   // the planar metric's k (format section 2)

// The layer mask (A10.2's LayerKey; K rows' "towns only, + water, + rivers, + roads, + peaks").
constexpr uint8_t kMaskWater = 1, kMaskRivers = 2, kMaskRoads = 4, kMaskPeaks = 8, kMaskAll = 15;

constexpr uint8_t kIdxNothing = 0, kIdxWater = 38, kIdxRiver = 39, kIdxRoad = 40;

// A shade value's index: 1 + floor(36 * v / 255), so the ramp's grey is 10 + floor(36 * v / 255).
inline int RampIndex(uint8_t v) { return 1 + 36 * (int)v / 255; }

// An index's colour is the theme's: wxtheme::Theme::base[idx] (3.5t, A12.47). PaletteRgb retired with it.

// Weatherscope's terrain level for a view: R_l = the view's radius x 246/240, in mm (format section 4), so 24,743,664,
// 49,487,328 and 98,974,656 mm at 15, 30 and 60 mi.
inline uint32_t LevelMm(int radiusMi) { return (uint32_t)((uint64_t)radiusMi * 1609344u * 41u / 40u); }

// Package units to screen pixels, through the view's own projection (format section 9), in double.
class Mapper {
public:
    Mapper(const bmpk::Package& pk, const wxrender::View& v)
        : lat0_(pk.lat0Micro / 1e6), lon0_(pk.lon0Micro / 1e6), rmm_((double)pk.radiusMm), v_(v)
    {
        c0_ = fmax(cos(lat0_ * (wxraster::kPi / 180)), 0.01);
    }

    void ToScreen(int32_t qx, int32_t qy, double* x, double* y) const
    {
        const double e = qx / 32767.0 * rmm_ / 1e6, s = qy / 32767.0 * rmm_ / 1e6;   // km
        const double lat = lat0_ - s / kKmPerDeg, lon = lon0_ + e / (kKmPerDeg * c0_);
        *x = wxraster::kC + (lon - v_.homeLon) / v_.rLon * wxraster::kC;
        *y = wxraster::kC - (lat - v_.homeLat) / v_.rLat * wxraster::kC;
    }

    // The level pixel containing the screen pixel (x, y)'s centre (truncation); false outside [0, n).
    bool LevelPixel(int x, int y, uint32_t levelRadiusMm, int n, int* i, int* j) const
    {
        const double c = wxraster::kC;
        const double latP = v_.homeLat + (c - (y + 0.5)) / c * v_.rLat;
        const double lonP = v_.homeLon + ((x + 0.5) - c) / c * v_.rLon;
        const double e = (lonP - lon0_) * kKmPerDeg * c0_, s = (lat0_ - latP) * kKmPerDeg;
        const double pix = 2 * (double)levelRadiusMm / 1e6 / n;
        *i = (int)floor(e / pix + n / 2.0);
        *j = (int)floor(s / pix + n / 2.0);
        return *i >= 0 && *i < n && *j >= 0 && *j < n;
    }

private:
    double lat0_, lon0_, rmm_, c0_;
    const wxrender::View& v_;
};

// One ring filled by A10.2's centre rule: a pixel is filled when its centre (x + 0.5, y + 0.5) is inside by even-odd;
// an edge counts at a scanline when (y1 > py) != (y2 > py); the centre lies in [x_a, x_b) between paired crossings.
// Writes off the image are dropped. `cross` holds at least n doubles. Returns the pixels written.
inline int FillRing(const double* xs, const double* ys, int n, double* cross, uint8_t* img, uint8_t value)
{
    if (n < 3) return 0;
    double lo = ys[0], hi = ys[0];
    for (int k = 1; k < n; ++k) {
        lo = fmin(lo, ys[k]);
        hi = fmax(hi, ys[k]);
    }
    int y0 = (int)floor(lo) - 1, y1 = (int)ceil(hi) + 2;
    if (y0 < 0) y0 = 0;
    if (y1 > kSize) y1 = kSize;
    int written = 0;
    for (int y = y0; y < y1; ++y) {
        const double py = y + 0.5;
        int m = 0;
        for (int k = 0; k < n; ++k) {
            const int k2 = (k + 1) % n;
            const double xa = xs[k], ya = ys[k], xb = xs[k2], yb = ys[k2];
            if ((ya > py) != (yb > py)) cross[m++] = xa + (py - ya) * (xb - xa) / (yb - ya);
        }
        for (int a = 1; a < m; ++a) {   // insertion sort: a scanline meets few edges
            const double t = cross[a];
            int b = a - 1;
            while (b >= 0 && cross[b] > t) {
                cross[b + 1] = cross[b];
                --b;
            }
            cross[b + 1] = t;
        }
        for (int a = 0; a + 1 < m; a += 2) {
            int x0 = (int)ceil(cross[a] - 0.5), x1 = (int)ceil(cross[a + 1] - 0.5);
            if (x0 < 0) x0 = 0;
            if (x1 > kSize) x1 = kSize;
            for (int x = x0; x < x1; ++x) {
                img[y * kSize + x] = value;
                ++written;
            }
        }
    }
    return written;
}

// A sink that writes one index into the image, for wxraster::Line (the polygon outline's and the arrow's DDA).
class IndexSink : public wxraster::Sink {
public:
    explicit IndexSink(uint8_t* img) : img_(img) {}
    uint8_t value = 0;
    void Put(int x, int y, wxraster::Rgb) override { img_[y * kSize + x] = value; }   // the colour is unused: callers pass Rgb{}

private:
    uint8_t* img_;
};

// The caller's vertex scratch, sized once from the package: qx, qy (int32) and x, y, crossings (double), each
// at least pk.maxVertices long.
struct Scratch {
    int32_t* qx = nullptr;
    int32_t* qy = nullptr;
    double* x = nullptr;
    double* y = nullptr;
    double* cross = nullptr;
    uint32_t cap = 0;
};

// What Build drew, for the firmware's log.
struct BuildInfo {
    bool terrain = false;   // a level for the view was found and drawn
    int rings = 0, rivers = 0, roads = 0;
};

// The basemap image for a view: terrain (if on, from the level for `radiusMi`), then the water rings, the rivers and
// the roads in the mask, in that order. False if the scratch is too small (nothing drawn: no partial basemap).
inline bool Build(const bmpk::Package& pk, const wxrender::View& v, int radiusMi, uint8_t mask, bool terrain,
                  const Scratch& sc, uint8_t* img, BuildInfo* info = nullptr)
{
    BuildInfo bi;
    if (sc.cap < pk.maxVertices) return false;
    memset(img, kIdxNothing, (size_t)kSize * kSize);
    const Mapper m(pk, v);
    if (terrain) {
        bmpk::Level lv[8];
        const int nl = bmpk::Levels(pk, lv, 8);
        const uint32_t want = LevelMm(radiusMi);
        for (int k = 0; k < nl && k < 8; ++k) {
            if (lv[k].radiusMm != want) continue;
            for (int y = 0; y < kSize; ++y)
                for (int x = 0; x < kSize; ++x) {
                    int i, j;
                    if (m.LevelPixel(x, y, lv[k].radiusMm, lv[k].n, &i, &j))
                        img[y * kSize + x] = (uint8_t)RampIndex(lv[k].values[j * lv[k].n + i]);
                }
            bi.terrain = true;
            break;
        }
    }
    if (mask & kMaskWater) {
        bmpk::ForEachRing(pk, sc.qx, sc.qy, sc.cap, [&](int n) {
            for (int k = 0; k < n; ++k) m.ToScreen(sc.qx[k], sc.qy[k], &sc.x[k], &sc.y[k]);
            FillRing(sc.x, sc.y, n, sc.cross, img, kIdxWater);
            ++bi.rings;
        });
    }
    IndexSink sink(img);
    if (mask & kMaskRivers) {
        sink.value = kIdxRiver;
        bmpk::ForEachRiver(pk, sc.qx, sc.qy, sc.cap, [&](int, int n) {
            for (int k = 0; k < n; ++k) m.ToScreen(sc.qx[k], sc.qy[k], &sc.x[k], &sc.y[k]);
            for (int k = 0; k + 1 < n; ++k) wxraster::Line(sink, sc.x[k], sc.y[k], sc.x[k + 1], sc.y[k + 1], wxraster::Rgb{});
            ++bi.rivers;
        });
    }
    if (mask & kMaskRoads) {
        sink.value = kIdxRoad;
        bmpk::ForEachRoad(pk, sc.qx, sc.qy, sc.cap, [&](int, int n) {
            for (int k = 0; k < n; ++k) m.ToScreen(sc.qx[k], sc.qy[k], &sc.x[k], &sc.y[k]);
            for (int k = 0; k + 1 < n; ++k) wxraster::Line(sink, sc.x[k], sc.y[k], sc.x[k + 1], sc.y[k + 1], wxraster::Rgb{});
            ++bi.roads;
        });
    }
    if (info) *info = bi;
    return true;
}

}  // namespace wxbase
