// Weatherscope 3.5a's layout rules (src/weatherscope/WxScreens.h, WxRaster.h) against the truth tables frozen in
// docs/weatherscope-stage3.md A9.10: 73 named cases (IC, OV, SD, CI, LB, RG/RD/RL, ST, TM, WN, AR, ZM), with the
// readings A9.14 records. Whole frames are the K rows' (against ref35.py), not this file's.
//
//     test_wx_layout <loop dir>     the 73 cases; WN1 and WN5 build the stored warning scenario from the loop's frame
//
// Its sabotages: python -I test/host/sabotage_wx_layout.py. Prints no coordinate: boxes and points are screen
// pixels, and the synthetic places of ST11, WN and AR are offsets from (0, 0), as test_wx_logic.cpp's are.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "WxCrop.h"
#include "WxLogic.h"
#include "WxRaster.h"
#include "WxScenarios.h"
#include "WxScreens.h"

using namespace wxscreens;
using wxraster::Rgb;

namespace {

constexpr int kCases = 73;
constexpr double kDegPerMi = wxlogic::kKmPerMi / wxlogic::kKmPerDeg;   // at the equator, the map's metric

int checks = 0, failures = 0;
void check(const char* name, bool ok, const std::string& what)
{
    ++checks;
    if (!ok) ++failures;
    std::printf("  %s %s %s\n", ok ? "ok  " : "FAIL", name, what.c_str());
}

std::string B(const Box& b)
{
    char s[64];
    std::snprintf(s, sizeof s, "(%d, %d, %d, %d)", b.x0, b.y0, b.x1, b.y1);
    return s;
}
bool Eq(const Box& a, const Box& b) { return a.x0 == b.x0 && a.y0 == b.y0 && a.x1 == b.x1 && a.y1 == b.y1; }

// The text with 0xF8 and 0xFA spelled out, so the log stays ASCII.
std::string Show(const char* s)
{
    std::string o = "\"";
    for (const unsigned char* p = (const unsigned char*)s; *p; ++p) {
        if (*p == 0xF8) o += "<F8>";
        else if (*p == 0xFA) o += "<FA>";
        else o += (char)*p;
    }
    return o + "\"";
}
const char* InkName(Rgb c)
{
    if (c == wxraster::kWhite) return "white";
    if (c == wxraster::kDim) return "dim";
    if (c == wxraster::kOrange) return "orange";
    return "other";
}

const Box kBands[2] = {{0, 0, 239, 23}, {0, 194, 239, 239}};

// One label through PlaceTown, among the two bands and `extra` fixed overlays, with nothing placed yet.
Placed Label(const char* label, double x, double y, std::vector<Box> extra = {})
{
    std::vector<Box> fixed(kBands, kBands + 2);
    fixed.insert(fixed.end(), extra.begin(), extra.end());
    return PlaceTown(label, x, y, Inner(60), fixed.data(), (int)fixed.size(), nullptr, 0);
}
std::string P(const Placed& p)
{
    if (!p.placed) return "skipped";
    char s[96];
    std::snprintf(s, sizeof s, "%s; box %s; text at (%d, %d)", p.side == 'R' ? "right" : "left", B(p.box).c_str(), p.tx,
                  p.ty);
    return s;
}
bool Is(const Placed& p, char side, Box box, int tx = -1, int ty = -1)
{
    return p.placed && p.side == side && Eq(p.box, box) && (tx < 0 || p.tx == tx) && (ty < 0 || p.ty == ty);
}

class Points : public wxraster::Sink {
public:
    std::vector<std::pair<int, int>> seq;
    std::set<std::pair<int, int>> set;
    void Put(int x, int y, Rgb) override
    {
        seq.push_back({x, y});
        set.insert({x, y});
    }
};

// LB3-LB5's ten towns, "A" to "J", 60 px from the centre and 36 degrees apart from north.
class Ring10 : public TownSet {
public:
    const char* Name(int i) const override
    {
        static const char* const k[10] = {"A", "B", "C", "D", "E", "F", "G", "H", "I", "J"};
        return k[i];
    }
    void At(int i, double* x, double* y) const override
    {
        const double a = 36.0 * i * (wxraster::kPi / 180.0);
        *x = wxraster::kC + 60 * sin(a);
        *y = wxraster::kC - 60 * cos(a);
    }
};

bool ReadFile(const std::string& path, std::vector<uint8_t>* out)
{
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    out->clear();
    uint8_t buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) out->insert(out->end(), buf, buf + n);
    std::fclose(f);
    return true;
}

void Cases(const wxcrop::Header& appH, const uint8_t* appCodes)
{
    std::printf("== IC: the inner circle (%.4f px) ==\n", Inner(60));
    { const Placed p = Label("A", 144.0, 120.0); check("IC1", !p.placed, P(p)); }
    { const Placed p = Label("A", 150.0, 120.0); check("IC2", Is(p, 'R', {149, 116, 159, 124}, 154, 117), P(p)); }
    { const Placed p = Label("ABCDEF", 146.0, 120.0, {{150, 110, 200, 130}}); check("IC3", !p.placed, P(p)); }
    { const Placed p = Label("A", 145.5, 120.0); check("IC4", !p.placed, P(p)); }
    { const Placed p = Label("A", 146.0, 120.0); check("IC5", Is(p, 'R', {145, 116, 155, 124}), P(p)); }

    std::printf("== OV: 4 px of air ==\n");
    {
        const Box a{100, 60, 130, 68};
        const bool o1 = Overlaps(a, {135, 60, 150, 68}, kAir), o2 = Overlaps(a, {134, 60, 150, 68}, kAir);
        const bool o3 = Overlaps(a, {100, 73, 130, 81}, kAir), o4 = Overlaps(a, {100, 72, 130, 80}, kAir);
        check("OV1", !o1, "4 px across: " + std::to_string(o1));
        check("OV2", o2, "3 px across: " + std::to_string(o2));
        check("OV3", !o3, "4 px down: " + std::to_string(o3));
        check("OV4", o4, "3 px down: " + std::to_string(o4));
    }
    { const Placed p = Label("A", 170.0, 185.0); check("OV5a", Is(p, 'R', {169, 181, 179, 189}), P(p)); }
    { const Placed p = Label("A", 170.0, 186.0); check("OV5b", !p.placed, P(p)); }
    { const Placed p = Label("A", 170.0, 32.0); check("OV5c", Is(p, 'R', {169, 28, 179, 36}), P(p)); }
    { const Placed p = Label("A", 170.0, 31.0); check("OV5d", !p.placed, P(p)); }
    { const Placed p = Label("A", 100.0, 75.0, {{91, 85, 148, 85}}); check("OV6a", Is(p, 'L', {90, 71, 102, 79}), P(p)); }
    { const Placed p = Label("A", 100.0, 77.0, {{91, 85, 148, 85}}); check("OV6b", !p.placed, P(p)); }

    std::printf("== SD: the side rule ==\n");
    { const Placed p = Label("A", 170.0, 80.0); check("SD1", Is(p, 'R', {169, 76, 179, 84}, 174), P(p)); }
    { const Placed p = Label("A", 70.0, 80.0); check("SD2", Is(p, 'L', {60, 76, 72, 84}, 61), P(p)); }
    { const Placed p = Label("A", 170.0, 80.0, {{180, 70, 230, 90}}); check("SD3", Is(p, 'L', {160, 76, 172, 84}, 161), P(p)); }
    { const Placed p = Label("A", 170.0, 80.0, {{150, 70, 200, 90}}); check("SD4", !p.placed, P(p)); }
    { const Placed p = Label("A", 120.0, 60.0); check("SD5", Is(p, 'R', {119, 56, 129, 64}), P(p)); }

    std::printf("== CI: the 116 px circle ==\n");
    { const Placed p = Label("ABCDEF", 200.0, 120.0); check("CI1", Is(p, 'L', {160, 116, 202, 124}, 161), P(p)); }
    { const Placed p = Label("ABCDEF", 236.0, 120.0); check("CI2", !p.placed, P(p)); }

    std::printf("== LB: ranking and caps ==\n");
    auto orderOf = [](std::vector<uint32_t> pop) {
        std::vector<int> o(pop.size());
        RankOrder(pop.data(), (int)pop.size(), o.data());
        std::string s;
        for (int v : o) s += (s.empty() ? "" : ",") + std::to_string(v);
        return s;
    };
    { const std::string o = orderOf({5000, 50000, 500000}); check("LB1", o == "2,1,0", "order [" + o + "]"); }
    { const std::string o = orderOf({999, 0}); check("LB2a", o == "1,0", "order [" + o + "]"); }
    { const std::string o = orderOf({1000, 0}); check("LB2b", o == "0,1", "order [" + o + "]"); }
    {
        const Ring10 ring;
        uint32_t pop[10];
        for (int i = 0; i < 10; ++i) pop[i] = 10000u * (uint32_t)(i + 1);
        int order[10];
        RankOrder(pop, 10, order);
        const struct { const char* name; int R; const char* want; } lb[3] = {
            {"LB3", 60, "JIHGFEDC"}, {"LB4", 30, "JIHGFE"}, {"LB5", 15, "JIHGF"}};
        for (const auto& c : lb) {
            PlacedLabel out[kMaxLabels];
            const int n = PlaceTowns(ring, order, 10, c.R, Inner(c.R), kBands, 2, out);
            std::string got;
            for (int i = 0; i < n; ++i) got += out[i].label;
            check(c.name, got == c.want, "R = " + std::to_string(c.R) + ": placed " + got + " (" + std::to_string(n) + ")");
        }
    }

    std::printf("== RG, RD, RL: the rings ==\n");
    {
        const struct { const char* name; int R; const char* want; } rg[3] = {
            {"RG1", 60, "15,30"}, {"RG2", 30, "5,15"}, {"RG3", 15, "5"}};
        for (const auto& c : rg) {
            int mi[3];
            const int n = RingsFor(c.R, mi);
            std::string got;
            for (int i = 0; i < n; ++i) got += (got.empty() ? "" : ",") + std::to_string(mi[i]);
            check(c.name, got == c.want, "R = " + std::to_string(c.R) + ": rings {" + got + "}");
        }
    }
    {
        Points pts;
        const int drawn = RingDots(pts, 60.0, wxraster::kRing);
        const bool start = pts.seq.size() >= 2 && pts.seq[0] == std::make_pair(120, 60) && pts.seq[1] == std::make_pair(122, 60);
        check("RD1", drawn == 188 && (int)(2 * wxraster::kPi * 60.0) == 376 && start,
              std::to_string(drawn) + " dots, from (" + std::to_string(pts.seq[0].first) + ", " +
                  std::to_string(pts.seq[0].second) + "), (" + std::to_string(pts.seq[1].first) + ", " +
                  std::to_string(pts.seq[1].second) + ")");
    }
    {
        Points pts;
        const int drawn = RingDots(pts, 30.0, wxraster::kRing);
        check("RD2", drawn == 94, std::to_string(drawn) + " dots");
    }
    {
        int tx = 0, ty = 0;
        Box b{0, 0, 0, 0};
        const bool ok = PlaceRingLabel(60.0, "30", nullptr, 0, &tx, &ty, &b);
        check("RL1", ok && tx == 164 && ty == 68 && Eq(b, {163, 67, 176, 76}),
              "text at (" + std::to_string(tx) + ", " + std::to_string(ty) + "); box " + B(b));
        const Box obs{160, 60, 180, 80};
        const bool ok2 = PlaceRingLabel(60.0, "30", &obs, 1, &tx, &ty, &b);
        check("RL2", ok2 && tx == 164 && ty == 161 && Eq(b, {163, 160, 176, 169}),
              "text at (" + std::to_string(tx) + ", " + std::to_string(ty) + "); box " + B(b));
    }

    std::printf("== ST: the state line ==\n");
    auto st = [](const char* name, StateIn in, const char* want, Rgb ink) {
        char out[48];
        Rgb got;
        StateLine(in, out, sizeof out, &got);
        check(name, std::strcmp(out, want) == 0 && got == ink, Show(out) + ", " + InkName(got));
    };
    auto echo = [](double mi, double deg) {
        StateIn in;
        in.echo.found = true;
        in.echo.mi = mi;
        in.echo.bearingDeg = deg;
        return in;
    };
    st("ST1", echo(14.328384, 108.2334), "RAIN 14 MI E", wxraster::kWhite);
    st("ST2", echo(23.555802, 162.1), "RAIN 24 MI S", wxraster::kDim);
    st("ST3", echo(15.0, 90), "RAIN 15 MI E", wxraster::kWhite);
    st("ST4", echo(15.01, 90), "RAIN 15 MI E", wxraster::kDim);
    st("ST5", echo(14.5, 0), "RAIN 15 MI N", wxraster::kWhite);
    { StateIn in; in.hereNow = wxlogic::Precip::Rain; st("ST6", in, "RAIN NOW", wxraster::kWhite); }
    { StateIn in; in.hereNow = wxlogic::Precip::Snow; st("ST7", in, "SNOW NOW", wxraster::kWhite); }
    { StateIn in; in.hasTemp = true; in.tempF = 54.6; st("ST8", in, "55\xF8 \xFA NO RAIN", wxraster::kDim); }
    { StateIn in; in.hasTemp = true; in.tempF = -3.5; st("ST9", in, "-3\xF8 \xFA NO RAIN", wxraster::kDim); }
    { StateIn in; st("ST10", in, "NO RAIN 60 MI", wxraster::kDim); }
    {
        // A one-column crop on the equator, 0.001 degree cells, centre (0, 0); one echo cell, k cells north.
        wxcrop::Header h;
        h.rows = 1801;
        h.cols = 1;
        h.latNwMicro = 900000;
        h.lonNwMicro = 0;
        h.cellLatMicro = 1000;
        h.cellLonMicro = 1000;
        const LatLon here = wxscen::Centre(h);
        auto at = [&](int k) {
            std::vector<uint8_t> codes(h.Cells(), 0);
            codes[900 - k] = 5;
            return NearestEchoInView(h, codes.data(), here, kInViewMi);
        };
        const Echo a = at(876), b = at(864);
        const double aMi = 876 * 0.001 * wxlogic::kKmPerDeg / wxlogic::kKmPerMi;
        char d[96];
        std::snprintf(d, sizeof d, "only echo at %.3f mi: found %d", aMi, a.found);
        check("ST11a", !a.found && aMi > 60.0, d);
        std::snprintf(d, sizeof d, "found %d, %.3f mi", b.found, b.mi);
        check("ST11b", b.found && fabs(b.mi - 59.6) <= 0.1, d);
    }
    { StateIn in = echo(14.328384, 108.2334); in.ageS = 1801; st("ST12", in, "RADAR STALE", wxraster::kDim); }
    { StateIn in = echo(14.328384, 108.2334); in.ageS = 1800; st("ST13", in, "RAIN 14 MI E", wxraster::kWhite); }

    std::printf("== TM: the frame time ==\n");
    auto tm = [](const char* name, int64_t age, bool warning, const char* want, Rgb ink) {
        char out[48];
        Rgb got;
        TimeText("4:24 PM", age, warning, out, sizeof out, &got);
        check(name, std::strcmp(out, want) == 0 && got == ink,
              std::string(warning ? "warning, " : "") + "age " + std::to_string((long)age) + ": " + Show(out) + ", " +
                  InkName(got));
    };
    tm("TM1", 600, false, "4:24 PM", wxraster::kDim);
    tm("TM2", 601, false, "4:24 PM", wxraster::kOrange);
    tm("TM3", 1801, true, "4:24 PM \xFA STALE", wxraster::kOrange);
    tm("TM4", 1800, true, "4:24 PM", wxraster::kOrange);
    tm("TM5", 1801, false, "4:24 PM", wxraster::kOrange);

    std::printf("== WN: the warning texts ==\n");
    const LatLon o{0, 0};
    auto wn = [](const char* name, const WarningTexts& t, const char* top1, const char* top2, const char* band1,
                 const char* band2) {
        const bool ok = std::strcmp(t.top1, top1) == 0 && (!top2 || std::strcmp(t.top2, top2) == 0) &&
                        std::strcmp(t.band1, band1) == 0 && std::strcmp(t.band2, band2) == 0;
        check(name, ok, Show(t.top1) + "; " + Show(t.top2) + "; " + Show(t.band1) + "; " + Show(t.band2));
    };
    {
        wxscen::Scenario s;
        const bool built = wxscen::Build(wxscen::Kind::Warning, appH, appCodes, &s);
        const wxlogic::MotionText m = wxlogic::MotionLine(wxlogic::ParseMotion(s.warn.motion), s.here);
        wn("WN1", MakeWarningTexts(s.warn, m), "SEVERE T-STORM", "WARNING \xFA UNTIL 5:09 PM",
           "HAIL 1 IN \xFA WIND 60 MPH", built ? "STORM 14 MI E \xFA NE 35 MPH" : "(scenario not built)");
        std::string still = s.motion;
        const size_t k = still.find("...30KT...");
        if (k != std::string::npos) still.replace(k, 10, "...0KT...");
        const wxlogic::MotionText m0 = wxlogic::MotionLine(wxlogic::ParseMotion(still.c_str()), s.here);
        wn("WN5", MakeWarningTexts(s.warn, m0), "SEVERE T-STORM", nullptr, "HAIL 1 IN \xFA WIND 60 MPH",
           k != std::string::npos ? "STORM 14 MI E \xFA STATIONARY" : "(no 30KT to replace)");
    }
    {
        char mot[96];
        const double d = 3.0 / sqrt(2.0) * kDegPerMi;
        std::snprintf(mot, sizeof mot, "t...storm...225DEG...25KT...%.8f,%.8f", -d, -d);
        wxlogic::Warn w;
        w.event = "Tornado Warning";
        w.tornadoDetection = "RADAR INDICATED";
        w.maxHailSize = "1.50";
        w.motion = mot;
        wn("WN2", MakeWarningTexts(w, wxlogic::MotionLine(wxlogic::ParseMotion(mot), o)), "TORNADO", nullptr,
           "RADAR INDICATED \xFA HAIL 1.5 IN", "STORM 3 MI SW \xFA NE 29 MPH");
    }
    {
        wxlogic::Warn w;
        w.event = "Flash Flood Warning";
        w.flashFloodDetection = "RADAR INDICATED";
        wn("WN3", MakeWarningTexts(w, wxlogic::MotionLine(wxlogic::ParseMotion(nullptr), o)), "FLASH FLOOD", nullptr,
           "RADAR INDICATED", "");
    }
    {
        char mot[96];
        std::snprintf(mot, sizeof mot, "t...storm...270DEG...20KT...0,%.8f", -8.0 * kDegPerMi);
        wxlogic::Warn w;
        w.event = "Snow Squall Warning";
        w.motion = mot;
        wn("WN4", MakeWarningTexts(w, wxlogic::MotionLine(wxlogic::ParseMotion(mot), o)), "SNOW SQUALL", nullptr, "",
           "STORM 8 MI W \xFA E 23 MPH");
    }
    {
        wxlogic::Warn w;
        w.event = "Tornado Warning";
        w.tornadoDamageThreat = "CATASTROPHIC";
        w.maxHailSize = "1.00";
        wn("WN6", MakeWarningTexts(w, wxlogic::MotionText()), "TORNADO", nullptr, "TORNADO EMERGENCY \xFA HAIL 1 IN", "");
    }

    std::printf("== AR: the arrow ==\n");
    {
        const Arrow a = MakeArrow(150.0, 100.0, 240);
        auto px = [](double x, double y) { return "(" + std::to_string(wxraster::Rnd(x)) + ", " + std::to_string(wxraster::Rnd(y)) + ")"; };
        const std::string s = px(a.seg[0][0], a.seg[0][1]) + " -> " + px(a.seg[0][2], a.seg[0][3]) + "; heads " +
                              px(a.seg[1][2], a.seg[1][3]) + ", " + px(a.seg[2][2], a.seg[2][3]);
        check("AR1", s == "(150, 100) -> (160, 94); heads (158, 97), (156, 94)", s);
        Points pts;
        DrawArrow(pts, a);
        std::string all;
        for (const auto& p : pts.set) all += " " + std::to_string(p.first) + "," + std::to_string(p.second);
        std::printf("AR1PIX%s\n", all.c_str());   // A9.14: the K harness holds this set to ref35.py's
    }
    {
        const LatLon far{0, 60.5 * kDegPerMi}, near{0, 59.5 * kDegPerMi};
        check("AR2a", !ArrowVisible(o, far), "60.5 mi: " + std::to_string(ArrowVisible(o, far)));
        check("AR2b", ArrowVisible(o, near), "59.5 mi: " + std::to_string(ArrowVisible(o, near)));
    }
    {
        Points pts;
        DrawArrow(pts, MakeArrow(150.0, 190.0, 0));
        const std::set<std::pair<int, int>> want = {{150, 190}, {150, 191}, {150, 192}, {150, 193}};
        std::string all;
        for (const auto& p : pts.set) all += " (" + std::to_string(p.first) + ", " + std::to_string(p.second) + ")";
        check("AR3", pts.set == want, "drawn:" + all);
    }
    {
        const Arrow a = MakeArrow(150.0, 100.0, 240);
        const Placed with = Label("A", 150.0, 100.0, {ArrowBox(a)});
        const Placed without = Label("A", 150.0, 100.0);
        check("AR4", !with.placed && without.placed,
              "arrow box " + B(ArrowBox(a)) + ": " + P(with) + "; without it: " + P(without));
    }

    std::printf("== ZM: zoom ==\n");
    const radarzoom::Ladder l = ZoomLadder();
    {
        std::string got;
        for (int i = 0; i < l.n; ++i) got += (got.empty() ? "" : ",") + std::to_string(ZoomMiles(l, i));
        check("ZM1", got == "15,30,60" && ZoomMiles(l, l.Top()) == 60, "{" + got + "}, starting at " +
              std::to_string(ZoomMiles(l, l.Top())));
    }
    {
        int idx = l.Top();
        radarzoom::Edge e = radarzoom::Edge::None;
        std::string got;
        for (int k = 0; k < 3; ++k) {
            idx = radarzoom::Step(idx, -1, l, e);
            got += (got.empty() ? "" : ",") + std::to_string(ZoomMiles(l, idx));
        }
        check("ZM2", got == "30,15,15" && e == radarzoom::Edge::Max, "in x3: " + got + (e == radarzoom::Edge::Max ? " (Max)" : ""));
    }
    {
        int idx = 0;
        radarzoom::Edge e = radarzoom::Edge::None;
        std::string got;
        for (int k = 0; k < 3; ++k) {
            idx = radarzoom::Step(idx, +1, l, e);
            got += (got.empty() ? "" : ",") + std::to_string(ZoomMiles(l, idx));
        }
        check("ZM3", got == "30,60,60" && e == radarzoom::Edge::Min, "out x3: " + got + (e == radarzoom::Edge::Min ? " (Min)" : ""));
    }
    {
        std::string got;
        for (int idx : {l.Top(), 1, 0}) {
            const int t = longpress::ToggleTarget(idx, l.Top());
            got += (got.empty() ? "" : ",") + (t < 0 ? std::string("none") : std::to_string(ZoomMiles(l, t)));
        }
        check("ZM4", got == "15,15,60", "long press at 60, 30, 15: " + got);
    }
    {
        radarzoom::Edge e;
        const int in = radarzoom::Step(l.Top(), -1, l, e);
        const int m0 = ZoomMiles(l, in);
        const int expired = ZoomMiles(l, IdleReturn(l, in, 600000, 0));
        const int kept = ZoomMiles(l, IdleReturn(l, in, 599999, 0));
        check("ZM5", expired == 60 && kept == m0, "at " + std::to_string(m0) + ": idle 600,000 ms -> " +
              std::to_string(expired) + "; 599,999 ms -> " + std::to_string(kept));
    }
    {
        char a[24] = "", b[24] = "", c[24] = "";
        const bool ta = ZoomTag(30, a, sizeof a), tb = ZoomTag(15, b, sizeof b), tc = ZoomTag(60, c, sizeof c);
        check("ZM6", ta && tb && !tc && std::strcmp(a, "ZOOM 30 MI") == 0 && std::strcmp(b, "ZOOM 15 MI") == 0,
              Show(a) + "; " + Show(b) + "; at 60 " + (tc ? Show(c) : std::string("none")));
    }
    check("ZM7", Cap(60) == 8 && Cap(30) == 6 && Cap(15) == 5,
          std::to_string(Cap(60)) + ", " + std::to_string(Cap(30)) + ", " + std::to_string(Cap(15)));
    {
        const TopStrip u = TopStripAt("4:24 PM", 60), z = TopStripAt("4:24 PM", 30);
        check("ZM8", u.timeX == 99 && !u.hasTag && z.hasTag && z.timeX == 63 && z.tagX == 117,
              "unzoomed: time at x " + std::to_string(u.timeX) + "; zoomed: the run at x " + std::to_string(z.timeX) +
                  ", the tag at x " + std::to_string(z.tagX));
    }
}

}  // namespace

int main(int argc, char** argv)
{
    if (argc != 2) {
        std::printf("usage: test_wx_layout <loop dir>\n");
        return 2;
    }
    std::vector<uint8_t> bytes;
    wxcrop::Header h;
    if (!ReadFile(std::string(argv[1]) + "/shsr-20240521-212400.wxc", &bytes) ||
        wxcrop::Validate(bytes.data(), bytes.size(), &h) != wxcrop::Status::Ok) {
        std::printf("cannot read the approaching frame (the RIG)\n");
        return 2;
    }
    std::vector<uint8_t> codes(h.Cells(), 0);
    if (wxcrop::DecodeInto(bytes.data(), bytes.size(), codes.data(), codes.size()) != wxcrop::Status::Ok) {
        std::printf("cannot decode the approaching frame (the RIG)\n");
        return 2;
    }
    Cases(h, codes.data());
    std::printf("%d checks, %d failures\n", checks, failures);
    if (checks != kCases) {
        std::printf("the RIG: %d cases ran, A9.10 froze %d\n", checks, kCases);
        return 2;
    }
    return failures ? 1 : 0;
}
