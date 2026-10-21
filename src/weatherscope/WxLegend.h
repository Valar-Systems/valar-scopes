#pragma once
// Weatherscope's legend, screen 7 (docs/weatherscope-screens.md §7, ruled 2026-10-20). Pure C++11: the layout only --
// which swatch shows which code, where -- so the board draws it and a host check reads the same description. The
// colours are never stored here: every swatch is drawn from the final palette (WxPalette.h), and the bench check
// (test/fixtures/wx/legend_check.py, addendum 7) compares the board's actual pixels with the palette table.
//
//   Light rain                  rain 1-4      Moderate     rain 5-7     Heavy    rain 8-10
//   Hail possible               rain 11-13    Snow, light -> heavy  snow 1-6 (7-13 hold code 6's colour)
//   No radar coverage           14            No data      15, hatched  Dry: no label (ruled)
// dBZ in small text is optional and left out of the bench version.
//
// ROUND-SCREEN LAYOUT (Daniel's look, addendum 8): every label box (6 px x chars, 8 px) and every swatch lies within
// kInnerRadius of the centre; the block is centred vertically (y 48..192 about 120); every row's swatches start at the
// same x; every swatch, "No data" included, is kSize x kSize. test/host/test_wx_legend.cpp holds all four (G1-G4).
// "Very heavy / possible hail" is 156 px and does not fit left of the swatch column, so the row reads "Hail possible".

#include <stdint.h>

namespace wxlegend {

enum class Kind : uint8_t { Rain, Snow, NoCoverage, NoData };

struct Row { const char* label; int16_t y; };
struct Swatch { uint8_t row; Kind kind; uint8_t code; int16_t x, y, w, h; };

constexpr int kLabelX = 36;       // inside kInnerRadius at every row
constexpr int kSwatchX0 = 144;    // every row's swatches start here, right of the longest label (102 px)
constexpr int kInnerRadius = 112; // 8 px inside the 120 px panel edge (frozen, addendum 8)
constexpr int kCharW = 6, kCharH = 8;  // LovyanGFX's default font at text size 1
constexpr int kPitch = 14;
constexpr int kSize = 12;

// Label y; each row's swatches sit 2 px higher. Rows at a 22 px pitch: swatches y 48..192, centred on 120.
constexpr Row kRows[7] = {{"Light rain", 50},      {"Moderate", 72},          {"Heavy", 94}, {"Hail possible", 116},
                          {"Snow light>heavy", 138}, {"No radar coverage", 160}, {"No data", 182}};

constexpr Swatch S(uint8_t row, Kind kind, uint8_t code, int i)
{
    return Swatch{row, kind, code, (int16_t)(kSwatchX0 + i * kPitch), (int16_t)(kRows[row].y - 2), (int16_t)kSize, (int16_t)kSize};
}

constexpr int kSwatchCount = 21;
constexpr Swatch kSwatches[kSwatchCount] = {
    S(0, Kind::Rain, 1, 0),  S(0, Kind::Rain, 2, 1),  S(0, Kind::Rain, 3, 2),  S(0, Kind::Rain, 4, 3),
    S(1, Kind::Rain, 5, 0),  S(1, Kind::Rain, 6, 1),  S(1, Kind::Rain, 7, 2),
    S(2, Kind::Rain, 8, 0),  S(2, Kind::Rain, 9, 1),  S(2, Kind::Rain, 10, 2),
    S(3, Kind::Rain, 11, 0), S(3, Kind::Rain, 12, 1), S(3, Kind::Rain, 13, 2),
    S(4, Kind::Snow, 1, 0),  S(4, Kind::Snow, 2, 1),  S(4, Kind::Snow, 3, 2),  S(4, Kind::Snow, 4, 3), S(4, Kind::Snow, 5, 4),
    S(4, Kind::Snow, 6, 5),
    S(5, Kind::NoCoverage, 14, 0),
    S(6, Kind::NoData, 15, 0)};

inline const char* KindName(Kind k)
{
    return k == Kind::Rain ? "rain" : k == Kind::Snow ? "snow" : k == Kind::NoCoverage ? "nocov" : "nodata";
}

}  // namespace wxlegend
