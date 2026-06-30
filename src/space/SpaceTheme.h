#pragma once

#include <cstdint>

#include "LGFX.h" // lgfx::color888

// Visual theme for the Spacescope screens: a cool "starlight" palette (pale cyan-blue) on true
// black (free power on the AMOLED tier), with white accents and amber/red for warnings/alerts.
// Mirrors EamTheme so the screen code shares the same Palette + ScaleColor shape.
namespace space {

struct Palette {
    uint32_t fg;     // primary text / lit elements
    uint32_t dim;    // secondary text
    uint32_t faint;  // labels / inactive
    uint32_t accent; // highlights (white)
    uint32_t warn;   // elevated state (amber)
    uint32_t alert;  // high / emergency (red)
    uint32_t bg;     // background (true black)
};

inline Palette PaletteDefault()
{
    return {
        lgfx::color888(120, 200, 255), lgfx::color888(70, 130, 190), lgfx::color888(30, 55, 90),
        lgfx::color888(255, 255, 255), lgfx::color888(255, 180, 0), lgfx::color888(255, 60, 40),
        lgfx::color888(0, 0, 0)
    };
}

// Scale a packed 0xRRGGBB colour by factor f (0..1). Used for night-dim and glow/fade effects.
inline uint32_t ScaleColor(uint32_t rgb, float f)
{
    if (f < 0.0f) f = 0.0f;
    if (f > 1.0f) f = 1.0f;
    const uint8_t r = (rgb >> 16) & 0xFF, g = (rgb >> 8) & 0xFF, b = rgb & 0xFF;
    return lgfx::color888((uint8_t)(r * f), (uint8_t)(g * f), (uint8_t)(b * f));
}

} // namespace space
