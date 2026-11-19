// W7 slice 3b's host rows (docs/weatherscope-stage4.md S4.39, S4.40): the live map (WxLiveMap.h) -- the reviewer's
// basemap ruling on Bend's real package, a live frame's age, no stored towns -- the setup screens (WxSetupScreen.h), and
// Decide()'s inputs (WxSetup.h InputsOf). Pure C++11. Its sabotages: test/host/sabotage_wx_livemap.py.
//
//   g++ -std=c++11 -Iinclude -Isrc/weatherscope test/host/test_wx_livemap.cpp
//   test_wx_livemap <test/fixtures/wx>
//
// Exit 0 all pass, 1 any fail, 2 the rig (a fixture missing or invalid). Prints ids, counts and screen words only: no
// coordinate. Its points are offsets from the package's origin, in km.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "DiscGeometry.h"
#include "WxLiveMap.h"
#include "WxSetupScreen.h"

namespace {

int failures = 0;

void check(bool ok, const char* id, const std::string& what)
{
    if (!ok) ++failures;
    std::printf("  %s  %s %s\n", ok ? "ok  " : "FAIL", id, what.c_str());
}

// A 240 x 240 RGB565 frame, the board's packing, so equal pixels mean an equal screen.
class Px : public wxraster::Sink {
public:
    explicit Px(wxraster::Rgb ground) : ground_(Pack(ground)), px(240 * 240, ground_) {}
    void Put(int x, int y, wxraster::Rgb c) override { px[y * 240 + x] = Pack(c); }
    static uint16_t Pack(wxraster::Rgb c) { return (uint16_t)(((c.r >> 3) << 11) | ((c.g >> 2) << 5) | (c.b >> 3)); }
    long Diff(const Px& o) const
    {
        long n = 0;
        for (size_t i = 0; i < px.size(); ++i) n += px[i] != o.px[i];
        return n;
    }
    bool Blank() const
    {
        for (uint16_t v : px)
            if (v != ground_) return false;
        return true;
    }
    bool operator==(const Px& o) const { return px == o.px; }

private:
    uint16_t ground_;
    std::vector<uint16_t> px;
};

const wxtheme::Theme& T() { return wxtheme::kDark; }

bool Read(const std::string& path, std::vector<uint8_t>* out)
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

struct Scr {
    std::vector<int32_t> qx, qy;
    std::vector<double> x, y, cross;
    wxbase::Scratch s;
    explicit Scr(uint32_t n) : qx(n), qy(n), x(n), y(n), cross(n)
    {
        s.qx = qx.data();
        s.qy = qy.data();
        s.x = x.data();
        s.y = y.data();
        s.cross = cross.data();
        s.cap = n;
    }
};

// The point e km east and s km south of the package's origin, in its own planar metric (WxBasemap.h's Mapper).
void PointAt(const bmpk::Package& pk, double e, double s, double* lat, double* lon)
{
    const double lat0 = pk.lat0Micro / 1e6, lon0 = pk.lon0Micro / 1e6;
    const double c0 = std::fmax(std::cos(lat0 * (wxraster::kPi / 180)), 0.01);
    *lat = lat0 - s / wxbase::kKmPerDeg;
    *lon = lon0 + e / (wxbase::kKmPerDeg * c0);
}

// A copy of `h`, moved so its centre is (lat, lon): the same frame, at another place.
wxcrop::Header MovedTo(const wxcrop::Header& h, double lat, double lon)
{
    const auto c = wxscen::Centre(h);
    wxcrop::Header m = h;
    m.latNwMicro = h.latNwMicro + (int32_t)std::lround((lat - c.lat) * 1e6);
    m.lonNwMicro = h.lonNwMicro + (int32_t)std::lround((lon - c.lon) * 1e6);
    return m;
}

Px ComposeIn(const wxscreens::ScreenIn& in, wxscreens::ScreenOut* out)
{
    Px p(T().bg);
    static wxrender::View v;
    *out = wxscreens::ScreenOut();
    wxscreens::Compose(p, in, v, out);
    return p;
}

// The reference setup screen: the frame and the state's two rows, drawn by the same Canvas and font.
Px RefSetup(const char* title, const char* line, wxraster::Rgb titleInk)
{
    Px p(T().bg);
    wxswipe::Canvas c(p, T());
    wxswipe::Frame(c, "RADAR", wxswipe::Page::Map);
    c.Centred(title, wxsetupscreen::kTitleY, 1, titleInk);
    if (line[0] != 0) c.Centred(line, wxsetupscreen::kLineY, 1, T().uiDim);
    return p;
}

}  // namespace

int main(int argc, char** argv)
{
    if (argc != 2) {
        std::printf("usage: test_wx_livemap <test/fixtures/wx>\n");
        return 2;
    }
    const std::string root = argv[1];
    std::vector<uint8_t> pkBytes, crop;
    bmpk::Package pk;
    wxcrop::Header h;
    if (!Read(root + "/basemap/bend.bmpk", &pkBytes) ||
        bmpk::Validate(pkBytes.data(), pkBytes.size(), &pk) != bmpk::Status::kOk) {
        std::printf("the RIG: basemap/bend.bmpk missing or invalid\n");
        return 2;
    }
    if (!Read(root + "/bend-loop/synthetic-clear.wxc", &crop) ||
        wxcrop::Validate(crop.data(), crop.size(), &h) != wxcrop::Status::Ok) {
        std::printf("the RIG: bend-loop/synthetic-clear.wxc missing or invalid\n");
        return 2;
    }
    std::vector<uint8_t> codes(h.Cells(), 0xEE);
    if (wxcrop::DecodeInto(crop.data(), crop.size(), codes.data(), codes.size()) != wxcrop::Status::Ok) {
        std::printf("the RIG: the crop does not decode\n");
        return 2;
    }
    const int off = wxscen::kBendOffsetMin;
    const int64_t F = h.frameEpoch;
    Scr sc(pk.maxVertices + 1);
    std::vector<uint8_t> img(wxbase::kSize * wxbase::kSize, 0);
    static wxlivemap::PackageView pv;

    // ------------------------------------------------------------------------------------------ the basemap ruling
    {
        struct Cv { double e, s; int mi; bool want; };
        const Cv cv[] = {{0, 0, 60, true},    {0.43, 0, 60, true},  {0.45, 0, 60, false}, {0, -0.43, 60, true},
                         {0, -0.45, 60, false}, {48.70, 0, 30, true}, {48.75, 0, 30, false}};
        int wrong = 0;
        for (const Cv& c : cv) {
            double lat, lon;
            PointAt(pk, c.e, c.s, &lat, &lon);
            if (wxlivemap::Covers(pk, lat, lon, c.mi * wxlivemap::kKmPerMi) != c.want) ++wrong;
        }
        check(wrong == 0, "LIVE-COVERS", "the whole view inside the package's 97 km disc, and nothing less: 7 cases, " +
                                             std::to_string(wrong) + " wrong");
    }
    {
        const double ds[] = {1.0, 20.0, 97.0, 145.0, 193.0};   // every view still overlaps the package's disc
        int points = 0, withBase = 0;
        long diff = 0;
        static wxscreens::ScreenIn live, ref;
        static wxscreens::ScreenOut out;
        for (int b = 0; b < 8; ++b)
            for (double d : ds) {
                const double a = b * 45.0 * (wxraster::kPi / 180);
                double lat, lon;
                PointAt(pk, d * std::sin(a), -d * std::cos(a), &lat, &lon);
                const wxcrop::Header m = MovedTo(h, lat, lon);
                live = wxscreens::ScreenIn();
                wxlivemap::FromLive(m, codes.data(), 60, F + 60, off, &live);
                wxlivemap::LivePackage(pk, m, 60, false, sc.s, img.data(), &pv, &live);
                if (live.base != nullptr || live.places != nullptr || live.peaks != nullptr || live.mask != 0) ++withBase;
                ref = wxscreens::ScreenIn();
                wxlivemap::FromLive(m, codes.data(), 60, F + 60, off, &ref);
                const Px a1 = ComposeIn(live, &out);
                diff += a1.Diff(ComposeIn(ref, &out));
                ++points;
            }
        live = wxscreens::ScreenIn();
        wxlivemap::FromLive(h, codes.data(), 60, F + 60, off, &live);
        const bool applied = wxlivemap::LivePackage(pk, h, 60, false, sc.s, img.data(), &pv, &live);
        ref = wxscreens::ScreenIn();
        wxlivemap::FromLive(h, codes.data(), 60, F + 60, off, &ref);
        const Px b1 = ComposeIn(live, &out);
        const long bend = b1.Diff(ComposeIn(ref, &out));
        check(points == 40 && diff == 0 && withBase == 0 && applied && live.base != nullptr && bend > 0, "LIVE-BASEMAP",
              std::to_string(points) + " points outside, every view overlapping the package: " + std::to_string(diff) +
                  " pixels differ from plain ground, " + std::to_string(withBase) + " carry a package field; at Bend's "
                  "own frame the package is applied and " + std::to_string(bend) + " pixels differ");
    }

    // ----------------------------------------------------------------------------------------- the live frame
    {
        struct Ag { int64_t age; wxtheme::Ink ink; const char* state; };
        const Ag ag[] = {{599, wxtheme::Ink::Dim, "NO RAIN 60 MI"},
                         {601, wxtheme::Ink::OrangeText, "NO RAIN 60 MI"},
                         {1801, wxtheme::Ink::OrangeText, "RADAR STALE"}};
        bool ok = true;
        std::string seen;
        static wxscreens::ScreenIn in;
        static wxscreens::ScreenOut out;
        for (const Ag& a : ag) {
            in = wxscreens::ScreenIn();
            wxlivemap::FromLive(h, codes.data(), 60, F + a.age, off, &in);
            ComposeIn(in, &out);
            ok &= out.timeInk == a.ink && std::strcmp(in.state, a.state) == 0;
            seen += (seen.empty() ? "" : "; ") + std::to_string(a.age) + " s: time " + wxscenes::InkName(out.timeInk) +
                    ", '" + in.state + "'";
        }
        check(ok, "LIVE-AGE", seen);
    }
    {
        static wxscreens::ScreenIn a, b;
        a = wxscreens::ScreenIn();
        b = wxscreens::ScreenIn();
        wxlivemap::FromLive(h, codes.data(), 60, F + 60, off, &a);
        wxlivemap::FromLiveLoopFrame(h, codes.data(), 3, (uint32_t)F, 60, off, &b);
        const bool ok = a.towns == nullptr && a.nTowns == 0 && !a.warning && b.towns == nullptr && b.nTowns == 0;
        check(ok, "LIVE-NOTOWNS", "a live frame and a live loop frame carry no towns and no warning");
    }
    {
        static wxscreens::ScreenIn a, b;
        a = wxscreens::ScreenIn();
        b = wxscreens::ScreenIn();
        wxlivemap::FromLiveLoopFrame(h, codes.data(), 3, (uint32_t)F, 60, off, &a);
        wxlivemap::FromLiveLoopFrame(h, codes.data(), 0, (uint32_t)F + 240, 60, off, &b);
        const bool ok = a.loop && a.loopIndex == 3 && a.ageS == 0 && std::strcmp(a.loopAge, "NOW") == 0 && b.loop &&
                        b.loopIndex == 0 && std::strcmp(b.loopAge, "4 MIN AGO") == 0;
        check(ok, "LIVE-LOOP", std::string("loop frames read '") + a.loopAge + "' and '" + b.loopAge + "'");
    }

    // -------------------------------------------------------------------------------------------- the setup screens
    using wxsetup::Refusal;
    using wxsetup::State;
    struct St { State s; Refusal r; };
    const St states[8] = {{State::NoWifi, Refusal::None},        {State::NoLocation, Refusal::None},
                          {State::NotEnrolled, Refusal::None},   {State::Waiting, Refusal::None},
                          {State::Refused, Refusal::KeyRejected}, {State::Refused, Refusal::DeviceUnknown},
                          {State::Refused, Refusal::NotActivated}, {State::Refused, Refusal::Other}};
    {
        int wrong = 0, same = 0;
        std::vector<Px> shots;
        for (const St& x : states) {
            Px p(T().bg);
            wxswipe::Canvas c(p, T());
            const bool drew = wxsetupscreen::ComposeSetup(c, x.s, x.r);
            const wxsetup::Words w = wxsetup::WordsOf(x.s, x.r);
            const Px ref = RefSetup(w.title, w.line, x.s == State::Refused ? T().uiWarn : T().uiFg);
            if (!drew || !(p == ref)) ++wrong;
            for (const Px& o : shots) same += p == o;
            shots.push_back(p);
        }
        check(wrong == 0 && same == 0, "SETUP-DRAW",
              "each of the 8 setup screens is the frame and its own two rows: " + std::to_string(wrong) + " wrong, " +
                  std::to_string(same) + " pairs alike");
    }
    {
        Px p(T().bg);
        wxswipe::Canvas c(p, T());
        const bool drew = wxsetupscreen::ComposeSetup(c, State::Live, Refusal::None);
        check(!drew && p.Blank(), "SETUP-LIVE-BLANK", "Live draws no setup screen: the live map draws instead");
    }
    {
        int rows = 0, over = 0;
        for (const St& x : states) {
            const wxsetup::Words w = wxsetup::WordsOf(x.s, x.r);
            const char* text[2] = {w.title, w.line};
            const int y[2] = {wxsetupscreen::kTitleY, wxsetupscreen::kLineY};
            for (int k = 0; k < 2; ++k) {
                if (text[k][0] == 0) continue;
                ++rows;
                if (wxraster::TextW(text[k], 1) > discgeom::ChordWidthPx(y[k], 8, 240)) ++over;
            }
        }
        check(rows == 15 && over == 0, "SETUP-FITS",
              std::to_string(rows) + " rows, " + std::to_string(over) + " wider than the disc at their row");
    }

    // ---------------------------------------------------------------------------------------------- the inputs
    {
        wxsetup::AuthLatch none, latched, streak;
        for (uint32_t m = 0; m <= 16; m += 4) wxsetup::OnAnswer(latched, 401, "unauthorized", m * 60000u);
        for (uint32_t m = 0; m < 4; ++m) wxsetup::OnAnswer(streak, 401, "unauthorized", m * 60000u);
        struct In { bool wifi, loc; const char* pasted; const char* factory; const wxsetup::AuthLatch* l; bool crop, clock;
                    bool wWifi, wLoc, wKey; Refusal wRef; bool wFrame; };
        const In cases[] = {
            {false, true, "", "F", &none, true, true, false, true, true, Refusal::None, true},
            {true, false, "P", "", &none, false, true, true, false, true, Refusal::None, false},
            {true, true, "", "", &none, true, true, true, true, false, Refusal::None, true},
            {true, true, "", "F", &latched, true, true, true, true, true, Refusal::KeyRejected, true},
            {true, true, "", "F", &streak, true, true, true, true, true, Refusal::None, true},
            {true, true, "", "F", &none, true, false, true, true, true, Refusal::None, false},
        };
        int wrong = 0;
        for (const In& c : cases) {
            const wxsetup::Inputs in = wxsetup::InputsOf(c.wifi, c.loc, c.pasted, c.factory, *c.l, c.crop, c.clock);
            if (in.wifi != c.wWifi || in.location != c.wLoc || in.key != c.wKey || in.refused != c.wRef ||
                in.frame != c.wFrame)
                ++wrong;
        }
        check(wrong == 0, "INPUTS-FILL", "6 cases, " + std::to_string(wrong) + " wrong");
    }
    {
        wxsetup::AuthLatch none;
        const State a = wxsetup::Decide(wxsetup::InputsOf(true, true, "", "F", none, true, false));
        const State b = wxsetup::Decide(wxsetup::InputsOf(true, true, "", "F", none, true, true));
        check(a == State::Waiting && b == State::Live &&
                  std::strcmp(wxsetup::WordsOf(a, Refusal::None).title, "WAITING FOR RADAR") == 0,
              "NOCLOCK", "a held frame before the clock is set: WAITING FOR RADAR; once it is set: Live");
    }

    std::printf("%s\n", failures ? "WX LIVE MAP TESTS FAILED" : "all WX live map checks passed");
    return failures ? 1 : 0;
}
