#pragma once

#include <cstdint>

#include "LGFX.h" // lgfx::color888

// Visual theme for the Speedscope edition screens: a "speed gun" amber-on-black palette (free
// power on the AMOLED tier), with over/under-limit and direction helpers the screens use.
// Mirrors FishingTheme / SeismicTheme so the screen code shares the same Palette + ScaleColor
// shape. Speedscope ties into a MiniSpeedCam (minispeedcam.com) over the LAN.
namespace speed {

struct Palette {
    uint32_t fg;     // primary text / lit elements (amber)
    uint32_t dim;    // secondary text
    uint32_t faint;  // labels / range rings / inactive
    uint32_t accent; // highlights (white)
    uint32_t warn;   // elevated state (orange)
    uint32_t alert;  // over the limit / speeder (red)
    uint32_t good;   // favourable state (green -- under limit, device online)
    uint32_t bg;     // background (true black)
};

inline Palette PaletteDefault()
{
    return {
        lgfx::color888(255, 176, 40), lgfx::color888(180, 120, 24), lgfx::color888(70, 46, 8),
        lgfx::color888(255, 255, 255), lgfx::color888(255, 120, 0),  lgfx::color888(255, 60, 40),
        lgfx::color888(120, 230, 140), lgfx::color888(0, 0, 0)
    };
}

// Colour a measured speed against an optional posted limit: over reads as alert (red), at/under
// as the calm "good" green. With no limit set (limitMph <= 0), fall back to the amber foreground.
inline uint32_t SpeedColor(const Palette& p, int speed, int limit)
{
    if (limit <= 0) return p.fg;
    return speed > limit ? p.alert : p.good;
}

// Short label for a travel direction. dir: 0 unknown, 1 approaching, 2 receding.
inline const char* DirLabel(int dir)
{
    if (dir == 1) return "approaching";
    if (dir == 2) return "leaving";
    return "";
}

// A single-glyph arrow for a direction (built-in GFX font glyphs): approaching -> down/incoming,
// receding -> up/outgoing. Returns "" for unknown so callers can skip it.
inline const char* DirGlyph(int dir)
{
    if (dir == 1) return "\x19"; // down arrow (0x19) -- coming toward the camera
    if (dir == 2) return "\x18"; // up arrow (0x18)   -- going away
    return "";
}

// Scale a packed 0xRRGGBB colour by factor f (0..1). Used for night-dim across all screens.
inline uint32_t ScaleColor(uint32_t rgb, float f)
{
    if (f < 0.0f) f = 0.0f;
    if (f > 1.0f) f = 1.0f;
    const uint8_t r = (rgb >> 16) & 0xFF, g = (rgb >> 8) & 0xFF, b = rgb & 0xFF;
    return lgfx::color888((uint8_t)(r * f), (uint8_t)(g * f), (uint8_t)(b * f));
}

} // namespace speed
