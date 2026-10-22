// Weatherscope's legend layout (src/weatherscope/WxLegend.h) against the round-screen rules frozen in
// docs/weatherscope-stage2.md, addendum 8 (Daniel's look):
//   G1  every label box (kCharW x chars by kCharH) and every swatch lies within kInnerRadius of (120, 120), all corners
//   G2  every row's first swatch starts at the same x, and all 21 swatches -- "No data" included -- are the same size
//   G3  the block (labels and swatches) is centred vertically on y = 120, within +/-1 px
//   G4  every label box ends left of the swatch column
// The colours are not here: the board's pixels are checked against the palette by legend_check.py (L1, L5, L6).
//   test_wx_legend.exe        exit 0 all pass, 1 a check failed
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

#include "WxLegend.h"

static int failures = 0;
static void check(bool ok, const char* id, const std::string& what)
{
    if (!ok) ++failures;
    std::printf("  %s  %s %s\n", ok ? "ok  " : "FAIL", id, what.c_str());
}

struct Box { int x, y, w, h; };
static double FarCorner(const Box& b)
{
    double far = 0;
    const int xs[2] = {b.x, b.x + b.w}, ys[2] = {b.y, b.y + b.h};
    for (int x : xs)
        for (int y : ys) far = std::fmax(far, std::hypot(x - 120.0, y - 120.0));
    return far;
}

int main()
{
    using namespace wxlegend;
    // G1: the inner radius
    double worst = 0;
    std::string worstWhat;
    for (const auto& r : kRows) {
        const Box b{kLabelX, r.y, kCharW * (int)std::strlen(r.label), kCharH};
        if (FarCorner(b) > worst) { worst = FarCorner(b); worstWhat = std::string("label '") + r.label + "'"; }
    }
    for (const auto& s : kSwatches) {
        const Box b{s.x, s.y, s.w, s.h};
        if (FarCorner(b) > worst) { worst = FarCorner(b); worstWhat = std::string("swatch ") + KindName(s.kind) + " " + std::to_string(s.code); }
    }
    char t[160];
    std::snprintf(t, sizeof t, "every label box and swatch within %d px of the centre (farthest corner %.1f px: %s)", kInnerRadius,
                  worst, worstWhat.c_str());
    check(worst <= kInnerRadius, "G1", t);

    // G2: one left edge for the swatch column; one swatch size
    bool aligned = true, sized = true;
    for (int row = 0; row < 7; ++row) {
        int first = 10000;
        for (const auto& s : kSwatches)
            if (s.row == row && s.x < first) first = s.x;
        aligned &= first == kSwatchX0;
    }
    for (const auto& s : kSwatches) sized &= s.w == kSize && s.h == kSize;
    check(aligned && sized, "G2", "every row's swatches start at x = " + std::to_string(kSwatchX0) + ", and all " +
                                      std::to_string(kSwatchCount) + " swatches are " + std::to_string(kSize) + " x " +
                                      std::to_string(kSize) + " (No data included)");

    // G3: vertical centring
    int top = 10000, bottom = -10000;
    for (const auto& r : kRows) { top = std::min(top, (int)r.y); bottom = std::max(bottom, r.y + kCharH); }
    for (const auto& s : kSwatches) { top = std::min(top, (int)s.y); bottom = std::max(bottom, s.y + s.h); }
    std::snprintf(t, sizeof t, "the block spans y %d..%d, centre %.1f (want 120 +/- 1)", top, bottom, (top + bottom) / 2.0);
    check(std::fabs((top + bottom) / 2.0 - 120.0) <= 1.0, "G3", t);

    // G4: labels clear of the swatch column
    int rightmost = 0;
    for (const auto& r : kRows) rightmost = std::max(rightmost, kLabelX + kCharW * (int)std::strlen(r.label));
    std::snprintf(t, sizeof t, "every label ends left of the swatch column (rightmost label edge %d < %d)", rightmost, kSwatchX0);
    check(rightmost < kSwatchX0, "G4", t);

    std::printf("%s\n", failures ? "WX LEGEND TESTS FAILED" : "all WX legend checks passed");
    return failures ? 1 : 0;
}
