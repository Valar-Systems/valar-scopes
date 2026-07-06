#pragma once

#include <cstdint>

#include "LGFX.h" // lgfx::color888

// Visual theme for the Fishing edition (product name "Reelscope") screens: a cool cyan/teal "water"
// palette on true black (free power on the AMOLED tier), with a few domain helpers (trend arrows,
// bite-window colour) the dials use. Mirrors SeismicTheme / BirdingTheme so the screen code shares
// the same Palette + ScaleColor shape.
namespace fishing {

struct Palette {
    uint32_t fg;     // primary text / lit elements
    uint32_t dim;    // secondary text
    uint32_t faint;  // labels / range rings / inactive
    uint32_t accent; // highlights (white)
    uint32_t warn;   // elevated state (amber)
    uint32_t alert;  // high / emergency (red)
    uint32_t good;   // favourable state (green -- e.g. an open bite window)
    uint32_t bg;     // background (true black)
};

inline Palette PaletteDefault()
{
    return {
        lgfx::color888(120, 220, 255), lgfx::color888(70, 150, 190), lgfx::color888(28, 66, 84),
        lgfx::color888(255, 255, 255), lgfx::color888(255, 190, 0),  lgfx::color888(255, 70, 40),
        lgfx::color888(120, 230, 140), lgfx::color888(0, 0, 0)
    };
}

// A short glyph for a rise/steady/fall trend (barometer, river flow, tide direction).
// dir: <0 falling, 0 steady, >0 rising.
inline const char* TrendGlyph(int dir)
{
    if (dir > 0) return "\x18"; // up arrow (built-in GFX font glyph 0x18)
    if (dir < 0) return "\x19"; // down arrow (0x19)
    return "\x1A";              // right arrow (0x1A) -- "steady"
}

// Colour a rising/steady/falling trend. Rising water/pressure reads "active" (accent), a sharp
// fall reads cautionary (warn); steady is muted.
inline uint32_t TrendColor(const Palette& p, int dir)
{
    if (dir > 0) return p.accent;
    if (dir < 0) return p.warn;
    return p.dim;
}

// Colour for a solunar feeding window: majors are the strong "good" green, minors a muted teal.
inline uint32_t WindowColor(const Palette& p, bool major)
{
    return major ? p.good : p.dim;
}

// Scale a packed 0xRRGGBB colour by factor f (0..1). Used for night-dim across all screens.
inline uint32_t ScaleColor(uint32_t rgb, float f)
{
    if (f < 0.0f) f = 0.0f;
    if (f > 1.0f) f = 1.0f;
    const uint8_t r = (rgb >> 16) & 0xFF, g = (rgb >> 8) & 0xFF, b = rgb & 0xFF;
    return lgfx::color888((uint8_t)(r * f), (uint8_t)(g * f), (uint8_t)(b * f));
}

} // namespace fishing
