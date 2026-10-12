// The seven-segment 1 must not read as the letter I (2026-10-12).
//
// Under the Missileer Clock, "1 ON WATCH" read as "I ON WATCH": segments b + c are a
// plain vertical bar, and next to letters a plain bar is an I. The 1 now carries a flag
// (include/SevenSegmentShapes.h, Flag). This draws the SHIPPING DrawDigit at every size a
// call site uses and reads the label font's own glyphs out of LovyanGFX's glcdfont.h
// (argv[1], found by run.sh under .pio/libdeps; missing = BLIND, exit 2).
//
// THE FLAG IS OPT-IN PER CALL SITE (Daniel, 2026-10-12): ON only for the ON WATCH count,
// where the digit sits right beside letters. Big digits never have a flag.
// "Reads as I" is made checkable as: every inked row is either the stem alone or inked on
// BOTH sides of it (an I's serifs are symmetric). A flag inks one side only.
//   - at the ON WATCH sizes (flagged): the 1 is NOT I-like and differs from the font's I;
//     its flag stays in its cell above the middle bar; its stroke is t thick across the
//     slope (geometry, and the pixel run); 0, 2..9 and DASH are unchanged
//   - at the Clock, radar night clock and drill sizes (unflagged): every digit, the 1
//     included, is pixel-identical to the drawing before the flag existed
// CONTROLS: the font's I IS I-like (the classifier knows an I); the old flagless 1 IS
// I-like (the test would have caught the bug); the font's own 1 (00 42 7F 40 00, which
// has a flag) is NOT I-like (the classifier knows a flagged 1).
// SABOTAGE: drop the Flag() calls -> the ON WATCH lines go red; flag every 1 (ignore
// flagOne) -> the unflagged lines go red.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include "../../include/SevenSegmentShapes.h"

static int failures = 0;
static void check(bool ok, const char* what)
{
    std::printf("  %s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++failures;
}

// Ink = bloom or core (2, 3); ghost (1) and background (0) are not ink.
enum { BG = 0, GHOST = 1, BLOOM = 2, LIT = 3 };
struct Grid {
    int w, h;
    std::vector<int> p;
    Grid(int w_, int h_) : w(w_), h(h_), p((size_t)w_ * h_, BG) {}
    void set(int x, int y, int v) { if (x >= 0 && x < w && y >= 0 && y < h) p[(size_t)y * w + x] = v; }
    int at(int x, int y) const { return (x >= 0 && x < w && y >= 0 && y < h) ? p[(size_t)y * w + x] : BG; }
    bool ink(int x, int y) const { return at(x, y) >= BLOOM; }
    void fillRect(int x, int y, int rw, int rh, int v)
    {
        for (int yy = y; yy < y + rh; ++yy) for (int xx = x; xx < x + rw; ++xx) set(xx, yy, v);
    }
    // Every pixel centre inside the closed triangle, row by row (see test_sevenseg_shapes).
    void fillTriangle(int x0, int y0, int x1, int y1, int x2, int y2, int v)
    {
        const int xs[3] = { x0, x1, x2 }, ys[3] = { y0, y1, y2 };
        int ylo = y0, yhi = y0;
        for (int i = 1; i < 3; ++i) { if (ys[i] < ylo) ylo = ys[i]; if (ys[i] > yhi) yhi = ys[i]; }
        for (int y = ylo; y <= yhi; ++y) {
            double lo = 1e9, hi = -1e9;
            for (int e = 0; e < 3; ++e) {
                const int ax = xs[e], ay = ys[e], bx = xs[(e + 1) % 3], by = ys[(e + 1) % 3];
                if (ay == by) {
                    if (ay == y) { lo = std::min(lo, (double)std::min(ax, bx)); hi = std::max(hi, (double)std::max(ax, bx)); }
                    continue;
                }
                if (y < std::min(ay, by) || y > std::max(ay, by)) continue;
                const double x = ax + (double)(y - ay) * (bx - ax) / (by - ay);
                lo = std::min(lo, x); hi = std::max(hi, x);
            }
            for (int x = (int)(lo + 0.999999); x <= (int)(hi + 1e-6); ++x) set(x, y, v);
        }
    }
};

// The digit as it was drawn before the flag: ghost all, then bloom and core of the mask.
static void OldDigit(Grid& g, int x, int y, int w, int h, int digit)
{
    using namespace sevenseg;
    const int t = Thickness(h), midY = y + (h - t) / 2, rightX = x + w - t, halfH = (h - t) / 2 + t, botY = y + h - t;
    auto seg = [&](int i, int col) {
        switch (i) {
            case 0: SegH(g, x, y, w, t, col); break;
            case 1: SegV(g, rightX, y, halfH, t, col); break;
            case 2: SegV(g, rightX, midY, halfH, t, col); break;
            case 3: SegH(g, x, botY, w, t, col); break;
            case 4: SegV(g, x, midY, halfH, t, col); break;
            case 5: SegV(g, x, y, halfH, t, col); break;
            case 6: SegH(g, x, midY, w, t, col); break;
        }
    };
    for (int i = 0; i < 7; ++i) seg(i, GHOST);
    const unsigned m = SegMask(digit);
    for (int i = 0; i < 7; ++i) if (m & (1u << i)) seg(i, BLOOM);
    for (int i = 0; i < 7; ++i) if (m & (1u << i)) seg(i, LIT);
}

// I-like: every inked row is stem-only or inked on both sides of the stem. The stem is the
// run of columns inked on at least 60 % of the glyph's inked rows.
static bool ILike(const Grid& g)
{
    int rows = 0;
    std::vector<int> colInk(g.w, 0);
    for (int y = 0; y < g.h; ++y) {
        bool any = false;
        for (int x = 0; x < g.w; ++x) if (g.ink(x, y)) { any = true; ++colInk[x]; }
        rows += any;
    }
    if (!rows) return false;
    int s0 = -1, s1 = -1;
    for (int x = 0; x < g.w; ++x) if (colInk[x] * 10 >= rows * 6) { if (s0 < 0) s0 = x; s1 = x; }
    if (s0 < 0) return false;
    for (int y = 0; y < g.h; ++y) {
        bool left = false, right = false;
        for (int x = 0; x < s0; ++x) left |= g.ink(x, y);
        for (int x = s1 + 1; x < g.w; ++x) right |= g.ink(x, y);
        if (left != right) return false;   // ink on one side only: a flag, not a serif
    }
    return true;
}

// A GLCD glyph (5 column bytes, bit 0 = top row), drawn at integer scale s.
static Grid Glcd(const std::vector<int>& font, int ch, int s)
{
    Grid g(6 * s + 2, 8 * s + 2);
    for (int col = 0; col < 5; ++col)
        for (int row = 0; row < 8; ++row)
            if (font[(size_t)ch * 5 + col] & (1 << row)) g.fillRect(1 + col * s, 1 + row * s, s, s, LIT);
    return g;
}

static std::vector<int> LoadGlcd(const char* path)
{
    std::ifstream f(path);
    std::stringstream ss; ss << f.rdbuf();
    const std::string src = ss.str();
    std::vector<int> bytes;
    const size_t start = src.find('{');
    for (size_t i = start; i != std::string::npos && i + 3 < src.size(); ++i) {
        if (src[i] == '}') break;
        if (src[i] == '0' && (src[i + 1] == 'x' || src[i + 1] == 'X')) {
            bytes.push_back((int)std::strtol(src.substr(i + 2, 2).c_str(), nullptr, 16));
            i += 3;
        }
    }
    return bytes;
}

static bool Same(const Grid& a, const Grid& b) { return a.w == b.w && a.h == b.h && a.p == b.p; }

int main(int argc, char** argv)
{
    std::printf("seven-segment 1 vs the letter I (SevenSegmentShapes.h + glcdfont.h)\n");
    const std::vector<int> font = LoadGlcd(argc > 1 ? argv[1] : "");
    const bool fontOk = font.size() >= 128 * 5 &&
                        font[65 * 5] == 0x7C && font[65 * 5 + 1] == 0x12 && font[65 * 5 + 2] == 0x11;   // 'A'
    if (!fontOk) {
        std::printf("BLIND: no GLCD font table at '%s' (%u bytes); run `pio run` once\n",
                    argc > 1 ? argv[1] : "", (unsigned)font.size());
        return 2;
    }
    const int I[5] = { 0x00, 0x41, 0x7F, 0x41, 0x00 }, ONE[5] = { 0x00, 0x42, 0x7F, 0x40, 0x00 };
    check(std::equal(I, I + 5, font.begin() + 'I' * 5) && std::equal(ONE, ONE + 5, font.begin() + '1' * 5),
          "CONTROL: the font table read is the GLCD one ('A', 'I' and '1' are the known bytes)");

    // The label is size 2 on the 1.28" and 3 on the 412 px panel (EamScreens.cpp, ON WATCH).
    for (int s = 2; s <= 3; ++s) {
        const Grid fontI = Glcd(font, 'I', s), font1 = Glcd(font, '1', s);
        char what[128];
        std::snprintf(what, sizeof what, "CONTROL: the font's I at size %d is I-like", s); check(ILike(fontI), what);
        std::snprintf(what, sizeof what, "CONTROL: the font's own (flagged) 1 at size %d is not", s); check(!ILike(font1), what);
    }

    // Every size a call site draws (w, h), and whether that call site flags its 1 (the
    // opt-in, Daniel 2026-10-12): ON only for the ON WATCH count, the one place a digit sits
    // right beside letters. The big Clock, the radar night clock and the drill face stay
    // pure seven-segment -- every digit, the 1 included, pixel-identical to before.
    struct Size { int w, h; bool flag; const char* where; };
    const Size sizes[] = {
        { 9, 16, true, "ON WATCH count, 1.28\" (label size 2)" },
        { 14, 24, true, "ON WATCH count, 1.46\" (label size 3)" },
        { 27, 46, false, "Missileer Clock, 1.28\" (57 x 34 scaled to fit 94 %)" },
        { 47, 80, false, "Missileer Clock, 1.46\"" },
        { 36, 60, false, "radar night clock, 1.28\"" },
        { 61, 103, false, "radar night clock, 1.46\"" },
        { 14, 24, false, "drill face, 1.28\"" },
        { 24, 41, false, "drill face, 1.46\"" },
    };
    for (const Size& z : sizes) {
        const int pad = 4;
        char what[180];
        if (!z.flag) {
            // PURE SEVEN-SEGMENT: with the call site's own (default) argument, every digit
            // including the 1 is exactly the drawing before the flag existed.
            int changed = 0;
            for (int d = 0; d <= sevenseg::DASH; ++d) {
                Grid a(z.w + 2 * pad, z.h + 2 * pad), b(z.w + 2 * pad, z.h + 2 * pad);
                sevenseg::DrawDigit(a, pad, pad, z.w, z.h, d, (int)LIT, (int)GHOST, (int)BLOOM);
                OldDigit(b, pad, pad, z.w, z.h, d);
                if (!Same(a, b)) ++changed;
            }
            std::snprintf(what, sizeof what, "%s: unflagged -- 0..9 and DASH (the 1 too) pixel-identical to before", z.where);
            check(changed == 0, what);
            continue;
        }
        Grid now(z.w + 2 * pad, z.h + 2 * pad), old(z.w + 2 * pad, z.h + 2 * pad);
        sevenseg::DrawDigit(now, pad, pad, z.w, z.h, 1, (int)LIT, (int)GHOST, (int)BLOOM, true);
        OldDigit(old, pad, pad, z.w, z.h, 1);
        // The stroke's thickness ACROSS its slope must match the segments' t. From the
        // geometry (tv vertical, so tv * reach / hypot across), and measured on the pixels:
        // the flag's own run in its middle column is tv tall.
        {
            const int t0 = sevenseg::Thickness(z.h);
            sevenseg::FlagGeom f = { 0, 0, 0, 0, 0 };
            const bool have = sevenseg::FlagGeometry(pad, pad, z.w, z.h, &f);
            const double reach = f.x0 - f.x1, drop = f.y1 - f.y0;
            const double across = have ? f.tv * reach / std::sqrt(reach * reach + drop * drop) : 0;
            const int mx = (f.x0 + f.x1) / 2;
            int run = 0;
            for (int y = 0; y < now.h; ++y) run += now.ink(mx, y) && !old.ink(mx, y);
            std::snprintf(what, sizeof what, "%s: flag stroke %.2f px across vs t=%d (vertical run %d px, tv %d)",
                          z.where, across, t0, run, have ? f.tv : -1);
            check(have && std::fabs(across - t0) <= 0.5 && std::abs(run - f.tv) <= 1, what);
        }
        const int t = sevenseg::Thickness(z.h), midY = pad + (z.h - t) / 2;
        bool inCell = true, aboveMid = true;
        for (int y = 0; y < now.h; ++y)
            for (int x = 0; x < now.w; ++x)
                if (now.ink(x, y) && !old.ink(x, y)) {          // the flag's own pixels
                    if (x < pad || x >= pad + z.w || y < pad || y >= pad + z.h) inCell = false;
                    if (y >= midY) aboveMid = false;
                }
        const Grid fontI = Glcd(font, 'I', 2);
        std::snprintf(what, sizeof what, "%s: the 1 is not I-like, and differs from the font's I", z.where);
        check(!ILike(now) && !Same(now, fontI), what);
        std::snprintf(what, sizeof what, "%s: CONTROL: the old flagless 1 IS I-like", z.where);
        check(ILike(old), what);
        std::snprintf(what, sizeof what, "%s: the flag is inside the cell and above the middle bar", z.where);
        check(inCell && aboveMid, what);
        int changed = 0;
        for (int d = 0; d <= sevenseg::DASH; ++d) {
            if (d == 1) continue;
            Grid a(now.w, now.h), b(now.w, now.h);
            sevenseg::DrawDigit(a, pad, pad, z.w, z.h, d, (int)LIT, (int)GHOST, (int)BLOOM, true);
            OldDigit(b, pad, pad, z.w, z.h, d);
            if (!Same(a, b)) ++changed;
        }
        std::snprintf(what, sizeof what, "%s: flagged -- 0, 2..9 and DASH still pixel-identical to before", z.where);
        check(changed == 0, what);
    }
    std::printf(failures ? "FAILED (%d)\n" : "ok\n", failures);
    return failures ? 1 : 0;
}
