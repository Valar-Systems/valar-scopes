// Step C's rows (docs/blipscope-map-client-steps.md, "Step C: the wedge's green added to the map"): include/MapSweep.h
// against S7 -- the wedge's green ADDED to the pixel under it, bit-identical to today's opaque wedge over black.
//
//   CS1          Add565(0, lvl) == wxsweep::Green565(lvl) for every lvl 0-255: S7's "over black", as an identity
//   CS2          for every RGB565 value and every level the wedge uses: red and blue unchanged, green raised by lvl / 4
//                and capped at 63 (an independent reference, not Add565), never darker
//   CS3          over an all-black frame with every code 0, mapsweep::Blend's frame equals wxsweep::Blend's, byte for
//                byte, at 360 beam angles: today's wedge
//   CS4-A/-B     over a frame with drawn weather codes, every drawn-code pixel equals wxsweep::Blend's (look A, look B)
//   CS5-once     over a random frame at 360 angles, every wedge pixel is the reference's single add of its own level
//   CS5-outside  ... and every pixel outside the wedge is unchanged
//   CS5-clean    ... and the slice is all zero after each blend, which blends exactly the pixels Rasterise marked
//   CS6          Bend's layer (maplayer::Build at the package's centre, 50 mi): no wedge pixel whose map pixel is not
//                itself a pure green (0x0000 or 0x0020, the colours the map shares with the wedge) comes out a pure
//                green. The excepted share is printed.
// Its sabotages: python -I test/host/sabotage_map_sweep.py (plants in a copy of include/MapSweep.h only).
//
//     test_map_sweep <fixtures dir: test/fixtures/wx>     exit 0 all pass; 1 a failure; 2 the rig
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "BasemapPackage.h"
#include "MapLayer.h"
#include "MapSweep.h"

namespace {

int checks = 0, failures = 0;

void check(bool ok, const char* id, const std::string& what)
{
    ++checks;
    if (!ok) ++failures;
    std::printf("  %s %s %s\n", ok ? "ok  " : "FAIL", id, what.c_str());
}

constexpr int W = 240, H = 240, C = 119, RAD = 120;   // DrawSweepOverWeather's call: SCREEN_SIZE_DIV_2 - 1, ..._DIV_2

// The reference add, written apart from mapsweep::Add565: channels unpacked, green raised by a quarter of the level and
// capped, packed again.
uint16_t RefAdd(uint16_t px, int lvl)
{
    const int r = (px >> 11) & 31, g = (px >> 5) & 63, b = px & 31;
    const int g2 = std::min(63, g + lvl / 4);
    return (uint16_t)((r << 11) | (g2 << 5) | b);
}

uint32_t g_seed = 12345u;
uint16_t Rand16()
{
    g_seed = g_seed * 1103515245u + 12345u;
    return (uint16_t)(g_seed >> 8);
}

struct Sweep {
    std::vector<uint8_t> slice = std::vector<uint8_t>(W * H, 0);
    std::vector<int16_t> lo = std::vector<int16_t>(H), hi = std::vector<int16_t>(H);
    int y0 = 0, y1 = -1;
    uint32_t marked = 0;
    void Mark(float angle) { marked = wxsweep::Rasterise(C, C, RAD, angle, slice.data(), W, H, &y0, &y1, lo.data(), hi.data()); }
};

uint32_t MapBlend(Sweep& s, int pct, std::vector<uint16_t>& frame, const std::vector<uint8_t>* codes)
{
    return mapsweep::Blend(pct, s.slice.data(), W, s.y0, s.y1, s.lo.data(), s.hi.data(),
                           [&](int x, int y, uint16_t v) { frame[(size_t)y * W + x] = v; },
                           [&](int x, int y) -> uint16_t { return frame[(size_t)y * W + x]; },
                           [&](int x, int y) -> uint8_t { return codes ? (*codes)[(size_t)y * W + x] : 0; });
}

uint32_t WxBlend(Sweep& s, int pct, std::vector<uint16_t>& frame, const std::vector<uint8_t>* codes)
{
    return wxsweep::Blend(pct, s.slice.data(), W, s.y0, s.y1, s.lo.data(), s.hi.data(),
                          [&](int x, int y, uint16_t v) { frame[(size_t)y * W + x] = v; },
                          [&](int x, int y) -> uint8_t { return codes ? (*codes)[(size_t)y * W + x] : 0; });
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

}  // namespace

int main(int argc, char** argv)
{
    if (argc != 2) {
        std::printf("usage: test_map_sweep <fixtures dir>\n");
        return 2;
    }
    char buf[200];

    // CS1
    {
        int bad = 0;
        for (int lvl = 0; lvl < 256; ++lvl) bad += mapsweep::Add565(0, (uint8_t)lvl) != wxsweep::Green565((uint8_t)lvl);
        std::snprintf(buf, sizeof buf, "over black, Add565 is Green565 for all 256 levels: %d differ", bad);
        check(bad == 0, "CS1", buf);
    }
    // CS2
    {
        long bad = 0, darker = 0, n = 0;
        for (int i = 1; i <= wxsweep::kSegs; ++i) {
            const uint8_t lvl = wxsweep::SliceLevel(i);
            for (uint32_t px = 0; px < 65536u; ++px) {
                const uint16_t got = mapsweep::Add565((uint16_t)px, lvl);
                bad += got != RefAdd((uint16_t)px, lvl);
                darker += ((got >> 5) & 63) < ((px >> 5) & 63) || (got >> 11) != (px >> 11) || (got & 31) != (px & 31);
                ++n;
            }
        }
        std::snprintf(buf, sizeof buf, "%ld pixel/level pairs: %ld off the reference, %ld darker or recoloured", n, bad, darker);
        check(bad == 0 && darker == 0, "CS2", buf);
    }
    // CS3
    {
        Sweep a, b;
        long diff = 0, countDiff = 0, px = 0;
        for (int k = 0; k < 360; ++k) {
            const float angle = (float)(k * 0.017453292519943295);
            std::vector<uint16_t> fa(W * H, 0), fb(W * H, 0);
            a.Mark(angle);
            b.Mark(angle);
            const uint32_t na = MapBlend(a, wxs2::kPercentA, fa, nullptr), nb = WxBlend(b, wxs2::kPercentA, fb, nullptr);
            countDiff += na != nb;
            for (int i = 0; i < W * H; ++i) diff += fa[i] != fb[i];
            px += na;
        }
        std::snprintf(buf, sizeof buf, "360 angles over black: %ld pixel(s) differ from the weather's wedge, %ld count(s) "
                      "differ (%ld blended)", diff, countDiff, px);
        check(diff == 0 && countDiff == 0 && px > 0, "CS3", buf);
    }
    // CS4
    {
        std::vector<uint8_t> codes(W * H);
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x) codes[(size_t)y * W + x] = (uint8_t)(((x / 6) + 3 * (y / 6)) % 16);
        const wxs2::Look looks[2] = {wxs2::Look::A, wxs2::Look::B};
        const char* const ids[2] = {"CS4-A", "CS4-B"};
        for (int l = 0; l < 2; ++l) {
            const int pct = wxs2::Percent(looks[l]);
            Sweep a, b;
            long diff = 0, drawn = 0;
            for (int k = 0; k < 16; ++k) {
                const float angle = k * 0.3926991f;
                std::vector<uint16_t> base(W * H);
                for (auto& v : base) v = Rand16();
                std::vector<uint16_t> fa = base, fb = base;
                a.Mark(angle);
                const std::vector<uint8_t> marks = a.slice;
                b.Mark(angle);
                MapBlend(a, pct, fa, &codes);
                WxBlend(b, pct, fb, &codes);
                for (int i = 0; i < W * H; ++i)
                    if (marks[i] && wxavionics::Drawn(codes[i])) {
                        ++drawn;
                        diff += fa[i] != fb[i];
                    }
            }
            std::snprintf(buf, sizeof buf, "%ld drawn-code wedge pixel(s) over 16 angles: %ld differ from the weather's", drawn, diff);
            check(diff == 0 && drawn > 0, ids[l], buf);
        }
    }
    // CS5
    {
        Sweep s;
        long notOnce = 0, moved = 0, dirty = 0, countBad = 0, wedge = 0;
        for (int k = 0; k < 360; ++k) {
            const float angle = (float)(k * 0.017453292519943295);
            std::vector<uint16_t> before(W * H);
            for (auto& v : before) v = Rand16();
            std::vector<uint16_t> frame = before;
            s.Mark(angle);
            const std::vector<uint8_t> marks = s.slice;
            const uint32_t n = MapBlend(s, wxs2::kPercentA, frame, nullptr);
            long m = 0;
            for (int i = 0; i < W * H; ++i) {
                if (marks[i]) {
                    ++m;
                    notOnce += frame[i] != RefAdd(before[i], wxsweep::SliceLevel(marks[i]));
                } else {
                    moved += frame[i] != before[i];
                }
                dirty += s.slice[i] != 0;
            }
            countBad += (long)n != m || n != s.marked;
            wedge += m;
        }
        std::snprintf(buf, sizeof buf, "%ld wedge pixel(s) over 360 angles: %ld not the single add", wedge, notOnce);
        check(notOnce == 0 && wedge > 0, "CS5-once", buf);
        std::snprintf(buf, sizeof buf, "%ld pixel(s) outside the wedge changed", moved);
        check(moved == 0, "CS5-outside", buf);
        std::snprintf(buf, sizeof buf, "%ld mark(s) left after the blends; %ld blend(s) not exactly the marked pixels", dirty, countBad);
        check(dirty == 0 && countBad == 0, "CS5-clean", buf);
    }
    // CS6
    {
        const std::vector<uint8_t> bytes = Read(std::string(argv[1]) + "/basemap/bend.bmpk");
        bmpk::Package pk;
        if (bytes.empty() || bmpk::Validate(bytes.data(), bytes.size(), &pk) != bmpk::Status::kOk) {
            std::printf("RIG: cannot read %s/basemap/bend.bmpk\n", argv[1]);
            return 2;
        }
        std::vector<int32_t> qx(pk.maxVertices + 1), qy(pk.maxVertices + 1);
        std::vector<double> xs(pk.maxVertices + 1), ys(pk.maxVertices + 1), cross(pk.maxVertices + 1);
        std::vector<uint8_t> idx((size_t)W * H);
        std::vector<uint16_t> layer((size_t)W * H);
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
        if (!maplayer::Build(pk, v, pk.radiusMm, maplayer::kLayersAll, maplayer::kThemes[maplayer::kThemeDark], sc,
                             layer.data())) {
            std::printf("RIG: Build refused Bend\n");
            return 2;
        }
        Sweep s;
        long wedge = 0, excepted = 0, green = 0;
        for (int k = 0; k < 36; ++k) {
            std::vector<uint16_t> frame = layer;
            s.Mark((float)(k * 10 * 0.017453292519943295));
            const std::vector<uint8_t> marks = s.slice;
            MapBlend(s, wxs2::kPercentA, frame, nullptr);
            for (int i = 0; i < W * H; ++i) {
                if (!marks[i]) continue;
                ++wedge;
                if (layer[i] == 0x0000 || layer[i] == 0x0020) { ++excepted; continue; }
                green += (frame[i] >> 11) == 0 && (frame[i] & 31) == 0;
            }
        }
        std::snprintf(buf, sizeof buf, "%ld wedge pixel(s) over 36 angles: %ld a pure green over a map colour; %.2f %% "
                      "excepted (the map's own 0x0000 / 0x0020)", wedge, green, wedge ? 100.0 * excepted / wedge : 0.0);
        check(green == 0 && wedge > 0, "CS6", buf);
    }
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
