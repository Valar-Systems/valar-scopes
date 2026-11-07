// Weatherscope 3.5c's swipe screens (docs/weatherscope-stage3.md A12.130, A12.138, A12.144): forecast, conditions and
// alerts, and the page model that reaches them -- pure C++11, drawn only through WxRaster.h. test/host/test_wx_swipe.cpp
// holds the rules to A12.144's truth tables, and whole frames are held to tools/weatherscope/screens/ref35c.py at 57,600
// pixels (the K rows), so every expression that decides a pixel is written in ref35c.py's order. Rounding is pinned in
// integers, because the Python reference and this file must agree (CLAUDE.md, a cross-language port pins its rounding).
//
// No colour is named here: every colour is a role in WxTheme.h. Every text goes through Canvas::Text, which also records
// the box of the pixels it lit, so the layout check reads what was drawn rather than what was meant.

#pragma once

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "DiscGeometry.h"
#include "RadarZoom.h"
#include "WxFeeds.h"
#include "WxRaster.h"
#include "WxScreens.h"
#include "WxTheme.h"

namespace wxswipe {

using wxraster::Rgb;

// ------------------------------------------------------------------------------------------- navigation (N)

enum class Page : uint8_t { Map, Forecast, Conditions, Alerts, Legend };
constexpr int kPages = 5;
enum class Gesture : uint8_t { Left, Right, Tap, Vertical, Hold };
enum class Act : uint8_t { None, Go, OpenLoop, Zoom, Toggle };

struct Move {
    Page next;
    Act act;
};

// A list a finger can't fall off: the ends stop (Q88). On the map everything but the left swipe is what it always was.
inline Move Step(Page p, Gesture g)
{
    const int i = (int)p;
    const Move stay = {p, Act::None};
    switch (g) {
    case Gesture::Left:
        return i < kPages - 1 ? Move{(Page)(i + 1), Act::Go} : stay;
    case Gesture::Right:
        return i > 0 ? Move{(Page)(i - 1), Act::Go} : stay;
    case Gesture::Tap:
        return p == Page::Map ? Move{p, Act::OpenLoop} : Move{Page::Map, Act::Go};
    case Gesture::Vertical:
        return p == Page::Map ? Move{p, Act::Zoom} : stay;
    case Gesture::Hold:
        return p == Page::Map ? Move{p, Act::Toggle} : stay;
    }
    return stay;
}

// The scenario cycle runs on the map alone (N5), as it has always paused for the legend.
inline bool CycleRuns(Page p) { return p == Page::Map; }

// Ten minutes untouched on any page but the map goes home (N6), on the zoom's own idle clock.
inline bool IdleHome(Page p, uint32_t nowMs, uint32_t lastTouchMs)
{
    return p != Page::Map && radarzoom::IdleExpired(nowMs, lastTouchMs, wxscreens::kZoomIdleMs);
}

// ---------------------------------------------------------------------------------------------- local time

struct Local {
    int hour, weekday;   // weekday 0 is Monday, as ref35c.py's WEEKDAYS
};

// An instant read offMin minutes from UTC. Both remainders are floored, so a time before local midnight (or before the
// epoch, on the local clock) lands on the right hour and day.
inline Local LocalAt(int64_t utc, int offMin)
{
    const int64_t t = utc + (int64_t)offMin * 60;
    const int64_t sec = ((t % 86400) + 86400) % 86400;
    const int64_t day = (t - sec) / 86400;
    Local l;
    l.hour = (int)(sec / 3600);
    l.weekday = (int)((((day + 3) % 7) + 7) % 7);
    return l;
}

inline const char* WeekdayName(int i)
{
    static const char* const kNames[7] = {"MON", "TUE", "WED", "THU", "FRI", "SAT", "SUN"};
    return kNames[i];
}

// "4 PM"
inline void HourText(int64_t utc, int offMin, char* out, size_t cap)
{
    const int h = LocalAt(utc, offMin).hour;
    snprintf(out, cap, "%d %s", h % 12 == 0 ? 12 : h % 12, h < 12 ? "AM" : "PM");
}

// The strip's "4P"; false for an hour that is not a multiple of 6, which has no label.
inline bool HourLabel(int64_t utc, int offMin, char* out, size_t cap)
{
    const int h = LocalAt(utc, offMin).hour;
    if (h % 6 != 0) return false;
    snprintf(out, cap, "%d%s", h % 12 == 0 ? 12 : h % 12, h < 12 ? "A" : "P");
    return true;
}

// ------------------------------------------------------------------------------------------------ the days

constexpr int kMaxDays = 8;

struct Day {
    int k;
    const wx::Period* day;
    const wx::Period* night;
};

// Q110: a period whose successor has started is over. NWS serves its forecast from a cache hours old, so a document can
// open on a period that has ended, and the device reads the times rather than trusting the feed to have dropped it.
inline size_t FirstCurrent(const wx::Forecast& f)
{
    size_t i = 0;
    while (i + 1 < f.nPeriods && f.periods[i + 1].startUtc <= f.issuedUtc) ++i;
    return i;
}

// By position: a daytime period opens a day, a night joins the current one, and a night-only first period is day 0.
inline int Days(const wx::Forecast& f, Day* out)
{
    int n = 0;
    for (size_t i = FirstCurrent(f); i < f.nPeriods; ++i) {
        const wx::Period& p = f.periods[i];
        if (p.isDaytime || n == 0) {
            if (n == kMaxDays) break;
            out[n].k = n;
            out[n].day = p.isDaytime ? &p : nullptr;
            out[n].night = p.isDaytime ? nullptr : &p;
            ++n;
        } else {
            out[n - 1].night = &p;
        }
    }
    return n;
}

// A holiday's period name never reaches the screen: the day is its weekday, by position from "now".
inline const char* DayName(const wx::Forecast& f, int k)
{
    return WeekdayName((LocalAt(f.issuedUtc, f.offsetMin).weekday + k) % 7);
}

// ------------------------------------------------------------------------------------------- the headline (H)

constexpr int kChanceLine = 30;   // Q91: where NWS wording turns from "slight chance" to "chance"

inline bool Names(const char* text, const char* word)
{
    const size_t n = strlen(word);
    for (const char* s = text; *s; ++s) {
        size_t i = 0;
        while (i < n && s[i] && (s[i] | 0x20) == word[i]) ++i;
        if (i == n) return true;
    }
    return false;
}

// Q95: SNOW when the period's short forecast names snow and not rain; otherwise RAIN.
inline const char* NextWord(const wx::Period* p)
{
    return p && Names(p->shortForecast, "snow") && !Names(p->shortForecast, "rain") ? "NEXT SNOW" : "NEXT RAIN";
}

// Q105: the period holding an hour, the last one starting at or before it. An hour before them all has none.
inline const wx::Period* PeriodAt(const wx::Forecast& f, int64_t utc)
{
    const wx::Period* hit = nullptr;
    for (size_t i = 0; i < f.nPeriods; ++i)
        if (f.periods[i].startUtc <= utc) hit = &f.periods[i];
    return hit;
}

struct Headline {
    char a[16], b[16];
    bool oneLine;
};

inline Headline MakeHeadline(const wx::Forecast& f)
{
    Headline h;
    strcpy(h.a, "NO RAIN");
    strcpy(h.b, "IN 7 DAYS");
    bool found = false;
    const size_t n = f.nHours < 24 ? f.nHours : 24;
    for (size_t i = 0; i < n && !found; ++i) {
        const wx::Hour& e = f.hours[i];
        if (e.hasPop && e.pop >= kChanceLine) {
            snprintf(h.a, sizeof h.a, "%s", NextWord(PeriodAt(f, e.startUtc)));
            HourText(e.startUtc, f.offsetMin, h.b, sizeof h.b);
            found = true;
        }
    }
    Day days[kMaxDays];
    const int nd = Days(f, days);
    for (int d = 0; d < nd && !found; ++d) {
        const wx::Period* parts[2] = {days[d].day, days[d].night};
        for (int part = 0; part < 2 && !found; ++part) {
            const wx::Period* p = parts[part];
            if (p && p->hasPop && p->pop >= kChanceLine) {
                snprintf(h.a, sizeof h.a, "%s", NextWord(p));
                if (days[d].k == 0) snprintf(h.b, sizeof h.b, "%s", part ? "TONIGHT" : "TODAY");
                else snprintf(h.b, sizeof h.b, "%s%s", DayName(f, days[d].k), part ? " NIGHT" : "");
                found = true;
            }
        }
    }
    h.oneLine = ((int)(strlen(h.a) + 1 + strlen(h.b))) * 12 - 2 <= discgeom::ChordWidthPx(34, 16, 240);
    return h;
}

// -------------------------------------------------------------------------------------- the strip (B, Y, X)

// The hour labels sit under the axis and the week stays where it was, so the strip is shorter (A12.138).
constexpr int kAxis = 132, kPlotBottom = 127, kPlotScale = 47, kLowLabel = 123, kHourLabel = 135;
constexpr int kHighLabel = 76, kWeekTop = 148, kWeekStep = 10, kStripLeft = 36, kStripRight = 203, kColumn = 7;

inline int BarHeight(int pop) { return (pop * 40 + 50) / 100; }

inline int TempRow(int t, int tmin, int tmax)
{
    const int s = tmax - tmin > 1 ? tmax - tmin : 1;
    return kPlotBottom - (2 * (t - tmin) * kPlotScale + s) / (2 * s);
}

// ---------------------------------------------------------------------------------------- the canvas (L)

constexpr int kC = 120;
constexpr int kBandTop = 16, kBandRows = 206;   // rows 16-221: under the title (last row 15), above the current dot

inline int BlockTop(int rows) { return kBandTop + (kBandRows - rows) / 2; }

struct Item {
    int x0, y0, x1, y1;   // the pixels it lit
    int x, y, size;       // where it was asked to go
    int width;            // its TextW, what the chord is compared with
};

class Canvas {
public:
    static constexpr int kMaxItems = 24;

    Canvas(wxraster::Sink& s, const wxtheme::Theme& t) : sink(s), th(t), nItems(0) {}

    void Text(int x, int y, const char* str, int size, Rgb ink)
    {
        wxraster::Text(sink, x, y, str, size, ink, nullptr);
        if (nItems == kMaxItems) return;
        Item it;
        it.x0 = it.y0 = 1 << 20;
        it.x1 = it.y1 = -(1 << 20);
        it.x = x;
        it.y = y;
        it.size = size;
        it.width = wxraster::TextW(str, size);
        for (int i = 0; str[i]; ++i) {
            const unsigned char ch = (unsigned char)str[i];
            for (int col = 0; col < 5; ++col) {
                const uint8_t bits = wxraster::glcd::font[ch * 5 + col];
                for (int row = 0; row < 7; ++row) {
                    if (!(bits & (1 << row))) continue;
                    const int cx = x + (i * 6 + col) * size, cy = y + row * size;
                    if (cx < it.x0) it.x0 = cx;
                    if (cy < it.y0) it.y0 = cy;
                    if (cx + size - 1 > it.x1) it.x1 = cx + size - 1;
                    if (cy + size - 1 > it.y1) it.y1 = cy + size - 1;
                }
            }
        }
        if (it.x1 >= it.x0) items[nItems++] = it;
    }

    void Centred(const char* str, int y, int size, Rgb ink)
    {
        Text(kC - ((int)strlen(str) * 6 * size) / 2, y, str, size, ink);
    }

    wxraster::Sink& sink;
    const wxtheme::Theme& th;
    Item items[kMaxItems];
    int nItems;
};

// The title, and five dots, the current one 5 px as well as brighter: a shade alone is faint in the light theme.
inline void Frame(Canvas& c, const char* title, Page page)
{
    c.Centred(title, 9, 1, c.th.uiDim);
    static const int kDotX[kPages] = {100, 110, 120, 130, 140};
    for (int i = 0; i < kPages; ++i) {
        if (i == (int)page) wxraster::Fill(c.sink, kDotX[i] - 2, 222, kDotX[i] + 2, 226, c.th.uiFg);
        else wxraster::Fill(c.sink, kDotX[i] - 1, 223, kDotX[i] + 1, 225, c.th.uiDim);
    }
}

// ref35c.py's line, step for step: the K rows compare it bit for bit.
inline void Line(Canvas& c, int x0, int y0, int x1, int y1, Rgb ink)
{
    const int dx = x1 > x0 ? x1 - x0 : x0 - x1, dy = -(y1 > y0 ? y1 - y0 : y0 - y1);
    const int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        wxraster::Put(c.sink, x0, y0, ink);
        if (x0 == x1 && y0 == y1) return;
        const int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

// ------------------------------------------------------------------------------------------- the forecast

// "SAT  67/52   8%": the day, its high and its night's low, and the larger of the two chances. W4: no chance reads 0.
inline void WeekRow(const wx::Forecast& f, const Day& d, char* out, size_t cap)
{
    char hi[8] = "--", lo[8] = "--";
    if (d.day) snprintf(hi, sizeof hi, "%d", d.day->tempF);
    if (d.night) snprintf(lo, sizeof lo, "%d", d.night->tempF);
    int pop = 0;
    if (d.day && d.day->hasPop && d.day->pop > pop) pop = d.day->pop;
    if (d.night && d.night->hasPop && d.night->pop > pop) pop = d.night->pop;
    snprintf(out, cap, "%s  %s/%s  %2d%%", d.day ? DayName(f, d.k) : "TON", hi, lo, pop);
}

// The strip's coldest and warmest hour; false when no hour has a temperature.
inline bool TempRange(const wx::Forecast& f, int* tmin, int* tmax)
{
    const int n = f.nHours < 24 ? (int)f.nHours : 24;
    bool any = false;
    for (int i = 0; i < n; ++i) {
        if (!f.hours[i].hasTemp) continue;
        const int t = f.hours[i].tempF;
        if (!any || t < *tmin) *tmin = t;
        if (!any || t > *tmax) *tmax = t;
        any = true;
    }
    return any;
}

// The line's points. An hour with no temperature has none, so the line joins across it.
inline int StripPoints(const wx::Forecast& f, int tmin, int tmax, int* px, int* py)
{
    const int n = f.nHours < 24 ? (int)f.nHours : 24;
    int np = 0;
    for (int i = 0; i < n; ++i) {
        if (!f.hours[i].hasTemp) continue;
        px[np] = kStripLeft + kColumn * i + 3;
        py[np] = TempRow(f.hours[i].tempF, tmin, tmax);
        ++np;
    }
    return np;
}

inline void ComposeForecast(Canvas& c, const wx::Forecast& f)
{
    Frame(c, "FORECAST", Page::Forecast);
    const Headline h = MakeHeadline(f);
    if (h.oneLine) {
        char whole[34];
        snprintf(whole, sizeof whole, "%s %s", h.a, h.b);
        c.Centred(whole, 34, 2, c.th.uiFg);
    } else {
        c.Centred(h.a, 34, 2, c.th.uiFg);
        c.Centred(h.b, 52, 2, c.th.uiFg);
    }
    const int n = f.nHours < 24 ? (int)f.nHours : 24;
    int tmin = 0, tmax = 0;
    const bool any = TempRange(f, &tmin, &tmax);
    wxraster::Fill(c.sink, kStripLeft, kAxis, kStripRight, kAxis, c.th.uiDim);
    int px[24], py[24];
    const int np = StripPoints(f, tmin, tmax, px, py);
    for (int i = 0; i < n; ++i) {
        const wx::Hour& e = f.hours[i];
        const int x0 = kStripLeft + kColumn * i;
        if (e.hasPop && e.pop > 0) {
            const int ht = BarHeight(e.pop);
            if (ht > 0) wxraster::Fill(c.sink, x0 + 1, kAxis - ht, x0 + 5, kAxis - 1, c.th.uiAccent);
        }
        char lab[8];
        if (HourLabel(e.startUtc, f.offsetMin, lab, sizeof lab))
            c.Text(x0 + 3 - ((int)strlen(lab) * 6) / 2, kHourLabel, lab, 1, c.th.uiDim);
    }
    for (int i = 0; i + 1 < np; ++i) Line(c, px[i], py[i], px[i + 1], py[i + 1], c.th.orangeText);
    if (any) {
        const int v[2] = {tmax, tmin}, y[2] = {kHighLabel, kLowLabel};
        for (int i = 0; i < 2; ++i) {
            char lab[12];
            snprintf(lab, sizeof lab, "%d\xF8", v[i]);
            c.Text(32 - ((int)strlen(lab) * 6 - 1), y[i], lab, 1, c.th.uiFg);
        }
    }
    Day days[kMaxDays];
    const int nd = Days(f, days);
    for (int j = 0; j < nd && j < 7; ++j) {
        char row[48];
        WeekRow(f, days[j], row, sizeof row);
        c.Centred(row, kWeekTop + kWeekStep * j, 1, c.th.uiFg);
    }
}

// ------------------------------------------------------------------------------------------ the conditions

constexpr int kConditionsRows = 127;      // 28 temperature, a 20 gap, three 14-row lines and a 7-row one, 10 apart
constexpr int64_t kObsStaleS = 5400;      // the observation's time turns orange past 90 min (A9.1 B)

inline bool ObsStale(int64_t nowUtc, int64_t obsUtc) { return nowUtc - obsUtc > kObsStaleS; }

inline const char* WindDirName(int deg)
{
    static const char* const kDirs[8] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
    return kDirs[((2 * deg + 45) / 90) % 8];
}

inline void WindText(const wx::Observation& o, char* out, size_t cap)
{
    if (!o.hasWind) { snprintf(out, cap, "WIND --"); return; }
    if (o.windMph == 0) { snprintf(out, cap, "WIND CALM"); return; }
    int n;
    if (o.hasWindDir) n = snprintf(out, cap, "WIND %s %d", WindDirName(o.windDirDeg), o.windMph);
    else n = snprintf(out, cap, "WIND %d", o.windMph);
    if (o.hasGust && o.gustMph > o.windMph) snprintf(out + n, cap - (size_t)n, " G%d", o.gustMph);
}

inline void HighLowText(const wx::Forecast& f, char* out, size_t cap)
{
    const wx::Period* hi = nullptr;
    const wx::Period* lo = nullptr;
    for (size_t i = FirstCurrent(f); i < f.nPeriods; ++i) {
        const wx::Period& p = f.periods[i];
        if (p.isDaytime && !hi) hi = &p;
        if (!p.isDaytime && !lo) lo = &p;
    }
    char a[8] = "--", b[8] = "--";
    if (hi) snprintf(a, sizeof a, "%d", hi->tempF);
    if (lo) snprintf(b, sizeof b, "%d", lo->tempF);
    snprintf(out, cap, "HI %s  LO %s", a, b);
}

inline void HumidityText(const wx::Observation& o, char* out, size_t cap)
{
    if (o.hasHumidity) snprintf(out, cap, "HUMIDITY %d%%", o.humidityPct);
    else snprintf(out, cap, "HUMIDITY --");
}

inline void ObservedText(int64_t obsUtc, int offMin, char* out, size_t cap)
{
    char clock[12];
    wxscreens::ClockText(obsUtc, offMin, clock, sizeof clock);
    snprintf(out, cap, "OBSERVED %s", clock);
}

inline void ComposeConditions(Canvas& c, const wx::Observation& o, const wx::Forecast& f)
{
    Frame(c, "NOW", Page::Conditions);
    const int top = BlockTop(kConditionsRows);
    if (o.hasTempF) {
        // The degree sign is size 2 beside size-4 digits, at the natural advance. The glyph's first lit row and the
        // digits' are both row 0, so one y levels their tops. The two are centred as one.
        char digits[8];
        snprintf(digits, sizeof digits, "%d", (int)wxraster::Rnd(o.tempF));
        const int n = (int)strlen(digits);
        const int x = kC - (n * 24 + 10) / 2;
        c.Text(x, top, digits, 4, c.th.uiFg);
        c.Text(x + n * 24, top, "\xF8", 2, c.th.uiFg);
    } else {
        c.Centred("--", top, 4, c.th.uiFg);
    }
    char row[32];
    WindText(o, row, sizeof row);
    c.Centred(row, top + 48, 2, c.th.uiFg);
    HumidityText(o, row, sizeof row);
    c.Centred(row, top + 72, 2, c.th.uiFg);
    HighLowText(f, row, sizeof row);
    c.Centred(row, top + 96, 2, c.th.uiFg);
    ObservedText(o.timeUtc, f.offsetMin, row, sizeof row);
    c.Centred(row, top + 120, 1, ObsStale(f.issuedUtc, o.timeUtc) ? c.th.orangeText : c.th.uiDim);
}

// ---------------------------------------------------------------------------------------------- the alerts

constexpr int kMaxAlerts = 16, kAlertsShown = 4, kAlertStep = 26;

// The watches and advisories, ordered by start, ties in stored order: the warning screen's four events never appear.
inline int SelectAlerts(const wx::AlertList& list, const wx::Alert** out)
{
    int n = 0;
    for (size_t i = 0; i < list.count && n < kMaxAlerts; ++i) {
        const wx::Alert& a = list.items[i];
        const char* l1;
        const char* l2;
        if (wxscreens::WarningTitle(a.event, &l1, &l2)) continue;
        int j = n++;
        while (j > 0 && out[j - 1]->startUtc > a.startUtc) {
            out[j] = out[j - 1];
            --j;
        }
        out[j] = &a;
    }
    return n;
}

inline int ListTop(int shown, int more) { return BlockTop(more ? kAlertStep * shown + 7 : kAlertStep * shown - 9); }

inline void AlertSpan(const wx::Alert& a, int offMin, char* out, size_t cap)
{
    char from[12], to[12];
    HourText(a.startUtc, offMin, from, sizeof from);
    HourText(a.expiresUtc, offMin, to, sizeof to);
    snprintf(out, cap, "%s %s - %s %s", WeekdayName(LocalAt(a.startUtc, offMin).weekday), from,
             WeekdayName(LocalAt(a.expiresUtc, offMin).weekday), to);
}

inline void UpperText(const char* in, char* out, size_t cap)
{
    size_t i = 0;
    for (; in[i] && i + 1 < cap; ++i) out[i] = (in[i] >= 'a' && in[i] <= 'z') ? (char)(in[i] - 32) : in[i];
    out[i] = 0;
}

inline void ComposeAlerts(Canvas& c, const wx::AlertList& list, int offMin)
{
    Frame(c, "ALERTS", Page::Alerts);
    const wx::Alert* shown[kMaxAlerts];
    const int n = SelectAlerts(list, shown);
    if (n == 0) {
        c.Centred("NO WATCHES OR ADVISORIES", 116, 1, c.th.uiDim);
        return;
    }
    const int vis = n < kAlertsShown ? n : kAlertsShown, more = n - vis;
    const int top = ListTop(vis, more);
    for (int i = 0; i < vis; ++i) {
        const int y = top + kAlertStep * i;
        char event[40], span[40];
        UpperText(shown[i]->event, event, sizeof event);
        c.Centred(event, y, 1, c.th.uiWarn);
        AlertSpan(*shown[i], offMin, span, sizeof span);
        c.Centred(span, y + 10, 1, c.th.uiDim);
    }
    if (more) {
        char line[20];
        snprintf(line, sizeof line, "+%d MORE", more);
        c.Centred(line, top + kAlertStep * vis, 1, c.th.uiDim);
    }
}

}  // namespace wxswipe
