// Weatherscope 3.3d's screen layer (docs/weatherscope-stage3.md A8.20): everything about a screen that is not a pixel
// of the map -- the stored-mode scenario cycle, the warning's title, the clock text, the headline's wrap, every
// overlay's rectangle, and the town labels on top of Q6(a) -- as pure C++11, so the host builds the SAME layout the
// board draws (test/host/test_wx_screens.cpp, and its --layout mode, which A8.21's rectangle tables come from).
//
// Every overlay is a rectangle, drawn opaque (a black box under each text), so the board's /diag/fb image can be
// held to the map's reference everywhere else (A8.19 item 3). No coordinate is stored here.

#pragma once

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "WxCrop.h"
#include "WxLogic.h"
#include "WxRender.h"
#include "WxScenarios.h"
#include "WxTowns.h"

namespace wxscreens {

using wxlogic::LatLon;

// ------------------------------------------------------------------------- the scenario cycle (Q10 (a); CY)

constexpr uint32_t kHoldMs = 30000;
constexpr int kScenarios = 4;   // wxscen::Kind's order: Dry, Approaching, HereNow, Warning

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

// ------------------------------------------------------------------------------------ text (WT, CK, HW, TB)

// The warning screen's two title lines (mockup 3: "SEVERE T-STORM" / "WARNING"); false for anything else.
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

constexpr int kMaxLine = 40;

// Greedy word wrap at maxChars per line; returns the line count (at most maxLines). A word longer than a line is
// placed alone on its line, uncut.
inline int Wrap(const char* s, int maxChars, char (*lines)[kMaxLine], int maxLines)
{
    int n = 0, len = 0;
    for (const char* p = s; *p && n <= maxLines;) {
        while (*p == ' ') ++p;
        if (!*p) break;
        const char* e = p;
        while (*e && *e != ' ') ++e;
        const int w = (int)(e - p);
        if (n > 0 && len > 0 && len + 1 + w <= maxChars) {
            lines[n - 1][len++] = ' ';
        } else {
            if (n == maxLines) break;
            ++n;
            len = 0;
        }
        for (int i = 0; i < w && len < kMaxLine - 1; ++i) lines[n - 1][len++] = p[i];
        lines[n - 1][len] = '\0';
        p = e;
    }
    return n;
}

struct Rect {
    int x, y, w, h;   // pixels: [x, x + w) x [y, y + h)
};
inline bool operator==(const Rect& a, const Rect& b) { return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h; }
inline bool Overlaps(const Rect& a, const Rect& b)
{
    return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}

// The panel font: 6 x 8 px per character per size step. A text item is centred on x = 120 (A8.20).
constexpr int kCharW = 6, kCharH = 8;
inline Rect TextBox(size_t nChars, int size, int y)
{
    const int w = kCharW * (int)nChars * size;
    return Rect{wxrender::kSize / 2 - w / 2, y, w, kCharH * size};
}

// The pixels at least one rectangle covers, inside the 240 x 240 screen (UN).
inline long UnionArea(const Rect* r, int n)
{
    long count = 0;
    for (int y = 0; y < wxrender::kSize; ++y)
        for (int x = 0; x < wxrender::kSize; ++x)
            for (int i = 0; i < n; ++i)
                if (x >= r[i].x && x < r[i].x + r[i].w && y >= r[i].y && y < r[i].y + r[i].h) { ++count; break; }
    return count;
}

// ---------------------------------------------------------------------------------- the map's projection

// The pixel a place lands on: WxRender's projection, x = 120 + dLon / rLon * 120, y = 120 - dLat / rLat * 120,
// floored (pixel x covers [x, x + 1), and is sampled at its centre).
inline int ProjX(const wxrender::View& v, double lon)
{
    return (int)floor(wxrender::kCentre + (lon - v.homeLon) / v.rLon * wxrender::kCentre);
}
inline int ProjY(const wxrender::View& v, double lat)
{
    return (int)floor(wxrender::kCentre - (lat - v.homeLat) / v.rLat * wxrender::kCentre);
}

// ------------------------------------------------------------------------------------------- the layout

enum class Ink : uint8_t { White, Dim, Orange, Label };
enum class ItemKind : uint8_t { Text, Marker, Edge, Town };

struct Item {
    Rect r;
    ItemKind kind;
    uint8_t size;       // Text: the font size
    Ink ink;
    int dotX, dotY;     // Town: the dot's centre pixel
    char text[kMaxLine];
};

constexpr int kMaxItems = 96;
constexpr int kMaxCandidates = 64;   // PlaceLabels' own cap

struct Layout {
    Item items[kMaxItems];
    int n = 0;
    int nFixed = 0;   // items[0, nFixed) are the screen's fixed overlays; the rest are placed town labels
};

// The texts a screen shows, worked out by WxLogic from a scenario (K3's functions).
struct Texts {
    char clock[16] = "";
    char headline[kMaxLine] = "";
    const char* title1 = nullptr;
    const char* title2 = nullptr;
    char until[24] = "";
    char lineA[kMaxLine] = "";
    char storm[kMaxLine] = "";
    char travel[kMaxLine] = "";
};

inline void BuildTexts(const wxscen::Scenario& s, const wxlogic::Choice& c, Texts* t)
{
    ClockText(s.nowUtc, wxscen::kOffsetMin, t->clock, sizeof(t->clock));
    snprintf(t->headline, sizeof(t->headline), "%s", wxlogic::Headline(c));
    if (c.screen != wxlogic::Screen::Warning || c.warning < 0) return;
    const wxlogic::Warn& w = (&s.warn)[c.warning];
    WarningTitle(w.event, &t->title1, &t->title2);
    wxlogic::UntilText(w.expiresIso, t->until, sizeof(t->until));
    const wxlogic::Tokens k = wxlogic::LineA(w);
    size_t len = 0;
    for (int i = 0; i < k.n; ++i)
        len += (size_t)snprintf(t->lineA + len, sizeof(t->lineA) - len, "%s%s", i ? " - " : "", k.t[i]);
    const wxlogic::MotionText m = wxlogic::MotionLine(wxlogic::ParseMotion(w.motion), s.here);
    if (m.valid) {
        snprintf(t->storm, sizeof(t->storm), "STORM %s %s", m.distance, m.bearing);
        snprintf(t->travel, sizeof(t->travel), "%s", m.travel);
    }
}

inline void AddText(Layout& L, const char* s, int size, int y, Ink ink)
{
    if (!s || !*s || L.n >= kMaxItems) return;
    Item& it = L.items[L.n++];
    it.r = TextBox(strlen(s), size, y);
    it.kind = ItemKind::Text;
    it.size = (uint8_t)size;
    it.ink = ink;
    it.dotX = it.dotY = 0;
    snprintf(it.text, sizeof(it.text), "%s", s);
}
inline void AddRect(Layout& L, Rect r, ItemKind kind, Ink ink)
{
    if (L.n >= kMaxItems) return;
    Item& it = L.items[L.n++];
    it.r = r;
    it.kind = kind;
    it.size = 0;
    it.ink = ink;
    it.dotX = it.dotY = 0;
    it.text[0] = '\0';
}

constexpr Rect kHomeMarker{115, 115, 10, 10};

// A town label's box: the 3 x 3 dot at (px, py), the text from px + 4, 8 px high (A8.20).
inline Rect LabelRect(int px, int py, size_t nChars) { return Rect{px - 1, py - 4, 5 + kCharW * (int)nChars, kCharH}; }

// True when a label box touches none of the screen's fixed overlays (LF).
inline bool ClearOf(const Rect& b, const Item* fixed, int nFixed)
{
    for (int i = 0; i < nFixed; ++i) if (Overlaps(b, fixed[i].r)) return false;
    return true;
}

// The screen's fixed overlays (A8.20's table), then the town labels Q6(a) places among what is left.
// `scratch` holds one LabelBox per town and `scratchTown` its town index (the caller's memory, nt entries each).
inline void BuildLayout(Layout& L, wxlogic::Screen screen, const Texts& t, const wxrender::View& v,
                        const LatLon* box /* the warning square: S, N in .lat; W, E in .lon; or null */,
                        const wxtowns::Town* towns, unsigned nt, wxlogic::LabelBox* scratch, int* scratchTown)
{
    L.n = 0;
    AddText(L, "N", 1, 4, Ink::Dim);
    if (screen == wxlogic::Screen::Warning) {
        AddText(L, t.title1, 2, 38, Ink::Orange);
        AddText(L, t.title2, 2, 56, Ink::Orange);
        AddText(L, t.until, 1, 76, Ink::White);
        AddText(L, t.lineA, 1, 168, Ink::White);
        AddText(L, t.storm, 1, 180, Ink::White);
        AddText(L, t.travel, 1, 192, Ink::White);
        AddRect(L, kHomeMarker, ItemKind::Marker, Ink::White);
        if (box) {
            const int xw = ProjX(v, box[0].lon), xe = ProjX(v, box[1].lon);
            const int yn = ProjY(v, box[1].lat), ys = ProjY(v, box[0].lat);
            AddRect(L, Rect{xw, yn, xe - xw + 1, 1}, ItemKind::Edge, Ink::Orange);   // north
            AddRect(L, Rect{xw, ys, xe - xw + 1, 1}, ItemKind::Edge, Ink::Orange);   // south
            AddRect(L, Rect{xw, yn, 1, ys - yn + 1}, ItemKind::Edge, Ink::Orange);   // west
            AddRect(L, Rect{xe, yn, 1, ys - yn + 1}, ItemKind::Edge, Ink::Orange);   // east
        }
    } else {
        AddText(L, t.clock, 2, 20, Ink::White);
        AddRect(L, kHomeMarker, ItemKind::Marker, Ink::White);
        char lines[2][kMaxLine];
        const int nl = Wrap(t.headline, 13, lines, 2);
        for (int i = 0; i < nl; ++i) AddText(L, lines[i], 2, 184 + 18 * i, Ink::White);
    }
    L.nFixed = L.n;

    // Candidates: every town clear of the fixed overlays, nearest first; the 64 nearest go to PlaceLabels.
    const LatLon home{v.homeLat, v.homeLon};
    int nc = 0;
    for (unsigned i = 0; i < nt; ++i) {
        char label[kMaxLine];
        wxlogic::LabelText(towns[i].name, label, sizeof(label));
        const int px = ProjX(v, towns[i].lon), py = ProjY(v, towns[i].lat);
        const Rect b = LabelRect(px, py, strlen(label));
        if (!ClearOf(b, L.items, L.nFixed)) continue;
        wxlogic::LabelBox lb;
        lb.x = b.x;
        lb.y = b.y;
        lb.w = b.w;
        lb.h = b.h;
        lb.distMi = wxlogic::DistanceMi(home, LatLon{towns[i].lat, towns[i].lon});
        int j = nc++;   // insertion by distance, stable: ties keep the file's order
        while (j > 0 && lb.distMi < scratch[j - 1].distMi) {
            scratch[j] = scratch[j - 1];
            scratchTown[j] = scratchTown[j - 1];
            --j;
        }
        scratch[j] = lb;
        scratchTown[j] = (int)i;
    }
    const int m = nc < kMaxCandidates ? nc : kMaxCandidates;
    int drawn[kMaxCandidates];
    const int nd = wxlogic::PlaceLabels(scratch, m, drawn);
    for (int k = 0; k < nd && L.n < kMaxItems; ++k) {
        const wxlogic::LabelBox& lb = scratch[drawn[k]];
        Item& it = L.items[L.n++];
        it.r = Rect{lb.x, lb.y, lb.w, lb.h};
        it.kind = ItemKind::Town;
        it.size = 1;
        it.ink = Ink::Label;
        it.dotX = lb.x + 1;
        it.dotY = lb.y + 4;
        wxlogic::LabelText(towns[scratchTown[drawn[k]]].name, it.text, sizeof(it.text));
    }
}

// The warning square's corners for BuildLayout: S/W in [0], N/E in [1].
inline void WarningBox(const wxscen::Scenario& s, LatLon out[2])
{
    out[0] = s.polygon[0];   // (s0, w0)
    out[1] = s.polygon[2];   // (n0, e0)
}

}  // namespace wxscreens
