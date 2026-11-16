#pragma once
// P5 1c, the dirty-region clear (docs/speed-p5p6.md section 1c and amendment 4m, on weatherscope/speed-p5p6): restore
// only what the LAST frame drawn into this buffer changed, from the weather layer, instead of copying all 115,200 B.
//
// THE CLEAR IS EXACT ONLY IF EVERY WRITE IS MARKED. After a full clear the buffer equals the layer. Every draw then
// marks a box that holds every pixel it can write (the Mark* functions below, one per LovyanGFX primitive), and the
// next time THIS buffer is drawn, Restore copies back exactly those rows' spans. So a buffer's pixels outside its set
// still equal the layer, and after Restore the whole buffer does. Anything that writes without marking must call
// All(), which makes the next Restore of that buffer a full one.
//
// PER BUFFER (M3's two): each buffer keeps its own set, from the last frame drawn into it, which is two frames back.
// A FULL restore whenever the buffer is fresh (All, the first use) or the layer has been rebuilt since the buffer
// last matched it (the generation).
// THE INDICATOR'S BAND IS RESTORED ON EVERY FRAME (section 1c): map-client's frame-trust test reads
// DrawScreenIndicator's dots as proof that a /diag/fb grab is finished, so a frame stopped before the indicator must
// never show the previous frame's dots, however those dots were drawn.
//
// Header-only, no Arduino and no LovyanGFX: test/host/test_p5_clear.cpp compiles exactly this.
#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace dirtyclear {

constexpr int kMaxRows = 480;

struct Set {
    int16_t lo[kMaxRows];   // per row, the first and last dirty column (lo > hi: the row is clean)
    int16_t hi[kMaxRows];
    int w = 0, h = 0;
    bool full = true;       // the next Restore copies everything
    uint32_t gen = 0;       // the layer generation this buffer's unmarked pixels hold

    void Init(int width, int height)
    {
        w = width;
        h = height > kMaxRows ? kMaxRows : height;
        full = true;
        Reset();
    }
    void Reset()
    {
        for (int y = 0; y < h; ++y) { lo[y] = (int16_t)w; hi[y] = -1; }
    }
    void All() { full = true; }
    void Span(int x0, int x1, int y)
    {
        if (y < 0 || y >= h) return;
        if (x0 < 0) x0 = 0;
        if (x1 >= w) x1 = w - 1;
        if (x0 > x1) return;
        if (x0 < lo[y]) lo[y] = (int16_t)x0;
        if (x1 > hi[y]) hi[y] = (int16_t)x1;
    }
    void Box(int x0, int y0, int x1, int y1)   // inclusive corners, in any order
    {
        if (x0 > x1) { const int t = x0; x0 = x1; x1 = t; }
        if (y0 > y1) { const int t = y0; y0 = y1; y1 = t; }
        if (y0 < 0) y0 = 0;
        if (y1 >= h) y1 = h - 1;
        for (int y = y0; y <= y1; ++y) Span(x0, x1, y);
    }
};

inline int Min3(int a, int b, int c) { return a < b ? (a < c ? a : c) : (b < c ? b : c); }
inline int Max3(int a, int b, int c) { return a > b ? (a > c ? a : c) : (b > c ? b : c); }

// The marks: a box holding every pixel the primitive can write (test_p5_clear's MARK-COVER moves the ported raster
// through each and requires nothing outside).
inline void MarkTriangle(Set& s, int x0, int y0, int x1, int y1, int x2, int y2)
{
    s.Box(Min3(x0, x1, x2), Min3(y0, y1, y2), Max3(x0, x1, x2), Max3(y0, y1, y2));
}
inline void MarkLine(Set& s, int x0, int y0, int x1, int y1) { s.Box(x0, y0, x1, y1); }
inline void MarkCircle(Set& s, int x, int y, int r)   // drawCircle and fillCircle; r <= 0 draws the centre pixel
{
    if (r < 0) r = 0;
    s.Box(x - r, y - r, x + r, y + r);
}
inline void MarkRect(Set& s, int x, int y, int w, int h)   // fillRect and drawRect, negative sizes as LovyanGFX takes them
{
    if (w < 0) { x += w; w = -w; }
    if (h < 0) { y += h; h = -h; }
    if (w == 0 || h == 0) return;
    s.Box(x, y, x + w - 1, y + h - 1);
}
inline void MarkHLine(Set& s, int x, int y, int w) { MarkRect(s, x, y, w, 1); }

// The screen indicator's rows and columns, restored on every frame.
struct Band {
    int x0, y0, x1, y1;
};

// Brings `buf` back to `layer` before this frame draws into it: a full copy when the set says so or the layer's
// generation moved; otherwise the marked spans, and the indicator's band. Then the set starts empty for this frame.
// Returns the pixels copied.
inline uint32_t Restore(uint16_t* buf, const uint16_t* layer, Set& s, uint32_t layerGen, const Band& ind)
{
    const int w = s.w, h = s.h;
    uint32_t px = 0;
    if (s.full || s.gen != layerGen) {
        memcpy(buf, layer, (size_t)w * (size_t)h * sizeof(uint16_t));
        px = (uint32_t)w * (uint32_t)h;
    } else {
        for (int y = 0; y < h; ++y) {
            if (s.hi[y] < s.lo[y]) continue;
            const size_t at = (size_t)y * (size_t)w + (size_t)s.lo[y];
            const size_t n = (size_t)(s.hi[y] - s.lo[y] + 1);
            memcpy(buf + at, layer + at, n * sizeof(uint16_t));
            px += (uint32_t)n;
        }
        const int bx0 = ind.x0 < 0 ? 0 : ind.x0, bx1 = ind.x1 >= w ? w - 1 : ind.x1;
        for (int y = ind.y0; y <= ind.y1; ++y) {   // the indicator's band, every frame
            if (y < 0 || y >= h || bx0 > bx1) continue;
            const size_t at = (size_t)y * (size_t)w + (size_t)bx0;
            memcpy(buf + at, layer + at, (size_t)(bx1 - bx0 + 1) * sizeof(uint16_t));
            px += (uint32_t)(bx1 - bx0 + 1);
        }
    }
    s.full = false;
    s.gen = layerGen;
    s.Reset();
    return px;
}

}  // namespace dirtyclear
