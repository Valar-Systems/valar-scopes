// Weatherscope 3.5t t4's SW rows (docs/weatherscope-stage3.md A12.43, as A12.60 restates them and A12.61's Q58 (a) moves
// the base row to t5): one Layer through Auto's own path -- daynight::Decide over an elevation, then wxtheme::Select --
// and wxscreens::ComposeLayered on the Dallas loop's frame 0 at 60 mi. Each step counts the layer's rebuilds and names
// their cause by the LayerKey fields that changed.
//   SW1  Dark -> Light (the sun up): 1 rebuild, cause theme      SW4  Light -> Dark (the sun down): 1, cause theme
//   SW2  a no-op evaluation (the sun still up): 0                 SW5  setting the shown theme again (Dark): 0
//   SW3  a hover inside the band (-0.5 deg after Day): 0          SW-X the control: a mask change rebuilds, cause mask
//
//     test_wx_switch <loop dir>
//
// Exit 0 all pass, 1 any fail, 2 the rig. Its sabotage: S-RPT in python -I test/host/sabotage_wx_light.py. Prints no
// coordinate.

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "DayNight.h"
#include "WxCrop.h"
#include "WxRaster.h"
#include "WxScenes.h"
#include "WxScreens.h"
#include "WxTheme.h"
#include "WxTowns.h"

namespace {

int checks = 0, failures = 0;
void check(const std::string& name, bool ok, const std::string& what)
{
    ++checks;
    if (!ok) ++failures;
    std::printf("  %s %s %s\n", ok ? "ok  " : "FAIL", name.c_str(), what.c_str());
}

class Null : public wxraster::Sink {
public:
    void Put(int, int, wxraster::Rgb) override {}
};

// The LayerKey fields that differ between two keys, by name.
std::string Cause(const wxscreens::LayerKey& a, const wxscreens::LayerKey& b)
{
    std::string c;
    auto add = [&](bool differ, const char* name) {
        if (differ) c += (c.empty() ? "" : ",") + std::string(name);
    };
    add(a.which != b.which, "theme");
    add(a.mask != b.mask, "mask");
    add(a.pkgCrc != b.pkgCrc || a.pkgLen != b.pkgLen, "package");
    add(a.radiusMi != b.radiusMi, "zoom");
    add(a.scenario != b.scenario, "scenario");
    add(a.latNwMicro != b.latNwMicro || a.lonNwMicro != b.lonNwMicro || a.rows != b.rows || a.cols != b.cols, "place");
    add(a.warning != b.warning, "warning");
    return c.empty() ? "none" : c;
}

}  // namespace

int main(int argc, char** argv)
{
    if (argc != 2) {
        std::printf("usage: test_wx_switch <loop dir>\n");
        return 2;
    }
    std::vector<uint8_t> bytes, codes;
    {
        FILE* f = std::fopen((std::string(argv[1]) + "/shsr-20240521-210400.wxc").c_str(), "rb");
        if (!f) { std::printf("cannot read the loop's frame 0 (the RIG)\n"); return 2; }
        uint8_t buf[4096];
        size_t n;
        while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) bytes.insert(bytes.end(), buf, buf + n);
        std::fclose(f);
    }
    wxcrop::Header h;
    if (wxcrop::Validate(bytes.data(), bytes.size(), &h) != wxcrop::Status::Ok) { std::printf("frame 0 invalid (the RIG)\n"); return 2; }
    codes.assign(h.Cells(), 0);
    if (wxcrop::DecodeInto(bytes.data(), bytes.size(), codes.data(), codes.size()) != wxcrop::Status::Ok) {
        std::printf("frame 0 does not decode (the RIG)\n");
        return 2;
    }
    static uint32_t pops[256];
    static int order[256];
    for (unsigned i = 0; i < wxtowns::kCount; ++i) pops[i] = wxtowns::kTowns[i].pop;
    wxscreens::RankOrder(pops, (int)wxtowns::kCount, order);

    static wxscreens::Layer layer;
    Null sink;
    daynight::Phase phase = daynight::Phase::Unknown;
    // one evaluation of Auto (or a set theme), then one composition through the layer: (rebuilds, cause)
    auto step = [&](wxtheme::Setting setting, float elevDeg, uint8_t mask) {
        phase = daynight::Decide(phase, true, true, elevDeg);
        static wxscreens::ScreenIn in;
        in = wxscreens::ScreenIn();
        wxscenes::FromLoopFrame(h, codes.data(), 0, h.frameEpoch, 60, order, &in);
        in.which = wxtheme::Select(setting, phase);
        in.mask = mask;
        const wxscreens::LayerKey before = layer.key, after = wxscreens::MakeLayerKey(in, -1);
        static wxscreens::ScreenOut out;
        out = wxscreens::ScreenOut();
        const bool rebuilt = wxscreens::ComposeLayered(sink, in, layer, -1, &out);
        return std::make_pair(rebuilt ? 1 : 0, rebuilt ? Cause(before, after) : std::string("-"));
    };
    std::printf("== SW: the switch, through one Layer ==\n");
    step(wxtheme::Setting::Auto, -10.0f, 0);   // the start: night, Dark, the layer built (not a row)
    const auto sw1 = step(wxtheme::Setting::Auto, 10.0f, 0);
    check("SW1", sw1.first == 1 && sw1.second == "theme", "Dark -> Light: " + std::to_string(sw1.first) + " rebuild(s), cause " + sw1.second);
    const auto sw2 = step(wxtheme::Setting::Auto, 10.0f, 0);
    check("SW2", sw2.first == 0, "a no-op evaluation: " + std::to_string(sw2.first) + " rebuild(s)");
    const auto sw3 = step(wxtheme::Setting::Auto, -0.5f, 0);
    check("SW3", sw3.first == 0 && phase == daynight::Phase::Day,
          "a hover inside the band: " + std::to_string(sw3.first) + " rebuild(s), the phase " + (phase == daynight::Phase::Day ? "Day" : "NOT Day"));
    const auto sw4 = step(wxtheme::Setting::Auto, -10.0f, 0);
    check("SW4", sw4.first == 1 && sw4.second == "theme", "Light -> Dark: " + std::to_string(sw4.first) + " rebuild(s), cause " + sw4.second);
    const auto sw5 = step(wxtheme::Setting::Dark, -10.0f, 0);
    check("SW5", sw5.first == 0, "setting the shown theme again: " + std::to_string(sw5.first) + " rebuild(s)");
    const auto swx = step(wxtheme::Setting::Dark, -10.0f, 1);
    check("SW-X", swx.first == 1 && swx.second == "mask", "the control, a mask change: " + std::to_string(swx.first) +
                                                              " rebuild(s), cause " + swx.second);

    std::printf("%d checks, %d failures\n", checks, failures);
    if (checks != 6) {
        std::printf("the RIG: %d checks, not 6\n", checks);
        return 2;
    }
    return failures ? 1 : 0;
}
