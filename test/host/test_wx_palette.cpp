// Weatherscope's palette (src/weatherscope/WxPalette.h) against the pinned MetPy table and the ruled properties:
// C3 (a)-(f), docs/weatherscope-stage2.md addendum 2. The table is READ FROM THE COMMITTED FILE here, so the
// header and the pin cannot drift apart unnoticed.
//
//   test_wx_palette.exe <fixtures-dir>        exit 0 all pass, 1 a check failed, 2 the rig (file unreadable)
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "WxPalette.h"

using wxpalette::Rgb;

static int failures = 0;
static void check(bool ok, const char* id, const std::string& what)
{
    if (!ok) ++failures;
    std::printf("  %s  %s %s\n", ok ? "ok  " : "FAIL", id, what.c_str());
}

static std::string S(Rgb c)
{
    char b[32];
    std::snprintf(b, sizeof b, "(%u,%u,%u)", c.r, c.g, c.b);
    return b;
}

static bool Achromatic(Rgb c) { return c.r == c.g && c.g == c.b; }
static int Luma(Rgb c) { return 299 * c.r + 587 * c.g + 114 * c.b; }

int main(int argc, char** argv)
{
    if (argc < 2) { std::printf("usage: test_wx_palette.exe <fixtures-dir>\n"); return 2; }
    const std::string path = std::string(argv[1]) + "/NWSReflectivity-metpy-v1.6.3.tbl";
    FILE* f = std::fopen(path.c_str(), "r");
    if (!f) { std::printf("FAIL: cannot read %s (rig)\n", path.c_str()); return 2; }
    std::vector<Rgb> tbl;   // the file's entries, 5 dBZ steps from 5 dBZ; each value is k/255
    double r, g, b;
    while (std::fscanf(f, " (%lf, %lf, %lf)", &r, &g, &b) == 3)
        tbl.push_back(Rgb{(uint8_t)(r * 255 + 0.5), (uint8_t)(g * 255 + 0.5), (uint8_t)(b * 255 + 0.5)});
    std::fclose(f);
    if (tbl.size() != 15) { std::printf("FAIL: the pinned table has %u entries, not 15 (rig)\n", (unsigned)tbl.size()); return 2; }

    const int dims[2] = {wxpalette::kDim85, wxpalette::kDim70};

    // (a) no coverage and missing are achromatic: "never violet" as a property, not a promise.
    check(Achromatic(wxpalette::kNoCoverage) && Achromatic(wxpalette::kMissingDim) && Achromatic(wxpalette::kMissingHatch),
          "C3a", "no coverage " + S(wxpalette::kNoCoverage) + ", missing dim " + S(wxpalette::kMissingDim) +
                     ", missing hatch " + S(wxpalette::kMissingHatch) + " are achromatic");

    // (b) dry, missing (dim and hatch) and no coverage are four different colours.
    const Rgb four[4] = {wxpalette::kDry, wxpalette::kMissingDim, wxpalette::kMissingHatch, wxpalette::kNoCoverage};
    bool distinct = true;
    for (int i = 0; i < 4; ++i)
        for (int j = i + 1; j < 4; ++j) distinct &= four[i] != four[j];
    check(distinct, "C3b", "dry, missing dim, missing hatch and no coverage are four different colours");

    // (c) rain 2-12 equal the pinned table's 20-70 dBZ entries; code 13 is white.
    bool tableOk = true;
    for (int code = 2; code <= 12; ++code) {
        const Rgb want = tbl[(size_t)code + 1];   // code 1 = 15 dBZ = entry index 2
        if (wxpalette::Rain((uint8_t)code, wxpalette::kDim85) != want) {
            tableOk = false;
            std::printf("        rain %d is %s, the table says %s\n", code, S(wxpalette::Rain((uint8_t)code, 85)).c_str(), S(want).c_str());
        }
    }
    const bool white = wxpalette::Rain(13, wxpalette::kDim85) == Rgb{255, 255, 255};
    check(tableOk && white, "C3c", "rain 2-12 equal the pinned 20-70 dBZ entries; rain 13 is white " + S(wxpalette::Rain(13, 85)));

    // (d) code 1 = the table's 15 dBZ entry x the dim factor, (v * p + 50) / 100.
    bool dimOk = true;
    for (int p : dims) {
        const Rgb t = tbl[2];
        const Rgb want{(uint8_t)((t.r * p + 50) / 100), (uint8_t)((t.g * p + 50) / 100), (uint8_t)((t.b * p + 50) / 100)};
        dimOk &= wxpalette::Rain(1, p) == want;
    }
    check(dimOk, "C3d", "rain 1 = the 15 dBZ entry " + S(tbl[2]) + " at 85 % " + S(wxpalette::Rain(1, 85)) + " and 70 % " +
                            S(wxpalette::Rain(1, 70)));

    // (e) snow luma rises with each code up to code 8, flat after; the top is pale blue, not white.
    bool rises = true;
    for (int c = 2; c <= wxpalette::kSnowTopCode; ++c)
        rises &= Luma(wxpalette::Snow((uint8_t)c, 85)) > Luma(wxpalette::Snow((uint8_t)(c - 1), 85));
    for (int c = wxpalette::kSnowTopCode + 1; c <= 13; ++c) rises &= wxpalette::Snow((uint8_t)c, 85) == wxpalette::Snow(8, 85);
    const Rgb top = wxpalette::Snow(13, 85);
    const bool pale = top.b > top.r && top != Rgb{255, 255, 255};
    check(rises && pale, "C3e", "snow rises to code 8 and is flat after; its top " + S(top) + " is pale blue, not white");

    // (f) no snow colour equals any rain colour, codes 1-13, both dim levels.
    int clashes = 0;
    for (int p : dims)
        for (int s = 1; s <= 13; ++s)
            for (int q : dims)
                for (int rc = 1; rc <= 13; ++rc)
                    if (wxpalette::Snow((uint8_t)s, p) == wxpalette::Rain((uint8_t)rc, q)) ++clashes;
    check(clashes == 0, "C3f", "no snow colour equals any rain colour (" + std::to_string(clashes) + " clashes)");

    // A control the suite needs: the file really is the pinned one (its 75 dBZ entry is black, which is WHY 13 is a ruling).
    check(tbl[14] == Rgb{0, 0, 0} && tbl[2] == Rgb{0, 0, 246}, "pin", "the file's 15 dBZ entry is " + S(tbl[2]) +
                                                                         " and its 75 dBZ entry is " + S(tbl[14]));
    std::printf("%s\n", failures ? "WX PALETTE TESTS FAILED" : "all WX palette checks passed");
    return failures ? 1 : 0;
}
