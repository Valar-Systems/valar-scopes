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

// The criterion, whole: composite the contacts onto bg and compare with dev over Dilate(Footprint, kDilate).
inline Result Anchor(const Img& bg, const Img& dev, const Frame& f)
{
    std::unique_ptr<Img> host = Composite(bg, f);
    return Compare(*host, dev, Dilate(Footprint(f), kDilate));
}

}  // namespace anchor
