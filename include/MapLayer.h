#pragma once
// The Blipscope's map layer (docs/blipscope-basemap-client.md; step M in docs/blipscope-map-client-steps.md): a
// VALIDATED BMPK package (include/BasemapPackage.h) drawn under the radar as one 240 x 240 RGB565 layer. It is rebuilt
// only when the view or the package changes, so a steady frame costs one memcpy and draws no vectors. Pure C++11, no
// Arduino: test/host/test_map_layer.cpp holds it to CM1-CM8, CM10 and CM11, test/host/check_map_layer.py to CM9.
//
// THE PROJECTION IS THE RADAR'S, DERIVED IN DOUBLE ONCE PER BUILD. Format section 9 followed by radarproj::Project's
// window-up rotation is affine in the package's (qx, qy), so Build works out six coefficients in double and maps each
// vertex with float multiply-adds: the S3 has a single-precision FPU and no double one. CM2 holds that path within
// 0.01 px of wxbase::Mapper (the Weatherscope's own transcription of section 9), and CM3 within 1 px of
// radarproj::Project, the function the planes are drawn with.
//
// REUSED BY INCLUDE, NEVER COPIED: wxbase::FillRing (the centre rule). It writes an 8-bit image, so the layer is built
// as an index image first (land, water, river, road; the last write wins, in S3's order) and coloured in one final
// pass, which samples terrain only where land shows. wxraster::Line is NOT reused: it clips to the Weatherscope's map
// area (rows 24-193), and the radar draws its whole disc (the steps doc, "registered before step M's code", item 1).
// The layer's line repeats its arithmetic exactly, and CM11 holds the two equal pixel for pixel inside that area.
//
// No coordinate is printed or stored here. The caller owns every buffer; Build allocates nothing.
#include <math.h>
#include <stdint.h>
#include <string.h>

#include "BasemapPackage.h"
#include "WxLayer.h"   // wxlayer::View (what ProjectAt reads) and wxlayer::OffDisc (the round screen)
#include "WxS2.h"      // wxs2::To565: the panel's one RGB888 -> RGB565 rule
#include "../src/weatherscope/WxBasemap.h"   // wxbase::FillRing

namespace maplayer {

constexpr int kSize = wxbase::kSize;   // 240: FillRing's image width, and the s3-128's screen (the client's one SKU)
constexpr double kKmPerDeg = 111.0;   // the planar metric's k (format section 2)
constexpr double kPi = 3.14159265358979323846;

// The index image's values: what each pixel shows before it is coloured.
constexpr uint8_t kIdxLand = 0, kIdxWater = 1, kIdxRiver = 2, kIdxRoad = 3;

// Build's layer mask. kLayersAll is the client; the bench and the tests switch layers off one at a time.
constexpr uint8_t kLayerTerrain = 1, kLayerWater = 2, kLayerRivers = 4, kLayerRoads = 8, kLayersAll = 15;

struct Rgb8 {
    uint8_t r, g, b;
};

struct Theme {
    Rgb8 land;
    double terrainGain;   // S2: land + (shade - 128) x gain, per channel
    Rgb8 water, river, road, place;
};

// THE THEME TABLE BEGIN -- every colour the layer draws (the plan's section 4). check_map_layer.py (CM9) refuses a
// colour literal anywhere else in this file and holds the Dark row to the plan's own table. Light and Auto, when they
// come, are one more row each and no code.
enum ThemeId : uint8_t { kThemeDark = 0, kThemeCount };
constexpr Theme kThemes[kThemeCount] = {
    // Dark, the cockpit look (Daniel, 2026-11-06)
    {{6, 8, 10}, 0.075, {18, 34, 56}, {30, 56, 104}, {72, 72, 72}, {86, 86, 86}},
};
// THE THEME TABLE END

inline uint16_t To565(Rgb8 c) { return wxs2::To565(((uint32_t)c.r << 16) | ((uint32_t)c.g << 8) | c.b); }

// One S2 channel, rounded floor(v + 0.5) as the format rounds everything. The bounds are a colour channel's own range:
// with Dark's 0.075 the ramp spans about -4 to 20, so dark slopes do reach below 0 and are drawn black. Nothing out of
// range is hidden by them -- every shade 0-255 is a legal input.
inline uint8_t Channel(uint8_t land, uint8_t v, double gain)
{
    const int c = (int)floor(land + (v - 128) * gain + 0.5);
    return (uint8_t)(c < 0 ? 0 : (c > 255 ? 255 : c));
}

// S2: a shade value's colour on the theme's land.
inline Rgb8 TerrainRgb(const Theme& t, uint8_t v)
{
    return Rgb8{Channel(t.land.r, v, t.terrainGain), Channel(t.land.g, v, t.terrainGain),
                Channel(t.land.b, v, t.terrainGain)};
}

// ------------------------------------------------------------------------------------------------------- the gate

// The gate (format section 8; the plan's F3): the ONLY way to a Package that Build will draw. A refusal names its
// reason and leaves the package empty, so nothing half-right reaches the screen.
struct Verdict {
    bool accepted;
    bmpk::Status why;
};

inline Verdict Load(const uint8_t* bytes, size_t len, bmpk::Package* pk)
{
    const bmpk::Status s = bmpk::Validate(bytes, len, pk);
    return Verdict{s == bmpk::Status::kOk, s};
}

// ------------------------------------------------------------------------------------------------- the projection

// Package units to screen pixels: x = ax + bx qx + cx qy, y = ay + by qx + cy qy.
struct Affine {
    double ax, bx, cx, ay, by, cy;
};

// Format section 9 with the view's own centre, then Project's rotation about the screen's centre, worked out once.
// `radiusMm` is the disc the package's header names (F4), never the one the client asked for.
inline Affine AffineFor(const bmpk::Package& pk, uint32_t radiusMm, const wxlayer::View& v)
{
    const double lat0 = pk.lat0Micro / 1e6, lon0 = pk.lon0Micro / 1e6;
    const double c0 = fmax(cos(lat0 * (kPi / 180)), 0.01);
    const double C = v.size / 2.0, kmPerQ = radiusMm / 1e6 / 32767.0;
    // North-up, as offsets from the centre: u = (lon - homeLon) / rLon * C and w = (homeLat - lat) / rLat * C, with
    // lon = lon0 + qx kmPerQ / (k c0) and lat = lat0 - qy kmPerQ / k.
    const double u0 = (lon0 - v.homeLon) / v.rLon * C;
    const double w0 = (v.homeLat - lat0) / v.rLat * C;
    const double uq = kmPerQ / (kKmPerDeg * c0) / v.rLon * C, wq = kmPerQ / kKmPerDeg / v.rLat * C;
    // radarproj::Project's rotation: x = C + u cos + w sin, y = C - u sin + w cos.
    const double c = v.rotCos, s = v.rotSin;
    Affine a;
    a.ax = C + u0 * c + w0 * s;
    a.bx = uq * c;
    a.cx = wq * s;
    a.ay = C - u0 * s + w0 * c;
    a.by = -uq * s;
    a.cy = wq * c;
    return a;
}

// The same six coefficients in float: the per-vertex path.
struct AffineF {
    float ax, bx, cx, ay, by, cy;
};

inline AffineF Fast(const Affine& a)
{
    return AffineF{(float)a.ax, (float)a.bx, (float)a.cx, (float)a.ay, (float)a.by, (float)a.cy};
}

inline void ToScreen(const AffineF& f, int32_t qx, int32_t qy, float* x, float* y)
{
    *x = f.ax + f.bx * (float)qx + f.cx * (float)qy;
    *y = f.ay + f.by * (float)qx + f.cy * (float)qy;
}

// The pixel a point lands in when the layer draws it: the Weatherscope's rounding, floor(v + 0.5).
inline int Rnd(float v) { return (int)floorf(v + 0.5f); }

// ----------------------------------------------------------------------------------------------------------- S1

// S1's "in view" (the plan's section 3): a segment, mapped to screen pixels in double, whose closest point to the
// screen's centre is within 120 px. The test is a distance from the centre, so the rotation changes nothing.
inline bool SegmentInView(const Affine& a, double half, int32_t qx0, int32_t qy0, int32_t qx1, int32_t qy1)
{
    const double ax = a.ax - half + a.bx * qx0 + a.cx * qy0, ay = a.ay - half + a.by * qx0 + a.cy * qy0;
    const double bx = a.ax - half + a.bx * qx1 + a.cx * qy1, by = a.ay - half + a.by * qx1 + a.cy * qy1;
    const double dx = bx - ax, dy = by - ay, dd = dx * dx + dy * dy;
    double t = dd > 0 ? -(ax * dx + ay * dy) / dd : 0.0;
    t = t < 0 ? 0 : (t > 1 ? 1 : t);   // the segment's own ends: its closest point lies on it
    const double ex = ax + t * dx, ey = ay + t * dy;
    return sqrt(ex * ex + ey * ey) <= half;
}

// ----------------------------------------------------------------------------------------------------- the line

// The layer's line: wxraster::Line's DDA exactly -- n = int(max(|dx|, |dy|)) + 1, and point k at
// floor(x0 + dx * k / n + 0.5), in double -- so CM11 can hold the two pixel for pixel inside the Weatherscope's map area.
// Where it writes is the difference: the whole disc (wxlayer::OffDisc), not rows 24-193. A Liang-Barsky clip to the screen
// widened by two pixels picks the range of k worth visiting; it skips points that would land off the screen and never
// moves one, so a segment far off a zoomed-in screen costs nothing.
inline void LineIdx(uint8_t* idx, double x0, double y0, double x1, double y1, uint8_t value)
{
    const double dx = x1 - x0, dy = y1 - y0;
    const int n = (int)(fabs(dx) > fabs(dy) ? fabs(dx) : fabs(dy)) + 1;
    const double p[4] = {-dx, dx, -dy, dy};
    const double q[4] = {x0 + 2.0, (kSize + 1.0) - x0, y0 + 2.0, (kSize + 1.0) - y0};
    double t0 = 0.0, t1 = 1.0;
    for (int k = 0; k < 4; ++k) {
        if (p[k] == 0.0) {
            if (q[k] < 0.0) return;
            continue;
        }
        const double r = q[k] / p[k];
        if (p[k] < 0.0) {
            if (r > t1) return;
            if (r > t0) t0 = r;
        } else {
            if (r < t0) return;
            if (r < t1) t1 = r;
        }
    }
    int kLo = (int)floor(t0 * n) - 1, kHi = (int)ceil(t1 * n) + 1;
    if (kLo < 0) kLo = 0;
    if (kHi > n) kHi = n;
    for (int k = kLo; k <= kHi; ++k) {
        const int x = (int)floor(x0 + dx * k / n + 0.5), y = (int)floor(y0 + dy * k / n + 0.5);
        if (x >= 0 && x < kSize && y >= 0 && y < kSize && !wxlayer::OffDisc(x, y, kSize)) idx[y * kSize + x] = value;
    }
}

// -------------------------------------------------------------------------------------------------- the terrain

// S2's level (Q-MC7 (a), ruled 2026-11-07): the smallest level whose half-width covers the view's radius plus the home's
// distance from the package's origin, both in format section 2's metric. When none covers, the largest: its outside
// takes land. Returns the level's index in directory order, or -1 for a package with none.
inline int ChooseLevel(const bmpk::Package& pk, const wxlayer::View& v, bmpk::Level* chosen, bool* covers)
{
    bmpk::Level lv[8];
    const int n = bmpk::Levels(pk, lv, 8);
    const double lat0 = pk.lat0Micro / 1e6, lon0 = pk.lon0Micro / 1e6;
    const double c0 = fmax(cos(lat0 * (kPi / 180)), 0.01);
    const double e = (v.homeLon - lon0) * kKmPerDeg * c0, s = (lat0 - v.homeLat) * kKmPerDeg;
    const double off = sqrt(e * e + s * s);
    const double view = fmax(v.rLat * kKmPerDeg, v.rLon * kKmPerDeg * c0);
    int best = -1, largest = -1;
    for (int k = 0; k < n && k < 8; ++k) {
        if (largest < 0 || lv[k].radiusMm > lv[largest].radiusMm) largest = k;
        if (lv[k].radiusMm / 1e6 >= view + off && (best < 0 || lv[k].radiusMm < lv[best].radiusMm)) best = k;
    }
    if (covers) *covers = best >= 0;
    const int pick = best >= 0 ? best : largest;
    if (pick >= 0 && chosen) *chosen = lv[pick];
    return pick;
}

// The level pixel each screen pixel's CENTRE falls in (format section 9, with Project's rotation undone), for every
// pixel, row-major: fn(x, y, i, j). i or j outside [0, n) means the pixel lies off the level. One loop for Build and for
// CM10, so the test grades the indices Build uses. Fixed point at 2^-32 level pixels, as WxLayer.h steps its cells.
template <typename Fn>
inline void ForEachLevelPixel(const bmpk::Package& pk, const bmpk::Level& lv, const wxlayer::View& v, Fn&& fn)
{
    const double lat0 = pk.lat0Micro / 1e6, lon0 = pk.lon0Micro / 1e6;
    const double c0 = fmax(cos(lat0 * (kPi / 180)), 0.01);
    const double C = v.size / 2.0, pix = 2.0 * lv.radiusMm / 1e6 / lv.n;   // km per level pixel
    const double c = v.rotCos, s = v.rotSin;
    // A pixel centre at (X, Y) from the centre: u = c X - s Y and w = s X + c Y undo the rotation, then
    // lon = homeLon + u rLon / C and lat = homeLat - w rLat / C, so i = E / pix + n/2 = i0 + iu u, j = j0 + jw w.
    const double iu = v.rLon / C * kKmPerDeg * c0 / pix, jw = v.rLat / C * kKmPerDeg / pix;
    const double i0 = (v.homeLon - lon0) * kKmPerDeg * c0 / pix + lv.n / 2.0;
    const double j0 = (lat0 - v.homeLat) * kKmPerDeg / pix + lv.n / 2.0;
    const double scale = 4294967296.0;   // 2^32
    const int64_t dI = (int64_t)llround(iu * c * scale), dJ = (int64_t)llround(jw * s * scale);   // per +1 px in x
    for (int y = 0; y < v.size; ++y) {
        const double Y = y + 0.5 - C, X0 = 0.5 - C;
        int64_t iF = (int64_t)llround((i0 + iu * (c * X0 - s * Y)) * scale);
        int64_t jF = (int64_t)llround((j0 + jw * (s * X0 + c * Y)) * scale);
        for (int x = 0; x < v.size; ++x, iF += dI, jF += dJ) fn(x, y, (int32_t)(iF >> 32), (int32_t)(jF >> 32));
    }
}

// ------------------------------------------------------------------------------------------------------- Build

// The caller's scratch, sized once from the package: qx, qy (int32) and FillRing's x, y and crossings (double), each
// at least pk.maxVertices long; and the index image, kSize x kSize.
struct Scratch {
    int32_t* qx = nullptr;
    int32_t* qy = nullptr;
    double* x = nullptr;
    double* y = nullptr;
    double* cross = nullptr;
    uint32_t cap = 0;
    uint8_t* idx = nullptr;
};

// What Build drew, for the firmware's log and the tests. The us* fields are the passes' times when Build is given a
// clock (CF5); they stay 0 on the host.
struct BuildInfo {
    uint32_t headerMm = 0, askedMm = 0;   // F4: the disc drawn, and the one asked for
    int level = -1;                       // the terrain level used, in directory order; -1 for none
    uint32_t levelMm = 0;
    bool levelCovers = false;
    int rings = 0, rivers = 0, primary = 0, secondary = 0;   // features drawn
    int primaryInView = 0, secondaryInView = 0;              // S1's counts
    bool secondaryDrawn = false;                             // S1's decision
    uint32_t usS1 = 0, usWater = 0, usRivers = 0, usRoads = 0, usColour = 0, usTotal = 0;
};

typedef uint32_t (*ClockUs)();

// The layer for a view: the index image in S3's order (water, rivers, roads; the last write wins), then one colouring
// pass that puts terrain (S2) under land and 0 off the disc. Writes logical RGB565 into out[kSize * kSize]. False, with
// nothing written to `out`, for an empty package, a short scratch or a view of another size.
inline bool Build(const bmpk::Package& pk, const wxlayer::View& v, uint32_t askedMm, uint8_t layers, const Theme& t,
                  const Scratch& sc, uint16_t* out, BuildInfo* info = nullptr, ClockUs clock = nullptr)
{
    if (pk.data == nullptr || sc.idx == nullptr || sc.cap < pk.maxVertices || v.size != kSize) return false;
    BuildInfo bi;
    const uint32_t t0 = clock ? clock() : 0;
    uint32_t tp = t0;
    const auto lap = [&](uint32_t* field) {
        if (!clock) return;
        const uint32_t now = clock();
        *field = now - tp;
        tp = now;
    };
    const uint32_t rMm = pk.radiusMm;   // F4: the disc the header names
    bi.headerMm = rMm;
    bi.askedMm = askedMm;
    const Affine a = AffineFor(pk, rMm, v);
    const AffineF f = Fast(a);
    const double half = kSize / 2.0;
    memset(sc.idx, kIdxLand, (size_t)kSize * kSize);

    // S1, decided per view before any road is drawn.
    bmpk::ForEachRoad(pk, sc.qx, sc.qy, sc.cap, [&](int cls, int n) {
        bool near = false;
        for (int k = 0; k + 1 < n && !near; ++k)
            near = SegmentInView(a, half, sc.qx[k], sc.qy[k], sc.qx[k + 1], sc.qy[k + 1]);
        if (cls == 1) {
            if (near) ++bi.primaryInView;
        } else if (near) {
            ++bi.secondaryInView;
        }
    });
    bi.secondaryDrawn = bi.primaryInView == 0;
    lap(&bi.usS1);

    if (layers & kLayerWater) {
        bmpk::ForEachRing(pk, sc.qx, sc.qy, sc.cap, [&](int n) {
            for (int k = 0; k < n; ++k) {
                float x, y;
                ToScreen(f, sc.qx[k], sc.qy[k], &x, &y);
                sc.x[k] = x;
                sc.y[k] = y;
            }
            wxbase::FillRing(sc.x, sc.y, n, sc.cross, sc.idx, kIdxWater);
            ++bi.rings;
        });
    }
    lap(&bi.usWater);

    // THE RIVERS PASS
    if (layers & kLayerRivers) {
        bmpk::ForEachRiver(pk, sc.qx, sc.qy, sc.cap, [&](int, int n) {
            float x0, y0, x1, y1;
            ToScreen(f, sc.qx[0], sc.qy[0], &x0, &y0);
            for (int k = 1; k < n; ++k, x0 = x1, y0 = y1) {
                ToScreen(f, sc.qx[k], sc.qy[k], &x1, &y1);
                LineIdx(sc.idx, x0, y0, x1, y1, kIdxRiver);
            }
            ++bi.rivers;
        });
    }
    lap(&bi.usRivers);
    // THE RIVERS PASS END

    // THE ROADS PASS: primary always; secondary only when no primary is in view (S1).
    if (layers & kLayerRoads) {
        bmpk::ForEachRoad(pk, sc.qx, sc.qy, sc.cap, [&](int cls, int n) {
            if (cls != 1 && !bi.secondaryDrawn) return;
            float x0, y0, x1, y1;
            ToScreen(f, sc.qx[0], sc.qy[0], &x0, &y0);
            for (int k = 1; k < n; ++k, x0 = x1, y0 = y1) {
                ToScreen(f, sc.qx[k], sc.qy[k], &x1, &y1);
                LineIdx(sc.idx, x0, y0, x1, y1, kIdxRoad);
            }
            if (cls == 1) ++bi.primary;
            else ++bi.secondary;
        });
    }
    lap(&bi.usRoads);
    // THE ROADS PASS END

    // Colour: off the disc 0; land is terrain where a level reaches it, else the theme's land.
    uint16_t ramp[256];
    for (int k = 0; k < 256; ++k) ramp[k] = To565(TerrainRgb(t, (uint8_t)k));
    const uint16_t land = To565(t.land), water = To565(t.water), river = To565(t.river), road = To565(t.road);
    bmpk::Level lv;
    bool covers = false;
    const int level = (layers & kLayerTerrain) ? ChooseLevel(pk, v, &lv, &covers) : -1;
    bi.level = level;
    bi.levelMm = level >= 0 ? lv.radiusMm : 0;
    bi.levelCovers = covers;
    const auto colour = [&](int x, int y, int32_t i, int32_t j, bool sampled) {
        const size_t p = (size_t)y * kSize + x;
        if (wxlayer::OffDisc(x, y, kSize)) {
            out[p] = 0;
            return;
        }
        const uint8_t k = sc.idx[p];
        if (k == kIdxWater) out[p] = water;
        else if (k == kIdxRiver) out[p] = river;
        else if (k == kIdxRoad) out[p] = road;
        else if (sampled && i >= 0 && i < lv.n && j >= 0 && j < lv.n) out[p] = ramp[lv.values[(size_t)j * lv.n + i]];
        else out[p] = land;
    };
    if (level >= 0) {
        ForEachLevelPixel(pk, lv, v, [&](int x, int y, int32_t i, int32_t j) { colour(x, y, i, j, true); });
    } else {
        for (int y = 0; y < kSize; ++y)
            for (int x = 0; x < kSize; ++x) colour(x, y, -1, -1, false);
    }
    lap(&bi.usColour);
    if (clock) bi.usTotal = clock() - t0;
    if (info) *info = bi;
    return true;
}

}  // namespace maplayer
