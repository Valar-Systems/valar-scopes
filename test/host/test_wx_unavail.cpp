// W7's second slice, host-side (docs/weatherscope-stage4.md S4.32 Q126 (a), S4.35): in a live image a page whose feed is
// not live draws its UNAVAILABLE state, never the stored data as the user's, and the alerts page never draws the
// all-clear for a feed it cannot read (1.4's structural row; section 8's sabotage). Each row compares a composed page's
// PIXELS against a reference render: the frame and one centred line, drawn by the same Canvas and font. A page equal to
// its reference holds exactly that line and nothing else; the controls prove the comparison can tell the lines apart.
//
//   g++ -std=c++11 -Iinclude -Isrc/weatherscope test/host/test_wx_unavail.cpp
//
// Its sabotages: test/host/sabotage_wx_unavail.py. Prints ids and outcomes only.
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "WxForecastData.h"
#include "WxSwipe.h"

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
    explicit Px(wxraster::Rgb ground) : px(240 * 240, Pack(ground)) {}
    void Put(int x, int y, wxraster::Rgb c) override { px[y * 240 + x] = Pack(c); }
    static uint16_t Pack(wxraster::Rgb c) { return (uint16_t)(((c.r >> 3) << 11) | ((c.g >> 2) << 5) | (c.b >> 3)); }
    bool operator==(const Px& o) const { return px == o.px; }
    bool operator!=(const Px& o) const { return px != o.px; }

private:
    std::vector<uint16_t> px;
};

const wxtheme::Theme& T() { return wxtheme::kDark; }

// The reference: the page's frame and ONE centred line, at the size and ink the frozen words are drawn in.
Px Ref(const char* title, wxswipe::Page page, const char* line, int size, wxraster::Rgb ink)
{
    Px p(T().bg);
    wxswipe::Canvas c(p, T());
    wxswipe::Frame(c, title, page);
    c.Centred(line, 116, size, ink);
    return p;
}

Px Alerts(const wx::AlertList& l)
{
    Px p(T().bg);
    wxswipe::Canvas c(p, T());
    wxswipe::ComposeAlerts(c, l, wxnws::kOffsetMin);
    return p;
}

// The stored forecast, as the screens read it.
struct Fx {
    wx::Hour h[24];
    wx::Period p[14];
    wx::Forecast f;
    Fx()
    {
        for (int i = 0; i < 24; ++i) h[i] = wxnws::kHours[i];
        for (int i = 0; i < 14; ++i) p[i] = wxnws::kPeriods[i];
        f.issuedUtc = wxnws::kCapturedUtc;
        f.offsetMin = wxnws::kOffsetMin;
        f.hours = h;
        f.nHours = 24;
        f.periods = p;
        f.nPeriods = 14;
    }
};

Px Forecast(const wx::Forecast& f)
{
    Px p(T().bg);
    wxswipe::Canvas c(p, T());
    wxswipe::ComposeForecast(c, f);
    return p;
}

Px Conditions(const wx::Observation& o, const wx::Forecast& f)
{
    Px p(T().bg);
    wxswipe::Canvas c(p, T());
    wxswipe::ComposeConditions(c, o, f);
    return p;
}

}  // namespace

int main()
{
    using wxswipe::Page;
    const Px unavail = Ref("ALERTS", Page::Alerts, "ALERTS UNAVAILABLE", 1, T().uiWarn);
    const Px allClear = Ref("ALERTS", Page::Alerts, "NO WATCHES OR ADVISORIES", 1, T().uiDim);
    size_t n = 0;
    const wx::Alert* stored = wxnws::SyntheticAlerts(&n);

    {
        wx::AlertList l;
        l.available = false;
        check(Alerts(l) == unavail, "ALERTS-UNAVAIL-EMPTY",
              "a feed it cannot read, with nothing in it, draws ALERTS UNAVAILABLE and nothing else (never the all-clear)");
    }
    {
        wx::AlertList l;
        l.items = stored;
        l.count = n;
        l.available = false;
        check(n > 0 && Alerts(l) == unavail, "ALERTS-UNAVAIL-STORED",
              "with " + std::to_string(n) + " stored alerts in the list, not live: ALERTS UNAVAILABLE, none of them drawn");
    }
    {
        wx::AlertList l;   // available by default, empty
        check(unavail != allClear && Alerts(l) == allClear, "ALERTS-CONTROL",
              "the comparison tells the two lines apart, and a readable empty feed still draws the all-clear");
    }
    {
        wx::AlertList l;
        l.items = stored;
        l.count = n;
        const Px live = Alerts(l);
        check(live != unavail && live != allClear, "ALERTS-LIVE-LIST", "a readable feed with alerts still lists them");
    }
    {
        Fx x;
        x.f.available = false;
        check(Forecast(x.f) == Ref("FORECAST", Page::Forecast, "NO FORECAST", 1, T().uiDim), "FORECAST-UNAVAIL",
              "a forecast it cannot read draws NO FORECAST and nothing else, its stored hours and periods not drawn");
    }
    {
        Fx x;
        check(Forecast(x.f) != Ref("FORECAST", Page::Forecast, "NO FORECAST", 1, T().uiDim), "FORECAST-CONTROL",
              "a readable forecast is drawn as before, not as NO FORECAST");
    }
    {
        Fx x;
        wx::Observation o = wxnws::DayObservation();
        o.valid = false;   // its values are still set: none may be drawn
        check(Conditions(o, x.f) == Ref("NOW", Page::Conditions, "NO CONDITIONS", 1, T().uiDim), "CONDITIONS-UNAVAIL",
              "conditions it cannot read draw NO CONDITIONS and nothing else, the stored values not drawn");
    }
    {
        Fx x;
        const wx::Observation o = wxnws::DayObservation();
        check(Conditions(o, x.f) != Ref("NOW", Page::Conditions, "NO CONDITIONS", 1, T().uiDim), "CONDITIONS-CONTROL",
              "readable conditions are drawn as before, not as NO CONDITIONS");
    }

    std::printf("%s\n", failures ? "WX UNAVAILABLE TESTS FAILED" : "all WX unavailable checks passed");
    return failures ? 1 : 0;
}
