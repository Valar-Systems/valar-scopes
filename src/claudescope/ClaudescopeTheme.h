#pragma once

#include <cstdint>

#include "LGFX.h" // lgfx::color888

// Visual theme for the Claudescope edition screens: Anthropic's warm "clay"/coral accent on true
// black (free power on the AMOLED tier), with a utilization-colour helper the gauges use to fade
// green -> amber -> red as a limit fills. Mirrors SeismicTheme / FishingTheme so the screen code
// shares the same Palette + ScaleColor shape.
namespace claudescope {

struct Palette {
    uint32_t fg;     // primary text / lit elements (Claude clay)
    uint32_t dim;    // secondary text
    uint32_t faint;  // labels / gauge tracks / inactive
    uint32_t accent; // highlights (white)
    uint32_t warn;   // elevated utilization (amber)
    uint32_t alert;  // near/at limit (red)
    uint32_t good;   // plenty of headroom (green)
    uint32_t bg;     // background (true black)
};

inline Palette PaletteDefault()
{
    return {
        lgfx::color888(217, 119, 87),  lgfx::color888(150, 92, 72),  lgfx::color888(64, 40, 32),
        lgfx::color888(255, 255, 255), lgfx::color888(255, 190, 0),  lgfx::color888(255, 70, 40),
        lgfx::color888(120, 230, 140), lgfx::color888(0, 0, 0)
    };
}

// Colour a utilization percentage (0..100): green with headroom, amber as it fills, red near the
// cap. Drives both the gauge fill and the "% used" readout so the number and the ring agree.
inline uint32_t UtilColor(const Palette& p, float pct)
{
    if (pct >= 90.0f) return p.alert;
    if (pct >= 70.0f) return p.warn;
    return p.good;
}

// Scale a packed 0xRRGGBB colour by factor f (0..1). Used for night-dim across all screens.
inline uint32_t ScaleColor(uint32_t rgb, float f)
{
    if (f < 0.0f) f = 0.0f;
    if (f > 1.0f) f = 1.0f;
    const uint8_t r = (rgb >> 16) & 0xFF, g = (rgb >> 8) & 0xFF, b = rgb & 0xFF;
    return lgfx::color888((uint8_t)(r * f), (uint8_t)(g * f), (uint8_t)(b * f));
}

} // namespace claudescope
