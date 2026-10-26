// Weatherscope's screen layer: the stored-mode scenario cycle (3.3d, Q10), and 3.5a's map screens
// (docs/weatherscope-stage3.md A9.2, A9.10) -- the reviewer's generator's layout rules (wxmock.py), the state line, the
// warning texts and arrow (Q17), the stale rules (Q15, Q18) and the zoom ladder -- as pure C++11, drawn only through
// WxRaster.h. test/host/test_wx_layout.cpp holds the rules to A9.10's truth tables, and whole frames are held to
// tools/weatherscope/screens/ref35.py at 57,600 pixels (the K rows). No coordinate is stored here.
//
// Every expression that decides a pixel is written in the SAME order as ref35.py (and so as wxmock.py), because the
// two are compared bit for bit: int() is (int), and every pixel rounding is wxraster::Rnd, floor(v + 0.5) (A9.4).

#pragma once

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "LongPressPolicy.h"
#include "RadarZoom.h"
#include "WxCrop.h"
#include "WxLogic.h"
#include "WxPalette.h"
#include "WxRaster.h"
#include "WxRender.h"
#include "WxTowns.h"

namespace wxscreens {

using wxlogic::LatLon;
using wxraster::Rgb;

// ------------------------------------------------------------------------- the scenario cycle (Q10 (a); CY)

constexpr uint32_t kHoldMs = 30000;
constexpr int kScenarios = 6;   // wxscen::Kind's order: Dry, Approaching, HereNow, Warning, Stale, Clear (Q15)

struct Cycle {
    int index = 0;          // into wxscen::Kind
    uint32_t heldMs = 0;    // unpaused time spent in this scenario
    uint32_t lastMs = 0;
    bool started = false;
    bool paused = false;    // the loop or the legend is open
};

// Feeds the clock; true when the scenario advanced. A paused interval is never counted, and the cycle NEVER advances
// unless the feeds are the stored ones: the live path shows the real weather, never a made-up warning (A8.19 item 5).
inline bool Step(Cycle& c, uint32_t nowMs, bool storedFeeds)
{
    if (!c.started) {
        c.started = true;
        c.lastMs = nowMs;
        return false;
    }
    const uint32_t dt = nowMs - c.lastMs;
    c.lastMs = nowMs;
    if (c.paused) return false;
    if (!storedFeeds) return false;
    c.heldMs += dt;
    if (c.heldMs < kHoldMs) return false;
    c.heldMs = 0;
    c.index = (c.index + 1) % kScenarios;
    return true;
}
inline void Pause(Cycle& c, bool paused) { c.paused = paused; }

// ------------------------------------------------------------------------------------------- text (WT, CK)

// The warning's title (mockup 3: "SEVERE T-STORM") and the word under it; false for anything but the four.
inline bool WarningTitle(const char* event, const char** line1, const char** line2)
{
    static const char* const kTitle[4] = {"TORNADO", "SNOW SQUALL", "SEVERE T-STORM", "FLASH FLOOD"};
    const int rank = wxlogic::WarningRank(event);
    if (rank < 0 || rank > 3) return false;
    *line1 = kTitle[rank];
    *line2 = "WARNING";
    return true;
}

// "4:24 PM": a UTC instant read on a clock offMin minutes from UTC (the scenario's own place and time, A8.1).
inline void ClockText(int64_t utc, int offMin, char* out, size_t cap)
{
    const int64_t local = utc + (int64_t)offMin * 60;
    const int64_t day = ((local % 86400) + 86400) % 86400;
    const int h = (int)(day / 3600), m = (int)(day / 60 % 60);
    snprintf(out, cap, "%d:%02d %s", h % 12 == 0 ? 12 : h % 12, m, h < 12 ? "AM" : "PM");
}

// ------------------------------------------------------------------------------- boxes (OV, CI, IC)

struct Box {
    int x0, y0, x1, y1;   // INCLUSIVE, as the generator's boxes
};

constexpr int kAir = 4;       // a town label's air from every fixed overlay and every placed label (OV)
constexpr int kRingAir = 2;   // a ring number's air from every obstacle (Q22)
constexpr int kCircle = 116;  // every box corner within this many px of the centre (CI)

// Overlapping unless more than `pad` px apart on some axis.
inline bool Overlaps(const Box& a, const Box& b, int pad)
{
    return !(a.x1 + pad < b.x0 || b.x1 + pad < a.x0 || a.y1 + pad < b.y0 || b.y1 + pad < a.y0);
}

inline bool InsideCircle(const Box& b)
{
    const double c = wxraster::kC, r = kCircle;
    const int xs[2] = {b.x0, b.x1}, ys[2] = {b.y0, b.y1};
    for (int i = 0; i < 2; ++i)
        for (int j = 0; j < 2; ++j)
            if (!((xs[i] - c) * (xs[i] - c) + (ys[j] - c) * (ys[j] - c) <= r * r)) return false;
    return true;
}

// The box's nearest point is at least `inner` px from the centre (IC).
inline bool ClearsCentre(const Box& b, double inner)
{
    const double c = wxraster::kC;
    const double nx = fmin(fmax(c, (double)b.x0), (double)b.x1), ny = fmin(fmax(c, (double)b.y0), (double)b.y1);
    return (nx - c) * (nx - c) + (ny - c) * (ny - c) >= inner * inner;
}

// The no-label circle at view radius R: 17 % of the radius plus 4 px, in the generator's own order (24.4 px).
inline double Inner(double R) { return 0.17 * R * (wxraster::kC / R) + 4; }

// ----------------------------------------------------------------------------------- town labels (IC, OV, SD, LB)

struct Placed {
    bool placed = false;
    char side = 0;          // 'R' or 'L'
    Box box{0, 0, 0, 0};
    int tx = 0, ty = 0;     // the text's top-left: (tx, iy - 3)
    int ix = 0, iy = 0;     // the dot's centre: int(x), int(y)
};

// One label anchored at the float point (x, y). Skipped when the anchor is inside the inner circle. Otherwise the side
// away from the centre first (right, unless x < 120), the other as the fallback; a side is rejected when its box
// leaves 26..192 in y, a corner is beyond 116 px, its nearest point is inside the inner circle, or it comes within
// 4 px of a fixed overlay or a placed label.
inline Placed PlaceTown(const char* label, double x, double y, double inner, const Box* fixed, int nFixed,
                        const Box* placed, int nPlaced)
{
    Placed p;
    const double c = wxraster::kC;
    if ((x - c) * (x - c) + (y - c) * (y - c) < inner * inner) return p;
    const int ix = (int)x, iy = (int)y, w = wxraster::TextW(label, 1);
    Box cands[2] = {{ix - 1, iy - 4, ix + 4 + w, iy + 4}, {ix - 4 - w - 1, iy - 4, ix + 2, iy + 4}};
    int txs[2] = {ix + 4, ix - 4 - w};
    char sides[2] = {'R', 'L'};
    if (x < c) {
        const Box b = cands[0]; cands[0] = cands[1]; cands[1] = b;
        const int t = txs[0]; txs[0] = txs[1]; txs[1] = t;
        const char s = sides[0]; sides[0] = sides[1]; sides[1] = s;
    }
    for (int k = 0; k < 2; ++k) {
        const Box& b = cands[k];
        if (b.y0 < wxraster::kTopBand + 2 || b.y1 > wxraster::kBotBand - 2) continue;
        if (!InsideCircle(b)) continue;
        if (!ClearsCentre(b, inner)) continue;
        bool clash = false;
        for (int i = 0; i < nFixed && !clash; ++i) clash = Overlaps(b, fixed[i], kAir);
        for (int i = 0; i < nPlaced && !clash; ++i) clash = Overlaps(b, placed[i], kAir);
        if (clash) continue;
        p.placed = true;
        p.side = sides[k];
        p.box = b;
        p.tx = txs[k];
        p.ty = iy - 3;
        p.ix = ix;
        p.iy = iy;
        return p;
    }
    return p;
}

// `order` = the n towns by population, largest first; 0 means no figure and ranks as 1,000; ties keep the file order
// (a stable insertion sort, as Python's sorted()).
inline void RankOrder(const uint32_t* pop, int n, int* order)
{
    for (int i = 0; i < n; ++i) order[i] = i;
    for (int i = 1; i < n; ++i) {
        const int v = order[i];
        const uint32_t kv = pop[v] ? pop[v] : 1000u;
        int j = i - 1;
        while (j >= 0 && (pop[order[j]] ? pop[order[j]] : 1000u) < kv) {
            order[j + 1] = order[j];
            --j;
        }
        order[j + 1] = v;
    }
}

// The number of labels placed at most: 8 at 60 mi, 6 at 30, 5 at 15 (ZM7).
inline int Cap(int radiusMi) { return radiusMi >= 60 ? 8 : (radiusMi >= 30 ? 6 : 5); }

// Where the towns are, as the screen sees them: a name for LabelText and a float anchor in screen pixels.
class TownSet {
public:
    virtual ~TownSet() {}
    virtual const char* Name(int i) const = 0;
    virtual void At(int i, double* x, double* y) const = 0;
};

constexpr int kMaxLabels = 8;

struct PlacedLabel {
    int town = -1;
    char label[40] = "";
    Placed at;
};

// Walks the ranking and places labels until the cap is reached; returns the count, in placement order (LB3-LB5).
inline int PlaceTowns(const TownSet& t, const int* order, int n, int radiusMi, double inner, const Box* fixed,
                      int nFixed, PlacedLabel* out)
{
    const int cap = Cap(radiusMi);
    Box boxes[kMaxLabels];
    int np = 0;
    for (int k = 0; k < n && np < cap && np < kMaxLabels; ++k) {
        PlacedLabel& o = out[np];
        wxlogic::LabelText(t.Name(order[k]), o.label, sizeof o.label);
        double x, y;
        t.At(order[k], &x, &y);
        o.at = PlaceTown(o.label, x, y, inner, fixed, nFixed, boxes, np);
        if (!o.at.placed) continue;
        o.town = order[k];
        boxes[np++] = o.at.box;
    }
    return np;
}

// ------------------------------------------------------------------------------------------ rings (RG, RD, RL)

// The ring radii in miles at view radius R: 5, 15 and 30, each where 15 % <= mi <= 85 % of R.
inline int RingsFor(double R, int* mi)
{
    static const int kAll[3] = {5, 15, 30};
    int n = 0;
    for (int i = 0; i < 3; ++i)
        if (0.15 * R <= kAll[i] && kAll[i] <= 0.85 * R) mi[n++] = kAll[i];
    return n;
}

// A ring's number at the first angle of 45, 135, 315, 225, 90, 270, 0, 180 whose box stays in 25..193, inside the
// 116 px circle, and 2 px clear of every obstacle.
inline bool PlaceRingLabel(double rpx, const char* label, const Box* obs, int nObs, int* tx, int* ty, Box* box)
{
    static const int kAng[8] = {45, 135, 315, 225, 90, 270, 0, 180};
    const double c = wxraster::kC;
    const int w = wxraster::TextW(label, 1);
    for (int i = 0; i < 8; ++i) {
        const int ang = kAng[i];
        const double a = ang * (wxraster::kPi / 180.0);
        const int x = (int)(c + rpx * sin(a)) + (ang < 180 ? 2 : -w - 2);
        const int up = (ang == 45 || ang == 315 || ang == 0) ? 9 : ((ang == 135 || ang == 225 || ang == 180) ? 1 : -4);
        const int y = (int)(c - rpx * cos(a)) - up;
        const Box b{x - 1, y - 1, x + w + 1, y + 8};
        if (b.y0 < wxraster::kTopBand + 1 || b.y1 > wxraster::kBotBand - 1 || !InsideCircle(b)) continue;
        bool clash = false;
        for (int k = 0; k < nObs && !clash; ++k) clash = Overlaps(b, obs[k], kRingAir);
        if (clash) continue;
        *tx = x;
        *ty = y;
        *box = b;
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------- the nearest echo in view (ST11)

constexpr double kInViewMi = 60.0;   // Q16: an echo counts only within 60.0 mi of the location, at every zoom

struct Echo {
    bool found = false;
    double mi = 0, bearingDeg = 0;
};

// The nearest echo cell (code 1-13) within boundMi of `here`: row-major, the first minimum wins.
inline Echo NearestEchoInView(const wxcrop::Header& h, const uint8_t* codes, LatLon here, double boundMi)
{
    Echo e;
    const double latNw = h.latNwMicro / 1e6, lonNw = h.lonNwMicro / 1e6;
    const double cellLat = h.cellLatMicro / 1e6, cellLon = h.cellLonMicro / 1e6;
    const double kx = wxlogic::kKmPerDeg * cos(here.lat * (wxlogic::kPi / 180.0)), ky = wxlogic::kKmPerDeg;
    double best = -1, bx = 0, by = 0;
    for (uint32_t r = 0; r < h.rows; ++r) {
        const double dy = (latNw - r * cellLat - here.lat) * ky;
        for (uint32_t c = 0; c < h.cols; ++c) {
            if (!wxcrop::IsEcho(codes[r * (uint32_t)h.cols + c])) continue;
            const double dx = (lonNw + c * cellLon - here.lon) * kx;
            const double d2 = dx * dx + dy * dy;
            if (sqrt(d2) / wxlogic::kKmPerMi > boundMi) continue;
            if (best < 0 || d2 < best) {
                best = d2;
                bx = dx;
                by = dy;
            }
        }
    }
    if (best < 0) return e;
    e.found = true;
    e.mi = sqrt(best) / wxlogic::kKmPerMi;
    e.bearingDeg = fmod(atan2(bx, by) * (180.0 / wxlogic::kPi) + 360, 360);
    return e;
}

// ------------------------------------------------------------------------------- the state line and time (ST, TM)

constexpr int64_t kOrangeAfterS = 600;   // the frame time turns orange past 10 min
constexpr int64_t kStaleAfterS = 1800;   // RADAR STALE past 30 min (Q15, Q18)

struct StateIn {
    wxlogic::Precip hereNow = wxlogic::Precip::None;
    Echo echo;                // the nearest echo in view
    bool hasTemp = false;
    double tempF = 0;
    int64_t ageS = 0;         // the frame's age
};

// The band's line on every screen but the warning's: RADAR STALE (dim) past 30 min; RAIN NOW / SNOW NOW (white); the
// nearest echo, "RAIN 14 MI E", white within 15 mi and dim beyond; else "54° · NO RAIN" or "NO RAIN 60 MI" (dim, Q20).
inline void StateLine(const StateIn& in, char* out, size_t cap, Rgb* ink)
{
    *ink = wxraster::kDim;
    if (in.ageS > kStaleAfterS) {
        snprintf(out, cap, "RADAR STALE");
    } else if (in.hereNow != wxlogic::Precip::None) {
        snprintf(out, cap, "%s", in.hereNow == wxlogic::Precip::Snow ? "SNOW NOW" : "RAIN NOW");
        *ink = wxraster::kWhite;
    } else if (in.echo.found) {
        snprintf(out, cap, "RAIN %d MI %s", wxraster::Rnd(in.echo.mi), wxlogic::Compass8(in.echo.bearingDeg));
        if (in.echo.mi <= 15.0) *ink = wxraster::kWhite;
    } else if (in.hasTemp) {
        snprintf(out, cap, "%d\xF8 \xFA NO RAIN", wxraster::Rnd(in.tempF));
    } else {
        snprintf(out, cap, "NO RAIN 60 MI");
    }
}

// The top strip's frame time: orange past 10 min; on the warning screen past 30 min, "4:24 PM · STALE" (Q18).
inline void TimeText(const char* clock, int64_t ageS, bool warning, char* out, size_t cap, Rgb* ink)
{
    if (warning && ageS > kStaleAfterS) snprintf(out, cap, "%s \xFA STALE", clock);
    else snprintf(out, cap, "%s", clock);
    *ink = ageS > kOrangeAfterS ? wxraster::kOrange : wxraster::kDim;
}

// "ZOOM 30 MI" when zoomed in; false at the ladder's top.
inline bool ZoomTag(int radiusMi, char* out, size_t cap)
{
    if (radiusMi >= 60) return false;
    snprintf(out, cap, "ZOOM %d MI", radiusMi);
    return true;
}

// Where the top strip's time and tag start (ZM8): the time centred alone, or the time and tag as one centred run.
struct TopStrip {
    int timeX = 0, tagX = 0;
    bool hasTag = false;
    char tag[24] = "";   // "ZOOM %d MI" at its longest int: 20 bytes (g++ 11 -Wformat-truncation)
};
inline TopStrip TopStripAt(const char* time, int radiusMi)
{
    TopStrip t;
    t.hasTag = ZoomTag(radiusMi, t.tag, sizeof t.tag);
    if (!t.hasTag) {
        t.timeX = (int)(wxraster::kC - wxraster::TextW(time, 1) / 2.0);
        return t;
    }
    char run[80], lead[64];
    snprintf(run, sizeof run, "%s  %s", time, t.tag);
    snprintf(lead, sizeof lead, "%s  ", time);
    t.timeX = (int)(wxraster::kC - wxraster::TextW(run, 1) / 2.0);
    t.tagX = t.timeX + wxraster::TextW(lead, 1) + 1;
    return t;
}

// ---------------------------------------------------------------------------------- the warning texts (WN)

struct WarningTexts {
    char top1[24] = "", top2[48] = "", band1[64] = "", band2[64] = "";
};

// The title; "WARNING · UNTIL 5:09 PM"; LineA's tokens joined by " · "; "STORM 14 MI E · NE 35 MPH" (the travel
// without its "MOVING ") when the warning carries a motion, else empty.
inline WarningTexts MakeWarningTexts(const wxlogic::Warn& w, const wxlogic::MotionText& m)
{
    WarningTexts t;
    const char* l1 = nullptr;
    const char* l2 = nullptr;
    if (WarningTitle(w.event, &l1, &l2)) snprintf(t.top1, sizeof t.top1, "%s", l1);
    char until[24] = "";
    if (wxlogic::UntilText(w.expiresIso, until, sizeof until)) snprintf(t.top2, sizeof t.top2, "WARNING \xFA %s", until);
    else snprintf(t.top2, sizeof t.top2, "WARNING");
    const wxlogic::Tokens k = wxlogic::LineA(w);
    size_t len = 0;
    for (int i = 0; i < k.n; ++i) {
        const int n = snprintf(t.band1 + len, sizeof t.band1 - len, "%s%s", i ? " \xFA " : "", k.t[i]);
        if (n < 0 || (size_t)n >= sizeof t.band1 - len) break;
        len += (size_t)n;
    }
    if (m.valid) {
        const char* travel = m.travel;
        if (strncmp(travel, "MOVING ", 7) == 0) travel += 7;
        char buf[96];
        snprintf(buf, sizeof buf, "STORM %s %s \xFA %s", m.distance, m.bearing, travel);
        const size_t n = strlen(buf) < sizeof t.band2 - 1 ? strlen(buf) : sizeof t.band2 - 1;
        memcpy(t.band2, buf, n);
        t.band2[n] = '\0';
    }
    return t;
}

// ----------------------------------------------------------------------------------------- the arrow (AR)

struct Arrow {
    double seg[3][4];   // the shaft, then the two head strokes: x0, y0, x1, y1 in screen pixels
};

// From the storm's projected position: a 12 px shaft along the direction of travel (fromDeg + 180), and a head of
// two 4 px strokes at +-150 degrees from it, at the tip (Q17).
inline Arrow MakeArrow(double sx, double sy, int fromDeg)
{
    Arrow a;
    const int travel = (fromDeg + 180) % 360;
    const double t = travel * (wxraster::kPi / 180.0);
    const double tipx = sx + 12 * sin(t), tipy = sy - 12 * cos(t);
    const double h1 = (travel + 150) * (wxraster::kPi / 180.0), h2 = (travel - 150) * (wxraster::kPi / 180.0);
    const double v[3][4] = {{sx, sy, tipx, tipy},
                            {tipx, tipy, tipx + 4 * sin(h1), tipy - 4 * cos(h1)},
                            {tipx, tipy, tipx + 4 * sin(h2), tipy - 4 * cos(h2)}};
    memcpy(a.seg, v, sizeof v);
    return a;
}

// The arrow's box, a fixed overlay for the labels: its end points' bounds, int() as the polygon edges' boxes.
inline Box ArrowBox(const Arrow& a)
{
    double x0 = a.seg[0][0], y0 = a.seg[0][1], x1 = x0, y1 = y0;
    for (int s = 0; s < 3; ++s)
        for (int p = 0; p < 2; ++p) {
            x0 = fmin(x0, a.seg[s][2 * p]);
            y0 = fmin(y0, a.seg[s][2 * p + 1]);
            x1 = fmax(x1, a.seg[s][2 * p]);
            y1 = fmax(y1, a.seg[s][2 * p + 1]);
        }
    return Box{(int)x0, (int)y0, (int)x1, (int)y1};
}

inline void DrawArrow(wxraster::Sink& s, const Arrow& a)
{
    for (int k = 0; k < 3; ++k) wxraster::Line(s, a.seg[k][0], a.seg[k][1], a.seg[k][2], a.seg[k][3], wxraster::kOrange);
}

// No arrow when the storm is outside the 60 mi view (Q17).
inline bool ArrowVisible(LatLon here, LatLon storm) { return wxlogic::DistanceMi(here, storm) <= kInViewMi; }

// ------------------------------------------------------------------------------------------------ zoom (ZM)

constexpr uint32_t kZoomIdleMs = 600000;   // back to 60 mi after 10 min without a touch (A9.3)

// The {15, 30, 60} mi ladder, as a literal: radarzoom::BuildLadder(60) would give {5, 10, 25, 50, 60} (A9.3).
inline radarzoom::Ladder ZoomLadder()
{
    radarzoom::Ladder l;
    l.step[0] = 15.0f;
    l.step[1] = 30.0f;
    l.step[2] = 60.0f;
    l.n = 3;
    return l;
}
inline int ZoomMiles(const radarzoom::Ladder& l, int idx) { return (int)l.step[idx]; }
inline int IdleReturn(const radarzoom::Ladder& l, int idx, uint32_t nowMs, uint32_t lastTouchMs)
{
    return radarzoom::IdleExpired(nowMs, lastTouchMs, kZoomIdleMs) ? l.Top() : idx;
}

// ------------------------------------------------------------------------------------------ the compositor

// One row of codes as the map is drawn (the loop hashes them, A8.20).
class CodeRows {
public:
    virtual ~CodeRows() {}
    virtual void Row(const uint8_t* codes, int n) = 0;
};

// The map layer: black off the disc, the crop's colours on it (rain only until WXC1 v2, A9.6), hatched where code 15.
// Every row's codes are worked out and handed to `rows` (B2 hashes all 57,600), but the band rows (y < 24, y >= 194)
// are not drawn: the bands overwrite them in every composition, so drawing them first was 16,800 wasted writes
// (A9.23). A 3.5b basemap goes UNDER the radar here, as a dry pixel's colour (A9.23, the second paste's item 3).
inline void DrawMap(wxraster::Sink& s, const wxcrop::Header& h, const uint8_t* codes, const wxrender::View& v,
                    CodeRows* rows)
{
    uint8_t row[wxrender::kSize];
    for (int y = 0; y < wxrender::kSize; ++y) {
        for (int x = 0; x < wxrender::kSize; ++x) row[x] = wxrender::CodeAt(h, codes, v, x, y);
        if (rows) rows->Row(row, wxrender::kSize);
        if (y < wxraster::kTopBand || y >= wxraster::kBotBand) continue;
        for (int x = 0; x < wxrender::kSize; ++x) {
            const uint8_t c = row[x];
            if (c == wxrender::kOffDisc) {
                s.Put(x, y, wxraster::kBlack);
                continue;
            }
            const wxpalette::Rgb p = c == 15 ? wxpalette::MissingAt(x, y) : wxpalette::Code(c, false, wxpalette::kDim55);
            s.Put(x, y, Rgb{p.r, p.g, p.b});
        }
    }
}

// Projection at the view's radius: x = 120 + dLon / rLon * 120, y = 120 - dLat / rLat * 120, as floats.
inline void Project(const wxrender::View& v, double lat, double lon, double* x, double* y)
{
    *x = wxraster::kC + (lon - v.homeLon) / v.rLon * wxraster::kC;
    *y = wxraster::kC - (lat - v.homeLat) / v.rLat * wxraster::kC;
}

// The Census towns, projected through a view.
class ViewTowns : public TownSet {
public:
    ViewTowns(const wxtowns::Town* t, const wxrender::View& v) : t_(t), v_(v) {}
    const char* Name(int i) const override { return t_[i].name; }
    void At(int i, double* x, double* y) const override { Project(v_, t_[i].lat, t_[i].lon, x, y); }

private:
    const wxtowns::Town* t_;
    const wxrender::View& v_;
};

constexpr int kLoopFrames = 15;

struct ScreenIn {
    const wxcrop::Header* h = nullptr;
    const uint8_t* codes = nullptr;
    int radiusMi = 60;
    char clock[16] = "";              // the frame's time on the scenario's clock
    int64_t ageS = 0;                 // the frame's age (TM, ST12)
    bool warning = false;
    LatLon polygon[4];                // the warning's four corners, edges i -> i + 1 and back
    WarningTexts wt;
    bool arrow = false;               // a motion is present and the storm is within 60 mi (Q17)
    LatLon storm{0, 0};
    int fromDeg = 0;
    char state[40] = "";              // the band's line, every screen but the warning's and the loop's
    Rgb stateInk{120, 120, 120};
    bool loop = false;                // the loop's band instead (A9.10's K rows)
    int loopIndex = 0;
    char loopAge[32] = "";            // "%ld MIN AGO" at its widest long: 29 bytes (-Wformat-truncation)
    const wxtowns::Town* towns = nullptr;
    int nTowns = 0;
    const int* order = nullptr;       // RankOrder of the towns' populations, worked out once
};

struct ScreenOut {
    PlacedLabel labels[kMaxLabels];
    int nLabels = 0;
    char time[40] = "";
    Rgb timeInk{0, 0, 0};
};

// Compose's phases, timed by the caller's clock (A9.22: timing only -- a profile changes no pixel). t[0] is taken on
// entry and t[i + 1] at the end of phase i, in this order. A full Compose's replay takes no time; a layered frame's
// invariant phases take none (A9.23).
struct Profile {
    enum Phase { kView, kMap, kBands, kRings, kWarning, kLabels, kRingLabels, kHome, kReplay, kStrip, kBand, kPhases };
    uint32_t (*clock)() = nullptr;
    uint32_t t[kPhases + 1] = {};
    uint32_t Us(int phase) const { return t[phase + 1] - t[phase]; }
    uint32_t Sum() const { return t[kPhases] - t[0]; }
    static const char* Name(int phase)
    {
        static const char* const k[kPhases] = {"view", "map", "bands", "rings", "warning", "labels", "ringlabels",
                                               "home", "replay", "strip", "band"};
        return k[phase];
    }
};
inline void Stamp(Profile* p, int i)
{
    if (p && p->clock) p->t[i] = p->clock();
}

// The frame-INVARIANT group (A9.23): the rings, the warning (polygon, title, subtitle, arrow), the town labels, the
// ring numbers and the home marker, in the generator's order. One function, so the full Compose and the layer's
// build draw it with the same code. Nothing here reads a pixel: every write is an opaque Put, so each pixel's final
// value is its last write, whatever is beneath it (A9.23's exactness check).
inline void DrawInvariant(wxraster::Sink& s, const ScreenIn& in, const wxrender::View& v, PlacedLabel* labels,
                          int* nLabels, Profile* prof = nullptr)
{
    using namespace wxraster;
    Box fixed[12];
    int nFixed = 0;
    fixed[nFixed++] = Box{0, 0, kSize - 1, kTopBand - 1};
    fixed[nFixed++] = Box{0, kBotBand, kSize - 1, kSize - 1};
    const double R = in.radiusMi, ppm = kC / R;
    int rings[3];
    const int nRings = RingsFor(R, rings);
    for (int i = 0; i < nRings; ++i) RingDots(s, rings[i] * ppm, kRing);
    Stamp(prof, Profile::kRings + 1);

    if (in.warning) {
        double px[4], py[4];
        for (int i = 0; i < 4; ++i) Project(v, in.polygon[i].lat, in.polygon[i].lon, &px[i], &py[i]);
        for (int i = 0; i < 4; ++i) {
            const int j = (i + 1) % 4;
            Line(s, px[i], py[i], px[j], py[j], kOrange);
            fixed[nFixed++] = Box{(int)fmin(px[i], px[j]), (int)fmin(py[i], py[j]), (int)fmax(px[i], px[j]),
                                  (int)fmax(py[i], py[j])};
        }
        const int tx = (int)(kC - TextW(in.wt.top1, 2) / 2.0);
        Text(s, tx, 36, in.wt.top1, 2, kOrange, &kBlack);
        const int sx = (int)(kC - TextW(in.wt.top2, 1) / 2.0);
        Text(s, sx, 54, in.wt.top2, 1, kOrange, &kBlack);
        fixed[nFixed++] = Box{tx - 2, 34, tx + TextW(in.wt.top1, 2) + 2, 36 + 14 + 2};
        fixed[nFixed++] = Box{sx - 2, 52, sx + TextW(in.wt.top2, 1) + 2, 54 + 8 + 2};
        if (in.arrow) {
            double ax, ay;
            Project(v, in.storm.lat, in.storm.lon, &ax, &ay);
            const Arrow a = MakeArrow(ax, ay, in.fromDeg);
            DrawArrow(s, a);
            fixed[nFixed++] = ArrowBox(a);
        }
    }
    Stamp(prof, Profile::kWarning + 1);

    const ViewTowns towns(in.towns, v);
    *nLabels = PlaceTowns(towns, in.order, in.nTowns, in.radiusMi, Inner(R), fixed, nFixed, labels);
    for (int i = 0; i < *nLabels; ++i) {
        const Placed& p = labels[i].at;
        Fill(s, p.ix - 1, p.iy - 1, p.ix + 1, p.iy + 1, kLabel);
        Text(s, p.tx, p.ty, labels[i].label, 1, kLabel, &kBlack);
    }
    Stamp(prof, Profile::kLabels + 1);

    Box obs[12 + kMaxLabels + 3];
    int nObs = 0;
    for (int i = 0; i < nFixed; ++i) obs[nObs++] = fixed[i];
    for (int i = 0; i < *nLabels; ++i) obs[nObs++] = labels[i].at.box;
    for (int i = 0; i < nRings; ++i) {
        char num[12];   // an int's widest, for -Wformat-truncation
        snprintf(num, sizeof num, "%d", rings[i]);
        int tx, ty;
        Box b;
        if (!PlaceRingLabel(rings[i] * ppm, num, obs, nObs, &tx, &ty, &b)) continue;
        Text(s, tx, ty, num, 1, kRingText, &kBlack);
        obs[nObs++] = b;
    }
    Stamp(prof, Profile::kRingLabels + 1);

    HomeMarker(s, kWhite);
    Stamp(prof, Profile::kHome + 1);
}

// The frame-VARIANT tail: the top strip (N, the time, the ZOOM tag -- its x depends on the time's width) and the band.
// Its pixels never meet the invariant group's (y <= 20 against y >= 24 above the band; the band's against y <= 193).
inline void DrawStripAndBand(wxraster::Sink& s, const ScreenIn& in, ScreenOut* out, Profile* prof)
{
    using namespace wxraster;
    Text(s, 117, 3, "N", 1, kDim, nullptr);
    TimeText(in.clock, in.loop ? 0 : in.ageS, in.warning, out->time, sizeof out->time, &out->timeInk);
    const TopStrip top = TopStripAt(out->time, in.radiusMi);
    Text(s, top.timeX, 13, out->time, 1, out->timeInk, nullptr);
    if (top.hasTag) Text(s, top.tagX, 13, top.tag, 1, kWhite, nullptr);
    Stamp(prof, Profile::kStrip + 1);

    if (in.warning) {
        Text(s, (int)(kC - TextW(in.wt.band1, 1) / 2.0), 197, in.wt.band1, 1, kWhite, nullptr);
        Text(s, (int)(kC - TextW(in.wt.band2, 1) / 2.0), 207, in.wt.band2, 1, kWhite, nullptr);
    } else if (in.loop) {
        Text(s, (int)(kC - TextW(in.loopAge, 1) / 2.0), 198, in.loopAge, 1, kDim, nullptr);
        Fill(s, 60, 212, 179, 213, kRingText);
        const int kx = 60 + in.loopIndex * 116 / (kLoopFrames - 1);
        Fill(s, kx, 210, kx + 3, 215, kWhite);
    } else {
        Text(s, (int)(kC - TextW(in.state, 2) / 2.0), 198, in.state, 2, in.stateInk, nullptr);
    }
    Stamp(prof, Profile::kBand + 1);
}

// A whole screen, in the generator's order (A9.2), with the top strip drawn after the invariant group (A9.23: their
// pixels never meet, so no frame changes; the K rows hold it). `v` is the caller's view, rebuilt for the radius.
// `prof`, when given, is stamped at each phase boundary and changes nothing drawn.
inline void Compose(wxraster::Sink& s, const ScreenIn& in, wxrender::View& v, ScreenOut* out, CodeRows* rows = nullptr,
                    Profile* prof = nullptr)
{
    using namespace wxraster;
    Stamp(prof, 0);
    wxrender::BuildViewKm(*in.h, &v, in.radiusMi * 1.609344);
    Stamp(prof, Profile::kView + 1);
    DrawMap(s, *in.h, in.codes, v, rows);
    Stamp(prof, Profile::kMap + 1);
    Fill(s, 0, 0, kSize - 1, kTopBand - 1, kBlack);
    Fill(s, 0, kBotBand, kSize - 1, kSize - 1, kBlack);
    Stamp(prof, Profile::kBands + 1);
    DrawInvariant(s, in, v, out->labels, &out->nLabels, prof);
    Stamp(prof, Profile::kReplay + 1);
    DrawStripAndBand(s, in, out, prof);
}

// ------------------------------------------------------------------------------------- the layer (A9.21 item 3)

// What a layer was built for. Rebuilt when the zoom, the scenario, the warning or the home (the crop's geometry)
// differs (A9.21's rule). A plain struct with no padding (A9.23: 192 B on the board), so it compares bytewise.
struct LayerKey {
    int32_t scenario, radiusMi;
    int32_t latNwMicro, lonNwMicro;
    uint32_t cellLatMicro, cellLonMicro;
    uint16_t rows, cols;
    int32_t warning, arrow, fromDeg;
    LatLon polygon[4];
    LatLon storm;
    char top1[24], top2[48];
};

inline LayerKey MakeLayerKey(const ScreenIn& in, int scenario)
{
    LayerKey k;
    memset(&k, 0, sizeof k);
    k.scenario = scenario;
    k.radiusMi = in.radiusMi;
    k.latNwMicro = in.h->latNwMicro;
    k.lonNwMicro = in.h->lonNwMicro;
    k.cellLatMicro = in.h->cellLatMicro;
    k.cellLonMicro = in.h->cellLonMicro;
    k.rows = in.h->rows;
    k.cols = in.h->cols;
    k.warning = in.warning ? 1 : 0;
    if (in.warning) {
        for (int i = 0; i < 4; ++i) k.polygon[i] = in.polygon[i];
        memcpy(k.top1, in.wt.top1, sizeof k.top1);
        memcpy(k.top2, in.wt.top2, sizeof k.top2);
        k.arrow = in.arrow ? 1 : 0;
        if (in.arrow) {
            k.storm = in.storm;
            k.fromDeg = in.fromDeg;
        }
    }
    return k;
}

inline bool SameLayer(const LayerKey& a, const LayerKey& b) { return memcmp(&a, &b, sizeof a) == 0; }

// The invariant group, drawn once: a dense palette-index image (0 = not in the layer), its 15 colours, the view it was
// drawn through, and the labels it placed. 59,488 B on the board (A9.23), in PSRAM.
struct Layer {
    LayerKey key;
    bool valid = false;
    wxrender::View view;
    Rgb palette[16];
    uint8_t nPalette = 0;
    PlacedLabel labels[kMaxLabels];
    int nLabels = 0;
    uint32_t px32[wxraster::kSize * wxraster::kSize / 4];   // one byte a pixel, read a word at a time
};

// Records the invariant group into a layer's image: the last write to a pixel wins, as on a screen.
class LayerSink : public wxraster::Sink {
public:
    explicit LayerSink(Layer& l) : l_(l), px_((uint8_t*)l.px32) {}
    bool overflow = false;
    void Put(int x, int y, Rgb c) override
    {
        uint8_t k = 1;
        while (k <= l_.nPalette && !(l_.palette[k - 1] == c)) ++k;
        if (k > l_.nPalette) {
            if (l_.nPalette == 15) {
                overflow = true;
                return;
            }
            l_.palette[l_.nPalette++] = c;
        }
        px_[y * wxraster::kSize + x] = k;
    }

private:
    Layer& l_;
    uint8_t* px_;
};

// Builds the layer for `in` (its view, its invariant group, its labels); returns the pixels it holds. A palette past
// 15 colours leaves it invalid, and ComposeLayered then draws the full Compose instead (exact either way).
inline int BuildLayer(Layer& l, const ScreenIn& in, const LayerKey& key)
{
    l.key = key;
    l.valid = false;
    l.nPalette = 0;
    memset(l.px32, 0, sizeof l.px32);
    wxrender::BuildViewKm(*in.h, &l.view, in.radiusMi * 1.609344);
    LayerSink sink(l);
    DrawInvariant(sink, in, l.view, l.labels, &l.nLabels);
    l.valid = !sink.overflow;
    int n = 0;
    const uint8_t* b = (const uint8_t*)l.px32;
    for (int i = 0; i < wxraster::kSize * wxraster::kSize; ++i) n += b[i] != 0;
    return n;
}

// Puts the layer's pixels, a word at a time: the image is mostly empty.
inline void Replay(wxraster::Sink& s, const Layer& l)
{
    const int words = wxraster::kSize * wxraster::kSize / 4;
    for (int w = 0; w < words; ++w) {
        const uint32_t v = l.px32[w];
        if (!v) continue;
        const uint8_t* b = (const uint8_t*)&l.px32[w];
        for (int k = 0; k < 4; ++k) {
            if (!b[k]) continue;
            const int i = w * 4 + k;
            s.Put(i % wxraster::kSize, i / wxraster::kSize, l.palette[b[k] - 1]);
        }
    }
}

// A loop frame through the layer (A9.21 item 3 (a)): the map, the bands, the layer's replay, the strip and the band --
// the same 57,600 pixels as Compose (test_wx_replay.cpp holds every loop frame to it). The layer is rebuilt here only
// if its key no longer matches (the firmware builds it at the tap, outside any frame). Returns whether it rebuilt.
inline bool ComposeLayered(wxraster::Sink& s, const ScreenIn& in, Layer& l, int scenario, ScreenOut* out,
                           CodeRows* rows = nullptr, Profile* prof = nullptr)
{
    using namespace wxraster;
    const LayerKey key = MakeLayerKey(in, scenario);
    bool rebuilt = false;
    if (!l.valid || !SameLayer(l.key, key)) {
        BuildLayer(l, in, key);
        rebuilt = true;
    }
    if (!l.valid) {   // a palette past 15 colours: the full Compose, through the layer's own view (no static state)
        Compose(s, in, l.view, out, rows, prof);
        return rebuilt;
    }
    Stamp(prof, 0);
    Stamp(prof, Profile::kView + 1);
    DrawMap(s, *in.h, in.codes, l.view, rows);
    Stamp(prof, Profile::kMap + 1);
    Fill(s, 0, 0, kSize - 1, kTopBand - 1, kBlack);
    Fill(s, 0, kBotBand, kSize - 1, kSize - 1, kBlack);
    Stamp(prof, Profile::kBands + 1);
    for (int p = Profile::kRings; p <= Profile::kHome; ++p) Stamp(prof, p + 1);
    Replay(s, l);
    out->nLabels = l.nLabels;
    for (int i = 0; i < l.nLabels; ++i) out->labels[i] = l.labels[i];
    Stamp(prof, Profile::kReplay + 1);
    DrawStripAndBand(s, in, out, prof);
    return rebuilt;
}

}  // namespace wxscreens
