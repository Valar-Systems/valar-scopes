#pragma once

#include "LGFX.h"

// Banded-rendering facade over a single band-height LGFX_Sprite.
//
// The radar/list/detail draw code is written in absolute 240x240 screen coordinates.
// To halve the framebuffer's RAM (a full 240x240x8-bit sprite is ~56 KB, which on the
// single-core ESP32-C3 leaves too little contiguous heap for a TLS handshake) we render
// the screen in horizontal bands using a sprite only one band tall, pushing each band
// to its screen rows. LovyanGFX has no drawing-origin offset of its own, so this thin
// wrapper forwards every draw call to the band sprite while shifting Y up by the band's
// top. Drawing that falls outside the band is clipped by the sprite automatically.
//
// Only the small set of primitives the draw code actually uses is exposed; colour and
// string arguments are forwarded as-is (templated) so LovyanGFX's own overload
// resolution is unchanged -- the wrapper only adjusts Y.
class BandCanvas {
public:
    BandCanvas(LGFX_Sprite& sprite, int offsetY) : s_(sprite), oy_(offsetY) {}

    // Escape hatches for the one site that blits another sprite onto the band.
    LGFX_Sprite& sprite() const { return s_; }
    int offsetY() const { return oy_; }

    // fills the whole band (no coordinates) -> forward unchanged
    template <typename T> void fillScreen(const T& color) { s_.fillScreen(color); }

    // coordinate-bearing primitives: shift Y by the band top
    template <typename T>
    int32_t drawString(const T& str, int32_t x, int32_t y) { return s_.drawString(str, x, y - oy_); }
    template <typename T>
    void drawLine(int32_t x0, int32_t y0, int32_t x1, int32_t y1, const T& color) { s_.drawLine(x0, y0 - oy_, x1, y1 - oy_, color); }
    template <typename T>
    void drawCircle(int32_t x, int32_t y, int32_t r, const T& color) { s_.drawCircle(x, y - oy_, r, color); }
    template <typename T>
    void fillCircle(int32_t x, int32_t y, int32_t r, const T& color) { s_.fillCircle(x, y - oy_, r, color); }
    template <typename T>
    void fillTriangle(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t x2, int32_t y2, const T& color) {
        s_.fillTriangle(x0, y0 - oy_, x1, y1 - oy_, x2, y2 - oy_, color);
    }
    template <typename T>
    void fillRect(int32_t x, int32_t y, int32_t w, int32_t h, const T& color) { s_.fillRect(x, y - oy_, w, h, color); }
    template <typename T>
    void drawRect(int32_t x, int32_t y, int32_t w, int32_t h, const T& color) { s_.drawRect(x, y - oy_, w, h, color); }
    template <typename T>
    void fillRoundRect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, const T& color) { s_.fillRoundRect(x, y - oy_, w, h, r, color); }
    template <typename T>
    void drawRoundRect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, const T& color) { s_.drawRoundRect(x, y - oy_, w, h, r, color); }
    template <typename T>
    void drawFastHLine(int32_t x, int32_t y, int32_t w, const T& color) { s_.drawFastHLine(x, y - oy_, w, color); }
    template <typename T>
    void drawFastVLine(int32_t x, int32_t y, int32_t h, const T& color) { s_.drawFastVLine(x, y - oy_, h, color); }
    // Gauge/dial arcs. Angles in degrees, 0 = 3 o'clock, growing clockwise (LovyanGFX convention).
    template <typename T>
    void fillArc(int32_t x, int32_t y, int32_t r0, int32_t r1, float a0, float a1, const T& color) { s_.fillArc(x, y - oy_, r0, r1, a0, a1, color); }
    template <typename T>
    void drawArc(int32_t x, int32_t y, int32_t r0, int32_t r1, float a0, float a1, const T& color) { s_.drawArc(x, y - oy_, r0, r1, a0, a1, color); }

    // text/state setters and queries: no coordinates -> forward unchanged
    template <typename T> void setTextColor(const T& color) { s_.setTextColor(color); }
    void setTextSize(float size) { s_.setTextSize(size); }
    template <typename T> int32_t textWidth(const T& str) { return s_.textWidth(str); }
    int32_t fontHeight() { return s_.fontHeight(); }

private:
    LGFX_Sprite& s_;
    int oy_;
};
