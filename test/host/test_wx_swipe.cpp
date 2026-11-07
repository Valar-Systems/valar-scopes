// Weatherscope 3.5c's swipe screens (src/weatherscope/WxSwipe.h) against the truth tables frozen in
// docs/weatherscope-stage3.md A12.144: N the page model, H the headline, B, Y and X the strip, W the week, C the
// conditions, A the alerts, D the dots, L the layout check, and K the 10 screens through the device's own path against
// tools/weatherscope/screens/ref35c.py (test/fixtures/wx/nws/k35c.ref.txt) at 57,600 of 57,600 pixels. A12.155 adds H18,
// C26 and W7, a period already over (Q110). 136 checks.
//
//     test_wx_swipe <k35c.ref.txt>
//
// The stored record reaches this file through src/weatherscope/WxForecastData.h, the header the image carries. Its
// sabotages: python -I test/host/sabotage_wx_swipe.py. Prints texts, hashes and screen pixels: no coordinate.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "WxForecastData.h"
#include "WxSwipe.h"

namespace {

using namespace wxswipe;

int checks = 0, failures = 0;

void check(const std::string& id, bool ok, const std::string& what)
{
    ++checks;
    if (!ok) ++failures;
    std::printf("  %s %s  %s\n", ok ? "ok  " : "FAIL", id.c_str(), what.c_str());
}

// SHA-256, as in test_wx_crop.cpp and test_wx_k35.cpp.
const uint32_t K256[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

uint32_t Rot(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

std::string Sha256Hex(const std::vector<uint8_t>& in)
{
    uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    std::vector<uint8_t> m(in);
    const uint64_t bits = (uint64_t)in.size() * 8;
    m.push_back(0x80);
    while (m.size() % 64 != 56) m.push_back(0);
    for (int i = 7; i >= 0; --i) m.push_back((uint8_t)(bits >> (i * 8)));
    for (size_t off = 0; off < m.size(); off += 64) {
        uint32_t w[64];
        for (int i = 0; i < 16; ++i)
            w[i] = (uint32_t)m[off + i * 4] << 24 | (uint32_t)m[off + i * 4 + 1] << 16 | (uint32_t)m[off + i * 4 + 2] << 8 |
                   (uint32_t)m[off + i * 4 + 3];
        for (int i = 16; i < 64; ++i) {
            const uint32_t s0 = Rot(w[i - 15], 7) ^ Rot(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const uint32_t s1 = Rot(w[i - 2], 17) ^ Rot(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 64; ++i) {
            const uint32_t t1 = hh + (Rot(e, 6) ^ Rot(e, 11) ^ Rot(e, 25)) + ((e & f) ^ (~e & g)) + K256[i] + w[i];
            const uint32_t t2 = (Rot(a, 2) ^ Rot(a, 13) ^ Rot(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
            hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }
    char out[65];
    for (int i = 0; i < 8; ++i) std::snprintf(out + i * 8, 9, "%08x", h[i]);
    return std::string(out, 64);
}

// The board's SpriteSink, on the host: big-endian RGB565, color565's truncation.
class Frame : public wxraster::Sink {
public:
    explicit Frame(wxraster::Rgb ground) : px(240 * 240, Pack(ground)) {}
    void Put(int x, int y, wxraster::Rgb c) override { px[y * 240 + x] = Pack(c); }
    uint16_t At(int x, int y) const { return px[y * 240 + x]; }
    static uint16_t Pack(wxraster::Rgb c) { return (uint16_t)(((c.r >> 3) << 11) | ((c.g >> 2) << 5) | (c.b >> 3)); }
    std::string Hash() const
    {
        std::vector<uint8_t> b;
        b.reserve(px.size() * 2);
        for (size_t i = 0; i < px.size(); ++i) {
            b.push_back((uint8_t)(px[i] >> 8));
            b.push_back((uint8_t)(px[i] & 0xFF));
        }
        return Sha256Hex(b);
    }
    int Count(wxraster::Rgb c) const
    {
        const uint16_t v = Pack(c);
        int n = 0;
        for (size_t i = 0; i < px.size(); ++i) n += px[i] == v;
        return n;
    }

private:
    std::vector<uint16_t> px;
};

// A mutable copy of the stored forecast. `shift` drops leading periods, to make a night-first record.
struct Fx {
    wx::Hour h[24];
    wx::Period p[14];
    wx::Forecast f;

    explicit Fx(int shift = 0)
    {
        for (int i = 0; i < 24; ++i) {
            h[i] = wxnws::kHours[i];
            h[i].pop = 0;
            h[i].hasPop = true;
        }
        for (int i = 0; i < 14; ++i) {
            p[i] = wxnws::kPeriods[i];
            p[i].pop = 0;
            p[i].hasPop = true;
            p[i].shortForecast = "Sunny";
        }
        f.issuedUtc = wxnws::kCapturedUtc;
        f.offsetMin = wxnws::kOffsetMin;
        f.hours = h;
        f.nHours = 24;
        f.periods = p + shift;
        f.nPeriods = (size_t)(14 - shift);
    }
    int HourAt(int local) const
    {
        for (int i = 0; i < 24; ++i)
            if (LocalAt(h[i].startUtc, f.offsetMin).hour == local) return i;
        return -1;
    }
    void TonightStarted() { f.issuedUtc = p[1].startUtc + 60; }   // Q110: the first period is over
};

std::string Read(const Headline& h) { return h.oneLine ? std::string(h.a) + " " + h.b : std::string(h.a) + " / " + h.b; }

void Row(const std::string& id, const Headline& h, const std::string& want)
{
    check(id, Read(h) == want, "reads '" + Read(h) + "', want '" + want + "'");
}

// ------------------------------------------------------------------------------------------------- N

void TableN()
{
    static const char* const kName[kPages] = {"map", "fc", "cond", "alerts", "legend"};
    const Page left[kPages] = {Page::Forecast, Page::Conditions, Page::Alerts, Page::Legend, Page::Legend};
    const Act leftAct[kPages] = {Act::Go, Act::Go, Act::Go, Act::Go, Act::None};
    const Page right[kPages] = {Page::Map, Page::Map, Page::Forecast, Page::Conditions, Page::Alerts};
    const Act rightAct[kPages] = {Act::None, Act::Go, Act::Go, Act::Go, Act::Go};
    for (int i = 0; i < kPages; ++i) {
        const Page p = (Page)i;
        const std::string n = kName[i];
        Move m = Step(p, Gesture::Left);
        check("N-" + n + "-left", m.next == left[i] && m.act == leftAct[i], "");
        m = Step(p, Gesture::Right);
        check("N-" + n + "-right", m.next == right[i] && m.act == rightAct[i], "");
        m = Step(p, Gesture::Tap);
        check("N-" + n + "-tap", p == Page::Map ? (m.next == Page::Map && m.act == Act::OpenLoop)
                                                : (m.next == Page::Map && m.act == Act::Go), "");
        m = Step(p, Gesture::Vertical);
        check("N-" + n + "-vert", m.next == p && m.act == (p == Page::Map ? Act::Zoom : Act::None), "");
        m = Step(p, Gesture::Hold);
        check("N-" + n + "-hold", m.next == p && m.act == (p == Page::Map ? Act::Toggle : Act::None), "");
        const uint32_t t0 = 4000000000u;   // the clock's wrap sits inside the window, as a long uptime's does
        const bool early = IdleHome(p, t0 + 599999u, t0), late = IdleHome(p, t0 + 600000u, t0);
        check("N-" + n + "-idle", !early && late == (p != Page::Map), "");
        check("N-" + n + "-cycle", CycleRuns(p) == (p == Page::Map), "");
    }
}

// ------------------------------------------------------------------------------------------------- H

void TableH()
{
    {
        Fx x;
        x.h[x.HourAt(16)].pop = 40;
        Row("H1", MakeHeadline(x.f), "NEXT RAIN / 4 PM");
    }
    {
        Fx x;
        for (int i = 0; i < 24; ++i) x.h[i].pop = 29;
        for (int i = 0; i < 14; ++i) x.p[i].pop = 29;
        Row("H2", MakeHeadline(x.f), "NO RAIN / IN 7 DAYS");
    }
    {
        Fx x;
        x.h[x.HourAt(16)].pop = 30;
        Row("H3", MakeHeadline(x.f), "NEXT RAIN / 4 PM");
    }
    {
        Fx x;
        x.h[x.HourAt(16)].pop = 99;
        x.h[x.HourAt(16)].hasPop = false;
        Row("H4", MakeHeadline(x.f), "NO RAIN / IN 7 DAYS");
    }
    {
        Fx x;
        x.h[x.HourAt(18)].pop = 60;
        x.h[x.HourAt(16)].pop = 40;
        Row("H5", MakeHeadline(x.f), x.HourAt(16) < x.HourAt(18) ? "NEXT RAIN / 4 PM" : "NEXT RAIN / 6 PM");
    }
    {
        Fx x;
        x.h[x.HourAt(16)].pop = 40;
        x.p[0].pop = 80;
        Row("H6", MakeHeadline(x.f), "NEXT RAIN / 4 PM");
    }
    {
        Fx x;
        x.p[8].pop = 30;
        Row("H7", MakeHeadline(x.f), "NEXT RAIN TUE");
    }
    {
        Fx x;
        x.p[9].pop = 40;
        Row("H8", MakeHeadline(x.f), "NEXT RAIN / TUE NIGHT");
    }
    {
        Fx x;
        x.p[0].pop = 40;
        Row("H9", MakeHeadline(x.f), "NEXT RAIN / TODAY");
    }
    {
        Fx x;
        x.p[1].pop = 40;
        Row("H10", MakeHeadline(x.f), "NEXT RAIN / TONIGHT");
    }
    {
        Fx x(1);   // the record opens on "Tonight"
        x.p[1].pop = 40;
        const std::string a = Read(MakeHeadline(x.f));
        x.p[1].pop = 0;
        x.p[2].pop = 40;
        const std::string b = Read(MakeHeadline(x.f));
        check("H11", a == "NEXT RAIN / TONIGHT" && b == "NEXT RAIN SAT", "reads '" + a + "', then '" + b + "'");
    }
    {
        Fx x;
        x.p[10].pop = 40;   // "Veterans Day", day 5 of a Friday's record
        Row("H12", MakeHeadline(x.f), "NEXT RAIN WED");
    }
    const char* const kShort[3] = {"Chance Snow Showers", "Rain And Snow", "Wintry Mix"};
    const char* const kWant[3] = {"NEXT SNOW TUE", "NEXT RAIN TUE", "NEXT RAIN TUE"};
    for (int i = 0; i < 3; ++i) {
        Fx x;
        x.p[8].pop = 40;
        x.p[8].shortForecast = kShort[i];
        Row(i == 0 ? "H13" : i == 1 ? "H14" : "H15", MakeHeadline(x.f), kWant[i]);
    }
    {
        Fx x;
        const int i = x.HourAt(16);
        x.h[i].pop = 40;
        const wx::Period* holding = PeriodAt(x.f, x.h[i].startUtc);
        if (holding) x.p[holding - x.p].shortForecast = "Light Snow";
        Row("H16", MakeHeadline(x.f), "NEXT SNOW / 4 PM");
    }
    {
        Fx x;
        x.p[8].pop = 40;
        x.p[8].shortForecast = "SNOW SHOWERS";
        const std::string a = Read(MakeHeadline(x.f));
        x.p[8].shortForecast = "light snow";
        const std::string b = Read(MakeHeadline(x.f));
        check("H17", a == "NEXT SNOW TUE" && b == a, "reads '" + a + "', then '" + b + "'");
    }
    {
        Fx x;
        x.TonightStarted();
        x.p[0].pop = 80;   // over, so it names nothing
        const std::string a = Read(MakeHeadline(x.f));
        x.p[1].pop = 40;
        const std::string b = Read(MakeHeadline(x.f));
        check("H18", a == "NO RAIN / IN 7 DAYS" && b == "NEXT RAIN / TONIGHT", "reads '" + a + "', then '" + b + "'");
    }
}

// ---------------------------------------------------------------------------------------------- B, Y, X

void TableStrip()
{
    const int pops[5] = {0, 1, 2, 25, 100}, want[5] = {0, 0, 1, 10, 40};
    for (int i = 0; i < 5; ++i) {
        char id[8], what[48];
        std::snprintf(id, sizeof id, "B%d", i + 1);
        std::snprintf(what, sizeof what, "chance %d reads %d px, want %d", pops[i], BarHeight(pops[i]), want[i]);
        check(id, BarHeight(pops[i]) == want[i], what);
    }
    {
        Fx x;
        x.h[3].pop = 99;
        x.h[3].hasPop = false;
        Frame a(wxtheme::kDark.bg);
        Canvas c(a, wxtheme::kDark);
        ComposeForecast(c, x.f);
        x.h[3].hasPop = true;
        Frame b(wxtheme::kDark.bg);
        Canvas d(b, wxtheme::kDark);
        ComposeForecast(d, x.f);
        check("B6", a.Count(wxtheme::kDark.uiAccent) == 0 && b.Count(wxtheme::kDark.uiAccent) > 0, "hasPop false draws no bar");
    }
    check("Y1", TempRow(53, 53, 68) == 127, "t = min");
    check("Y2", TempRow(68, 53, 68) == 80, "t = max");
    check("Y3", TempRow(60, 60, 60) == 127, "max = min");
    check("Y4", TempRow(54, 53, 55) == 103, "span 2, one above min");
    {
        Fx x;
        x.h[7].hasTemp = false;
        int lo = 0, hi = 0, px[24], py[24];
        TempRange(x.f, &lo, &hi);
        check("Y5", StripPoints(x.f, lo, hi, px, py) == 23 && px[7] - px[6] == 2 * kColumn, "the line joins across a missing hour");
    }
    const int off = wxnws::kOffsetMin;
    const int64_t mid = 6 * 3600 + 86400 * 20000;   // local midnight on a clock 360 minutes behind UTC
    const int hours[4] = {0, 6, 12, 18};
    const char* const labels[4] = {"12A", "6A", "12P", "6P"};
    for (int i = 0; i < 4; ++i) {
        char lab[8] = "";
        const bool has = HourLabel(mid + hours[i] * 3600, off, lab, sizeof lab);
        char id[8];
        std::snprintf(id, sizeof id, "X%d", i + 1);
        check(id, has && std::string(lab) == labels[i], std::string("reads '") + lab + "'");
    }
    bool none = true;
    for (int h = 0; h < 24; ++h) {
        char lab[8];
        if (h % 6 != 0) none = none && !HourLabel(mid + h * 3600, off, lab, sizeof lab);
    }
    check("X5", none, "any other hour has no label");
    {
        char lab[8] = "";
        const bool has = HourLabel(0, off, lab, sizeof lab);
        const Local l = LocalAt(0, off);
        check("X6", has && std::string(lab) == "6P" && l.hour == 18 && l.weekday == 2, "the epoch, on a clock behind UTC: Wednesday, 6P");
    }
}

// ------------------------------------------------------------------------------------------------- W

void TableW()
{
    Fx x;
    wx::Period day = x.p[2], night = x.p[3];
    day.tempF = 67;
    night.tempF = 52;
    day.pop = 8;
    night.pop = 3;
    Day d = {1, &day, &night};
    char row[48];
    WeekRow(x.f, d, row, sizeof row);
    check("W1", std::string(row) == "SAT  67/52   8%", std::string("reads '") + row + "'");
    {
        wx::Period only = x.p[1];
        only.tempF = 53;
        Day t = {0, nullptr, &only};
        WeekRow(x.f, t, row, sizeof row);
        check("W2", std::string(row) == "TON  --/53   0%", std::string("reads '") + row + "'");
    }
    night.pop = 40;
    WeekRow(x.f, d, row, sizeof row);
    check("W3", std::string(row) == "SAT  67/52  40%", std::string("reads '") + row + "'");
    {
        wx::Period a = day, b = night;
        a.hasPop = false;
        b.hasPop = false;
        Day t = {1, &a, &b};
        WeekRow(x.f, t, row, sizeof row);
        check("W4", std::string(row) == "SAT  67/52   0%", std::string("reads '") + row + "'");
    }
    {
        Frame fr(wxtheme::kDark.bg);
        Canvas c(fr, wxtheme::kDark);
        ComposeForecast(c, x.f);
        int rows = 0;
        for (int i = 0; i < c.nItems; ++i) rows += c.items[i].y >= kWeekTop;
        check("W5", rows == 7, "14 periods give 7 rows");
    }
    {
        Fx o;
        o.TonightStarted();
        Day days[kMaxDays];
        const int n = Days(o.f, days);
        WeekRow(o.f, days[0], row, sizeof row);
        char want[32];
        std::snprintf(want, sizeof want, "TON  --/%d   0%%", o.p[1].tempF);
        check("W7", n > 0 && days[0].day == nullptr && days[0].night == &o.p[1] && std::string(row) == want, std::string("reads '") + row + "'");
    }
    day.pop = 100;
    WeekRow(x.f, d, row, sizeof row);
    const int w = wxraster::TextW(row, 1);
    check("W6", std::string(row) == "SAT  67/52  100%" && w <= discgeom::ChordWidthPx(kWeekTop + kWeekStep * 6, 8, 240), std::string("reads '") + row + "'");
}

// ------------------------------------------------------------------------------------------------- C

void TableC()
{
    Fx x;
    wx::Observation o = wxnws::DayObservation();
    const int tops[3] = {70, 100, -5}, wantX[3] = {91, 79, 91}, wantDeg[3] = {139, 151, 139};
    for (int i = 0; i < 3; ++i) {
        o.tempF = tops[i];
        Frame fr(wxtheme::kDark.bg);
        Canvas c(fr, wxtheme::kDark);
        ComposeConditions(c, o, x.f);
        const Item& d = c.items[1];
        const Item& g = c.items[2];
        char id[8], what[64];
        std::snprintf(id, sizeof id, "C%d", i + 1);
        std::snprintf(what, sizeof what, "digits size %d at x %d, degree size %d at x %d", d.size, d.x, g.size, g.x);
        check(id, d.size == 4 && d.x == wantX[i] && g.size == 2 && g.x == wantDeg[i] && g.y == d.y, what);
    }
    {
        o.hasTempF = false;
        Frame fr(wxtheme::kDark.bg);
        Canvas c(fr, wxtheme::kDark);
        ComposeConditions(c, o, x.f);
        check("C4", c.nItems == 6 && c.items[1].size == 4, "'--' at size 4, no degree sign");
    }
    wx::Observation w;
    char row[32];
    w.hasWind = true;
    w.windMph = 12;
    w.hasWindDir = true;
    w.windDirDeg = 315;
    WindText(w, row, sizeof row);
    check("C5", std::string(row) == "WIND NW 12", row);
    w.hasGust = true;
    w.gustMph = 20;
    WindText(w, row, sizeof row);
    check("C6", std::string(row) == "WIND NW 12 G20", row);
    w.gustMph = 12;
    WindText(w, row, sizeof row);
    check("C7", std::string(row) == "WIND NW 12", row);
    w.hasGust = false;
    w.windMph = 0;
    WindText(w, row, sizeof row);
    check("C8", std::string(row) == "WIND CALM", row);
    w.hasWind = false;
    WindText(w, row, sizeof row);
    check("C9", std::string(row) == "WIND --", row);
    w.hasWind = true;
    w.windMph = 12;
    w.hasWindDir = false;
    WindText(w, row, sizeof row);
    check("C10", std::string(row) == "WIND 12", row);
    const int degs[5] = {22, 23, 337, 338, 360};
    const char* const dirs[5] = {"N", "NE", "NW", "N", "N"};
    for (int i = 0; i < 5; ++i) {
        char id[8];
        std::snprintf(id, sizeof id, "C%d", 11 + i);
        check(id, std::string(WindDirName(degs[i])) == dirs[i], WindDirName(degs[i]));
    }
    wx::Observation h;
    h.hasHumidity = true;
    h.humidityPct = 53;
    HumidityText(h, row, sizeof row);
    check("C16", std::string(row) == "HUMIDITY 53%", row);
    h.hasHumidity = false;
    HumidityText(h, row, sizeof row);
    check("C17", std::string(row) == "HUMIDITY --", row);
    HighLowText(x.f, row, sizeof row);
    check("C18", std::string(row) == "HI 68  LO 53", row);
    {
        Fx n(1);
        HighLowText(n.f, row, sizeof row);
        char want[24];
        std::snprintf(want, sizeof want, "HI %d  LO %d", n.p[2].tempF, n.p[1].tempF);
        check("C19", std::string(row) == want, row);
        n.f.nPeriods = 0;
        HighLowText(n.f, row, sizeof row);
        check("C20", std::string(row) == "HI --  LO --", row);
    }
    const int64_t now = 2000000000;
    check("C21", !ObsStale(now, now - 5400), "5,400 s is fresh");
    check("C22", ObsStale(now, now - 5401), "5,401 s is stale");
    check("C23", !ObsStale(now, now + 420), "a negative age is fresh, the capture's own case");
    const int64_t mid = 6 * 3600 + 86400 * 20000;
    ObservedText(mid + 13 * 3600 + 40 * 60, -360, row, sizeof row);
    check("C24", std::string(row) == "OBSERVED 1:40 PM", row);
    ObservedText(mid + 5 * 60, -360, row, sizeof row);
    check("C25", std::string(row) == "OBSERVED 12:05 AM", row);
    {
        Fx o;
        o.TonightStarted();
        HighLowText(o.f, row, sizeof row);
        char want[24];
        std::snprintf(want, sizeof want, "HI %d  LO %d", o.p[2].tempF, o.p[1].tempF);
        check("C26", std::string(row) == want, row);
    }
}

// ------------------------------------------------------------------------------------------------- A

wx::Alert MakeAlert(const char* event, int64_t start, int64_t end)
{
    wx::Alert a;
    a.event = event;
    a.startUtc = start;
    a.expiresUtc = end;
    return a;
}

void TableA()
{
    const int off = wxnws::kOffsetMin;
    const wx::Alert* out[kMaxAlerts];
    {
        const wx::Alert items[5] = {MakeAlert("Tornado Warning", 1, 2), MakeAlert("Snow Squall Warning", 1, 2),
                                    MakeAlert("Severe Thunderstorm Warning", 1, 2), MakeAlert("Flash Flood Warning", 1, 2),
                                    MakeAlert("Wind Advisory", 1, 2)};
        wx::AlertList l;
        l.items = items;
        l.count = 5;
        const int n = SelectAlerts(l, out);
        check("A1", n == 1 && std::string(out[0]->event) == "Wind Advisory", "the warning screen's four are excluded");
        Frame fr(wxtheme::kDark.bg);
        Canvas c(fr, wxtheme::kDark);
        l.count = 4;
        ComposeAlerts(c, l, off);
        check("A6", c.nItems == 2 && c.items[1].y == 116, "only warnings: the same sentence");
    }
    {
        const wx::Alert items[3] = {MakeAlert("C", 30, 40), MakeAlert("A", 10, 40), MakeAlert("B", 20, 40)};
        wx::AlertList l;
        l.items = items;
        l.count = 3;
        const int n = SelectAlerts(l, out);
        check("A2", n == 3 && *out[0]->event == 'A' && *out[1]->event == 'B' && *out[2]->event == 'C', "by start");
    }
    {
        const wx::Alert items[3] = {MakeAlert("B", 10, 40), MakeAlert("A", 10, 40), MakeAlert("C", 10, 40)};
        wx::AlertList l;
        l.items = items;
        l.count = 3;
        SelectAlerts(l, out);
        check("A3", *out[0]->event == 'B' && *out[1]->event == 'A' && *out[2]->event == 'C', "equal starts keep stored order");
    }
    {
        size_t count = 0;
        wx::AlertList l;
        l.items = wxnws::SyntheticAlerts(&count);
        l.count = count;
        Frame fr(wxtheme::kDark.bg);
        Canvas c(fr, wxtheme::kDark);
        ComposeAlerts(c, l, off);
        check("A4", c.nItems == 10, "five watches: 4 entries and +1 MORE");
    }
    {
        wx::AlertList l;
        Frame fr(wxtheme::kDark.bg);
        Canvas c(fr, wxtheme::kDark);
        ComposeAlerts(c, l, off);
        check("A5", c.nItems == 2 && c.items[1].y == 116, "none: the sentence, row 116");
    }
    {
        const int64_t mid = 6 * 3600 + 86400 * 20000;
        int back = 0;
        while (LocalAt(mid - back * 86400, off).weekday != 4) ++back;
        const int64_t fri = mid - back * 86400;
        char span[40];
        AlertSpan(MakeAlert("X", fri + 15 * 3600, fri + 24 * 3600), off, span, sizeof span);
        const std::string a = span;
        AlertSpan(MakeAlert("X", fri + 12 * 3600, fri + 24 * 3600 + 3 * 3600), off, span, sizeof span);
        const std::string b = span;
        check("A7", a == "FRI 3 PM - SAT 12 AM" && b == "FRI 12 PM - SAT 3 AM", "reads '" + a + "', then '" + b + "'");
    }
    {
        const int got[5] = {ListTop(1, 0), ListTop(2, 0), ListTop(3, 0), ListTop(4, 0), ListTop(4, 1)};
        const bool ok = got[0] == 110 && got[1] == 97 && got[2] == 84 && got[3] == 71 && got[4] == 63;
        char what[64];
        std::snprintf(what, sizeof what, "rows %d, %d, %d, %d, %d", got[0], got[1], got[2], got[3], got[4]);
        check("A8", ok, what);
    }
    {
        char up[40];
        UpperText("Wind Advisory", up, sizeof up);
        check("A9", std::string(up) == "WIND ADVISORY", up);
    }
    {
        const int w = wxraster::TextW("TROPICAL CYCLONE LOCAL STATEMENT", 1);
        int narrowest = 1 << 20;
        const int shown[5] = {1, 2, 3, 4, 4}, more[5] = {0, 0, 0, 0, 1};
        for (int k = 0; k < 5; ++k)
            for (int i = 0; i < shown[k]; ++i) {
                const int cw = discgeom::ChordWidthPx(ListTop(shown[k], more[k]) + kAlertStep * i, 8, 240);
                if (cw < narrowest) narrowest = cw;
            }
        char what[64];
        std::snprintf(what, sizeof what, "%d px, narrowest row %d px", w, narrowest);
        check("A10", w == 191 && narrowest >= w, what);
    }
}

// ---------------------------------------------------------------------------------------------- the 10 screens

struct Scene {
    std::string name;
    std::string kind;
};

void Build(const std::string& kind, const wxtheme::Theme& th, Frame& fr, Canvas& c)
{
    wx::Forecast f;
    f.issuedUtc = wxnws::kCapturedUtc;
    f.offsetMin = wxnws::kOffsetMin;
    f.hours = wxnws::kHours;
    f.nHours = 24;
    f.periods = wxnws::kPeriods;
    f.nPeriods = 14;
    (void)th;
    (void)fr;
    if (kind == "forecast-capture") {
        ComposeForecast(c, f);
    } else if (kind == "forecast-SYNTHETIC-RAIN") {
        // ref35c.py's labelled copy: the next 24 hours' chances a ramp under 30 %, and Tuesday Night 60 %.
        static const uint8_t kRamp[24] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 5, 10, 15, 20, 25, 20, 15, 10, 5, 0, 0, 0};
        wx::Hour h[24];
        wx::Period p[14];
        for (int i = 0; i < 24; ++i) {
            h[i] = wxnws::kHours[i];
            h[i].pop = kRamp[i];
            h[i].hasPop = true;
        }
        for (int i = 0; i < 14; ++i) {
            p[i] = wxnws::kPeriods[i];
            if (std::strcmp(p[i].name, "Tuesday Night") == 0) {
                p[i].pop = 60;
                p[i].hasPop = true;
            }
        }
        f.hours = h;
        f.periods = p;
        ComposeForecast(c, f);
    } else if (kind == "conditions-day") {
        ComposeConditions(c, wxnws::DayObservation(), f);
    } else if (kind == "alerts-SYNTHETIC") {
        wx::AlertList l;
        l.items = wxnws::SyntheticAlerts(&l.count);
        ComposeAlerts(c, l, f.offsetMin);
    } else {
        ComposeAlerts(c, wx::AlertList(), f.offsetMin);
    }
}

// The larger of the horizontal and vertical blank pixels between two boxes, negative when they overlap, never clamped.
int MinGap(const Canvas& c)
{
    int best = 1 << 20;
    for (int i = 0; i < c.nItems; ++i)
        for (int j = i + 1; j < c.nItems; ++j) {
            const Item& a = c.items[i];
            const Item& b = c.items[j];
            const int dx = (a.x0 > b.x0 ? a.x0 : b.x0) - (a.x1 < b.x1 ? a.x1 : b.x1) - 1;
            const int dy = (a.y0 > b.y0 ? a.y0 : b.y0) - (a.y1 < b.y1 ? a.y1 : b.y1) - 1;
            const int g = dx > dy ? dx : dy;
            if (g < best) best = g;
        }
    return best;
}

bool ChordsFit(const Canvas& c)
{
    for (int i = 0; i < c.nItems; ++i)
        if (c.items[i].width > discgeom::ChordWidthPx(c.items[i].y, 8 * c.items[i].size, 240)) return false;
    return true;
}

bool DotsAre(const Frame& fr, const wxtheme::Theme& th, int page)
{
    static const int kDotX[kPages] = {100, 110, 120, 130, 140};
    const uint16_t fg = Frame::Pack(th.uiFg), dim = Frame::Pack(th.uiDim), bg = Frame::Pack(th.bg);
    int lit = 0;
    for (int y = 218; y < 232; ++y)
        for (int x = 90; x < 150; ++x) {
            const uint16_t v = fr.At(x, y);
            if (v == bg) continue;
            ++lit;
            bool in = false;
            for (int i = 0; i < kPages; ++i) {
                const int r = i == page ? 2 : 1;
                if (x >= kDotX[i] - r && x <= kDotX[i] + r && y >= 224 - r && y <= 224 + r) in = v == (i == page ? fg : dim);
            }
            if (!in) return false;
        }
    return lit == 61;
}

void Screens(const std::map<std::string, std::string>& ref)
{
    static const char* const kKinds[5] = {"forecast-capture", "forecast-SYNTHETIC-RAIN", "conditions-day", "alerts-SYNTHETIC",
                                          "alerts-capture"};
    const int gaps[5] = {3, 3, 4, 3, 100};
    const int dotPage[5] = {1, 1, 2, 3, 3};
    for (int t = 0; t < 2; ++t) {
        const wxtheme::Theme& th = t ? wxtheme::kLight : wxtheme::kDark;
        const std::string theme = t ? "light" : "dark";
        for (int k = 0; k < 5; ++k) {
            const std::string name = std::string(kKinds[k]) + "-" + theme;
            Frame fr(th.bg);
            Canvas c(fr, th);
            Build(kKinds[k], th, fr, c);
            if (t == 0 && k != 1 && k != 4) {
                const char* id = k == 0 ? "D1" : k == 2 ? "D2" : "D3";
                check(id, DotsAre(fr, th, dotPage[k]), name + ": 61 dot pixels");
            }
            const int gap = MinGap(c);
            char what[64];
            std::snprintf(what, sizeof what, "smallest gap %d px, want %d; chords %s", gap, gaps[k], ChordsFit(c) ? "fit" : "REFUSED");
            check("L-" + name, gap == gaps[k] && gap >= 3 && ChordsFit(c), what);
            const std::string got = fr.Hash();
            std::map<std::string, std::string>::const_iterator it = ref.find(name);
            check("K-" + name, it != ref.end() && it->second == got, got.substr(0, 12));
        }
    }
}

}  // namespace

int main(int argc, char** argv)
{
    if (argc != 2) {
        std::printf("usage: test_wx_swipe <k35c.ref.txt>\n");
        return 2;
    }
    std::ifstream in(argv[1]);
    if (!in) {
        std::printf("cannot read %s (the RIG)\n", argv[1]);
        return 2;
    }
    std::map<std::string, std::string> ref;
    std::string line;
    while (std::getline(in, line)) {
        std::istringstream row(line);
        std::string tag, name, hash;
        row >> tag >> name >> hash;
        if (tag == "ROW") ref[name] = hash;
    }
    if (ref.size() != 10) {
        std::printf("k35c.ref.txt holds %d rows, want 10 (the RIG)\n", (int)ref.size());
        return 2;
    }
    TableN();
    TableH();
    TableStrip();
    TableW();
    TableC();
    TableA();
    Screens(ref);
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
