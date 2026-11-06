#pragma once
// Weatherscope's two themes (step 3.5t; docs/weatherscope-stage3.md A11.5, A12.43, A12.44, A12.46). Pure C++11: no
// Arduino, no allocation. Every colour a Weatherscope screen draws is a ROLE here, and drawing code names a role, never
// a number (NL's scan, from step t3).
//
//   kDark   today's look, to the byte. Its rain, snow, no coverage and hatch read WxPalette.h, so C3
//           (test_wx_palette.cpp) keeps testing what is drawn.
//   kLight  light (b), as Daniel picked it on the glass (A12.38), with the values A12.43 computed and A12.44 ruled.
//           test/host/test_wx_theme.cpp holds it to C4 (RGB888 and, for text, RGB565 as the panel draws it).
//
// The setting (wx-theme: auto | light | dark), Select and the dim are here too; the day/night decision is the
// product-neutral include/DayNight.h.
#include <stdint.h>
#include <string.h>

#include "DayNight.h"
#include "WxPalette.h"
#include "WxRaster.h"

namespace wxtheme {

using wxraster::Rgb;

struct Theme {
    Rgb bg;            // off the disc, the bands, and the splash, status and legend grounds
    Rgb dry;           // code 0 with no basemap
    Rgb base[41];      // the basemap's index: 0 nothing, 1-37 terrain (shadow to lit), 38 water, 39 river, 40 road
    Rgb rain[13];      // codes 1-13
    Rgb snow[13];      // codes 1-13 (6 holds to 13)
    Rgb noCoverage;    // code 14
    Rgb hatchLine;     // code 15's line, where (x + y) % 4 == 0
    Rgb hatchGround;   // code 15 elsewhere
    Rgb label;         // town labels and dots
    Rgb halo;          // behind every haloed text
    Rgb peak;          // the peak glyph and its label
    Rgb ring;          // the range rings' dots
    Rgb ringText;      // the ring numbers and the loop track
    Rgb dim;           // "N", the time, the far or NO RAIN state line, the loop age
    Rgb white;         // the white roles: home marker, near state line, zoom tag, warning band text, loop knob
    Rgb orangeText;    // the stale clock; the warning title and sub
    Rgb orangeGraphic; // the warning polygon and arrow
    Rgb uiFg;          // splash and status: the primary text
    Rgb uiDim;         // splash and status: the secondary text
    Rgb uiAccent;      // status: "Wi-Fi connected"
    Rgb uiWarn;        // status: "No Wi-Fi"
    Rgb holdTrack;     // the long-press ring's track
    Rgb holdArc;       // the long-press ring's progress
};
static_assert(sizeof(Theme) == 87 * 3, "87 roles of 3 bytes: the table A12.43 priced at 261 B a theme");

constexpr Rgb P(wxpalette::Rgb c) { return Rgb{c.r, c.g, c.b}; }

constexpr Theme kDark = {
    {0, 0, 0},
    P(wxpalette::kDry),
    {{0, 0, 0},
     {10, 10, 12}, {11, 11, 13}, {12, 12, 14}, {13, 13, 15}, {14, 14, 16}, {15, 15, 17}, {16, 16, 18},
     {17, 17, 19}, {18, 18, 20}, {19, 19, 21}, {20, 20, 22}, {21, 21, 23}, {22, 22, 24}, {23, 23, 25},
     {24, 24, 26}, {25, 25, 27}, {26, 26, 28}, {27, 27, 29}, {28, 28, 30}, {29, 29, 31}, {30, 30, 32},
     {31, 31, 33}, {32, 32, 34}, {33, 33, 35}, {34, 34, 36}, {35, 35, 37}, {36, 36, 38}, {37, 37, 39},
     {38, 38, 40}, {39, 39, 41}, {40, 40, 42}, {41, 41, 43}, {42, 42, 44}, {43, 43, 45}, {44, 44, 46},
     {45, 45, 47}, {46, 46, 48},
     {10, 34, 72}, {26, 70, 128}, {62, 62, 62}},
    {P(wxpalette::kRain1), P(wxpalette::kRainTable[1]), P(wxpalette::kRainTable[2]), P(wxpalette::kRainTable[3]),
     P(wxpalette::kRainTable[4]), P(wxpalette::kRainTable[5]), P(wxpalette::kRainTable[6]), P(wxpalette::kRainTable[7]),
     P(wxpalette::kRainTable[8]), P(wxpalette::kRainTable[9]), P(wxpalette::kRainTable[10]),
     P(wxpalette::kRainTable[11]), P(wxpalette::kRain75)},
    {P(wxpalette::kSnowTable[0]), P(wxpalette::kSnowTable[1]), P(wxpalette::kSnowTable[2]), P(wxpalette::kSnowTable[3]),
     P(wxpalette::kSnowTable[4]), P(wxpalette::kSnowTable[5]), P(wxpalette::kSnowTable[5]), P(wxpalette::kSnowTable[5]),
     P(wxpalette::kSnowTable[5]), P(wxpalette::kSnowTable[5]), P(wxpalette::kSnowTable[5]), P(wxpalette::kSnowTable[5]),
     P(wxpalette::kSnowTable[5])},
    P(wxpalette::kNoCoverage),
    P(wxpalette::kMissingHatch),
    P(wxpalette::kDry),
    {176, 176, 176},   // label
    {0, 0, 0},         // halo
    {176, 176, 176},   // peak
    {64, 64, 64},      // ring
    {96, 96, 96},      // ringText
    {120, 120, 120},   // dim
    {255, 255, 255},   // white
    {255, 140, 0},     // orangeText
    {255, 140, 0},     // orangeGraphic
    {230, 237, 234},   // uiFg
    {130, 150, 160},   // uiDim
    {90, 200, 255},    // uiAccent
    {255, 176, 40},    // uiWarn
    {0, 90, 0},        // holdTrack
    {0, 200, 0},       // holdArc
};

// Light (b). Paper (234, 231, 223); the terrain ramp is shadow (168, 165, 156) to lit (240, 237, 228) in 36 steps,
// floor(v + 0.5) -- exactly shadow + 2i; rain keeps the pinned table except teal, the two darkened yellows and black;
// snow is A12.43's S-M ramp; the hold ring swaps (A12.44).
constexpr Theme kLight = {
    {234, 231, 223},
    {234, 231, 223},
    {{234, 231, 223},
     {168, 165, 156}, {170, 167, 158}, {172, 169, 160}, {174, 171, 162}, {176, 173, 164}, {178, 175, 166},
     {180, 177, 168}, {182, 179, 170}, {184, 181, 172}, {186, 183, 174}, {188, 185, 176}, {190, 187, 178},
     {192, 189, 180}, {194, 191, 182}, {196, 193, 184}, {198, 195, 186}, {200, 197, 188}, {202, 199, 190},
     {204, 201, 192}, {206, 203, 194}, {208, 205, 196}, {210, 207, 198}, {212, 209, 200}, {214, 211, 202},
     {216, 213, 204}, {218, 215, 206}, {220, 217, 208}, {222, 219, 210}, {224, 221, 212}, {226, 223, 214},
     {228, 225, 216}, {230, 227, 218}, {232, 229, 220}, {234, 231, 222}, {236, 233, 224}, {238, 235, 226},
     {240, 237, 228},
     {171, 204, 237}, {96, 146, 206}, {178, 150, 112}},
    {P(wxpalette::kRain1), P(wxpalette::kRainTable[1]), P(wxpalette::kRainTable[2]), P(wxpalette::kRainTable[3]),
     {135, 135, 0}, {155, 128, 0}, P(wxpalette::kRainTable[6]), P(wxpalette::kRainTable[7]),
     P(wxpalette::kRainTable[8]), P(wxpalette::kRainTable[9]), P(wxpalette::kRainTable[10]),
     P(wxpalette::kRainTable[11]), {0, 0, 0}},
    {{0, 108, 206}, {0, 95, 177}, {0, 83, 149}, {0, 70, 121}, {0, 57, 94}, {0, 43, 66}, {0, 43, 66},
     {0, 43, 66}, {0, 43, 66}, {0, 43, 66}, {0, 43, 66}, {0, 43, 66}, {0, 43, 66}},
    {93, 93, 93},      // noCoverage
    {124, 124, 124},   // hatchLine
    {234, 231, 223},   // hatchGround: paper ("nothing", A12.33)
    {38, 38, 40},      // label
    {236, 233, 226},   // halo
    {92, 70, 50},      // peak
    {150, 146, 138},   // ring
    {96, 92, 86},      // ringText
    {96, 92, 86},      // dim
    {38, 38, 40},      // white: the label ink
    {159, 85, 0},      // orangeText
    {255, 140, 0},     // orangeGraphic
    {38, 38, 40},      // uiFg: the label ink, by role
    {96, 92, 86},      // uiDim: the dim text, by role
    {0, 112, 151},     // uiAccent
    {143, 94, 0},      // uiWarn
    {0, 200, 0},       // holdTrack: lighter than the arc, so the progress stays the strong mark
    {0, 90, 0},        // holdArc
};

// The state line's and the time's ink, as a ROLE: StateLine and TimeText choose it, the band draws it through the
// theme (A12.47), and the K rows name it ("dim", "white", "orange").
enum class Ink : uint8_t { Dim, White, OrangeText };
inline Rgb InkRgb(const Theme& t, Ink i) { return i == Ink::White ? t.white : i == Ink::OrangeText ? t.orangeText : t.dim; }

// ------------------------------------------------------------------------------------------------- the setting
enum class Setting : uint8_t { Auto, Light, Dark };
enum class Read : uint8_t { Known, Unset, Unknown };
struct Parsed {
    Setting setting;
    Read read;
};

// wx-theme's stored value, exactly: "auto", "light" or "dark". Empty (never saved) reads as auto, unset. Anything else
// reads as auto, UNKNOWN, so the boot line can name it -- it is never coerced into a member of the set.
inline Parsed ParseSetting(const char* s)
{
    if (!s || !*s) return Parsed{Setting::Auto, Read::Unset};
    if (strcmp(s, "auto") == 0) return Parsed{Setting::Auto, Read::Known};
    if (strcmp(s, "light") == 0) return Parsed{Setting::Light, Read::Known};
    if (strcmp(s, "dark") == 0) return Parsed{Setting::Dark, Read::Known};
    return Parsed{Setting::Auto, Read::Unknown};
}

enum class Which : uint8_t { Dark, Light };

// Light and Dark are fixed; Auto follows the phase, and is Dark until the first verdict (today's look at boot).
inline Which Select(Setting s, daynight::Phase p)
{
    if (s == Setting::Light) return Which::Light;
    if (s == Setting::Dark) return Which::Dark;
    return p == daynight::Phase::Day ? Which::Light : Which::Dark;
}

inline const Theme& Get(Which w) { return w == Which::Light ? kLight : kDark; }

// Q38: the night dim follows the same phase as the theme, so the two never disagree. Unknown is no dim.
inline bool NightDim(bool autoDim, daynight::Phase p) { return autoDim && p == daynight::Phase::Night; }

}  // namespace wxtheme
