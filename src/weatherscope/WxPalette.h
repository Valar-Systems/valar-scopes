#pragma once
// Weatherscope's palette (Stage 2, step 2.1). Pure C++11: no Arduino, no allocation. Ruled 2026-10-20
// (docs/weatherscope-stage1.md "Ruled for Stage 2"; docs/weatherscope-stage2.md, addenda 2 and 4 -- addendum 4
// and 5 are Daniel's looks at the board). The whole palette is FINAL (ruled 2026-10-20, addendum 6).
//
//   rain 2-12 (20-70 dBZ)  MetPy's NWSReflectivity table, pinned: test/fixtures/wx/NWSReflectivity-metpy-v1.6.3.tbl
//                          (sha256 f73f0d6264d4...).
//   rain 1 (15-20 dBZ)     TEAL (0, 150, 135), a ruled exception (Daniel, 2026-10-28; A10.16): the table's dark blue,
//                          dimmed to 55 %, was hard to see on black. Reopened by his decision; the rest stays final.
//   rain 13 (>= 75 dBZ)    WHITE, a ruled exception: the table's 75 dBZ entry is black, which would read as dry.
//   snow 1-13              its own ramp: a medium sky blue at code 1 (clearly distinct from rain 1's teal, C3g),
//                          visible steps over codes 1-6 (15-40 dBZ, where snow sits; C3h), held at a pale blue from
//                          code 6 up -- never white, with a clear margin from rain 75+ white (C3i). Not dimmed.
//   no coverage (14)       a dark neutral grey, so large no-coverage areas don't outshine the weather.
//   missing (15)           grey hatching on black (ruled: option B). NEVER violet (it reads as a hail core).
//   dry (0)                black.
// test/host/test_wx_palette.cpp holds these to the pinned file and to the ruled properties (C3 a-l).

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

// Snow codes 1-6, interpolated evenly in CIELAB (D65) between a medium sky blue (0, 110, 210) and a pale blue
// (170, 212, 250), then rounded to sRGB, so each step is about the same visible size; code 6's colour holds to 13.
constexpr Rgb kSnowTable[6] = {{0, 110, 210}, {66, 129, 218}, {98, 149, 226}, {124, 169, 234}, {148, 190, 242}, {170, 212, 250}};
constexpr int kSnowTopCode = 6;

constexpr Rgb kDry = {0, 0, 0};
constexpr Rgb kNoCoverage = {40, 40, 40};
constexpr Rgb kMissingHatch = {72, 72, 72};  // the hatch lines, drawn on kDry
constexpr int kHatchPeriod = 4;              // a hatch line where (x + y) % 4 == 0

// Code 1 (15-20 dBZ): TEAL, a ruled exception beside code 13's white (Daniel, 2026-10-28: "i'm not sure if dark blue
// is a good color for light rain against a black background"; he chose "3. Teal"; docs/weatherscope-stage3.md
// A10.16). Not dimmed: the 55 % dim of the table's 15 dBZ blue retired with it. The pinned table above still holds
// that blue; it is simply no longer what code 1 draws.
constexpr Rgb kRain1 = {0, 150, 135};

// (v * pct + 50) / 100 in integers: the rounding the doc freezes. Rain() no longer dims, but blipscope-5c's
// WxS2.h reads kRainTable through Dim() directly (A10.16), so it stays.
constexpr uint8_t Dim(uint8_t v, int pct) { return (uint8_t)((v * pct + 50) / 100); }
constexpr Rgb Dim(Rgb c, int pct) { return Rgb{Dim(c.r, pct), Dim(c.g, pct), Dim(c.b, pct)}; }

inline Rgb Rain(uint8_t code)
{
    if (code < 1 || code > 13) return kDry;
    if (code == 13) return kRain75;
    if (code == 1) return kRain1;
    return kRainTable[code - 1];
}

inline Rgb Snow(uint8_t code)
{
    if (code < 1 || code > 13) return kDry;
    return kSnowTable[(code > kSnowTopCode ? kSnowTopCode : code) - 1];
}

// Missing is a pattern, not a colour: the hatch line where (x + y) % kHatchPeriod == 0, dry elsewhere.
inline Rgb MissingAt(int x, int y) { return (x + y) % kHatchPeriod == 0 ? kMissingHatch : kDry; }

// The colour a code is drawn in. For missing (15) this is the hatch LINE colour; a renderer draws 15 with
// MissingAt(), pixel by pixel.
inline Rgb Code(uint8_t code, bool snow)
{
    if (code == 0) return kDry;
    if (code == 14) return kNoCoverage;
    if (code == 15) return kMissingHatch;
    return snow ? Snow(code) : Rain(code);
}

}  // namespace wxpalette
