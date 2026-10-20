// Weatherscope's palette (src/weatherscope/WxPalette.h) against the pinned MetPy table and the ruled properties:
// C3 (a)-(g), docs/weatherscope-stage2.md addenda 2 and 4. The table is READ FROM THE COMMITTED FILE here, so the
// header and the pin cannot drift apart unnoticed.
//
//   test_wx_palette.exe <fixtures-dir>        exit 0 all pass, 1 a check failed, 2 the rig (file unreadable)
#include <cmath>
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

// C3g's metric, frozen in addendum 4: CIE76 dE*ab. sRGB (IEC 61966-2-1) -> linear -> XYZ -> CIELAB, D65 white.
struct Lab { double L, a, b; };
static double Lin(uint8_t v)
{
    const double c = v / 255.0;
    return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
}
static double F(double t)
{
    const double d = 6.0 / 29.0;
    return t > d * d * d ? std::cbrt(t) : t / (3 * d * d) + 4.0 / 29.0;
}
static Lab ToLab(Rgb c)
{
    const double R = Lin(c.r), G = Lin(c.g), B = Lin(c.b);
    const double X = 0.4124564 * R + 0.3575761 * G + 0.1804375 * B;
    const double Y = 0.2126729 * R + 0.7151522 * G + 0.0721750 * B;
    const double Z = 0.0193339 * R + 0.1191920 * G + 0.9503041 * B;
    const double fx = F(X / 0.95047), fy = F(Y / 1.0), fz = F(Z / 1.08883);
    return Lab{116 * fy - 16, 500 * (fx - fy), 200 * (fy - fz)};
}

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

    const int dims[3] = {wxpalette::kDim55, wxpalette::kDim70, wxpalette::kDim85};

    // (a) no coverage and the missing hatch are achromatic: "never violet" as a property, not a promise.
    check(Achromatic(wxpalette::kNoCoverage) && Achromatic(wxpalette::kMissingHatch),
          "C3a", "no coverage " + S(wxpalette::kNoCoverage) + " and the missing hatch " + S(wxpalette::kMissingHatch) +
                     " are achromatic");

    // (b) dry, the missing hatch and no coverage are three different colours.
    check(wxpalette::kDry != wxpalette::kMissingHatch && wxpalette::kDry != wxpalette::kNoCoverage &&
              wxpalette::kMissingHatch != wxpalette::kNoCoverage,
          "C3b", "dry, the missing hatch and no coverage are three different colours");

    // (c) rain 2-12 equal the pinned table's 20-70 dBZ entries; code 13 is white.
    bool tableOk = true;
    for (int code = 2; code <= 12; ++code) {
        const Rgb want = tbl[(size_t)code + 1];   // code 1 = 15 dBZ = entry index 2
        if (wxpalette::Rain((uint8_t)code, wxpalette::kDim70) != want) {
            tableOk = false;
            std::printf("        rain %d is %s, the table says %s\n", code, S(wxpalette::Rain((uint8_t)code, 70)).c_str(), S(want).c_str());
        }
    }
    const bool white = wxpalette::Rain(13, wxpalette::kDim70) == Rgb{255, 255, 255};
    check(tableOk && white, "C3c", "rain 2-12 equal the pinned 20-70 dBZ entries; rain 13 is white " + S(wxpalette::Rain(13, 70)));

    // (d) rain code 1 = the table's 15 dBZ entry x the dim factor, (v * p + 50) / 100, at 55 / 70 / 85 %.
    bool dimOk = true;
    std::string dimText;
    for (int p : dims) {
        const Rgb t = tbl[2];
        const Rgb want{(uint8_t)((t.r * p + 50) / 100), (uint8_t)((t.g * p + 50) / 100), (uint8_t)((t.b * p + 50) / 100)};
        dimOk &= wxpalette::Rain(1, p) == want;
        dimText += " " + std::to_string(p) + "% " + S(wxpalette::Rain(1, p));
    }
    check(dimOk, "C3d", "rain 1 = the 15 dBZ entry " + S(tbl[2]) + " dimmed:" + dimText);

    // (e) snow luma rises with each code up to code 8, flat after; the top is pale blue, not white.
    bool rises = true;
    for (int c = 2; c <= wxpalette::kSnowTopCode; ++c)
        rises &= Luma(wxpalette::Snow((uint8_t)c)) > Luma(wxpalette::Snow((uint8_t)(c - 1)));
    for (int c = wxpalette::kSnowTopCode + 1; c <= 13; ++c) rises &= wxpalette::Snow((uint8_t)c) == wxpalette::Snow(8);
    const Rgb top = wxpalette::Snow(13);
    const bool pale = top.b > top.r && top != Rgb{255, 255, 255};
    check(rises && pale, "C3e", "snow rises from " + S(wxpalette::Snow(1)) + " to code 8 and is flat after; its top " + S(top) +
                                    " is pale blue, not white");

    // (f) no snow colour equals any rain colour, codes 1-13, every dim level.
    int clashes = 0;
    for (int s = 1; s <= 13; ++s)
        for (int q : dims)
            for (int rc = 1; rc <= 13; ++rc)
                if (wxpalette::Snow((uint8_t)s) == wxpalette::Rain((uint8_t)rc, q)) ++clashes;
    check(clashes == 0, "C3f", "no snow colour equals any rain colour (" + std::to_string(clashes) + " clashes)");

    // (g) snow code 1 against rain code 1 at 55 / 70 / 85 %: dE*ab >= 40 AND dL* >= 25 (addendum 4, frozen first).
    bool clear = true;
    std::string gText;
    const Lab s1 = ToLab(wxpalette::Snow(1));
    for (int p : dims) {
        const Lab r1 = ToLab(wxpalette::Rain(1, p));
        const double dE = std::sqrt((s1.L - r1.L) * (s1.L - r1.L) + (s1.a - r1.a) * (s1.a - r1.a) + (s1.b - r1.b) * (s1.b - r1.b));
        const double dL = std::fabs(s1.L - r1.L);
        clear &= dE >= 40.0 && dL >= 25.0;
        char t[96];
        std::snprintf(t, sizeof t, " %d%%: dE %.1f dL %.1f;", p, dE, dL);
        gText += t;
    }
    check(clear, "C3g", "snow 1 " + S(wxpalette::Snow(1)) + " vs rain 1 (dE*ab >= 40 and dL* >= 25):" + gText);

    // A control the suite needs: the file really is the pinned one (its 75 dBZ entry is black, which is WHY 13 is a ruling).
    check(tbl[14] == Rgb{0, 0, 0} && tbl[2] == Rgb{0, 0, 246}, "pin", "the file's 15 dBZ entry is " + S(tbl[2]) +
                                                                         " and its 75 dBZ entry is " + S(tbl[14]));
    // And one for (g)'s arithmetic: sRGB white is L* 100, a* 0, b* 0 to within rounding.
    const Lab w = ToLab(Rgb{255, 255, 255});
    check(std::fabs(w.L - 100) < 0.01 && std::fabs(w.a) < 0.01 && std::fabs(w.b) < 0.01, "lab",
          "sRGB white converts to L* 100, a* 0, b* 0 (the dE arithmetic is wired right)");
    std::printf("%s\n", failures ? "WX PALETTE TESTS FAILED" : "all WX palette checks passed");
    return failures ? 1 : 0;
}
