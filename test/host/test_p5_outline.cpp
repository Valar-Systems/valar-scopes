// P5 1a's host rows (docs/speed-p5p6.md section 1a and amendment 4e, on weatherscope/speed-p5p6): the outline mask
// (include/OutlineMask.h, DrawEdge) against today's 8 black copies, both drawn with LovyanGFX's raster as ported in
// test/host/LgfxPort.h, every frame compared whole.
//
//   test_p5_outline <repo-root>   exit 0 every row ok, 1 a row failed, 2 the rig
//
// Rows (each prints "ok" or "FAIL" and its id; test/host/sabotage_p5_outline.py reads the ids):
//   H-OVERLAP@40/@60  pairs and triples whose markers and edges overlap, in both draw orders, well inside the sprite
//   H-EDGE@40/@60     every kind at headings 0, 45, ... 315, centred 4 px inside to 6 px past each of the disc's four
//                     tangent points, one per point per frame, with non-overlapping fillers inside
//   H-CULL@40/@60     zoomed in: contacts centred just inside the cull circle (bodies reaching past it) and just
//                     outside it, on the diagonals (well inside the sprite), with fillers inside
//   H-CULL-ORDER      read from src/AircraftManager.cpp: in the radar loop every outline call lies AFTER the loop's
//                     cull and BEFORE the contact's own colour marker
//   CMP control       the comparison sees a single changed pixel
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "LgfxPort.h"
#include "OutlineMask.h"
#include "p5_outline_port.h"

namespace {

using namespace p5port;


int failures = 0;
void Row(bool ok, const std::string& id, const std::string& what)
{
    if (!ok) ++failures;
    std::printf("  %s  %s  %s\n", ok ? "ok  " : "FAIL", id.c_str(), what.c_str());
}


struct Bits {
    uint32_t row[outlinemask::kSide];
    void Set(int32_t x, int32_t y, uint32_t c)
    {
        if (c) row[y] |= 1u << x;
        else row[y] &= ~(1u << x);
    }
};
// The host's scratch: LovyanGFX's raster (the port) on a 32 x 32 bit grid, clipped to it as a sprite would be.
struct PortScratch {
    Bits b;
    lgfxport::Port<Bits> port{b, 0, 0, outlinemask::kSide - 1, outlinemask::kSide - 1};
    bool Begin() { std::memset(b.row, 0, sizeof b.row); return true; }
    uint32_t On() const { return 1u; }
    uint32_t Row(int r) const { return b.row[r]; }
    template <class T> void fillTriangle(int32_t a, int32_t c, int32_t d, int32_t e, int32_t f, int32_t g, const T& k) { port.fillTriangle(a, c, d, e, f, g, k); }
    template <class T> void drawLine(int32_t a, int32_t c, int32_t d, int32_t e, const T& k) { port.drawLine(a, c, d, e, k); }
    template <class T> void fillCircle(int32_t a, int32_t c, int32_t r, const T& k) { port.fillCircle(a, c, r, k); }
    template <class T> void drawCircle(int32_t a, int32_t c, int32_t r, const T& k) { port.drawCircle(a, c, r, k); }
};


struct Stats {
    long mask = 0, fallback = 0;
};

// THE MASK: the edge (DrawEdge), as AircraftManager::DrawOutlineMask calls it.
void NewEdge(Canvas& c, PortScratch& s, const Frame& f, const Contact& k, Stats& st)
{
    bool used;
    if (k.dot) {
        used = outlinemask::DrawEdge(c, s, outlinemask::BuildDot(k.x, k.y),
                                     [](int ax, int ay) { return outlinemask::BuildDot(ax, ay); }, k.x, k.y, kBlack);
    } else {
        const outlinemask::Dir d = outlinemask::Heading(k.track, f.up);
        const int cat = k.cat;
        used = outlinemask::DrawEdge(c, s, outlinemask::Build(cat, d, k.x, k.y),
                                     [&](int ax, int ay) { return outlinemask::Build(cat, d, ax, ay); }, k.x, k.y, kBlack);
    }
    ++(used ? st.mask : st.fallback);
}


void RenderNew(Img& img, PortScratch& s, const Frame& f, Stats& st)
{
    img.Background();
    Canvas c(img, 0, 0, W - 1, W - 1);
    // P5-ONEPASS's anchor: each contact's edge immediately before its own marker, in the same loop (today's order).
    for (const Contact& k : f.k) {
        if (Culled(f, k)) continue;
        NewEdge(c, s, f, k, st);
        Colour(c, f, k);
    }
}

long Diff(const Img& a, const Img& b)
{
    long n = 0;
    for (int i = 0; i < W * W; ++i) n += a.p[i] != b.p[i];
    return n;
}

// A small deterministic generator, so every run draws the same scenes.
struct Lcg {
    uint32_t s;
    uint32_t Next() { s = s * 1664525u + 1013904223u; return s >> 8; }
    int Int(int lo, int hi) { return lo + (int)(Next() % (uint32_t)(hi - lo + 1)); }
};

// Kinds: 0 light dart, 1 heavy dart (+ cross-wing), 2 glider, 3 rotor, 4 balloon, 5 dot.
constexpr int kKinds = 6;
void SetKind(Contact& k, int kind, Lcg& g)
{
    static const int light[] = {0, 1, 2, 3, 7};
    static const int heavy[] = {4, 5, 6};
    k.dot = kind == 5;
    k.cat = kind == 0 ? light[g.Int(0, 4)] : kind == 1 ? heavy[g.Int(0, 2)] : kind == 2 ? 9 : kind == 3 ? 8 : kind == 4 ? 10 : 0;
}

float Track(Lcg& g)   // half on the eight exact headings (the float edge cases), half anywhere
{
    return g.Int(0, 1) ? 45.0f * g.Int(0, 7) : (float)g.Int(0, 35999) / 100.0f;
}

const int kUps[] = {0, 0, 90, 37, 270};

// Interior filler slots on a grid of the given spacing (no two markers with their edges can touch at spacing >= 22:
// a marker reaches at most 9 px from its contact, its edge one more), optionally inside a radius of the centre.
std::vector<std::pair<int, int>> Slots(int lo, int hi, int step, int maxR)
{
    std::vector<std::pair<int, int>> v;
    for (int y = lo; y <= hi; y += step)
        for (int x = lo; x <= hi; x += step)
            if (maxR <= 0 || (x - C) * (x - C) + (y - C) * (y - C) <= maxR * maxR) v.push_back({x, y});
    return v;
}

uint32_t gColor = 2000;
Contact Make(int kind, float track, int x, int y, Lcg& g)
{
    Contact k{};
    SetKind(k, kind, g);
    k.track = track;
    k.x = x;
    k.y = y;
    k.color = gColor++ % 60000 + 2000;
    return k;
}

void Fill(Frame& f, const std::vector<std::pair<int, int>>& slots, int n, Lcg& g)
{
    for (int i = 0; (int)f.k.size() < n; ++i) {
        if (i >= (int)slots.size()) { std::printf("RIG: not enough filler slots\n"); std::exit(2); }
        f.k.push_back(Make(g.Int(0, kKinds - 1), Track(g), slots[(size_t)i].first, slots[(size_t)i].second, g));
    }
}

std::vector<Frame> Overlap(int n, Lcg& g)
{
    std::vector<Frame> out;
    const auto slots = Slots(30, 198, 28, 0);
    for (int fi = 0; fi < 12; ++fi) {
        Frame f;
        f.up = kUps[fi % 5];
        size_t slot = 0;
        while ((int)f.k.size() < n) {
            const int left = n - (int)f.k.size();
            int size = g.Int(2, 3);   // a cluster and its reverse add 2 * size; never leave 2 (a lone contact)
            if (left - 2 * size == 2 || left - 2 * size < 0) size = 5 - size;
            if (left - 2 * size == 2 || left - 2 * size < 0) { std::printf("RIG: cannot fill %d with clusters\n", left); std::exit(2); }
            std::vector<Contact> cl;
            for (int i = 0; i < size; ++i) cl.push_back(Make(g.Int(0, kKinds - 1), Track(g), g.Int(-6, 6), g.Int(-6, 6), g));
            for (int order = 0; order < 2; ++order) {   // the cluster, then the same cluster in the reverse order
                if (slot >= slots.size()) { std::printf("RIG: not enough cluster slots\n"); std::exit(2); }
                const auto at = slots[slot++];
                for (int i = 0; i < size; ++i) {
                    Contact k = cl[(size_t)(order ? size - 1 - i : i)];
                    k.x += at.first;
                    k.y += at.second;
                    k.color = gColor++ % 60000 + 2000;
                    f.k.push_back(k);
                }
            }
        }
        if ((int)f.k.size() != n) { std::printf("RIG: an overlap frame holds %d, not %d\n", (int)f.k.size(), n); std::exit(2); }
        out.push_back(f);
    }
    return out;
}

std::vector<Frame> Edge(int n, Lcg& g)
{
    // The disc's tangent points and their inward normals; a negative depth is past the sprite's edge.
    const int pt[4][4] = {{0, C, 1, 0}, {W - 2, C, -1, 0}, {C, 0, 0, 1}, {C, W - 2, 0, -1}};
    std::vector<Contact> cases[4];
    for (int p = 0; p < 4; ++p)
        for (int depth = -6; depth <= 4; ++depth)
            for (int t = -4; t <= 4; t += 4)
                for (int h = 0; h < 8; ++h)
                    for (int kind = 0; kind < kKinds; ++kind) {
                        const int nx = pt[p][2], ny = pt[p][3];
                        const int x = pt[p][0] + nx * depth + (nx ? 0 : t), y = pt[p][1] + ny * depth + (ny ? 0 : t);
                        cases[p].push_back(Make(kind, 45.0f * h, x, y, g));
                    }
    const auto slots = Slots(36, 204, 24, 0);
    std::vector<Frame> out;
    for (size_t i = 0; i < cases[0].size(); ++i) {
        Frame f;
        f.up = kUps[i % 5] == 37 ? 0 : kUps[i % 5];   // the edge headings stay exact: no odd window-up here
        for (int p = 0; p < 4; ++p) f.k.push_back(cases[p][i]);
        Fill(f, slots, n, g);
        out.push_back(f);
    }
    return out;
}

std::vector<Frame> Cull(int n, Lcg& g)
{
    const double kPi = 3.14159265358979323846;
    const int ang[] = {30, 45, 60, 120, 135, 150, 210, 225, 240, 300, 315, 330};
    const int radii[] = {116, 117, 118, 119, 120, 121, 122};
    const auto slots = Slots(31, 207, 22, 88);
    std::vector<Frame> out;
    for (int ri = 0; ri < 7; ++ri)
        for (int kind = 0; kind < kKinds; ++kind)
            for (int h = 0; h < 8; ++h) {
                Frame f;
                f.zoomed = true;
                f.up = kUps[(ri + kind + h) % 5];
                for (int a : ang) {
                    const double th = a * kPi / 180.0;
                    const int x = C + (int)std::lround(radii[ri] * std::cos(th));
                    const int y = C + (int)std::lround(radii[ri] * std::sin(th));
                    f.k.push_back(Make(kind, 45.0f * h + (float)(a % 7), x, y, g));
                }
                Fill(f, slots, n, g);
                out.push_back(f);
            }
    return out;
}

void Grade(const char* id, const std::vector<Frame>& frames, int* culledOut = nullptr)
{
    static Img a, b;
    PortScratch s;
    Stats st;
    long diff = 0, worst = 0;
    int bad = 0, culled = 0, inside = 0;
    for (const Frame& f : frames) {
        RenderOld(a, f);
        RenderNew(b, s, f, st);
        const long d = Diff(a, b);
        diff += d;
        if (d) { ++bad; if (d > worst) worst = d; }
        for (const Contact& k : f.k) {
            const int dx = k.x - C, dy = k.y - C;
            if (Culled(f, k)) ++culled;
            else if (f.zoomed && dx * dx + dy * dy >= 110 * 110) ++inside;
        }
    }
    if (culledOut) *culledOut = culled;
    char what[512];
    std::snprintf(what, sizeof what, "%ld differing px over %d frames (%d frames differ, worst %ld); mask %ld, fallback %ld%s",
                  diff, (int)frames.size(), bad, worst, st.mask, st.fallback,
                  frames[0].zoomed ? (std::string("; culled ") + std::to_string(culled) + ", near-circle drawn " +
                                      std::to_string(inside)).c_str() : "");
    Row(diff == 0, id, what);
}

std::string ReadFile(const std::string& path, bool& ok)
{
    std::string s;
    FILE* fp = std::fopen(path.c_str(), "rb");
    ok = fp != nullptr;
    if (!fp) return s;
    char buf[65536];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, fp)) > 0) s.append(buf, n);
    std::fclose(fp);
    return s;
}

}  // namespace

int main(int argc, char** argv)
{
    if (argc < 2) { std::printf("usage: test_p5_outline <repo-root>\n"); return 2; }

    // CMP control: the comparison sees one changed pixel.
    {
        static Img a, b;
        a.Background();
        b.Background();
        b.p[W * 17 + 23] ^= 1;
        Row(Diff(a, b) == 1, "CMP control", "one changed pixel reads " + std::to_string(Diff(a, b)) + " (want 1)");
    }

    for (int n : {40, 60}) {
        Lcg g{0x5eed0000u + (uint32_t)n};
        const std::string at = "@" + std::to_string(n);
        Grade(("H-OVERLAP" + at).c_str(), Overlap(n, g));
        Grade(("H-EDGE" + at).c_str(), Edge(n, g));
        int culled = 0;
        Grade(("H-CULL" + at).c_str(), Cull(n, g), &culled);
        if (culled == 0) { std::printf("RIG: the cull scenes cull nothing\n"); return 2; }
    }

    // H-CULL-ORDER, from the source: the loop's cull, then every outline call, then the colour marker.
    bool ok;
    const std::string src = ReadFile(std::string(argv[1]) + "/src/AircraftManager.cpp", ok);
    if (!ok) { std::printf("RIG: cannot read src/AircraftManager.cpp\n"); return 2; }
    const size_t block = src.find("const bool outline = WxOutlineNow();");
    const size_t colour = src.find("DrawAircraftTriangle(backbuffer, x, y, tracked, markerColor);", block == std::string::npos ? 0 : block);
    const size_t cull = block == std::string::npos ? std::string::npos : src.rfind("if (ZoomCulled(x, y)) continue;", block);
    if (block == std::string::npos || colour == std::string::npos || cull == std::string::npos) {
        std::printf("RIG: the radar loop's marker block, its cull or its colour marker was not found\n");
        return 2;
    }
    const std::string call = "DrawOutlineMask(backbuffer, x, y, tracked);";
    int calls = 0, misplaced = 0;
    for (size_t at = src.find(call); at != std::string::npos; at = src.find(call, at + 1)) {
        ++calls;
        if (!(at > cull && at < colour)) ++misplaced;
    }
    const size_t loop8 = src.find("for (int oy = -1; oy <= 1; ++oy)", cull);
    Row(calls >= 1 && misplaced == 0 && loop8 != std::string::npos && loop8 < colour, "H-CULL-ORDER",
        std::to_string(calls) + " outline call(s), " + std::to_string(misplaced) +
            " outside the cull..colour-marker span; the bench's 8-copy loop " +
            (loop8 != std::string::npos && loop8 < colour ? "inside it" : "NOT inside it"));

    std::printf("%s\n", failures ? "P5 OUTLINE TESTS FAILED" : "all P5 outline checks passed");
    return failures ? 1 : 0;
}
