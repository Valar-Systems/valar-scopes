#pragma once
// Weatherscope's palette (Stage 2, step 2.1). Pure C++11: no Arduino, no allocation. Ruled 2026-10-20
// (docs/weatherscope-stage1.md "Ruled for Stage 2"; docs/weatherscope-stage2.md, addendum 2), and shown on
// the board for Daniel to look at BEFORE it is final.
//
//   rain 1-12 (15-70 dBZ)  MetPy's NWSReflectivity table, pinned: test/fixtures/wx/NWSReflectivity-metpy-v1.6.3.tbl
//                          (sha256 f73f0d6264d4...). Code 1 (15-20 dBZ) is dimmed slightly: 85 % or 70 %, both shown.
//   rain 13 (>= 75 dBZ)    WHITE, a ruled exception: the table's 75 dBZ entry is black, which would read as dry.
//   snow 1-13              its own ramp, blue to PALE BLUE (never white, so heavy snow and 75+ rain never share a
//                          colour), linear in RGB over codes 1-8 and flat above. Code 1 is dimmed like rain's.
//   no coverage (14)       solid neutral grey.
//   missing (15)           dim grey, or grey hatching on black -- both shown; NEVER violet (it reads as a hail core).
//   dry (0)                black.
// test/host/test_wx_palette.cpp holds these to the pinned file and to the ruled properties (C3 a-f).

#include <stdint.h>

namespace wxpalette {

struct Rgb { uint8_t r, g, b; };

constexpr bool operator==(Rgb a, Rgb b) { return a.r == b.r && a.g == b.g && a.b == b.b; }
constexpr bool operator!=(Rgb a, Rgb b) { return !(a == b); }

// The pinned table's 15-70 dBZ entries (codes 1-12), each exactly k/255 in the file, so x 255 is an integer.
constexpr Rgb kRainTable[12] = {
    {0, 0, 246},   {0, 255, 0},   {0, 200, 0},   {0, 144, 0},   {255, 255, 0}, {231, 192, 0},
    {255, 144, 0}, {255, 0, 0},   {214, 0, 0},   {192, 0, 0},   {255, 0, 255}, {153, 85, 201}};
constexpr Rgb kRain75 = {255, 255, 255};   // code 13: white, by ruling

constexpr Rgb kSnowStart = {30, 60, 160};  // code 1, before dimming
constexpr Rgb kSnowTop = {175, 215, 255};  // code 8 and above: pale blue
constexpr int kSnowTopCode = 8;

constexpr Rgb kDry = {0, 0, 0};
constexpr Rgb kNoCoverage = {96, 96, 96};
constexpr Rgb kMissingDim = {48, 48, 48};
constexpr Rgb kMissingHatch = {72, 72, 72};  // the hatch lines, drawn on kDry
constexpr int kHatchPeriod = 4;              // a hatch line where (x + y) % 4 == 0

constexpr int kDim85 = 85;
constexpr int kDim70 = 70;

// (v * pct + 50) / 100 in integers: the rounding the doc freezes.
constexpr uint8_t Dim(uint8_t v, int pct) { return (uint8_t)((v * pct + 50) / 100); }
constexpr Rgb Dim(Rgb c, int pct) { return Rgb{Dim(c.r, pct), Dim(c.g, pct), Dim(c.b, pct)}; }

// Linear in RGB from kSnowStart (code 1) to kSnowTop (code kSnowTopCode), rounded half up; flat above.
constexpr uint8_t Lerp(uint8_t a, uint8_t b, int k)
{
    return (uint8_t)((a * (kSnowTopCode - k) + b * (k - 1) + (kSnowTopCode - 1) / 2) / (kSnowTopCode - 1));
}

inline Rgb Rain(uint8_t code, int dimPct)
{
    if (code < 1 || code > 13) return kDry;
    if (code == 13) return kRain75;
    if (code == 1) return Dim(kRainTable[0], dimPct);
    return kRainTable[code - 1];
}

inline Rgb Snow(uint8_t code, int dimPct)
{
    if (code < 1 || code > 13) return kDry;
    const int k = code > kSnowTopCode ? kSnowTopCode : code;
    const Rgb c{Lerp(kSnowStart.r, kSnowTop.r, k), Lerp(kSnowStart.g, kSnowTop.g, k), Lerp(kSnowStart.b, kSnowTop.b, k)};
    return code == 1 ? Dim(c, dimPct) : c;
}

// The colour a code is drawn in, for everything except the hatch (whose pixels depend on position).
inline Rgb Code(uint8_t code, bool snow, int dimPct)
{
    if (code == 0) return kDry;
    if (code == 14) return kNoCoverage;
    if (code == 15) return kMissingDim;
    return snow ? Snow(code, dimPct) : Rain(code, dimPct);
}

}  // namespace wxpalette
