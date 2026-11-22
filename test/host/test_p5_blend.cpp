// P5 1b's host rows (REG-1b-main; docs/speed-p5p6.md section 1b and amendment 4k, on weatherscope/speed-p5p6): the
// table blends against their per-pixel references. W5c: include/WxSweepLut.h's wxsweeplut::Blend against
// wxsweep::Blend, W5a's comparison carried over (the same frame, count and cleared slice, at all 3,600 beam angles,
// over a frame of varied pixels and codes). W5d: include/MapSweep.h's Table overload against mapsweep::Blend's pct
// overload, over test_map_sweep.cpp's populations and W5c's scene.
//
//   test_p5_blend <CS6's package file>    exit 0 every row ok, 1 a row failed, 2 the rig (also: not 12 rows)
// The package is the one test_map_sweep.cpp reads. Its name is written there and nowhere else: run.sh and
// sabotage_p5_blend.py take it from that file's one "/basemap/<name>.bmpk" literal, and refuse unless there is exactly one.
//
// Rows (each prints "ok" or "FAIL" and its id; test/host/sabotage_p5_blend.py reads the ids):
//   CMP control   the comparison sees one changed pixel
//   W5c@A, W5c@B  each look: the table blend against Blend at every angle, codes 0-39 and the layer's kOffDisc (255),
//                 so both the table and its direct path run (the control: both above 0 pixels)
//   W5c-PCT       one table across frames whose look alternates A, B, A, ...: a table built for the wrong percent shows
//   W5d-CS3       CS3's population: all-black frames, codes 0, 360 angles, look A. Its blended count is CS3's
//   W5d-CS4@A/@B  CS4's: Rand16 frames, CS4's code grid, 16 angles; the WHOLE frame compared. Its drawn count is CS4's
//   W5d-CS5       CS5's: Rand16 frames, codes 0, 360 angles, look A. Its wedge count is CS5's
//   W5d-CS6       CS6's: the fixture's map layer, 36 angles, look A. Its wedge count is CS6's
//   W5d@A, W5d@B  W5c's scene through the map blend, 3,600 angles (the control: table, direct, drawn and added all > 0)
//   W5d-PCT       W5c-PCT's frames through the map blend, one table
// Each W5d count is printed so run.sh's output can be read against test_map_sweep's own: the populations are built by
// its generator rules (Rand16, seed 12345, consumed in its order: CS4-A, CS4-B, then CS5). Each row's pixel count above
// 0 is its control.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "BasemapPackage.h"
#include "MapLayer.h"
#include "MapSweep.h"
#include "WxSweepLut.h"

namespace {

int failures = 0, rows = 0;
void Row(bool ok, const std::string& id, const std::string& what)
{
    ++rows;
    if (!ok) ++failures;
    std::printf("  %s  %s  %s\n", ok ? "ok  " : "FAIL", id.c_str(), what.c_str());
}

constexpr int W = 240, H = 240, C = 119, RAD = 120, STEPS = 3600;

struct Scene {
    std::vector<uint8_t> codes;
    std::vector<uint16_t> base;
    Scene() : codes(W * H), base(W * H)
    {
        for (int i = 0; i < W * H; ++i) {
            const int x = i % W, y = i / W;
            codes[(size_t)i] = (x + y) % 97 == 0 ? (uint8_t)255 : (uint8_t)((x * 7 + y * 13) % 40);   // 255: kOffDisc
            base[(size_t)i] = (uint16_t)(((uint32_t)i * 2654435761u) >> 16);
        }
    }
};

struct Tally {
    long angles = 0, bad = 0, tablePx = 0, directPx = 0;
};

// One angle: Blend and the table blend from the same marks; true when frame, count and cleared slice all agree.
bool OneAngle(const Scene& s, const wxsweeplut::Table& t, int pct, float angle, Tally& tally)
{
    static std::vector<uint8_t> sa(W * H, 0), sb(W * H, 0);
    static std::vector<int16_t> lo(H), hi(H);
    int y0 = 0, y1 = -1;
    std::fill(sa.begin(), sa.end(), (uint8_t)0);
    wxsweep::Rasterise(C, C, RAD, angle, sa.data(), W, H, &y0, &y1, lo.data(), hi.data());
    sb = sa;
    for (int y = y0; y <= y1; ++y)
        for (int x = lo[(size_t)y]; x <= hi[(size_t)y]; ++x)
            if (sa[(size_t)y * W + x]) ++(s.codes[(size_t)y * W + x] < wxsweeplut::kCodes ? tally.tablePx : tally.directPx);
    std::vector<uint16_t> fa = s.base, fb = s.base;
    const uint32_t na = wxsweep::Blend(pct, sa.data(), W, y0, y1, lo.data(), hi.data(),
                                       [&](int x, int y, uint16_t v) { fa[(size_t)y * W + x] = v; },
                                       [&](int x, int y) { return s.codes[(size_t)y * W + x]; });
    const uint32_t nb = wxsweeplut::Blend(t, sb.data(), W, y0, y1, lo.data(), hi.data(),
                                          [&](int x, int y, uint16_t v) { fb[(size_t)y * W + x] = v; },
                                          [&](int x, int y) { return s.codes[(size_t)y * W + x]; });
    ++tally.angles;
    const bool same = na == nb && fa == fb && sa == sb;
    if (!same) ++tally.bad;
    return same;
}

void Look(const Scene& s, wxs2::Look l, const char* id)
{
    const int pct = wxs2::Percent(l);
    wxsweeplut::Table t;
    wxsweeplut::For(t, pct);
    Tally tally;
    for (int k = 0; k < STEPS; ++k) OneAngle(s, t, pct, (float)(k * 6.283185307179586 / STEPS), tally);
    const bool control = tally.tablePx > 0 && tally.directPx > 0;
    Row(tally.bad == 0 && control, id,
        std::to_string(tally.bad) + " of " + std::to_string(tally.angles) + " angles differ (want 0); pixels from the " +
            "table " + std::to_string(tally.tablePx) + ", direct " + std::to_string(tally.directPx) +
            " (control: both above 0: " + (control ? "yes" : "NO") + ")");
}

void Alternating(const Scene& s)
{
    const int pa = wxs2::Percent(wxs2::Look::A), pb = wxs2::Percent(wxs2::Look::B);
    wxsweeplut::Table t;   // ONE table, as the device keeps
    Tally tally;
    for (int k = 0; k < 360; ++k) {
        const int pct = (k / 3) % 2 ? pb : pa;   // the look changes every third frame
        OneAngle(s, wxsweeplut::For(t, pct), pct, (float)(k * 6.283185307179586 / 360), tally);
    }
    Row(tally.bad == 0 && pa != pb, "W5c-PCT",
        std::to_string(tally.bad) + " of " + std::to_string(tally.angles) + " frames differ (want 0), one table, the " +
            "look alternating " + std::to_string(pa) + "% / " + std::to_string(pb) + "%");
}

// ---- W5d: the map's blend ------------------------------------------------------------------------------------------

// test_map_sweep.cpp's generator, transcribed: the same seed, the same recurrence, consumed in the same order.
uint32_t g_seed = 12345u;
uint16_t Rand16()
{
    g_seed = g_seed * 1103515245u + 12345u;
    return (uint16_t)(g_seed >> 8);
}

struct MapTally {
    long frames = 0, bad = 0, marked = 0, drawnPx = 0, addedPx = 0, directPx = 0;
};

// One frame: mapsweep::Blend's pct overload (the reference) and its Table overload from the same marks, each over its
// own copy of `base`, reading back the pixel it adds to; true when frame, count and cleared slice all agree. `codes` is
// null for the map alone (code 0 everywhere, as DrawSweepOverMap's lambda returns without the weather).
bool OneMapFrame(const std::vector<uint16_t>& base, const std::vector<uint8_t>* codes, const wxsweeplut::Table& t,
                 int pct, float angle, MapTally& tally)
{
    static std::vector<uint8_t> sa(W * H, 0), sb(W * H, 0);
    static std::vector<int16_t> lo(H), hi(H);
    int y0 = 0, y1 = -1;
    std::fill(sa.begin(), sa.end(), (uint8_t)0);
    wxsweep::Rasterise(C, C, RAD, angle, sa.data(), W, H, &y0, &y1, lo.data(), hi.data());
    sb = sa;
    for (int i = 0; i < W * H; ++i) {
        if (!sa[(size_t)i]) continue;
        ++tally.marked;
        const uint8_t c = codes ? (*codes)[(size_t)i] : 0;
        if (c >= wxsweeplut::kCodes) ++tally.directPx;
        else ++(wxavionics::Drawn(c) ? tally.drawnPx : tally.addedPx);
    }
    std::vector<uint16_t> fa = base, fb = base;
    auto code = [&](int x, int y) -> uint8_t { return codes ? (*codes)[(size_t)y * W + x] : 0; };
    const uint32_t na = mapsweep::Blend(pct, sa.data(), W, y0, y1, lo.data(), hi.data(),
                                        [&](int x, int y, uint16_t v) { fa[(size_t)y * W + x] = v; },
                                        [&](int x, int y) -> uint16_t { return fa[(size_t)y * W + x]; }, code);
    const uint32_t nb = mapsweep::Blend(t, sb.data(), W, y0, y1, lo.data(), hi.data(),
                                        [&](int x, int y, uint16_t v) { fb[(size_t)y * W + x] = v; },
                                        [&](int x, int y) -> uint16_t { return fb[(size_t)y * W + x]; }, code);
    ++tally.frames;
    const bool same = na == nb && fa == fb && sa == sb;
    if (!same) ++tally.bad;
    return same;
}

std::string Says(const MapTally& t, const char* unit)
{
    return std::to_string(t.bad) + " of " + std::to_string(t.frames) + " " + unit + " differ (want 0)";
}

std::vector<uint8_t> Read(const std::string& path)
{
    std::vector<uint8_t> b;
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return b;
    uint8_t buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) b.insert(b.end(), buf, buf + n);
    std::fclose(f);
    return b;
}

// CS6's layer, built exactly as test_map_sweep.cpp builds it. False: the rig.
bool Cs6Layer(const std::string& package, std::vector<uint16_t>& layer)
{
    const std::vector<uint8_t> bytes = Read(package);
    bmpk::Package pk;
    if (bytes.empty() || bmpk::Validate(bytes.data(), bytes.size(), &pk) != bmpk::Status::kOk) return false;
    std::vector<int32_t> qx(pk.maxVertices + 1), qy(pk.maxVertices + 1);
    std::vector<double> xs(pk.maxVertices + 1), ys(pk.maxVertices + 1), cross(pk.maxVertices + 1);
    std::vector<uint8_t> idx((size_t)W * H);
    layer.assign((size_t)W * H, 0);
    maplayer::Scratch sc;
    sc.qx = qx.data(); sc.qy = qy.data(); sc.x = xs.data(); sc.y = ys.data(); sc.cross = cross.data();
    sc.cap = pk.maxVertices + 1; sc.idx = idx.data();
    const double kDeg = 0.017453292519943295769236907684886, km = 50 * 1.609344;
    wxlayer::View v;
    const double lat = pk.lat0Micro / 1e6, lon = pk.lon0Micro / 1e6;
    v.homeLat = lat; v.homeLon = lon;
    v.rLat = std::min(std::max(km / 111.0, 0.001), 2.0);
    v.rLon = std::min(std::max(km / (111.0 * std::cos(lat * kDeg)), 0.001), 2.0);
    v.size = W;
    return maplayer::Build(pk, v, pk.radiusMm, maplayer::kLayersAll, maplayer::kThemes[maplayer::kThemeDark], sc,
                           layer.data());
}

// W5d-CS3 .. W5d-CS6, in test_map_sweep.cpp's order so Rand16 is consumed as it consumes it. False: the rig.
bool MapPopulations(const std::string& package)
{
    const int pa = wxs2::kPercentA;
    {   // CS3: all-black frames, codes 0, 360 angles, look A
        wxsweeplut::Table t;
        wxsweeplut::For(t, pa);
        MapTally tally;
        const std::vector<uint16_t> black(W * H, 0);
        for (int k = 0; k < 360; ++k) OneMapFrame(black, nullptr, t, pa, (float)(k * 0.017453292519943295), tally);
        Row(tally.bad == 0 && tally.marked > 0, "W5d-CS3",
            Says(tally, "angles") + "; blended " + std::to_string(tally.marked) + " (CS3's count)");
    }
    {   // CS4: Rand16 frames, CS4's code grid, 16 angles, each look
        std::vector<uint8_t> codes(W * H);
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x) codes[(size_t)y * W + x] = (uint8_t)(((x / 6) + 3 * (y / 6)) % 16);
        const wxs2::Look looks[2] = {wxs2::Look::A, wxs2::Look::B};
        const char* const ids[2] = {"W5d-CS4@A", "W5d-CS4@B"};
        for (int l = 0; l < 2; ++l) {
            const int pct = wxs2::Percent(looks[l]);
            wxsweeplut::Table t;
            wxsweeplut::For(t, pct);
            MapTally tally;
            for (int k = 0; k < 16; ++k) {
                std::vector<uint16_t> base(W * H);
                for (auto& px : base) px = Rand16();
                OneMapFrame(base, &codes, t, pct, k * 0.3926991f, tally);
            }
            Row(tally.bad == 0 && tally.drawnPx > 0 && tally.addedPx > 0, ids[l],
                Says(tally, "angles") + "; drawn " + std::to_string(tally.drawnPx) + " (CS4's count), added " +
                    std::to_string(tally.addedPx));
        }
    }
    {   // CS5: Rand16 frames, codes 0, 360 angles, look A
        wxsweeplut::Table t;
        wxsweeplut::For(t, pa);
        MapTally tally;
        for (int k = 0; k < 360; ++k) {
            std::vector<uint16_t> base(W * H);
            for (auto& px : base) px = Rand16();
            OneMapFrame(base, nullptr, t, pa, (float)(k * 0.017453292519943295), tally);
        }
        Row(tally.bad == 0 && tally.marked > 0, "W5d-CS5",
            Says(tally, "angles") + "; wedge " + std::to_string(tally.marked) + " (CS5's count)");
    }
    {   // CS6: the fixture's map layer, 36 angles, look A
        std::vector<uint16_t> layer;
        if (!Cs6Layer(package, layer)) {
            std::printf("RIG: cannot build CS6's layer from %s\n", package.c_str());
            return false;
        }
        wxsweeplut::Table t;
        wxsweeplut::For(t, pa);
        MapTally tally;
        for (int k = 0; k < 36; ++k) OneMapFrame(layer, nullptr, t, pa, (float)(k * 10 * 0.017453292519943295), tally);
        Row(tally.bad == 0 && tally.marked > 0, "W5d-CS6",
            Says(tally, "angles") + "; wedge " + std::to_string(tally.marked) + " (CS6's count)");
    }
    return true;
}

void MapLook(const Scene& s, wxs2::Look l, const char* id)
{
    const int pct = wxs2::Percent(l);
    wxsweeplut::Table t;
    wxsweeplut::For(t, pct);
    MapTally tally;
    for (int k = 0; k < STEPS; ++k) OneMapFrame(s.base, &s.codes, t, pct, (float)(k * 6.283185307179586 / STEPS), tally);
    const bool control = tally.drawnPx > 0 && tally.addedPx > 0 && tally.directPx > 0;
    Row(tally.bad == 0 && control, id,
        Says(tally, "angles") + "; pixels from the table drawn " + std::to_string(tally.drawnPx) + ", added " +
            std::to_string(tally.addedPx) + ", direct " + std::to_string(tally.directPx) +
            " (control: all above 0: " + (control ? "yes" : "NO") + ")");
}

void MapAlternating(const Scene& s)
{
    const int pa = wxs2::Percent(wxs2::Look::A), pb = wxs2::Percent(wxs2::Look::B);
    wxsweeplut::Table t;   // ONE table, as the device keeps
    MapTally tally;
    for (int k = 0; k < 360; ++k) {
        const int pct = (k / 3) % 2 ? pb : pa;   // the look changes every third frame
        OneMapFrame(s.base, &s.codes, wxsweeplut::For(t, pct), pct, (float)(k * 6.283185307179586 / 360), tally);
    }
    Row(tally.bad == 0 && pa != pb, "W5d-PCT",
        Says(tally, "frames") + ", one table, the look alternating " + std::to_string(pa) + "% / " +
            std::to_string(pb) + "%");
}

}  // namespace

int main(int argc, char** argv)
{
    if (argc != 2) {
        std::printf("usage: test_p5_blend <CS6's package file>\n");
        return 2;
    }
    {
        std::vector<uint16_t> a(16, 7), b(16, 7);
        b[5] = 8;
        Row(a != b, "CMP control", std::string("one changed pixel ") + (a != b ? "reads a difference" : "reads the same"));
    }
    const Scene s;
    Look(s, wxs2::Look::A, "W5c@A");
    Look(s, wxs2::Look::B, "W5c@B");
    Alternating(s);
    if (!MapPopulations(argv[1])) return 2;
    MapLook(s, wxs2::Look::A, "W5d@A");
    MapLook(s, wxs2::Look::B, "W5d@B");
    MapAlternating(s);
    std::printf("%s\n", failures ? "P5 BLEND TESTS FAILED" : "all P5 blend checks passed");
    // The row count is pinned in the binary, as test_map_sweep's is: a row dropped or compiled out is the RIG.
    if (rows != 12) {
        std::printf("the RIG: %d rows, not 12\n", rows);
        return 2;
    }
    return failures ? 1 : 0;
}
