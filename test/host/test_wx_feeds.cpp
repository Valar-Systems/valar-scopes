// D1 (docs/weatherscope-stage3.md, section 2 and A6.2): Weatherscope's three feed interfaces build and
// run on the host against FAKES, with no Arduino core and no stored implementation in sight. The
// consumer below sees only the interfaces -- the shape step 3.3's screen logic takes -- so it cannot
// tell which implementation stands behind them.
//
// Include path: include/ (for WxCrop.h) and src/weatherscope/ (for WxFeeds.h). Nothing else.
//
// THE SABOTAGE, built only by tools/weatherscope/s32/s32.py (never by run.sh): -DWX_FEEDS_SABOTAGE
// makes the fake alert feed drop its last alert, and exactly the "both alerts, in order" case must
// fail. A test nobody has seen fail is not evidence.

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "WxCrop.h"
#include "WxFeeds.h"

namespace {

int checks = 0, failures = 0;

void check(bool ok, const char* what)
{
    ++checks;
    if (!ok) ++failures;
    std::printf("  %s %s\n", ok ? "ok  " : "FAIL", what);
}

class FakeWxFeed : public wx::WxFeed {
public:
    std::vector<uint8_t> bytes;
    wx::CropBytes Latest() const override
    {
        wx::CropBytes c;
        if (!bytes.empty()) { c.data = bytes.data(); c.size = bytes.size(); }
        return c;
    }
};

class FakeObsFeed : public wx::ObsFeed {
public:
    wx::Observation obs;
    wx::Observation Latest() const override { return obs; }
};

class FakeAlertFeed : public wx::AlertFeed {
public:
    std::vector<wx::Alert> alerts;
    wx::AlertList Active() const override
    {
        wx::AlertList l;
        l.items = alerts.empty() ? nullptr : alerts.data();
        l.count = alerts.size();
#ifdef WX_FEEDS_SABOTAGE
        if (l.count) --l.count;
#endif
        return l;
    }
};

// The consumer: interfaces in, plain facts out.
struct Summary {
    bool cropEmpty = true;
    wxcrop::Status cropStatus = wxcrop::Status::TooShort;
    uint32_t cells = 0;
    bool obsValid = false;
    unsigned present = 0;
    size_t alerts = 0;
    const char* firstEvent = nullptr;
    const char* lastEvent = nullptr;
};

Summary Summarise(const wx::WxFeed& radar, const wx::ObsFeed& obs, const wx::AlertFeed& alerts)
{
    Summary s;
    const wx::CropBytes c = radar.Latest();
    s.cropEmpty = c.Empty();
    wxcrop::Header h;
    s.cropStatus = wxcrop::Validate(c.data, c.size, &h);
    if (s.cropStatus == wxcrop::Status::Ok) s.cells = h.Cells();
    const wx::Observation o = obs.Latest();
    s.obsValid = o.valid;
    s.present = o.presentCount;
    const wx::AlertList a = alerts.Active();
    s.alerts = a.count;
    if (a.count) { s.firstEvent = a.items[0].event; s.lastEvent = a.items[a.count - 1].event; }
    return s;
}

// Not aggregate-initialised: wx::Alert has default member initialisers, which C++11 (run.sh's -std) does not allow
// in an aggregate.
wx::Alert MakeAlert(const char* event, const char* headline, int64_t expiresUtc)
{
    wx::Alert a;
    a.event = event;
    a.headline = headline;
    a.expiresUtc = expiresUtc;
    return a;
}

bool ReadFile(const char* path, std::vector<uint8_t>& out)
{
    FILE* f = std::fopen(path, "rb");
    if (!f) return false;
    uint8_t buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) out.insert(out.end(), buf, buf + n);
    std::fclose(f);
    return !out.empty();
}

}  // namespace

int main(int argc, char** argv)
{
    if (argc != 2) {
        std::printf("usage: test_wx_feeds <test/fixtures/wx>\n");
        return 2;
    }
    std::vector<uint8_t> crop;
    const std::string path = std::string(argv[1]) + "/kdfw-60mi.wxc";
    if (!ReadFile(path.c_str(), crop)) {
        std::printf("cannot read %s\n", path.c_str());
        return 2;
    }

    FakeWxFeed radar;
    FakeObsFeed obs;
    FakeAlertFeed alerts;

    std::printf("== empty feeds ==\n");
    Summary s = Summarise(radar, obs, alerts);
    check(s.cropEmpty && s.cropStatus != wxcrop::Status::Ok, "no crop: empty, and refused by the decoder");
    check(!s.obsValid && s.present == 0, "no observation: not valid");
    check(s.alerts == 0 && s.firstEvent == nullptr, "no alerts: an empty list");

    std::printf("== the KDFW fixture, a two-entry observation, two alerts ==\n");
    radar.bytes = crop;
    obs.obs.valid = true;
    obs.obs.timeUtc = 1792619700;
    obs.obs.presentCount = 2;
    obs.obs.present[0] = "-RA";
    obs.obs.present[1] = "BR";
    alerts.alerts.push_back(MakeAlert("Tornado Warning", "first", 1792455300));
    alerts.alerts.push_back(MakeAlert("Flash Flood Warning", "second", 1792459000));
    s = Summarise(radar, obs, alerts);
    check(!s.cropEmpty && s.cropStatus == wxcrop::Status::Ok && s.cells > 0, "the fixture crop: served whole, and it validates");
    check(s.obsValid && s.present == 2 && std::strcmp(obs.Latest().present[1], "BR") == 0, "the observation: both entries served");
    check(s.alerts == 2 && s.firstEvent && std::strcmp(s.firstEvent, "Tornado Warning") == 0 &&
              s.lastEvent && std::strcmp(s.lastEvent, "Flash Flood Warning") == 0,
          "the alerts: both served, in order");

    std::printf("== a corrupted crop, through the interface ==\n");
    radar.bytes[0] ^= 0xFF;
    s = Summarise(radar, obs, alerts);
    check(s.cropStatus == wxcrop::Status::BadMagic, "a broken magic is refused as BadMagic, not served as weather");

    std::printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
