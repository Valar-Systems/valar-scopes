// The Blipscope's avionics weather look (include/WxAvionics.h; docs/wx-device.md, "The ruling (~15:07Z), and the
// avionics look's predictions" and "Re-registered (ruling of ~15:18Z)"). Every expected colour is TYPED HERE from the
// ruling, never read from the header under test. Rows are tagged AV1-AV7 so test/host/sabotage_wx_avionics.py can
// require exactly the sabotaged rows to go red.
//
//   test_wx_avionics.exe      exit 0 all pass, 1 a check failed
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <set>
#include <string>

#include "WxSweep.h"   // Lit565, and through it WxS2.h and WxAvionics.h

using wxpalette::Rgb;
using wxs2::Look;

static int failures = 0;
static void check(bool ok, const char* id, const std::string& what)
{
    if (!ok) ++failures;
    std::printf("  %s  %s %s\n", ok ? "ok  " : "FAIL", id, what.c_str());
}

static bool Eq(Rgb a, Rgb b) { return a.r == b.r && a.g == b.g && a.b == b.b; }
static uint16_t S565(Rgb c) { return wxs2::To565(wxs2::Pack(c)); }

// The ruling's colours (Daniel, 2026-11-06).
static const Rgb kGreen = {0, 200, 0}, kYellow = {240, 220, 0}, kRed = {230, 0, 0}, kMagenta = {255, 0, 255};
static const Rgb kCyan = {0, 255, 255}, kWhite = {255, 255, 255}, kDryRgb = {0, 0, 0};

// FNV-1a 64 over bytes, in the order the freeze stated.
struct Fnv {
    uint64_t h = 14695981039346656037ULL;
    void Byte(uint8_t b) { h ^= b; h *= 1099511628211ULL; }
    void Colour(Rgb c) { Byte(c.r); Byte(c.g); Byte(c.b); }
};

int main()
{
    std::printf("WxAvionics.h: the Blipscope's avionics weather look\n");

    // AV1: every band edge at 100 %, and code 1 (drizzle, hidden) dry with 0, 14, 15 and 16.
    {
        struct E { uint8_t code; Rgb want; };
        const E edges[] = {{2, kGreen},  {3, kGreen}, {4, kYellow},  {5, kYellow},  {6, kRed},
                           {7, kRed},    {8, kMagenta}, {13, kMagenta}, {1, kDryRgb}, {0, kDryRgb},
                           {14, kDryRgb}, {15, kDryRgb}, {16, kDryRgb}};
        std::string bad;
        for (const E& e : edges)
            if (!Eq(wxs2::EchoAt(e.code, 100), e.want)) bad += " " + std::to_string(e.code);
        check(bad.empty(), "AV1", "band edges 2-13 at 100 %, and code 1 dry with 0, 14, 15, 16" +
                                      (bad.empty() ? std::string() : "; wrong:" + bad));
    }

    // AV2: flat inside a band, at every percent.
    {
        int bad = 0;
        for (int p = 0; p <= 100; ++p) {
            bad += !Eq(wxs2::EchoAt(2, p), wxs2::EchoAt(3, p));
            bad += !Eq(wxs2::EchoAt(4, p), wxs2::EchoAt(5, p));
            bad += !Eq(wxs2::EchoAt(6, p), wxs2::EchoAt(7, p));
            for (uint8_t c = 9; c <= 13; ++c) bad += !Eq(wxs2::EchoAt(8, p), wxs2::EchoAt(c, p));
        }
        check(bad == 0, "AV2", "flat inside every band, percents 0-100: " + std::to_string(bad) + " unequal");
    }

    // AV3: Weatherscope's table is unchanged. The value was read from the source at the freeze (decimal: a 16-hex
    // literal trips the device-id hook).
    {
        Fnv f;
        for (const Rgb& c : wxpalette::kRainTable) f.Colour(c);
        f.Colour(wxpalette::kRain75);
        f.Colour(wxpalette::kRain1);
        for (const Rgb& c : wxpalette::kSnowTable) f.Colour(c);
        f.Byte((uint8_t)wxpalette::kSnowTopCode);
        f.Colour(wxpalette::kDry);
        f.Colour(wxpalette::kNoCoverage);
        f.Colour(wxpalette::kMissingHatch);
        f.Byte((uint8_t)wxpalette::kHatchPeriod);
        const uint64_t want = 17800467619102092433ULL;
        check(f.h == want, "AV3", "Weatherscope's table hash " + std::to_string(f.h) + " (pinned " + std::to_string(want) + ")");
    }

    // AV4: the planes' colour while the layer is on.
    {
        struct P { float alt; Rgb want; const char* name; };
        const P ps[] = {{0.0f, kCyan, "0"},           {-100.0f, kCyan, "-100"},    {2999.9f, kCyan, "2999.9"},
                        {3000.0f, kWhite, "3000.0"},  {12000.0f, kWhite, "12000"}, {NAN, kWhite, "NaN"}};
        std::string bad;
        for (const P& p : ps)
            if (!Eq(wxavionics::Plane(p.alt), p.want)) bad += std::string(" ") + p.name;
        check(bad.empty(), "AV4", "cyan below 3,000 m, white at and above, NaN white" + (bad.empty() ? std::string() : "; wrong at:" + bad));
    }

    // AV5: no faded plane colour (cyan, white; f = 0.30-1.0 in 1e-5 steps, truncating as the device does) is a colour
    // the layer can draw, in RGB565, in either look: every code through the lit sweep's range of percents, plus 14's
    // and 15's lit colours.
    {
        int eq = 0;
        std::string which;
        for (Look l : {Look::A, Look::B}) {
            std::set<uint16_t> layer = {S565(wxpalette::kNoCoverage), S565(wxpalette::kMissingHatch)};
            for (uint8_t c = 1; c <= 13; ++c)
                for (int p = wxs2::CodePercent(c, wxs2::Percent(l)); p <= 100; ++p)
                    if (wxavionics::Drawn(c)) layer.insert(S565(wxs2::EchoAt(c, p)));
            for (Rgb plane : {wxavionics::Plane(0.0f), wxavionics::Plane(5000.0f)})
                for (int i = 30000; i <= 100000; ++i) {
                    const float fd = i / 100000.0f;
                    const Rgb q = {(uint8_t)(plane.r * fd), (uint8_t)(plane.g * fd), (uint8_t)(plane.b * fd)};
                    if (layer.count(S565(q))) { ++eq; if (which.size() < 60) which += std::string(" ") + (l == Look::A ? "A" : "B"); }
                }
        }
        check(eq == 0, "AV5", std::to_string(eq) + " faded plane colours equal to a layer colour (RGB565)" + which);
    }

    // AV6: the at-rest anchors, hand-computed: (v * pct + 50) / 100 on the ruled colours.
    {
        struct A { Look l; uint8_t code; Rgb want; };
        const A as[] = {{Look::A, 2, {0, 80, 0}},  {Look::A, 5, {96, 88, 0}}, {Look::A, 7, {92, 0, 0}},
                        {Look::A, 13, {102, 0, 102}}, {Look::B, 2, {0, 50, 0}}, {Look::B, 5, {60, 55, 0}},
                        {Look::B, 7, {58, 0, 0}},  {Look::B, 13, {64, 0, 64}}};
        std::string bad;
        for (const A& a : as)
            if (!Eq(wxs2::Echo(a.code, a.l), a.want)) bad += std::string(" ") + (a.l == Look::A ? "A" : "B") + std::to_string(a.code);
        check(bad.empty(), "AV6", "8 anchors at rest" + (bad.empty() ? std::string() : "; wrong:" + bad));
    }

    // AV7: code 1 draws EXACTLY as code 0: the layer's pixel on a 16 x 16 patch, and look 4's lit sweep at every level,
    // in both looks. A black code 1 would leave a hole in the sweep's green wedge; this catches it.
    {
        int bad = 0;
        for (Look l : {Look::A, Look::B}) {
            for (int y = 0; y < 16; ++y)
                for (int x = 0; x < 16; ++x) bad += wxs2::Pixel888(1, x, y, l) != wxs2::Pixel888(0, x, y, l);
            for (int lvl = 0; lvl <= wxsweep::kPeak; ++lvl)
                bad += wxsweep::Lit565(1, wxs2::Percent(l), (uint8_t)lvl) != wxsweep::Lit565(0, wxs2::Percent(l), (uint8_t)lvl);
        }
        check(bad == 0, "AV7", "code 1 drawn as code 0 (layer and lit sweep, both looks): " + std::to_string(bad) + " differ");
    }

    std::printf("%s\n", failures ? "WX AVIONICS TESTS FAILED" : "all WX avionics checks passed");
    return failures ? 1 : 0;
}
