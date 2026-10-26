#pragma once
// Weatherscope Stage 1: which WXC1 crop cell each pixel of the 240 x 240 round screen shows.
// Pure C++11 (no Arduino), so the host test can hold it to test/fixtures/wx/render_ref.py.
//
// The projection is the radar's own (AircraftManager::ProjectAt), north-up:
//   rLat = R / 111.0, rLon = R / (111.0 * max(cos(home lat), 0.01)); NO 2.0-degree clamp (that protects
//   an OpenSky box; Weatherscope has none, and 60 mi is far below it);
//   x = 120 + dLon / rLon * 120, y = 120 - dLat / rLat * 120.
// A pixel is sampled at its centre and its cell index is floor(v + 0.5). Stage 1 is north-up, so the
// row index depends only on y and the column index only on x: 480 doubles, worked out once.
//
// Every expression below is written in the SAME order as render_ref.py, including
// home_lat * (pi / 180) (CPython's math.radians), so host and device can agree bit for bit.
#include <math.h>
#include <stdint.h>

#include "WxCrop.h"

namespace wxrender {

constexpr int kSize = 240;
constexpr double kCentre = 120.0;
constexpr double kKmPerDeg = 111.0;               // AircraftManager's KM_PER_DEGREE
constexpr double kRadiusKm = 60 * 1.609344;       // 60 mi
constexpr uint8_t kOffDisc = 255;                 // not a WXC1 code: outside the round screen
constexpr double kPi = 3.14159265358979323846;    // CPython's Py_MATH_PI; M_PI is not in strict C++11

struct View {
    double homeLat = 0, homeLon = 0;   // degrees: the crop's centre in Stage 1
    double rLat = 0, rLon = 0;         // degrees from the centre to the edge of the view
    int16_t row[kSize];                // the crop row each screen row samples
    int16_t col[kSize];                // the crop column each screen column samples
};

// Builds the view for a crop: home = the crop's centre (from its header), radius `radiusKm` (3.5a's zoom ladder
// passes 15, 30 or 60 mi; a view scale only -- the crop is unchanged).
inline void BuildViewKm(const wxcrop::Header& h, View* v, double radiusKm)
{
    const double latNw = h.latNwMicro / 1e6, lonNw = h.lonNwMicro / 1e6;
    const double cellLat = h.cellLatMicro / 1e6, cellLon = h.cellLonMicro / 1e6;
    v->homeLat = latNw - (h.rows - 1) / 2.0 * cellLat;
    v->homeLon = lonNw + (h.cols - 1) / 2.0 * cellLon;
    v->rLat = radiusKm / kKmPerDeg;
    double c = cos(v->homeLat * (kPi / 180.0));
    if (c < 0.01) c = 0.01;
    v->rLon = radiusKm / (kKmPerDeg * c);
    for (int y = 0; y < kSize; ++y) {
        const double lat = v->homeLat + 1.0 * (kCentre - (y + 0.5)) / kCentre * v->rLat;
        v->row[y] = (int16_t)floor((latNw - lat) / cellLat + 0.5);
    }
    for (int x = 0; x < kSize; ++x) {
        const double lon = v->homeLon + ((x + 0.5) - kCentre) / kCentre * v->rLon;
        v->col[x] = (int16_t)floor((lon - lonNw) / cellLon + 0.5);
    }
}

// The 60 mi view, as Stage 1 and 3.3 built it.
inline void BuildView(const wxcrop::Header& h, View* v) { BuildViewKm(h, v, kRadiusKm); }

// The code pixel (x, y) shows: kOffDisc outside the round screen; the crop cell's code inside it;
// 14 (no coverage, never dry) where the cell falls outside the crop.
inline uint8_t CodeAt(const wxcrop::Header& h, const uint8_t* codes, const View& v, int x, int y)
{
    // The disc test, (x + 0.5 - 120)^2 + (y + 0.5 - 120)^2 > 120^2, in integers: doubled, every term is
    // exact, so this IS render_ref.py's test -- without 57,600 software-double ops on an ESP32-S3,
    // which has no double-precision FPU.
    const int dx2 = 2 * x + 1 - 2 * (int)kCentre, dy2 = 2 * y + 1 - 2 * (int)kCentre;
    if (dx2 * dx2 + dy2 * dy2 > 4 * (int)(kCentre * kCentre)) return kOffDisc;
    const int r = v.row[y], c = v.col[x];
    if (r < 0 || r >= h.rows || c < 0 || c >= h.cols) return wxcrop::kCodeNoCoverage;
    return codes[(uint32_t)r * h.cols + c];
}

}  // namespace wxrender
