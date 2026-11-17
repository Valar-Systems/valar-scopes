// Board addendum 2's device anchor, the host half (docs/speed-p5p6.md section 7, on weatherscope/speed-p5p6): the
// port's 8-copy outline (test/host/p5_outline_port.h) composited onto the device's own background grab, compared with
// the device's 8-copy grab over every marker's footprint dilated by kDilate px. Pure C++11; no file or format here.
//
// THE SECOND PATH, named: Composite() is RenderOld's loop WITHOUT its first line. RenderOld paints Img::Background()
// before drawing, which is right for the host test (its frames are synthetic) and wrong here (the background is the
// device's grab). So Composite repeats the loop, cull and OldContact exactly, and repaints nothing; test A-SELF builds
// its "device" frame by a separate route, so a repaint in Composite reads red there.
#pragma once
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

#include "p5_outline_port.h"

namespace anchor {

using p5port::Canvas;
using p5port::Contact;
using p5port::Frame;
using p5port::Img;
using p5port::W;

constexpr int kDilate = 2;   // the registered region: every footprint dilated by 2 px (Chebyshev: a 5 x 5 square)

// LovyanGFX's rgb888 -> rgb565 for a uint32_t colour argument: each channel truncated (r & 0xF8, g & 0xFC, b & 0xF8).
inline uint16_t To565(uint32_t c888)
{
    return (uint16_t)(((c888 >> 8) & 0xF800u) | ((c888 >> 5) & 0x07E0u) | ((c888 >> 3) & 0x001Fu));
}

// One /diag/fb body: W * W pixels, RGB565 big-endian (byte 0 is the high byte). False on a wrong length.
inline bool Decode(const std::vector<uint8_t>& body, Img& out)
{
    if (body.size() != (size_t)W * W * 2) return false;
    for (int i = 0; i < W * W; ++i) out.p[i] = (uint16_t)((body[2 * i] << 8) | body[2 * i + 1]);
    return true;
}

inline std::unique_ptr<Img> NewImg() { return std::unique_ptr<Img>(new Img()); }

// The device's frame, as the port draws it: bg, then every non-culled contact's 8 black copies and marker, in order.
inline std::unique_ptr<Img> Composite(const Img& bg, const Frame& f)
{
    std::unique_ptr<Img> out = NewImg();
    std::memcpy(out->p, bg.p, sizeof out->p);
    Canvas c(*out, 0, 0, W - 1, W - 1);
    for (const Contact& k : f.k) {
        if (p5port::Culled(f, k)) continue;
        p5port::OldContact(c, f, k);
    }
    return out;
}

// Every pixel Composite writes, found through Composite itself: two flat backgrounds (1 and 2) composited; a pixel
// the contacts touched ends equal in both, an untouched one keeps its own background.
inline std::vector<uint8_t> Footprint(const Frame& f)
{
    std::unique_ptr<Img> a = NewImg(), b = NewImg();
    for (int i = 0; i < W * W; ++i) { a->p[i] = 1; b->p[i] = 2; }
    std::unique_ptr<Img> ca = Composite(*a, f), cb = Composite(*b, f);
    std::vector<uint8_t> t((size_t)W * W, 0);
    for (int i = 0; i < W * W; ++i) t[i] = ca->p[i] == cb->p[i];
    return t;
}

// Chebyshev dilation by r px, clipped to the frame.
inline std::vector<uint8_t> Dilate(const std::vector<uint8_t>& m, int r)
{
    std::vector<uint8_t> o((size_t)W * W, 0);
    for (int y = 0; y < W; ++y)
        for (int x = 0; x < W; ++x) {
            if (!m[y * W + x]) continue;
            for (int dy = -r; dy <= r; ++dy)
                for (int dx = -r; dx <= r; ++dx) {
                    const int xx = x + dx, yy = y + dy;
                    if (xx >= 0 && xx < W && yy >= 0 && yy < W) o[yy * W + xx] = 1;
                }
        }
    return o;
}

struct Result {
    long region = 0;     // pixels compared
    long mismatch = 0;   // of those, pixels that differ
};

inline Result Compare(const Img& host, const Img& dev, const std::vector<uint8_t>& region)
{
    Result r;
    for (int i = 0; i < W * W; ++i) {
        if (!region[i]) continue;
        ++r.region;
        if (host.p[i] != dev.p[i]) ++r.mismatch;
    }
    return r;
}

// THE TWO PRECONDITIONS (section 7d), here once so the L/rung pick and the file mode cannot disagree about them.
// One contact's 8-copy footprint.
inline std::vector<uint8_t> FootprintOf(const Frame& f, size_t i)
{
    Frame one;
    one.up = f.up;
    one.zoomed = f.zoomed;
    one.k.push_back(f.k[i]);
    return Footprint(one);
}
inline bool Overlap(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b)
{
    for (int i = 0; i < W * W; ++i)
        if (a[i] && b[i]) return true;
    return false;
}
// Pairs of contacts whose 8-copy footprints share a pixel.
inline int OverlapPairs(const Frame& f)
{
    std::vector<std::vector<uint8_t> > fp;
    for (size_t i = 0; i < f.k.size(); ++i) fp.push_back(FootprintOf(f, i));
    int pairs = 0;
    for (size_t a = 0; a < fp.size(); ++a)
        for (size_t b = a + 1; b < fp.size(); ++b) pairs += Overlap(fp[a], fp[b]);
    return pairs;
}
// Edge-clipped: some pixel of the contact is on screen, AND a primitive of its marker, at any of the 9 positions
// (the centre and the 8 one-pixel offsets), reaches past 0..W-1.
inline bool EdgeClipped(const Frame& f, size_t i)
{
    const std::vector<uint8_t> fp = FootprintOf(f, i);
    bool onScreen = false;
    for (int j = 0; j < W * W && !onScreen; ++j) onScreen = fp[j] != 0;
    if (!onScreen) return false;
    const Contact& k = f.k[i];
    const outlinemask::Marker m = k.dot ? outlinemask::BuildDot(k.x, k.y)
                                        : outlinemask::Build(k.cat, outlinemask::Heading(k.track, f.up), k.x, k.y);
    for (int p = 0; p < m.n; ++p) {
        const outlinemask::Prim& q = m.p[p];
        const int nc = outlinemask::Coords(q.kind);
        const int r = nc == 2 ? q.v[2] : 0;
        for (int j = 0; j < nc; ++j) {
            const int32_t v = q.v[j];
            if (v - r - 1 < 0 || v + r + 1 > W - 1) return true;
        }
    }
    return false;
}

// THE RE-REGISTRATION (docs/speed-p5p6.md section 7g, accepted 2026-11-17): the two elements the radar face draws AFTER
// its contacts, read from the firmware on bench/speed-p5 2b9a52da2e97. Their pixels are excluded from the compared
// region AND from each scene's byte-equality, here ONCE, so the region and the equality cannot disagree about them.
//   CLOCK      DrawClock: "%02d:%02d" (src/AircraftManager.cpp:5300) at cy = SCREEN_SIZE - 30 (:5356), x =
//              SCREEN_SIZE_DIV_2 - cw / 2 (:5371), called at :4233. 5 characters x 6 px at text size 1, font height 8.
//   INDICATOR  DrawScreenIndicator: dots at y = SCREEN_SIZE - 16 (:5269), x = cx - (shown - 1) * 6 + slot * 12, r <= 3
//              (:5285), called at :4226; shown <= 4.
// A box too small breaks the byte-equality (the alarming direction), so the font premise is tested, not assumed.
struct Box {
    int x0, x1, y0, y1;
};
constexpr Box kClockBox = {105, 134, 210, 217};
constexpr Box kIndicatorBox = {99, 141, 221, 227};
inline bool InBox(const Box& b, int x, int y) { return x >= b.x0 && x <= b.x1 && y >= b.y0 && y <= b.y1; }
inline bool Excluded(int x, int y) { return InBox(kClockBox, x, y) || InBox(kIndicatorBox, x, y); }

// The compared region: every footprint dilated by kDilate, less the excluded boxes.
inline std::vector<uint8_t> Region(const Frame& f)
{
    std::vector<uint8_t> r = Dilate(Footprint(f), kDilate);
    for (int y = 0; y < W; ++y)
        for (int x = 0; x < W; ++x)
            if (Excluded(x, y)) r[y * W + x] = 0;
    return r;
}

// A scene's grabs are equal when every pixel outside the excluded boxes is.
inline bool EqualOutside(const Img& a, const Img& b)
{
    for (int i = 0; i < W * W; ++i) {
        if (Excluded(i % W, i / W)) continue;
        if (a.p[i] != b.p[i]) return false;
    }
    return true;
}

// The criterion, whole: composite the contacts onto bg and compare with dev over Region(f).
inline Result Anchor(const Img& bg, const Img& dev, const Frame& f)
{
    std::unique_ptr<Img> host = Composite(bg, f);
    return Compare(*host, dev, Region(f));
}

}  // namespace anchor
