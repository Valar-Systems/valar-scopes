// Weatherscope's theme table (src/weatherscope/WxTheme.h; docs/weatherscope-stage3.md A11.6, A12.43, A12.44, A12.46),
// held to 72 named checks and nothing else:
//   C4      the light palette against light (b)'s rule, with A = CIEDE2000 of (0, 0, 135) on black; snow's mirrored C3
//           rows; the text rule (WCAG 2, its own constants) in RGB888 and in RGB565 as the panel draws it; the hold ring;
//           each metric behind its own control (Sharma, Wu and Dalal's 34 pairs; WCAG's published grey);
//   TS TP TD the setting's Select, its parse, and the night dim.
// C3, the dark palette, stays test_wx_palette.cpp's. Its sabotages: python -I test/host/sabotage_wx_theme.py.
//
//     test_wx_theme <test/fixtures/wx>
//
// Exit 0 all pass, 1 any fail, 2 the rig (a fixture missing, or a check count other than 72). Every value it grades
// is printed, because several margins are thin (orange text on paper 4.503 in RGB888; snow 1 over A by 0.13).

// WxTheme.h includes DayNight.h for Phase, and so SolarDim.h, which uses M_PI: the Arduino core provides it on the
// board, MinGW's math.h hides it under -std=c++11 unless this is defined before the first include (as in
// test_daynight.cpp). The shared header stays untouched.
#define _USE_MATH_DEFINES
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "DayNight.h"
#include "WxPalette.h"
#include "WxTheme.h"

namespace {

using wxtheme::Rgb;
using wxtheme::kLight;
using wxtheme::kDark;

constexpr int kChecks = 72;
int checks = 0, failures = 0;

void check(const std::string& name, bool ok, const std::string& what)
{
    ++checks;
    if (!ok) ++failures;
    std::printf("  %s %s %s\n", ok ? "ok  " : "FAIL", name.c_str(), what.c_str());
}

std::string S(Rgb c)
{
    char t[32];
    std::snprintf(t, sizeof t, "(%d, %d, %d)", c.r, c.g, c.b);
    return t;
}
std::string D(double v, int places)
{
    char t[64];
    std::snprintf(t, sizeof t, "%.*f", places, v);
    return t;
}

bool Same(Rgb a, Rgb b) { return a.r == b.r && a.g == b.g && a.b == b.b; }
bool Achromatic(Rgb c) { return c.r == c.g && c.g == c.b; }
int Luma(Rgb c) { return 299 * c.r + 587 * c.g + 114 * c.b; }   // C3e's, BT.601 in integers

// ---------------------------------------------------------------------------------------------- the metrics
// sRGB (IEC 61966-2-1) -> linear -> XYZ -> CIELAB, D65 white: C3's conversion (test_wx_palette.cpp).
struct Lab { double L, a, b; };
double Lin(uint8_t v)
{
    const double c = v / 255.0;
    return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
}
double F(double t)
{
    const double d = 6.0 / 29.0;
    return t > d * d * d ? std::cbrt(t) : t / (3 * d * d) + 4.0 / 29.0;
}
Lab ToLab(Rgb c)
{
    const double R = Lin(c.r), G = Lin(c.g), B = Lin(c.b);
    const double X = 0.4124564 * R + 0.3575761 * G + 0.1804375 * B;
    const double Y = 0.2126729 * R + 0.7151522 * G + 0.0721750 * B;
    const double Z = 0.0193339 * R + 0.1191920 * G + 0.9503041 * B;
    const double fx = F(X / 0.95047), fy = F(Y / 1.0), fz = F(Z / 1.08883);
    return Lab{116 * fy - 16, 500 * (fx - fy), 200 * (fy - fz)};
}
// Degrees and radians as one multiplication by a precomputed constant, as Python's math.degrees / math.radians do (the
// planning instrument's form, which reproduced all 34 of Sharma's pairs).
constexpr double kDegPerRad = 180.0 / 3.14159265358979323846;
constexpr double kRadPerDeg = 3.14159265358979323846 / 180.0;
double Deg(double r) { return r * kDegPerRad; }
double Rad(double d) { return d * kRadPerDeg; }
double Hue(Rgb c)
{
    const Lab l = ToLab(c);
    const double h = Deg(std::atan2(l.b, l.a));
    return h < 0 ? h + 360.0 : h;
}
double HueDiff(Rgb a, Rgb b)
{
    double d = std::fmod(Hue(a) - Hue(b) + 540.0, 360.0) - 180.0;
    return std::fabs(d);
}
double De76(Rgb x, Rgb y)
{
    const Lab p = ToLab(x), q = ToLab(y);
    return std::sqrt((p.L - q.L) * (p.L - q.L) + (p.a - q.a) * (p.a - q.a) + (p.b - q.b) * (p.b - q.b));
}

// A value rounded to a true 64-bit double. The rig's MinGW is 32-bit and uses the x87 FPU, whose atan2 returns 80 bits:
// at Sharma's pair 14 the two hues differ by exactly 180 deg in exact arithmetic, and the unrounded atan2 put h1 one ulp
// low (90.034511938077543 against 90.034511938077557), so |h1 - h2| read 180.00000000000003 and took the other
// mean-hue branch (4.746 against 4.8045; measured 2026-10-29). SSE (g++ 13.3 in CI) rounds every step to a double
// anyway; rounding atan2's result and the values each branch reads makes both compilers decide the same way.
double Rd(double x)
{
    volatile double v = x;
    return v;
}

// CIEDE2000 (CIE 142-2001), as Sharma, Wu and Dalal (2005) give it, from Lab.
double De2000(Lab p, Lab q)
{
    const double C1 = std::sqrt(p.a * p.a + p.b * p.b), C2 = std::sqrt(q.a * q.a + q.b * q.b);
    const double Cb = (C1 + C2) / 2, Cb7 = std::pow(Cb, 7);
    const double G = 0.5 * (1 - std::sqrt(Cb7 / (Cb7 + std::pow(25.0, 7))));
    const double a1 = (1 + G) * p.a, a2 = (1 + G) * q.a;
    const double C1p = std::sqrt(a1 * a1 + p.b * p.b), C2p = std::sqrt(a2 * a2 + q.b * q.b);
    auto hp = [](double b, double a) {
        if (a == 0 && b == 0) return 0.0;
        const double t = Rd(std::atan2(b, a));   // the x87 atan2 returns 80 bits: round before the degree product
        const double h = Rd(Deg(t));
        return Rd(h < 0 ? h + 360.0 : h);
    };
    const double h1 = hp(p.b, a1), h2 = hp(q.b, a2);
    const double dL = q.L - p.L, dC = C2p - C1p;
    double dh = 0;
    if (C1p * C2p != 0) {
        dh = Rd(h2 - h1);
        if (dh > 180) dh -= 360;
        else if (dh < -180) dh += 360;
    }
    const double dH = 2 * std::sqrt(C1p * C2p) * std::sin(Rad(dh / 2));
    const double Lb = (p.L + q.L) / 2, Cbp = (C1p + C2p) / 2;
    const double hdiff = Rd(std::fabs(h1 - h2)), hsum = Rd(h1 + h2);
    double hb;
    if (C1p * C2p == 0) hb = hsum;
    else if (hdiff <= 180) hb = hsum / 2;
    else if (hsum < 360) hb = (hsum + 360) / 2;
    else hb = (hsum - 360) / 2;
    const double T = 1 - 0.17 * std::cos(Rad(hb - 30)) + 0.24 * std::cos(Rad(2 * hb)) + 0.32 * std::cos(Rad(3 * hb + 6)) -
                     0.20 * std::cos(Rad(4 * hb - 63));
    const double dth = 30 * std::exp(-((hb - 275) / 25) * ((hb - 275) / 25));
    const double Cbp7 = std::pow(Cbp, 7);
    const double Rc = 2 * std::sqrt(Cbp7 / (Cbp7 + std::pow(25.0, 7)));
    const double Sl = 1 + 0.015 * (Lb - 50) * (Lb - 50) / std::sqrt(20 + (Lb - 50) * (Lb - 50));
    const double Sc = 1 + 0.045 * Cbp, Sh = 1 + 0.015 * Cbp * T;
    const double Rt = -std::sin(Rad(2 * dth)) * Rc;
    return std::sqrt((dL / Sl) * (dL / Sl) + (dC / Sc) * (dC / Sc) + (dH / Sh) * (dH / Sh) + Rt * (dC / Sc) * (dH / Sh));
}
double De(Rgb x, Rgb y) { return De2000(ToLab(x), ToLab(y)); }

// WCAG 2's relative luminance, with WCAG's own constants (A12.44), and its contrast ratio.
double RelLum(Rgb c) { return 0.2126 * Lin(c.r) + 0.7152 * Lin(c.g) + 0.0722 * Lin(c.b); }
double Wcag(Rgb a, Rgb b)
{
    const double x = RelLum(a), y = RelLum(b);
    return ((x > y ? x : y) + 0.05) / ((x > y ? y : x) + 0.05);
}
// What the panel shows (A12.44): LovyanGFX truncates to 5-6-5 bits, then replicates the bits back to 8.
Rgb P565(Rgb c)
{
    const int r = c.r >> 3, g = c.g >> 2, b = c.b >> 3;
    return Rgb{(uint8_t)((r << 3) | (r >> 2)), (uint8_t)((g << 2) | (g >> 4)), (uint8_t)((b << 3) | (b >> 2))};
}

const char* PhaseName(daynight::Phase p)
{
    return p == daynight::Phase::Day ? "Day" : p == daynight::Phase::Night ? "Night" : "Unknown";
}

}  // namespace

int main(int argc, char** argv)
{
    if (argc < 2) { std::printf("usage: test_wx_theme <test/fixtures/wx>\n"); return 2; }
    const std::string dir = argv[1];

    // ------------------------------------------------------------------------------------- the controls
    std::printf("== C4's controls: each metric against a published answer ==\n");
    {
        FILE* f = std::fopen((dir + "/ciede2000-sharma.txt").c_str(), "r");
        if (!f) { std::printf("FAIL: cannot read ciede2000-sharma.txt (the RIG)\n"); return 2; }
        char line[256];
        int n = 0, off = 0;
        double worst = 0;
        std::string which;   // the pairs off, by their 1-based place in the file
        while (std::fgets(line, sizeof line, f)) {
            if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') continue;
            Lab p{0, 0, 0}, q{0, 0, 0};
            double want = 0;
            if (std::sscanf(line, "%lf %lf %lf %lf %lf %lf %lf", &p.L, &p.a, &p.b, &q.L, &q.a, &q.b, &want) != 7) {
                std::fclose(f);
                std::printf("FAIL: a malformed Sharma row (the RIG)\n");
                return 2;
            }
            const double d = std::fabs(De2000(p, q) - want);
            if (d > worst) worst = d;
            ++n;
            if (d >= 1e-4) {
                ++off;
                which += (which.empty() ? " (" : ", ") + std::to_string(n);
            }
        }
        std::fclose(f);
        if (n != 34) { std::printf("FAIL: %d Sharma pairs, not 34 (the RIG)\n", n); return 2; }
        if (!which.empty()) which += ")";
        check("C4-CTL1", off == 0, "34 Sharma pairs, " + std::to_string(off) + " off by >= 1e-4" + which + "; worst " + D(worst, 6));
        const double g = Wcag(Rgb{119, 119, 119}, Rgb{255, 255, 255}), bw = Wcag(Rgb{0, 0, 0}, Rgb{255, 255, 255});
        check("C4-CTL2", std::fabs(g - 4.4781) <= 0.0005 && std::fabs(bw - 21.0) <= 0.0005,
              "WCAG (119, 119, 119) on white " + D(g, 4) + " (4.4781); black on white " + D(bw, 4) + " (21.0000)");
    }
    const double A = De(Rgb{0, 0, 135}, Rgb{0, 0, 0});
    check("C4-A", std::fabs(A - 30.2669) <= 5e-5, "the anchor, (0, 0, 135) on black: " + D(A, 4) + " (30.2669)");

    const Rgb paper = kLight.bg, water = kLight.base[38], black{0, 0, 0};
    auto worstLevel = [&](Rgb c) {
        double w = 1e9;
        for (int i = 1; i <= 37; ++i) w = std::fmin(w, De(c, kLight.base[i]));
        return w;
    };
    auto rule = [&](Rgb c, std::string* why) {
        const double p = De(c, paper), wa = De(c, water), t = worstLevel(c);
        *why = S(c) + " paper " + D(p, 2) + " / levels " + D(t, 2) + " / water " + D(wa, 2);
        return p > A && wa > A && t >= 25.0;
    };

    // ------------------------------------------------------------------------------------- the rain
    std::printf("== C4a, C4b, C4f: the light rain (> A on paper and water, >= 25 on all 37 levels) ==\n");
    for (int k = 1; k <= 13; ++k) {
        std::string why;
        const bool ok = rule(kLight.rain[k - 1], &why);
        check("C4a:" + std::to_string(k), ok, why);
    }
    check("C4b", Same(kLight.rain[12], black), "code 13 is black: " + S(kLight.rain[12]));
    {
        const double d5 = HueDiff(kLight.rain[4], Rgb{wxpalette::kRainTable[4].r, wxpalette::kRainTable[4].g, wxpalette::kRainTable[4].b});
        const double d6 = HueDiff(kLight.rain[5], Rgb{wxpalette::kRainTable[5].r, wxpalette::kRainTable[5].g, wxpalette::kRainTable[5].b});
        check("C4f", d5 <= 1.0 && d6 <= 1.0, "codes 5 and 6 from their table hue: " + D(d5, 2) + " and " + D(d6, 2) + " deg (<= 1)");
    }

    // ------------------------------------------------------------------------------------- no coverage and the hatch
    std::printf("== C4c, C4d: no coverage and the hatch ==\n");
    {
        const Rgb n = kLight.noCoverage;
        const double p = De(n, paper), wa = De(n, water), b = De(n, black), t = worstLevel(n);
        check("C4c", Achromatic(n) && p > A && wa > A && b >= 25.0 && t >= 25.0,
              S(n) + ": paper " + D(p, 2) + ", water " + D(wa, 2) + " (> A); black " + D(b, 2) + ", levels " + D(t, 2) + " (>= 25)");
        const Rgb h = kLight.hatchLine, g = kLight.hatchGround;
        const double hp = De(h, g);
        const bool three = !Same(g, h) && !Same(g, n) && !Same(h, n);
        check("C4d", Achromatic(h) && hp > A && three,
              S(h) + " on its ground " + S(g) + ": " + D(hp, 3) + " (> A); ground, line and no coverage three colours: " +
                  (three ? "yes" : "no") + ". Printed: line to no coverage " + D(De(h, n), 2) + "; teal to no coverage " +
                  D(De(kLight.rain[0], n), 2) + ", to the line " + D(De(kLight.rain[0], h), 2));
    }

    // ------------------------------------------------------------------------------------- snow
    std::printf("== C4e and C3e'-C3i': light snow (S-M) ==\n");
    for (int i = 0; i < 6; ++i) {
        std::string why;
        const bool r = rule(kLight.snow[i], &why);
        const double dh = HueDiff(kLight.snow[i], kDark.snow[i]);
        check("C4e:" + std::to_string(i + 1), r && dh <= 1.0, why + "; |dh| " + D(dh, 2) + " deg (<= 1)");
    }
    {
        const Rgb* s = kLight.snow;
        bool falls = true, flat = true;
        for (int i = 1; i < 6; ++i) falls = falls && Luma(s[i]) < Luma(s[i - 1]);
        for (int i = 6; i < 13; ++i) flat = flat && Same(s[i], s[5]);
        const bool blue = s[12].b > s[12].r && !Same(s[12], black);
        check("C3e'", falls && flat && blue, std::string("luma falls 1 -> 6: ") + (falls ? "yes" : "no") + "; flat after: " +
                                                 (flat ? "yes" : "no") + "; the top " + S(s[12]) + " blue, not black: " + (blue ? "yes" : "no"));
        int clashes = 0;
        for (int i = 0; i < 13; ++i)
            for (int k = 0; k < 13; ++k) clashes += Same(s[i], kLight.rain[k]) ? 1 : 0;
        check("C3f'", clashes == 0, std::to_string(clashes) + " snow colours equal a light rain colour");
        const double g = De76(s[0], kLight.rain[0]);
        check("C3g'", g >= 40.0, "snow 1 to rain 1 (teal): dE*ab " + D(g, 1) + " (>= 40)");
        bool steps = true;
        std::string st;
        for (int i = 0; i < 5; ++i) {
            const double d = De76(s[i], s[i + 1]);
            steps = steps && d >= 10.0;
            st += " " + std::to_string(i + 1) + "-" + std::to_string(i + 2) + ": " + D(d, 1) + ";";
        }
        check("C3h'", steps, "adjacent snow 1-6, dE*ab >= 10:" + st);
        const double i13 = De76(s[12], kLight.rain[12]);
        check("C3i'", i13 >= 20.0, "snow 13 to rain 13 (black): dE*ab " + D(i13, 1) + " (>= 20); printed, dE00 " +
                                       D(De(s[12], kLight.rain[12]), 1));
        std::printf("  (printed) snow 1 to water, dE00: RGB888 %.3f, RGB565 %.3f\n", De(s[0], water), De(P565(s[0]), P565(water)));
    }

    // ------------------------------------------------------------------------------------- text, the orange, the hold ring
    std::printf("== C4g: the text rule (>= 4.5:1), in RGB888 and in RGB565 as the panel draws it ==\n");
    {
        struct T { const char* name; Rgb ink, ground; double frozen565; };
        const T rows[9] = {
            {"label/halo", kLight.label, kLight.halo, 13.1259},       {"label/paper", kLight.white, kLight.bg, 12.7167},
            {"dim/paper", kLight.dim, kLight.bg, 5.3309},             {"ringnum/halo", kLight.ringText, kLight.halo, 5.5024},
            {"peak/halo", kLight.peak, kLight.halo, 7.5987},          {"otext/paper", kLight.orangeText, kLight.bg, 4.6204},
            {"otext/halo", kLight.orangeText, kLight.halo, 4.7690},   {"accent/paper", kLight.uiAccent, kLight.bg, 4.5269},
            {"warn/paper", kLight.uiWarn, kLight.bg, 4.6607},
        };
        for (const T& t : rows) {
            const double v = Wcag(t.ink, t.ground);
            check(std::string("C4g888:") + t.name, v >= 4.5, S(t.ink) + " on " + S(t.ground) + ": " + D(v, 4));
        }
        for (const T& t : rows) {
            const Rgb i = P565(t.ink), g = P565(t.ground);
            const double v = Wcag(i, g);
            check(std::string("C4g565:") + t.name, v >= 4.5 && std::fabs(v - t.frozen565) <= 0.0005,
                  S(i) + " on " + S(g) + ": " + D(v, 4) + " (>= 4.5; frozen " + D(t.frozen565, 4) + " +- 0.0005)");
        }
    }
    {
        std::string why;
        const bool ok = rule(kLight.orangeGraphic, &why);
        check("C4h", ok, "the orange graphic " + why);
    }
    std::printf("== C4-HOLD: the light hold ring (A12.44) ==\n");
    {
        const double arc = Wcag(kLight.holdArc, paper);
        check("C4-HOLD1", arc >= 3.0, "the arc " + S(kLight.holdArc) + " on paper: " + D(arc, 3) + " (>= 3)");
        const double lt = RelLum(kLight.holdTrack), la = RelLum(kLight.holdArc);
        check("C4-HOLD2", lt > la, "the track " + S(kLight.holdTrack) + " lighter than the arc: " + D(lt, 4) + " > " + D(la, 4));
    }

    // ------------------------------------------------------------------------------------- the setting
    std::printf("== TS: Select(setting, phase) ==\n");
    {
        using daynight::Phase;
        using wxtheme::Setting;
        using wxtheme::Which;
        struct R { Setting s; Phase p; Which want; };
        const R rows[9] = {
            {Setting::Auto, Phase::Unknown, Which::Dark},  {Setting::Auto, Phase::Day, Which::Light},
            {Setting::Auto, Phase::Night, Which::Dark},    {Setting::Light, Phase::Unknown, Which::Light},
            {Setting::Light, Phase::Day, Which::Light},    {Setting::Light, Phase::Night, Which::Light},
            {Setting::Dark, Phase::Unknown, Which::Dark},  {Setting::Dark, Phase::Day, Which::Dark},
            {Setting::Dark, Phase::Night, Which::Dark},
        };
        const char* sn[3] = {"auto", "light", "dark"};
        for (int i = 0; i < 9; ++i) {
            const Which got = wxtheme::Select(rows[i].s, rows[i].p);
            check("TS" + std::to_string(i + 1), got == rows[i].want,
                  std::string(sn[(int)rows[i].s]) + " + " + PhaseName(rows[i].p) + " -> " + (got == Which::Light ? "Light" : "Dark"));
        }
    }
    std::printf("== TP: the stored value's parse ==\n");
    {
        using wxtheme::Read;
        using wxtheme::Setting;
        struct R { const char* in; Setting s; Read r; };
        const R rows[7] = {{"auto", Setting::Auto, Read::Known},   {"light", Setting::Light, Read::Known},
                           {"dark", Setting::Dark, Read::Known},   {"", Setting::Auto, Read::Unset},
                           {"x", Setting::Auto, Read::Unknown},    {"Light", Setting::Auto, Read::Unknown},
                           {"light ", Setting::Auto, Read::Unknown}};
        const char* sn[3] = {"auto", "light", "dark"};
        const char* rn[3] = {"known", "unset", "unknown"};
        for (int i = 0; i < 7; ++i) {
            const wxtheme::Parsed got = wxtheme::ParseSetting(rows[i].in);
            check("TP" + std::to_string(i + 1), got.setting == rows[i].s && got.read == rows[i].r,
                  std::string("\"") + rows[i].in + "\" -> " + sn[(int)got.setting] + ", " + rn[(int)got.read]);
        }
    }
    std::printf("== TD: the night dim follows the phase (Q38) ==\n");
    {
        using daynight::Phase;
        struct R { bool autoDim; Phase p; bool want; };
        const R rows[4] = {{true, Phase::Night, true}, {true, Phase::Day, false}, {true, Phase::Unknown, false},
                           {false, Phase::Night, false}};
        for (int i = 0; i < 4; ++i) {
            const bool got = wxtheme::NightDim(rows[i].autoDim, rows[i].p);
            check("TD" + std::to_string(i + 1), got == rows[i].want,
                  std::string("auto-dim ") + (rows[i].autoDim ? "on" : "off") + ", " + PhaseName(rows[i].p) + " -> " +
                      (got ? "dim" : "no dim"));
        }
    }

    std::printf("%d checks, %d failures\n", checks, failures);
    if (checks != kChecks) {
        std::printf("FAIL: %d checks ran, not %d (the RIG)\n", checks, kChecks);
        return 2;
    }
    return failures ? 1 : 0;
}
