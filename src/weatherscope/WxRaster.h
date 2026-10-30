// Weatherscope 3.5a's drawing primitives (docs/weatherscope-stage3.md A9.2, A9.3, A9.10): the ONLY code that puts
// pixels on a map screen. Pure C++11 behind a pixel sink, so the host test draws the same frames into a buffer that
// the board draws into its sprite -- and both are held to tools/weatherscope/screens/ref35.py at 57,600 pixels.
//
// Pinned (A9.4): every pixel rounding is floor(v + 0.5) (never lround/round: CLAUDE.md's cross-language trap);
// truncations are the generator's int(), i.e. (int) on positive values. The font is the vendored Adafruit-GFX
// glcdfont.c (test/fixtures/wx/glcdfont.c, sha256 pinned in ref35.py's FT1), included from that one file -- it is
// byte-identical to LovyanGFX's default font (FT3), but no LovyanGFX text or circle routine draws a map screen.

#pragma once

#include <math.h>
#include <stdint.h>
#include <string.h>

namespace wxraster {

namespace glcd {
#include "../../test/fixtures/wx/glcdfont.c"   // defines `font[]`: 256 glyphs x 5 column bytes, bit 0 = top row
}  // namespace glcd

constexpr int kSize = 240;
constexpr double kC = 120.0;
constexpr int kTopBand = 24;   // y < 24 is the top strip
constexpr int kBotBand = 194;  // y >= 194 is the bottom band
constexpr double kPi = 3.14159265358979323846;

struct Rgb {
    uint8_t r, g, b;
};
inline bool operator==(Rgb a, Rgb b) { return a.r == b.r && a.g == b.g && a.b == b.b; }

// No colour is named here (3.5t, A12.47): every colour a screen draws is a role in WxTheme.h.

// Where pixels go: a 240 x 240 buffer on the host, the backbuffer sprite on the board.
class Sink {
public:
    virtual ~Sink() {}
    virtual void Put(int x, int y, Rgb c) = 0;   // called only with 0 <= x, y < 240
};

inline int Rnd(double v) { return (int)floor(v + 0.5); }

inline void Put(Sink& s, int x, int y, Rgb c)
{
    if (x >= 0 && x < kSize && y >= 0 && y < kSize) s.Put(x, y, c);
}

// The map area: between the strip and the band, inside the disc (integer pixel coordinates, as the generator).
inline bool InMap(int x, int y)
{
    return x >= 0 && x < kSize && y >= kTopBand && y < kBotBand && (x - kC) * (x - kC) + (y - kC) * (y - kC) <= kC * kC;
}

// Inclusive rectangle, clipped to the screen (PIL's rectangle, as the generator draws its dots and bands).
inline void Fill(Sink& s, int x0, int y0, int x1, int y1, Rgb c)
{
    for (int y = y0 < 0 ? 0 : y0; y <= (y1 > kSize - 1 ? kSize - 1 : y1); ++y)
        for (int x = x0 < 0 ? 0 : x0; x <= (x1 > kSize - 1 ? kSize - 1 : x1); ++x) s.Put(x, y, c);
}

// A string's width in the panel font: 6 px per character per size step, less the trailing column.
inline int TextW(const char* str, int size) { return (int)strlen(str) * 6 * size - size; }

// Text at (x, y), top-left, the 5 x 7 glyph in a 6 x 8 cell. With a halo: every lit cell's size x size block,
// dilated by 1 px, is painted `halo` first, then the glyph -- the generator's exact order.
inline void Text(Sink& s, int x, int y, const char* str, int size, Rgb fill, const Rgb* halo)
{
    const int n = (int)strlen(str);
    for (int pass = halo ? 0 : 1; pass < 2; ++pass) {
        for (int i = 0; i < n; ++i) {
            const unsigned char ch = (unsigned char)str[i];
            for (int col = 0; col < 5; ++col) {
                const uint8_t bits = glcd::font[ch * 5 + col];
                for (int row = 0; row < 7; ++row) {
                    if (!(bits & (1 << row))) continue;
                    const int cx = x + (i * 6 + col) * size, cy = y + row * size;
                    for (int sx = 0; sx < size; ++sx)
                        for (int sy = 0; sy < size; ++sy) {
                            if (pass == 0) {
                                for (int dx = -1; dx <= 1; ++dx)
                                    for (int dy = -1; dy <= 1; ++dy) Put(s, cx + sx + dx, cy + sy + dy, *halo);
                            } else {
                                Put(s, cx + sx, cy + sy, fill);
                            }
                        }
                }
            }
        }
    }
}

// A dotted ring of radius r px about the centre: n = int(2 pi r) steps, a dot at every even step, map area only.
inline int RingDots(Sink& s, double r, Rgb c)
{
    const int n = (int)(2 * kPi * r);
    int drawn = 0;
    for (int k = 0; k < n; k += 2) {
        const double a = 2 * kPi * k / n;
        const int x = Rnd(kC + r * sin(a)), y = Rnd(kC - r * cos(a));
        if (InMap(x, y)) {
            s.Put(x, y, c);
            ++drawn;
        }
    }
    return drawn;
}

// A DDA line between float end points: n = int(max(|dx|, |dy|)) + 1, points k = 0..n, each rounded, map area only.
inline void Line(Sink& s, double x0, double y0, double x1, double y1, Rgb c)
{
    const double dx = x1 - x0, dy = y1 - y0;
    const int n = (int)(fabs(dx) > fabs(dy) ? fabs(dx) : fabs(dy)) + 1;
    for (int k = 0; k <= n; ++k) {
        const int x = Rnd(x0 + dx * k / n), y = Rnd(y0 + dy * k / n);
        if (InMap(x, y)) s.Put(x, y, c);
    }
}

// The home marker: an 8 x 8 ring (x and y 116-123) and a 2 x 2 centre, frozen as a bitmap (A9.2).
inline void HomeMarker(Sink& s, Rgb c)
{
    static const uint8_t kRows[8] = {0x3C, 0x42, 0x81, 0x99, 0x99, 0x81, 0x42, 0x3C};   // bit 7 = x 116
    for (int r = 0; r < 8; ++r)
        for (int b = 0; b < 8; ++b)
            if (kRows[r] & (0x80 >> b)) s.Put(116 + b, 116 + r, c);
}

}  // namespace wxraster
