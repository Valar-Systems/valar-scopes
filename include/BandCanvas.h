#pragma once

#include "DirtyClear.h"
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
//
// P5 1c (docs/speed-p5p6.md 4o): with a dirty set attached (SetDirty; only the radar's weather frame attaches one),
// EVERY drawing method marks a box holding every pixel it can write, in screen coordinates, before it forwards
// (include/DirtyClear.h), and fillScreen calls All(). test/host/check_p5_writes.py's W-CANVAS row requires that of
// every method here that draws. With no set attached, nothing is marked: every other edition and face is unchanged.
class BandCanvas {
public:
    BandCanvas(LGFX_Sprite& sprite, int offsetY) : s_(sprite), oy_(offsetY) {}

    // Escape hatches for the sites that blit another sprite onto the band (the photo card, and the weather layer).
    // A site that writes through them must mark (MarkSpan) or call DirtyAll() itself (check_p5_writes.py, W-DIRECT).
    LGFX_Sprite& sprite() const { return s_; }
    int offsetY() const { return oy_; }

    // P5 1c: the dirty set this frame marks into (nullptr: none), and the two ways a direct write reports itself.
    void SetDirty(dirtyclear::Set* s) { dirty_ = s; }
    dirtyclear::Set* Dirty() const { return dirty_; }
    void DirtyAll() { if (dirty_) dirty_->All(); }
    void MarkSpan(int x0, int x1, int y) { if (dirty_) dirty_->Span(x0, x1, y); }

    // fills the whole band (no coordinates) -> forward unchanged
    template <typename T> void fillScreen(const T& color) { DirtyAll(); s_.fillScreen(color); }

    // coordinate-bearing primitives: shift Y by the band top
    template <typename T>
    int32_t drawString(const T& str, int32_t x, int32_t y) {
        const int32_t w = s_.drawString(str, x, y - oy_);
        if (dirty_) dirtyclear::MarkText(*dirty_, x, y, (int)w, (int)s_.fontHeight());   // any datum: x +- w, y +- h
        return w;
    }
    template <typename T>
    void drawLine(int32_t x0, int32_t y0, int32_t x1, int32_t y1, const T& color) {
        if (dirty_) dirtyclear::MarkLine(*dirty_, x0, y0, x1, y1);
        s_.drawLine(x0, y0 - oy_, x1, y1 - oy_, color);
    }
    template <typename T>
    void drawCircle(int32_t x, int32_t y, int32_t r, const T& color) {
        if (dirty_) dirtyclear::MarkCircle(*dirty_, x, y, r);
        s_.drawCircle(x, y - oy_, r, color);
    }
    template <typename T>
    void fillCircle(int32_t x, int32_t y, int32_t r, const T& color) {
        if (dirty_) dirtyclear::MarkCircle(*dirty_, x, y, r);
        s_.fillCircle(x, y - oy_, r, color);
    }
    template <typename T>
    void fillTriangle(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t x2, int32_t y2, const T& color) {
        if (dirty_) dirtyclear::MarkTriangle(*dirty_, x0, y0, x1, y1, x2, y2);
        s_.fillTriangle(x0, y0 - oy_, x1, y1 - oy_, x2, y2 - oy_, color);
    }
    template <typename T>
    void fillRect(int32_t x, int32_t y, int32_t w, int32_t h, const T& color) {
        if (dirty_) dirtyclear::MarkRect(*dirty_, x, y, w, h);
        s_.fillRect(x, y - oy_, w, h, color);
    }
    template <typename T>
    void drawRect(int32_t x, int32_t y, int32_t w, int32_t h, const T& color) {
        if (dirty_) dirtyclear::MarkRect(*dirty_, x, y, w, h);
        s_.drawRect(x, y - oy_, w, h, color);
    }
    template <typename T>
    void fillRoundRect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, const T& color) {
        if (dirty_) dirtyclear::MarkRect(*dirty_, x, y, w, h);
        s_.fillRoundRect(x, y - oy_, w, h, r, color);
    }
    template <typename T>
    void drawRoundRect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, const T& color) {
        if (dirty_) dirtyclear::MarkRect(*dirty_, x, y, w, h);
        s_.drawRoundRect(x, y - oy_, w, h, r, color);
    }
    template <typename T>
    void drawFastHLine(int32_t x, int32_t y, int32_t w, const T& color) {
        if (dirty_) dirtyclear::MarkHLine(*dirty_, x, y, w);
        s_.drawFastHLine(x, y - oy_, w, color);
    }
    template <typename T>
    void drawFastVLine(int32_t x, int32_t y, int32_t h, const T& color) {
        if (dirty_) dirtyclear::MarkRect(*dirty_, x, y, 1, h);
        s_.drawFastVLine(x, y - oy_, h, color);
    }
    // Gauge/dial arcs. Angles in degrees, 0 = 3 o'clock, growing clockwise (LovyanGFX convention).
    template <typename T>
    void fillArc(int32_t x, int32_t y, int32_t r0, int32_t r1, float a0, float a1, const T& color) {
        if (dirty_) dirtyclear::MarkCircle(*dirty_, x, y, r0 > r1 ? r0 : r1);
        s_.fillArc(x, y - oy_, r0, r1, a0, a1, color);
    }
    template <typename T>
    void drawArc(int32_t x, int32_t y, int32_t r0, int32_t r1, float a0, float a1, const T& color) {
        if (dirty_) dirtyclear::MarkCircle(*dirty_, x, y, r0 > r1 ? r0 : r1);
        s_.drawArc(x, y - oy_, r0, r1, a0, a1, color);
    }

    // text/state setters and queries: no coordinates -> forward unchanged
    template <typename T> void setTextColor(const T& color) { s_.setTextColor(color); }
    void setTextSize(float size) { s_.setTextSize(size); }
    template <typename T> int32_t textWidth(const T& str) { return s_.textWidth(str); }
    int32_t fontHeight() { return s_.fontHeight(); }

private:
    LGFX_Sprite& s_;
    int oy_;
    dirtyclear::Set* dirty_ = nullptr;
};
