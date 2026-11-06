#pragma once
// The Blipscope's avionics weather look (docs/wx-device.md, "The ruling (~15:07Z), and the avionics look's
// predictions"; Daniel's decision, 2026-11-06, the Blipscope only). Pure C++11, no Arduino.
//
// THE BLIPSCOPE'S OWN TABLE. Weatherscope's src/weatherscope/WxPalette.h stays the only table the Weatherscope draws
// with, unchanged; this header amends WxS2.h's one-table rule for this case only, as ruled. It takes the Rgb type,
// kDry and Dim() from WxPalette.h and none of its colours: test/host/test_wx_avionics.cpp's AV1 fails if the layer
// reads Weatherscope's rain table again, and its AV3 fails if Weatherscope's table changes.
//
// Flat colours by the crop's 5-dBZ codes, no ramp inside a band:
//   1      15-20 dBZ  NOT DRAWN: drizzle is hidden, as a cockpit radar hides it (Daniel, 2026-11-06, ~15:18Z). Code 1
//                     draws exactly as code 0, in the layer and under the sweep (Drawn(), test_wx_avionics.cpp AV7).
//                     Its colour, dim green, stays in the table: bringing drizzle back is kFirstDrawn = 1, one line,
//                     and its own ruling.
//   2-3    20-30      green
//   4-5    30-40      yellow
//   6-7    40-50      red
//   8-13   50+        magenta
// 14 (no coverage), 15 (missing) and dry stay as WxS2.h draws them. The looks' percentages (A 40, B 25) and look 4's
// lit sweep apply to these colours unchanged.
//
// The planes, while the layer is on: cyan below 3,000 m, white at and above it (an unknown altitude, NaN, is white, as
// altitudeColor() treats it). Neither is a colour the layer can draw (AV5).
#include <stdint.h>

#include "../src/weatherscope/WxPalette.h"

namespace wxavionics {

using wxpalette::Rgb;

constexpr Rgb kDimGreen = {0, 90, 0};     // code 1, while it is drawn (kFirstDrawn = 1)
constexpr Rgb kGreen = {0, 200, 0};       // codes 2-3
constexpr Rgb kYellow = {240, 220, 0};    // codes 4-5
constexpr Rgb kRed = {230, 0, 0};         // codes 6-7
constexpr Rgb kMagenta = {255, 0, 255};   // codes 8-13

/** The lowest code the Blipscope draws. 2: drizzle (code 1) hidden. */
constexpr uint8_t kFirstDrawn = 2;

/** Whether the layer draws this code at all. Everything not drawn is drawn exactly as dry (code 0) is. */
inline bool Drawn(uint8_t code) { return code >= kFirstDrawn && code <= 13; }

/** A drawn code's colour at full value; anything else is dry. */
inline Rgb Echo(uint8_t code)
{
    if (!Drawn(code)) return wxpalette::kDry;
    if (code == 1) return kDimGreen;
    if (code <= 3) return kGreen;
    if (code <= 5) return kYellow;
    if (code <= 7) return kRed;
    return kMagenta;
}

constexpr Rgb kPlaneLow = {0, 255, 255};     // cyan, below kPlaneSplitM
constexpr Rgb kPlaneHigh = {255, 255, 255};  // white, at and above it
constexpr float kPlaneSplitM = 3000.0f;

/** A plane's colour while the layer is on. NaN fails the comparison and is white. */
inline Rgb Plane(float altMeters) { return altMeters < kPlaneSplitM ? kPlaneLow : kPlaneHigh; }

}  // namespace wxavionics
