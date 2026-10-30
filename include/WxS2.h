#pragma once
// S2: the weather layer's colours under the planes (docs/wx-device.md, WD4 and its two addenda). Pure C++11, no
// Arduino. ONE table: Weatherscope's pinned palette (src/weatherscope/WxPalette.h: kRainTable, kRain75 and the
// rounding Dim), read from that header and never copied, because a second copy is how two palettes drift.
//
// Two looks until Daniel picks one on the bench (R2, ruled 2026-10-22). Until then the shipping image draws A:
//   A  every echo code at 40 %, and the planes get a 1 px black outline (WD5);
//   B  every echo code at 25 %, below the planes' 0.30 fade floor, with no outline.
// Code 13 (75+ dBZ) is the ruled 75+ white, dimmed like the rest (R1): never the table's black, which would draw
// the strongest cores as nothing. 14 (no coverage) and 15 (no data) are dithers in which no 2 x 2 window is all
// background, the same in both looks. test/host/test_wx_s2.cpp grades all of it, both looks.
#include <stdint.h>
#include "../src/weatherscope/WxPalette.h"

namespace wxs2 {

enum class Look : uint8_t { A, B };

constexpr int kPercentA = 40;
constexpr int kPercentB = 25;
inline int Percent(Look l) { return l == Look::A ? kPercentA : kPercentB; }

/** The ONE rule for the stored "wx-look" value (docs/wx-device.md, K2): exactly "B" is look B; anything else, absence
 *  ("") included, is look A, the default Daniel chose (2026-10-29). Both the page and Initialise() read through this. */
inline Look LookFromStored(const char* s) { return (s != nullptr && s[0] == 'B' && s[1] == '\0') ? Look::B : Look::A; }
inline bool Outline(Look l) { return l == Look::A; }

inline uint32_t Pack(wxpalette::Rgb c) { return ((uint32_t)c.r << 16) | ((uint32_t)c.g << 8) | c.b; }

/** An echo code's colour (1-13) at `pct` percent; anything else is the background. The base colour is the palette's
 *  own, wxpalette::Rain(): the pinned table, code 1's ruled teal and code 13's ruled white (A10.16), read and never
 *  copied. The one definition: Echo() below and the lit sweep (include/WxSweep.h, WD6 look 4) both use it. */
inline wxpalette::Rgb EchoAt(uint8_t code, int pct)
{
    if (code >= 1 && code <= 13) return wxpalette::Dim(wxpalette::Rain(code), pct);
    return wxpalette::kDry;
}

/** Light rain's own percent (WD7, ruled 2026-10-28): the look's percent x 55/40, rounded half up as Dim rounds, so
 *  the teal stays visible and below code 2 in luminance (look A 55 %, look B 34 %). Every other code: the look's. */
constexpr int kCode1Num = 55;
constexpr int kCode1Den = 40;
inline int CodePercent(uint8_t code, int pct)
{
    return code == 1 ? (pct * kCode1Num + kCode1Den / 2) / kCode1Den : pct;
}

/** An echo code's colour (1-13) in a look; anything else is the background. */
inline wxpalette::Rgb Echo(uint8_t code, Look l) { return EchoAt(code, CodePercent(code, Percent(l))); }

/** 14: lit on a checkerboard. 15: lit where x and y are both even. Every 2 x 2 window holds a lit pixel of each. */
inline bool NoCoverageLit(int x, int y) { return ((x + y) & 1) == 0; }
inline bool NoDataLit(int x, int y) { return (x & 1) == 0 && (y & 1) == 0; }

/** One screen pixel as RGB888 (0xRRGGBB). 0 = the background, so nothing is drawn; any code outside 0-15, such as
 *  wxlayer::kOffDisc, is the background too. x and y are screen coordinates (>= 0). */
inline uint32_t Pixel888(uint8_t code, int x, int y, Look l)
{
    if (code >= 1 && code <= 13) return Pack(Echo(code, l));
    if (code == 14) return NoCoverageLit(x, y) ? Pack(wxpalette::kNoCoverage) : 0;
    if (code == 15) return NoDataLit(x, y) ? Pack(wxpalette::kMissingHatch) : 0;
    return 0;
}

/** RGB888 -> RGB565, as the panel's sprites store it. */
inline uint16_t To565(uint32_t rgb)
{
    return (uint16_t)((((rgb >> 16) & 0xF8) << 8) | (((rgb >> 8) & 0xFC) << 3) | ((rgb & 0xF8) >> 3));
}

}  // namespace wxs2
