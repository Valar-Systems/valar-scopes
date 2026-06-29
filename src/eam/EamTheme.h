#pragma once

#include <cstdint>

#include "LGFX.h" // lgfx::color888

// Visual theme for the EAM screens: a command-console palette (green default, amber alt) on true
// black (free power on the AMOLED tier), plus a brightness-scale helper for night-dim and fades.
namespace eam {

struct Palette {
    uint32_t fg;     // primary text / lit elements
    uint32_t dim;    // secondary text
    uint32_t faint;  // labels / inactive
    uint32_t accent; // highlights (NEW, suggested freq)
    uint32_t warn;   // elevated state
    uint32_t alert;  // high / emergency
    uint32_t bg;     // background (true black)
};

inline Palette PaletteGreen()
{
    return {
        lgfx::color888(0, 255, 0), lgfx::color888(0, 200, 0), lgfx::color888(0, 90, 0),
        lgfx::color888(120, 255, 180), lgfx::color888(255, 180, 0), lgfx::color888(255, 40, 0),
        lgfx::color888(0, 0, 0)
    };
}

inline Palette PaletteAmber()
{
    return {
        lgfx::color888(255, 176, 0), lgfx::color888(210, 140, 0), lgfx::color888(90, 52, 0),
        lgfx::color888(255, 214, 90), lgfx::color888(255, 140, 0), lgfx::color888(255, 40, 0),
        lgfx::color888(0, 0, 0)
    };
}

inline Palette PaletteFor(const String& name)
{
    return name == "amber" ? PaletteAmber() : PaletteGreen();
}

// Scale a packed 0xRRGGBB colour by factor f (0..1). Mirrors AircraftManager's scaleColor:
// used for night-dim, the NEW-pulse fade, and the 7-seg bloom/ghost.
inline uint32_t ScaleColor(uint32_t rgb, float f)
{
    if (f < 0.0f) f = 0.0f;
    if (f > 1.0f) f = 1.0f;
    const uint8_t r = (rgb >> 16) & 0xFF, g = (rgb >> 8) & 0xFF, b = rgb & 0xFF;
    return lgfx::color888((uint8_t)(r * f), (uint8_t)(g * f), (uint8_t)(b * f));
}

// The Zulu clock's LED red (deep saturated #ef1409) and its faint unlit "ghost" so the panel
// reads as real segments. Ghost ~ rgba(255,46,30,0.055) flattened onto black.
inline uint32_t ClockLit()   { return lgfx::color888(0xEF, 0x14, 0x09); }
inline uint32_t ClockGhost() { return lgfx::color888(14, 3, 2); }
inline uint32_t ClockBloom() { return lgfx::color888(90, 10, 6); }

} // namespace eam
