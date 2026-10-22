// S2, the weather layer's colours (include/WxS2.h), against WD4's frozen properties for BOTH bench looks
// (docs/wx-device.md, WD4 and its addenda 1 and 2). The planes' colours are READ FROM src/AircraftManager.cpp, not
// transcribed: altitudeColor()'s five, the live green, and the NoData grey.
//
//   test_wx_s2.exe <repo-root>     exit 0 all pass, 1 a property failed, 2 the rig (the source could not be read)
// Sabotages: test/host/sabotage_wx_s2.py plants one break per property into a copy of WxS2.h.
#include <cstdio>
#include <cstring>
#include <regex>
#include <set>
#include <string>
#include <vector>

#include "WxS2.h"

using wxpalette::Rgb;
using wxs2::Look;

static int failures = 0;
static void check(bool ok, const char* id, const std::string& what)
{
    if (!ok) ++failures;
    std::printf("  %s  %s %s\n", ok ? "ok  " : "FAIL", id, what.c_str());
}

static const char* Name(Look l) { return l == Look::A ? "A (40 %)" : "B (25 %)"; }
static uint32_t P(Rgb c) { return wxs2::Pack(c); }

// The planes' unfaded colours, parsed from the radar's own drawing code.
static bool ReadPlaneColours(const std::string& root, std::vector<uint32_t>& out)
{
    FILE* f = std::fopen((root + "/src/AircraftManager.cpp").c_str(), "rb");
    if (!f) return false;
    std::string s;
    char buf[65536];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) s.append(buf, n);
    std::fclose(f);
    const std::regex rgb(R"(color888\((\d+),\s*(\d+),\s*(\d+)\))");
    auto take = [&](const std::string& body, size_t want) {
        size_t got = 0;
        for (std::sregex_iterator it(body.begin(), body.end(), rgb), end; it != end; ++it, ++got)
            out.push_back(((uint32_t)std::stoul((*it)[1]) << 16) | ((uint32_t)std::stoul((*it)[2]) << 8) | std::stoul((*it)[3]));
        return got == want;
    };
    const size_t a = s.find("uint32_t altitudeColor(float altMeters)");
    if (a == std::string::npos) return false;
    const size_t ae = s.find("\n}", a);   // LF or CRLF: the function's closing brace at column 0
    if (ae == std::string::npos || !take(s.substr(a, ae - a), 5)) return false;         // green, lime, yellow, cyan, white
    const size_t b = s.find("noData ? lgfx::color888(");
    if (b == std::string::npos) return false;
    const size_t be = s.find(";", b);
    return take(s.substr(b, be - b), 2);                                                // the NoData grey, the live green
}

int main(int argc, char** argv)
{
    if (argc < 2) { std::printf("usage: test_wx_s2.exe <repo-root>\n"); return 2; }
    std::vector<uint32_t> planes;
    if (!ReadPlaneColours(argv[1], planes)) {
        std::printf("FAIL: could not read the planes' colours from src/AircraftManager.cpp (rig)\n");
        return 2;
    }
    std::printf("planes' unfaded colours read from source: %u\n", (unsigned)planes.size());

    // Hand-computed anchors, independent of the header: (v * pct + 50) / 100 on the pinned table's entries.
    struct Anchor { Look l; uint8_t code; Rgb want; };
    const Anchor anchors[] = {
        { Look::A, 1, { 0, 0, 98 } },     { Look::A, 2, { 0, 102, 0 } },  { Look::A, 5, { 102, 102, 0 } },
        { Look::A, 13, { 102, 102, 102 } }, { Look::B, 1, { 0, 0, 62 } }, { Look::B, 2, { 0, 64, 0 } },
        { Look::B, 5, { 64, 64, 0 } },    { Look::B, 13, { 64, 64, 64 } },
    };

    for (Look l : { Look::A, Look::B }) {
        std::printf("== look %s ==\n", Name(l));
        const int pct = wxs2::Percent(l);

        // WD4a: code 0 is exactly the background, at every pixel of a 16 x 16 patch.
        bool a = true;
        for (int y = 0; y < 16; ++y)
            for (int x = 0; x < 16; ++x) a = a && wxs2::Pixel888(0, x, y, l) == 0;
        check(a, "WD4a", "code 0 is the background at every pixel");

        // WD4b: codes 1-12 are the pinned table at the look's factor; 13 is the ruled 75+ white at it (R1).
        bool b = true;
        std::string bad;
        for (uint8_t c = 1; c <= 13; ++c) {
            const Rgb want = c <= 12 ? wxpalette::Dim(wxpalette::kRainTable[c - 1], pct) : wxpalette::Dim(wxpalette::kRain75, pct);
            const Rgb got = wxs2::Echo(c, l);
            if (!(got == want) || wxs2::Pixel888(c, 3, 5, l) != P(want) || P(got) == 0) { b = false; bad += " " + std::to_string(c); }
        }
        for (const Anchor& an : anchors)
            if (an.l == l && !(wxs2::Echo(an.code, l) == an.want)) { b = false; bad += " anchor" + std::to_string(an.code); }
        check(b, "WD4b", "codes 1-12 = table x " + std::to_string(pct) + " %, 13 = 75+ white x " + std::to_string(pct) +
                             " %, none the background" + (bad.empty() ? "" : "; wrong:" + bad));

        // WD4c: 14 and 15 are never all background in any 2 x 2 window, at every offset of a 16 x 16 patch, and differ.
        int empty14 = 0, empty15 = 0;
        bool differ = false;
        for (int y = 0; y < 16; ++y)
            for (int x = 0; x < 16; ++x) {
                bool lit14 = false, lit15 = false;
                for (int dy = 0; dy < 2; ++dy)
                    for (int dx = 0; dx < 2; ++dx) {
                        lit14 = lit14 || wxs2::Pixel888(14, x + dx, y + dy, l) != 0;
                        lit15 = lit15 || wxs2::Pixel888(15, x + dx, y + dy, l) != 0;
                    }
                empty14 += !lit14;
                empty15 += !lit15;
                differ = differ || wxs2::Pixel888(14, x, y, l) != wxs2::Pixel888(15, x, y, l);
            }
        check(empty14 == 0 && empty15 == 0 && differ, "WD4c",
              "14 and 15: empty 2x2 windows " + std::to_string(empty14) + " / " + std::to_string(empty15) + " (of 256), and they differ");

        // WD4d: no code's colour equals an UNFADED plane colour, compared in RGB565 as the panel draws them.
        int eq = 0;
        std::string which;
        for (uint8_t c = 1; c <= 15; ++c) {
            const uint32_t mine = c <= 13 ? P(wxs2::Echo(c, l)) : (c == 14 ? P(wxpalette::kNoCoverage) : P(wxpalette::kMissingHatch));
            for (uint32_t p : planes)
                if (wxs2::To565(mine) == wxs2::To565(p)) { ++eq; which += " " + std::to_string(c); }
        }
        check(eq == 0, "WD4d", std::to_string(eq) + " equal to an unfaded plane colour (RGB565)" + which);

        // WD4f, reported not graded: which codes equal a SWEEP-FADED plane (f = 0.30 .. 1.0, float, truncating).
        std::set<int> faded;
        for (uint8_t c = 1; c <= 13; ++c) {
            const uint16_t mine = wxs2::To565(P(wxs2::Echo(c, l)));
            for (uint32_t p : planes)
                for (int i = 3000; i <= 10000 && !faded.count(c); ++i) {
                    const float f = i / 10000.0f;
                    const uint32_t q = ((uint32_t)(uint8_t)(((p >> 16) & 0xFF) * f) << 16) |
                                       ((uint32_t)(uint8_t)(((p >> 8) & 0xFF) * f) << 8) | (uint8_t)((p & 0xFF) * f);
                    if (wxs2::To565(q) == mine) faded.insert(c);
                }
        }
        std::string fl;
        for (int c : faded) fl += " " + std::to_string(c);
        std::printf("  info  WD4f codes equal to some sweep-faded plane:%s\n", fl.empty() ? " none" : fl.c_str());
        std::printf("  info  the plane outline: %s\n", wxs2::Outline(l) ? "on" : "off");
    }

    std::printf("%s\n", failures ? "WX S2 TESTS FAILED" : "all WX S2 checks passed");
    return failures ? 1 : 0;
}
