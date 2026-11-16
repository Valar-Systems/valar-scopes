// SCRATCH (map-server), never committed: S4-2's renderer and S4-3b's break finder (seam25.pred; S4-2 BEFORE's registration).
// Built exactly as fb_counts (-ffp-contract=off) against the same copy of the sibling's tree (bench/frame-bench-v20 at
// 08e68191f7b7; tree/ here, headers unchanged), so the pictures are the device's own maplayer::Build.
//   s4_render <pkg.bmpk> <outprefix> <lines.txt | ->
// For each rung 10, 25, 50 mi, at the package's own centre (RadarView, radar-up, as fb_counts), askedMm = Mm(km), Dark theme:
//   <outprefix>-<mi>mi.rgb565     the ALL-LAYERS framebuffer (240 x 240, uint16 little-endian): the picture
//   <outprefix>-<mi>mi-wr.rgb565  the WATER + RIVERS framebuffer (terrain and roads off): the finder's input
// Prints counts only, never a coordinate:
//   FRAME <mi> ok <0|1> rings <n> rivers <n> | wr: water <n> river <n> land <n> other <n> (in-disc pixels)
//     "other" must be 0: every in-disc pixel of the -wr frame is exactly the theme's water, river or land colour,
//     else the classification is not trusted (INSTRUMENT, exit 3).
//   BREAK <mi> segments <n> samples <n> breakpx <n>         S4-3b, when a lines file is given
// S4-3b's BREAK PIXEL (seam25.pred Amendment 1): a water or river pixel within 1 px of L whose mirror across L, along L's
// normal, at 1, 2 and 3 px holds no water or river at any of the three. L is sampled every 0.5 px inside the disc; at each
// sample s with unit normal n and each side sg = +1, -1: P = s + sg n; a break when P is water/river and none of
// s - sg k n (k = 1, 2, 3) is. Distinct P pixels are counted.
// L is projected with the device's own Mapper formula (WxBasemap.h): x = C + (lon - homeLon) / rLon * C,
// y = C - (lat - homeLat) / rLat * C, C = 120; a point's pixel is (floor x, floor y).
// The lines file: one segment per line, "lat1 lon1 lat2 lon2" in degrees (s4_lines.py, from the 2024 county file).
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <string>
#include <utility>
#include <vector>
#include "BasemapPackage.h"
#include "MapLayer.h"

namespace {
constexpr int kS = maplayer::kSize;
constexpr double kDegToRad = 0.017453292519943295769236907684886;
constexpr double kMi = 1.609344;

std::vector<uint8_t> Read(const char* p)
{
    std::vector<uint8_t> b;
    FILE* f = std::fopen(p, "rb");
    if (!f) return b;
    uint8_t buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) b.insert(b.end(), buf, buf + n);
    std::fclose(f);
    return b;
}

wxlayer::View RadarView(double homeLat, double homeLon, double radiusKm)   // fb_counts.cpp's, unchanged
{
    wxlayer::View v;
    double cosLat = std::cos(homeLat * kDegToRad);
    if (cosLat < 0.01) cosLat = 0.01;
    v.homeLat = homeLat;
    v.homeLon = homeLon;
    v.rLat = std::min(std::max(radiusKm / 111.0, 0.001), 2.0);
    v.rLon = std::min(std::max(radiusKm / (111.0 * cosLat), 0.001), 2.0);
    const float rotRad = (float)((float)0 * kDegToRad);
    v.rotCos = cosf(rotRad);
    v.rotSin = sinf(rotRad);
    v.size = kS;
    return v;
}

uint32_t Mm(double km)   // MapQuery.h:59-63, as fb_counts
{
    if (!(km > 0)) return 0;
    const double c = km < 222.0 ? km : 222.0;
    return (uint32_t)floor(c * 1e6 + 0.5);
}

struct Buffers {   // fb_counts.cpp's, unchanged
    std::vector<int32_t> qx, qy;
    std::vector<double> x, y, cross;
    std::vector<uint8_t> idx;
    std::vector<uint16_t> out;
    maplayer::Scratch sc;
    explicit Buffers(const bmpk::Package& pk)
        : qx(pk.maxVertices + 1), qy(pk.maxVertices + 1), x(pk.maxVertices + 1), y(pk.maxVertices + 1),
          cross(pk.maxVertices + 1), idx((size_t)kS * kS), out((size_t)kS * kS)
    {
        sc.qx = qx.data(); sc.qy = qy.data(); sc.x = x.data(); sc.y = y.data(); sc.cross = cross.data();
        sc.cap = pk.maxVertices + 1;
        sc.idx = idx.data();
    }
};

const maplayer::Theme& Dark() { return maplayer::kThemes[maplayer::kThemeDark]; }

bool Write565(const std::string& path, const std::vector<uint16_t>& fb)
{
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    for (uint16_t v : fb) { const uint8_t b[2] = {(uint8_t)(v & 0xff), (uint8_t)(v >> 8)}; std::fwrite(b, 1, 2, f); }
    return std::fclose(f) == 0;
}

struct Seg { double la1, lo1, la2, lo2; };
}  // namespace

int main(int argc, char** argv)
{
    if (argc != 4) { std::printf("usage: s4_render <pkg.bmpk> <outprefix> <lines.txt|->\n"); return 2; }
    const std::vector<uint8_t> bytes = Read(argv[1]);
    bmpk::Package pk;
    if (bytes.empty() || !maplayer::Load(bytes.data(), bytes.size(), &pk).accepted) { std::printf("RIG package refused\n"); return 2; }
    std::vector<Seg> L;
    if (std::strcmp(argv[3], "-") != 0) {
        FILE* f = std::fopen(argv[3], "r");
        if (!f) { std::printf("RIG lines\n"); return 2; }
        Seg s;
        while (std::fscanf(f, "%lf %lf %lf %lf", &s.la1, &s.lo1, &s.la2, &s.lo2) == 4) L.push_back(s);
        std::fclose(f);
        if (L.empty()) { std::printf("RIG lines empty\n"); return 2; }
    }
    const uint16_t cW = maplayer::To565(Dark().water), cR = maplayer::To565(Dark().river), cL = maplayer::To565(Dark().land);
    const double lat = pk.lat0Micro / 1e6, lon = pk.lon0Micro / 1e6, C = kS / 2.0;
    const int rungs[] = {10, 25, 50};
    int rc = 0;
    for (int mi : rungs) {
        const double km = mi * kMi;
        const wxlayer::View v = RadarView(lat, lon, km);
        Buffers a(pk), w(pk);
        maplayer::BuildInfo bi, bw;
        const bool ok = maplayer::Build(pk, v, Mm(km), maplayer::kLayersAll, Dark(), a.sc, a.out.data(), &bi);
        const bool okw = maplayer::Build(pk, v, Mm(km), (uint8_t)(maplayer::kLayerWater | maplayer::kLayerRivers), Dark(), w.sc, w.out.data(), &bw);
        const std::string pre = std::string(argv[2]) + "-" + std::to_string(mi) + "mi";
        if (!Write565(pre + ".rgb565", a.out) || !Write565(pre + "-wr.rgb565", w.out)) { std::printf("RIG write\n"); return 2; }
        long nW = 0, nR = 0, nL = 0, nO = 0;
        std::vector<uint8_t> wr((size_t)kS * kS, 0);   // 1 water or river
        for (int y = 0; y < kS; ++y)
            for (int x = 0; x < kS; ++x) {
                if (wxlayer::OffDisc(x, y, kS)) continue;
                const uint16_t p = w.out[(size_t)y * kS + x];
                if (p == cW) { ++nW; wr[(size_t)y * kS + x] = 1; }
                else if (p == cR) { ++nR; wr[(size_t)y * kS + x] = 1; }
                else if (p == cL) ++nL;
                else ++nO;
            }
        std::printf("FRAME %d ok %d rings %d rivers %d | wr: ok %d water %ld river %ld land %ld other %ld\n", mi, ok ? 1 : 0, bi.rings,
                    bi.rivers, okw ? 1 : 0, nW, nR, nL, nO);
        if (!ok || !okw) rc = 1;
        if (nO != 0) { std::printf("INSTRUMENT: %ld in-disc pixels of the -wr frame are no theme colour\n", nO); rc = 3; }
        if (L.empty()) continue;
        auto px = [&](double la, double lo, double* X, double* Y) { *X = C + (lo - v.homeLon) / v.rLon * C; *Y = C - (la - v.homeLat) / v.rLat * C; };
        auto W = [&](double X, double Y) {
            const int i = (int)std::floor(X), j = (int)std::floor(Y);
            if (i < 0 || j < 0 || i >= kS || j >= kS || wxlayer::OffDisc(i, j, kS)) return false;
            return wr[(size_t)j * kS + i] != 0;
        };
        std::set<std::pair<int, int>> brk;
        long samples = 0;
        for (const Seg& s : L) {
            double x1, y1, x2, y2;
            px(s.la1, s.lo1, &x1, &y1);
            px(s.la2, s.lo2, &x2, &y2);
            const double dx = x2 - x1, dy = y2 - y1, len = std::sqrt(dx * dx + dy * dy);
            if (!(len > 0)) continue;
            const double nx = -dy / len, ny = dx / len;
            const int steps = (int)std::ceil(len / 0.5);
            for (int k = 0; k <= steps; ++k) {
                const double sx = x1 + dx * k / steps, sy = y1 + dy * k / steps;
                const int si = (int)std::floor(sx), sj = (int)std::floor(sy);
                if (si < 0 || sj < 0 || si >= kS || sj >= kS || wxlayer::OffDisc(si, sj, kS)) continue;
                ++samples;
                for (int sg = -1; sg <= 1; sg += 2) {
                    const double Px = sx + sg * nx, Py = sy + sg * ny;
                    if (!W(Px, Py)) continue;
                    bool mirror = false;
                    for (int m = 1; m <= 3; ++m) if (W(sx - sg * m * nx, sy - sg * m * ny)) mirror = true;
                    if (!mirror) brk.insert(std::make_pair((int)std::floor(Px), (int)std::floor(Py)));
                }
            }
        }
        std::printf("BREAK %d segments %zu samples %ld breakpx %zu\n", mi, L.size(), samples, brk.size());
    }
    return rc;
}
