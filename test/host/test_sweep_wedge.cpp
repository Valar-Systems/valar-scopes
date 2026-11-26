// The sweep's wedge, ONE copy (include/SweepWedge.h; the owner's "one constant", 2026-11-22). The device draws it with
// wxsweep::FillWedge (DrawRadarSweep, include/DrawHelpers.h); the weather and map sweeps mark it with
// wxsweep::Rasterise (include/WxSweep.h). This holds the two to the same pixels whatever kTail, kSegs and kPeak are,
// and reads DrawHelpers.h to prove the device's path goes through FillWedge at all: the one part a host test cannot
// draw, since the host suite has no LovyanGFX (run.sh). Rows are tagged [SW0]..[SW3] so
// test/host/sabotage_sweep_wedge.py can require exactly the planted rows to go red.
// Usage: test_sweep_wedge <path to include/DrawHelpers.h>. Exit: 0 ok, 1 a check failed, 2 the rig.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "SweepWedge.h"
#include "WxSweep.h"

using namespace wxsweep;

static int failures = 0;
static void row(bool ok, const char* tag, const char* what, long bad)
{
    std::printf("  %s  [%s] %s -- %ld mismatches\n", ok ? "ok  " : "FAIL", tag, what, bad);
    if (!ok) ++failures;
}

static long count(const std::string& hay, const char* needle)
{
    long n = 0;
    for (size_t at = hay.find(needle); at != std::string::npos; at = hay.find(needle, at + 1)) ++n;
    return n;
}

// [SW0]: DrawRadarSweep's body, found exactly once, calls FillWedge once and draws nothing of the wedge itself.
static bool SW0(const char* path)
{
    std::FILE* f = std::fopen(path, "rb");
    if (!f) return false;
    std::string src;
    char buf[4096];
    for (size_t n; (n = std::fread(buf, 1, sizeof buf, f)) > 0;) src.append(buf, n);
    std::fclose(f);

    const char* sig = "inline void DrawRadarSweep(";
    long bad = 0;
    std::string body;
    if (count(src, sig) != 1) {
        ++bad;
    } else {
        const size_t open = src.find('{', src.find(sig));
        int depth = 0;
        size_t end = open;
        for (; end < src.size(); ++end) {
            if (src[end] == '{') ++depth;
            if (src[end] == '}' && --depth == 0) break;
        }
        if (open == std::string::npos || end >= src.size()) ++bad;
        else body = src.substr(open, end - open + 1);
    }
    bad += count(body, "DrawRadarSweepBeam(") != 1;   // the CONTROL: it read the right function
    bad += count(body, "wxsweep::FillWedge(") != 1;
    bad += count(body, "fillTriangle") != 0;
    bad += count(body, "TAIL") != 0;
    bad += count(body, "cos(") != 0;
    bad += count(body, "sin(") != 0;
    row(bad == 0, "SW0", "the device's path: DrawRadarSweep calls FillWedge and draws no wedge of its own", bad);
    return true;
}

struct Tri { int32_t x0, y0, x1, y1, x2, y2; uint8_t lvl; };

// FillWedge's canvas on the host: it records each triangle and its level.
struct Recorder {
    std::vector<Tri> tris;
    void fillTriangle(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t x2, int32_t y2, uint8_t lvl)
    {
        Tri t = {x0, y0, x1, y1, x2, y2, lvl};
        tris.push_back(t);
    }
};

// The ported fillTriangle's sink: each pixel keeps the index of the LAST triangle over it, as Rasterise's marks do.
struct Painter {
    uint8_t* img;
    int w;
    uint8_t cur;
    void operator()(int x, int y) { img[y * w + x] = cur; }
    void Spanned(int, int, int) {}
};

int main(int argc, char** argv)
{
    if (argc != 2 || !SW0(argv[1])) {
        std::printf("RIG: give the path to include/DrawHelpers.h, readable (got %s)\n", argc > 1 ? argv[1] : "none");
        return 2;
    }

    std::vector<float> angles;
    for (int d = 0; d < 360; ++d) angles.push_back(d * 3.14159265f / 180.0f);
    uint32_t seed = 20261122u;
    for (int k = 0; k < 64; ++k) {
        seed = (seed * 1103515245u + 12345u) & 0x7fffffffu;
        angles.push_back(-7.0f + 14.0f * (seed / 2147483648.0f));
    }
    const int radii[3] = {120, 206, 240};

    long bad1 = 0, bad2 = 0, bad3 = 0, frames = 0;
    uint32_t minMarked = 0xffffffffu;
    const auto level = [](uint8_t lvl) { return lvl; };
    for (const int r : radii) {
        const int w = 2 * r, h = 2 * r, cx = r - 1, cy = r - 1;
        std::vector<uint8_t> drawn(w * h), marked(w * h);
        std::vector<int16_t> rowLo(h), rowHi(h);
        for (const float angle : angles) {
            ++frames;
            Recorder rec;
            FillWedge(rec, cx, cy, r, angle, level);

            // [SW1] the triangles.
            if ((int)rec.tris.size() != kSegs) ++bad1;
            const int xLead = cx + (int)(std::cos(angle) * r);
            const int yLead = cy + (int)(std::sin(angle) * r);
            for (size_t k = 0; k < rec.tris.size(); ++k) {
                const Tri& t = rec.tris[k];
                bad1 += t.lvl != SliceLevel((int)k + 1);
                bad1 += t.x0 != cx || t.y0 != cy;
                const int32_t px = k ? rec.tris[k - 1].x2 : xLead, py = k ? rec.tris[k - 1].y2 : yLead;
                bad1 += t.x1 != px || t.y1 != py;
            }

            // [SW2] the pixels.
            std::memset(&drawn[0], 0, drawn.size());
            std::memset(&marked[0], 0, marked.size());
            Painter paint = {&drawn[0], w, 0};
            for (size_t k = 0; k < rec.tris.size(); ++k) {
                const Tri& t = rec.tris[k];
                paint.cur = (uint8_t)(k + 1);
                detail::Triangle(t.x0, t.y0, t.x1, t.y1, t.x2, t.y2, w, h, paint);
            }
            int yLo = 0, yHi = 0;
            const uint32_t n = Rasterise(cx, cy, r, angle, &marked[0], w, h, &yLo, &yHi, &rowLo[0], &rowHi[0]);
            uint32_t painted = 0;
            for (size_t p = 0; p < drawn.size(); ++p) {
                bad2 += drawn[p] != marked[p];
                painted += drawn[p] != 0;
            }

            // [SW3] the control: neither side is empty.
            if (n < 1000 || painted < 1000) ++bad3;
            if (n < minMarked) minMarked = n;
        }
    }
    row(bad1 == 0, "SW1", "the triangles: kSegs a frame, slice i at SliceLevel(i), a fan from the centre", bad1);
    row(bad2 == 0, "SW2", "the pixels: FillWedge's triangles, rasterised, equal Rasterise's marks", bad2);
    char what[160];
    std::snprintf(what, sizeof what, "control: every frame marks >= 1000 px (%ld frames, 3 radii, seed 20261122; "
                  "fewest marked %u)", frames, (unsigned)minMarked);
    row(bad3 == 0, "SW3", what, bad3);
    return failures ? 1 : 0;
}
