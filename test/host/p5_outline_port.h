// The host port of TODAY's outline (8 black copies, then the colour marker), shared by test_p5_outline.cpp and
// tools/speed-p5p6/anchor_compare.cpp (board addendum 2, docs/speed-p5p6.md section 7). MOVED VERBATIM out of
// test_p5_outline.cpp: PORT-MOVE requires that test's whole output to be byte-identical before and after.
#pragma once
#include <cstdint>
#include <vector>

#include "LgfxPort.h"
#include "OutlineMask.h"

namespace p5port {

constexpr int W = 240;
constexpr int C = W / 2 - 1;   // SCREEN_SIZE_DIV_2 - 1, ZoomCulled's centre and radius
constexpr uint32_t kBlack = 0;

struct Img {
    uint16_t p[W * W];
    void Set(int32_t x, int32_t y, uint32_t c) { p[y * W + x] = (uint16_t)c; }
    void Background()   // never black, and varied, so every black pixel shows
    {
        for (int y = 0; y < W; ++y)
            for (int x = 0; x < W; ++x) p[y * W + x] = (uint16_t)(1 + ((x * 7 + y * 13) % 997));
    }
};
using Canvas = lgfxport::Port<Img>;

struct Contact {
    int cat;      // the emitter category; ignored for a dot
    float track;  // degrees
    int x, y;
    bool dot;
    uint32_t color;
};
struct Frame {
    std::vector<Contact> k;
    int up = 0;          // radarUpDeg
    bool zoomed = false; // ZoomCulled active
};

bool Culled(const Frame& f, const Contact& k)   // AircraftManager::ZoomCulled, zoomed in
{
    if (!f.zoomed) return false;
    const int64_t dx = (int64_t)k.x - C, dy = (int64_t)k.y - C;
    return dx * dx + dy * dy > (int64_t)C * C;
}

// The colour marker, as AircraftManager draws it after the outline.
void Colour(Canvas& c, const Frame& f, const Contact& k)
{
    if (k.dot) c.fillCircle(k.x, k.y, 3, k.color);
    else outlinemask::Draw(c, outlinemask::Build(k.cat, outlinemask::Heading(k.track, f.up), k.x, k.y), k.color);
}

// TODAY: 8 black copies at the one-pixel offsets (the loop in AircraftManager's marker block), then the marker.
void OldContact(Canvas& c, const Frame& f, const Contact& k)
{
    for (int oy = -1; oy <= 1; ++oy)
        for (int ox = -1; ox <= 1; ++ox) {
            if (ox == 0 && oy == 0) continue;
            if (k.dot) c.fillCircle(k.x + ox, k.y + oy, 3, kBlack);
            else outlinemask::Draw(c, outlinemask::Build(k.cat, outlinemask::Heading(k.track, f.up), k.x + ox, k.y + oy), kBlack);
        }
    Colour(c, f, k);
}

void RenderOld(Img& img, const Frame& f)
{
    img.Background();
    Canvas c(img, 0, 0, W - 1, W - 1);
    for (const Contact& k : f.k) {
        if (Culled(f, k)) continue;
        OldContact(c, f, k);
    }
}

}  // namespace p5port
