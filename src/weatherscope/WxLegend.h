#pragma once
// Weatherscope's legend, screen 7 (docs/weatherscope-screens.md §7, ruled 2026-10-20). Pure C++11: the layout only --
// which swatch shows which code, where -- so the board draws it and a host check reads the same description. The
// colours are never stored here: every swatch is drawn from the final palette (WxPalette.h), and the bench check
// (test/fixtures/wx/legend_check.py, addendum 7) compares the board's actual pixels with the palette table.
//
//   Light rain                  rain 1-4      Moderate     rain 5-7     Heavy    rain 8-10
//   Very heavy / possible hail  rain 11-13    Snow, light -> heavy  snow 1-6 (7-13 hold code 6's colour)
//   No radar coverage           14            No data      15, hatched  Dry: no label (ruled)
// dBZ in small text is optional and left out of the bench version.

#include <stdint.h>

namespace wxlegend {

enum class Kind : uint8_t { Rain, Snow, NoCoverage, NoData };

struct Row { const char* label; int16_t y; };
struct Swatch { uint8_t row; Kind kind; uint8_t code; int16_t x, y, w, h; };

constexpr int kLabelX = 34;     // inside the round panel's chord at every row
constexpr int kSwatchX0 = 140;  // swatches start right of the longest label
constexpr int kPitch = 14;
constexpr int kSize = 12;

constexpr Row kRows[7] = {{"Light rain", 40},      {"Moderate", 62},          {"Heavy", 84}, {"Very heavy/hail", 106},
                          {"Snow light>heavy", 128}, {"No radar coverage", 150}, {"No data", 172}};

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
