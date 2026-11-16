// The Blipscope's place names (docs/blipscope-map-client-steps.md, step B: Q-MC7 (c), Q-MB3 to Q-MB9; CB6-CB8). Pure
// C++11, no Arduino, no allocation: the caller owns every buffer.
//
// THE WEATHERSCOPE'S RULE, ITS ARITHMETIC REPEATED WHERE ITS CODE IS TIED TO ITS FRAME (src/weatherscope/WxScreens.h:
// a y band for its strips, a north-up mapper). The radar has no strips and turns its window, so the rule is repeated
// here and held equal to the Weatherscope's by test (CB6 eq), as include/MapLayer.h repeats wxraster::Line under CM11:
//   - the list: the package's PLACES in file order, at most 256; more refuses the towns rather than cutting the list
//     (Q-MB6: a truncated list is a list that lies). No peaks (Q-MC7 (b));
//   - RankOrder: population descending, 0 ranking as 1,000, ties in file order; LabelText: the name as written, at most
//     39 bytes, one trailing " city" dropped;
//   - Cap(radius in miles): 8 from 60, 6 from 30, 5 below (Q-MB3); Inner: 24.4 px;
//   - the anchor: the map's own projection (maplayer::AffineFor, rotation included), in double, then int() truncation;
//   - boxes inclusive: right {ix-1, iy-4, ix+4+w, iy+4}, left {ix-5-w, iy-4, ix+2, iy+4}, w = 6 x bytes - 1; the side
//     away from the centre first; every corner within 116 px; 4 px of air from every obstacle and placed label; NO y band.
// The obstacles (Q-MB4): each airport's mark and code (include/AirportBoxes.h, the one rule DrawAirports draws by,
// CB8), the three ring labels, the four cardinals and the screen-indicator row. The values below are DrawRadarCircles'
// and DrawScreenIndicator's, transcribed; check_map_draw.py's CB8b derives each from its drawer's own text and fails the
// build if either side moves (Q-MB9 (b)).
// Nothing here prints: a nearby town's name says where the device is (CD2).
#pragma once

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "AirportBoxes.h"
#include "BasemapPackage.h"
#include "MapLayer.h"   // maplayer::Affine / AffineFor: the projection Build draws the map with
#include "WxLayer.h"    // wxlayer::View

namespace mapplaces {

using Box = radarbox::Box;

constexpr int kMaxPlaces = 256;   // a package's places held at once; more refuses the towns (Q-MB6)
constexpr int kLabelBytes = 40;   // LabelText's buffer: 39 bytes and the NUL
constexpr int kMaxLabels = 8;     // Cap's largest
constexpr int kAir = 4;           // a label's air from every obstacle and every placed label
constexpr int kCircle = 116;      // every box corner within this many px of the centre
constexpr double kC = 120.0;      // the screen's centre, as the Weatherscope's wxraster::kC

// THE PINNED VALUES BEGIN -- CB8b (check_map_draw.py) derives each from its drawer's own text and compares.
constexpr int kRingCentre = 119;   // DrawRadarCircles: CENTRE = SCREEN_SIZE_DIV_2 - 1
constexpr int kRingLabelX = 123;   // CENTRE + 4
constexpr int kRingLabelY0 = 14;   // CENTRE - ringPx[0] + inset[0]: 119 - 119 + RING_LABEL_OUTER_INSET (14)
constexpr int kRingLabelY1 = 44;   // CENTRE - ringPx[1] + inset[1]: 119 - 78 + 3
constexpr int kRingLabelY2 = 83;   // CENTRE - ringPx[2] + inset[2]: 119 - 39 + 3
constexpr int kCardinalR = 112;    // labelR = CENTRE - 7
constexpr int kDotsX0 = 99;        // DrawScreenIndicator: 4 dots at x 102, 114, 126, 138 (radius 3, then 2), y 224
constexpr int kDotsY0 = 221;
constexpr int kDotsX1 = 140;
constexpr int kDotsY1 = 227;
// THE PINNED VALUES END

struct Town {
    int32_t qx, qy;
    uint32_t pop;
    char label[kLabelBytes];
};

struct List {
    Town t[kMaxPlaces];
    int n = 0;
    bool refused = false;   // the package held more than kMaxPlaces places: no towns at all
};

struct Placed {
    int town = -1;   // its index in the List (file order)
    char side = 0;   // 'R' or 'L'
    Box box{0, 0, 0, 0};
    int tx = 0, ty = 0;   // the text's top-left
    int ix = 0, iy = 0;   // the dot's centre
};

// --------------------------------------------------------------------------------------- the Weatherscope's rule

// wxlogic::LabelText: the name as written (at most 39 bytes), one trailing " city" dropped.
inline void LabelText(const char* name, uint32_t len, char* out)
{
    uint32_t i = 0;
    for (; i < len && i + 1 < (uint32_t)kLabelBytes; ++i) out[i] = name[i];
    out[i] = '\0';
    const size_t n = strlen(out);
    if (n >= 5 && strcmp(out + n - 5, " city") == 0) out[n - 5] = '\0';
}

// wxscreens::RankOrder: population descending, 0 ranking as 1,000, ties keeping the file order (a stable insertion sort).
inline void Rank(const List& l, int* order)
{
    for (int i = 0; i < l.n; ++i) order[i] = i;
    for (int i = 1; i < l.n; ++i) {
        const int v = order[i];
        const uint32_t kv = l.t[v].pop ? l.t[v].pop : 1000u;
        int j = i - 1;
        while (j >= 0 && (l.t[order[j]].pop ? l.t[order[j]].pop : 1000u) < kv) {
            order[j + 1] = order[j];
            --j;
        }
        order[j + 1] = v;
    }
}

// wxscreens::Cap.
inline int Cap(int radiusMi) { return radiusMi >= 60 ? 8 : (radiusMi >= 30 ? 6 : 5); }

// wxscreens::Inner: 17 % of the radius plus 4 px, in the generator's own order (24.4 px at every radius).
inline double Inner(double R) { return 0.17 * R * (kC / R) + 4; }

// wxscreens::Overlaps: overlapping unless more than `pad` px apart on some axis.
inline bool Overlaps(const Box& a, const Box& b, int pad)
{
    return !(a.x1 + pad < b.x0 || b.x1 + pad < a.x0 || a.y1 + pad < b.y0 || b.y1 + pad < a.y0);
}

// wxscreens::InsideCircle: every corner within kCircle px of the centre.
inline bool InsideCircle(const Box& b)
{
    const double c = kC, r = kCircle;
    const int xs[2] = {b.x0, b.x1}, ys[2] = {b.y0, b.y1};
    for (int i = 0; i < 2; ++i)
        for (int j = 0; j < 2; ++j)
            if (!((xs[i] - c) * (xs[i] - c) + (ys[j] - c) * (ys[j] - c) <= r * r)) return false;
    return true;
}

// wxscreens::ClearsCentre: the box's nearest point is at least `inner` px from the centre.
inline bool ClearsCentre(const Box& b, double inner)
{
    const double c = kC;
    const double nx = fmin(fmax(c, (double)b.x0), (double)b.x1), ny = fmin(fmax(c, (double)b.y0), (double)b.y1);
    return (nx - c) * (nx - c) + (ny - c) * (ny - c) >= inner * inner;
}

// ---------------------------------------------------------------------------------------------- the list

// The package's places, in file order, labelled. More than kMaxPlaces refuses the towns (n = 0, refused = true): the
// list is never cut short. Returns false when refused.
inline bool Load(const bmpk::Package& pk, List* out)
{
    out->n = 0;
    out->refused = false;
    int total = 0;
    bmpk::ForEachPlace(pk, [&](int32_t qx, int32_t qy, uint32_t pop, const char* name, uint32_t len) {
        if (total++ >= kMaxPlaces) return;
        Town& t = out->t[out->n++];
        t.qx = qx;
        t.qy = qy;
        t.pop = pop;
        LabelText(name, len, t.label);
    });
    if (total > kMaxPlaces) {
        out->n = 0;
        out->refused = true;
        return false;
    }
    return true;
}

// ------------------------------------------------------------------------------------------- the obstacles

// An airport's two boxes, from the one rule DrawAirports draws by (CB8).
inline int AirportObstacles(int x, int y, const char* code, Box* out)
{
    radarbox::Airport(x, y, (int)strlen(code), &out[0], &out[1]);
    return 2;
}

// DrawRadarCircles' three range-ring labels at this view: the value as String(value, value < 10 ? 1 : 0) prints it, the
// unit on the outer ring only, in the 6 x 8 font at x 123, y 14 / 44 / 83.
inline int RingLabels(double viewRadiusDisplay, const char* unit, Box* out)
{
    static const float kFrac[3] = {1.0f, 2.0f / 3.0f, 1.0f / 3.0f};
    const int ys[3] = {kRingLabelY0, kRingLabelY1, kRingLabelY2};
    for (int i = 0; i < 3; ++i) {
        const float value = viewRadiusDisplay * kFrac[i];
        char buf[32];
        snprintf(buf, sizeof buf, "%.*f", value < 10.0f ? 1 : 0, (double)value);
        const int len = (int)strlen(buf) + (i == 0 ? (int)strlen(unit) : 0);
        out[i] = Box{kRingLabelX, ys[i], kRingLabelX + 6 * len - 1, ys[i] + 7};
    }
    return 3;
}

// DrawRadarCircles' four cardinals for this window-up bearing: at labelR from CENTRE, lroundf, the letter centred.
inline int Cardinals(int radarUpDeg, Box* out)
{
    const int bearings[4] = {0, 90, 180, 270};
    for (int i = 0; i < 4; ++i) {
        const float a = (float)(((float)(bearings[i] - radarUpDeg) - 90.0f) * 0.017453292519943295769236907684886);
        const int px = kRingCentre + (int)lroundf(kCardinalR * cosf(a));
        const int py = kRingCentre + (int)lroundf(kCardinalR * sinf(a));
        out[i] = Box{px - 3, py - 4, px + 2, py + 3};
    }
    return 4;
}

// DrawScreenIndicator's row of four dots.
inline Box IndicatorRow() { return Box{kDotsX0, kDotsY0, kDotsX1, kDotsY1}; }

// -------------------------------------------------------------------------------------------- the anchors

// The map's own projection for this view: Build's coefficients, so a town sits on the map where Build draws its point.
inline maplayer::Affine AnchorAffine(const bmpk::Package& pk, const wxlayer::View& v)
{
    return maplayer::AffineFor(pk, pk.radiusMm, v);
}

inline void Anchor(const maplayer::Affine& a, int32_t qx, int32_t qy, double* x, double* y)
{
    *x = a.ax + a.bx * qx + a.cx * qy;
    *y = a.ay + a.by * qx + a.cy * qy;
}

// ------------------------------------------------------------------------------------------- the placing

// wxscreens::PlaceTown's two candidates for a label anchored at (x, y), in the order they are tried: the side away from
// the centre first (right, unless x < 120). ONE function, read by Place and by CB6 eq.
inline void Candidates(double x, double y, const char* label, Box* cands, int* txs, char* sides)
{
    const int ix = (int)x, iy = (int)y, w = (int)strlen(label) * 6 - 1;
    cands[0] = Box{ix - 1, iy - 4, ix + 4 + w, iy + 4};
    cands[1] = Box{ix - 4 - w - 1, iy - 4, ix + 2, iy + 4};
    txs[0] = ix + 4;
    txs[1] = ix - 4 - w;
    sides[0] = 'R';
    sides[1] = 'L';
    if (x < kC) {
        const Box b = cands[0]; cands[0] = cands[1]; cands[1] = b;
        const int tt = txs[0]; txs[0] = txs[1]; txs[1] = tt;
        const char s = sides[0]; sides[0] = sides[1]; sides[1] = s;
    }
}

inline bool BoxFree(const Box& b, double inner, const Box* fixed, int nFixed, const Placed* placed, int nPlaced)
{
    if (!InsideCircle(b)) return false;
    if (!ClearsCentre(b, inner)) return false;
    for (int i = 0; i < nFixed; ++i)
        if (Overlaps(b, fixed[i], kAir)) return false;
    for (int i = 0; i < nPlaced; ++i)
        if (Overlaps(b, placed[i].box, kAir)) return false;
    return true;
}

// The labels for this view, largest towns first, at most Cap(radiusMi). `out` holds at least kMaxLabels. Returns the
// number placed.
inline int Place(const List& l, const bmpk::Package& pk, const wxlayer::View& v, int radiusMi, const Box* fixed,
                 int nFixed, Placed* out)
{
    if (l.refused || l.n == 0) return 0;
    int order[kMaxPlaces];
    Rank(l, order);
    const int cap = Cap(radiusMi);
    const double inner = Inner((double)radiusMi);
    const maplayer::Affine a = AnchorAffine(pk, v);
    int n = 0;
    for (int k = 0; k < l.n && n < cap && n < kMaxLabels; ++k) {
        const Town& t = l.t[order[k]];
        double x, y;
        Anchor(a, t.qx, t.qy, &x, &y);
        if ((x - kC) * (x - kC) + (y - kC) * (y - kC) < inner * inner) continue;
        if (fabs(x - kC) > 1e6 || fabs(y - kC) > 1e6) continue;   // far off the disc: no box can fit, and no int overflow
        Box cands[2];
        int txs[2];
        char sides[2];
        Candidates(x, y, t.label, cands, txs, sides);
        for (int c = 0; c < 2; ++c) {
            if (!BoxFree(cands[c], inner, fixed, nFixed, out, n)) continue;
            Placed& p = out[n++];
            p.town = order[k];
            p.side = sides[c];
            p.box = cands[c];
            p.tx = txs[c];
            p.ty = (int)y - 3;
            p.ix = (int)x;
            p.iy = (int)y;
            break;
        }
    }
    return n;
}

}  // namespace mapplaces
