// S8 for step D (docs/blipscope-map-client-steps.md, "S8, for the new default look"; docs/blipscope-basemap-client.md
// section 3, S8): the terrain setting changes the land under the lines, never the lines.
//
//   S8C  the measure: for each value (off, subtle, strong, xstrong) and each zoom (5, 10, 50, 100 mi; Bend's package,
//        rotation 0), the LINE PIXELS are where the value's full Build differs from its OWN terrain-only Build (so
//        blending cannot pass as a move). On every pixel row and every column of every picture, the runs of line
//        pixels are the crossings and their widths the band width. PREDICTED: identical across the four values, on
//        every row and column (graded against Subtle, today's map). Any row or column that differs is OFF.
//        The corridor, US 97 through Bend at 10 mi: the centre row's and centre column's runs are printed per value.
//        A zoom with no line pixel at all is the rig (the measure could not see a line).
//   --out <dir>: also writes the 16 pictures the owner sees, drawn by the composer (mapcompose::Frame: the layer and
//        its town names, as the device draws them), as binary PPM, s8-<mi>mi-<value>.ppm.
//
//     test_map_s8 <fixtures dir: test/fixtures/wx> [--out <dir>]     exit 0 as predicted; 1 off; 2 the rig
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "MapCompose.h"
#include "MapTerrain.h"

namespace {

std::vector<uint8_t> Read(const std::string& path)
{
    std::vector<uint8_t> b;
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return b;
    std::fseek(f, 0, SEEK_END);
    const long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (n > 0) {
        b.resize((size_t)n);
        if (std::fread(b.data(), 1, b.size(), f) != b.size()) b.clear();
    }
    std::fclose(f);
    return b;
}

void* Alloc(size_t n) { return std::malloc(n); }
void Free(void* p) { std::free(p); }
bool BuildFn(const bmpk::Package& pk, const wxlayer::View& v, uint32_t asked, uint8_t layers, const maplayer::Theme& t,
             const maplayer::Scratch& sc, uint16_t* out, maplayer::BuildInfo* info, maplayer::ClockUs clock, int mid)
{
    return maplayer::Build(pk, v, asked, layers, t, sc, out, info, clock, mid);
}

constexpr double kDeg = 0.017453292519943295769236907684886;
constexpr int kS = mapcompose::kSize;

wxlayer::View RadarView(double lat, double lon, double km)
{
    wxlayer::View v;
    double c = std::cos(lat * kDeg);
    if (c < 0.01) c = 0.01;
    v.homeLat = lat;
    v.homeLon = lon;
    v.rLat = std::min(std::max(km / 111.0, 0.001), 2.0);
    v.rLon = std::min(std::max(km / (111.0 * c), 0.001), 2.0);
    v.rotCos = 1.0f;
    v.rotSin = 0.0f;
    v.size = kS;
    return v;
}

// Runs of set pixels along a line of `n` mask entries at stride `step`: "start+width" per run.
std::string Runs(const std::vector<uint8_t>& m, size_t at, size_t step, int n)
{
    std::string out;
    int x = 0;
    while (x < n) {
        if (!m[at + (size_t)x * step]) { ++x; continue; }
        const int s = x;
        while (x < n && m[at + (size_t)x * step]) ++x;
        char b[24];
        std::snprintf(b, sizeof b, "%s%d+%d", out.empty() ? "" : " ", s, x - s);
        out += b;
    }
    return out.empty() ? "-" : out;
}

bool WritePpm(const std::string& path, const uint16_t* canvas)
{
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    std::fprintf(f, "P6\n%d %d\n255\n", kS, kS);
    for (int p = 0; p < kS * kS; ++p) {
        const uint16_t v = mapcompose::Swap(canvas[p]);   // canvas order back to logical RGB565
        const uint8_t rgb[3] = {(uint8_t)(((v >> 11) & 31) * 255 / 31), (uint8_t)(((v >> 5) & 63) * 255 / 63),
                                (uint8_t)((v & 31) * 255 / 31)};
        std::fwrite(rgb, 1, 3, f);
    }
    return std::fclose(f) == 0;
}

}  // namespace

int main(int argc, char** argv)
{
    if (argc != 2 && !(argc == 4 && std::strcmp(argv[2], "--out") == 0)) {
        std::printf("usage: test_map_s8 <fixtures dir> [--out <dir>]\n");
        return 2;
    }
    const std::string outDir = argc == 4 ? argv[3] : "";
    const std::vector<uint8_t> bytes = Read(std::string(argv[1]) + "/basemap/bend.bmpk");
    bmpk::Package pk;
    if (bytes.empty() || bmpk::Validate(bytes.data(), bytes.size(), &pk) != bmpk::Status::kOk) {
        std::printf("RIG: cannot read %s/basemap/bend.bmpk\n", argv[1]);
        return 2;
    }
    const size_t cap = (size_t)pk.maxVertices + 1;
    std::vector<int32_t> qx(cap), qy(cap);
    std::vector<double> x(cap), y(cap), cr(cap);
    std::vector<uint8_t> idx((size_t)kS * kS);
    maplayer::Scratch sc;
    sc.qx = qx.data(); sc.qy = qy.data(); sc.x = x.data(); sc.y = y.data(); sc.cross = cr.data();
    sc.cap = (uint32_t)cap; sc.idx = idx.data();

    const mapterrain::Level lv[] = {mapterrain::Level::Subtle, mapterrain::Level::Off, mapterrain::Level::Strong,
                                    mapterrain::Level::Xstrong};   // Subtle first: today's map, the reference
    const int miles[] = {5, 10, 50, 100};
    std::vector<uint16_t> full((size_t)kS * kS), terr((size_t)kS * kS);
    int off = 0;
    for (int mi : miles) {
        const wxlayer::View v = RadarView(pk.lat0Micro / 1e6, pk.lon0Micro / 1e6, mi * 1.609344);
        std::vector<std::vector<uint8_t>> masks;
        long linePx = 0;
        for (mapterrain::Level l : lv) {
            const mapterrain::Shading s = mapterrain::Of(l);
            maplayer::Theme t = maplayer::kThemes[maplayer::kThemeDark];
            t.terrainGain = s.gain;
            const uint8_t all = s.drawn ? maplayer::kLayersAll : (uint8_t)(maplayer::kLayersAll & ~maplayer::kLayerTerrain);
            const uint8_t terrOnly = s.drawn ? maplayer::kLayerTerrain : (uint8_t)0;
            if (!maplayer::Build(pk, v, 0, all, t, sc, full.data(), nullptr, nullptr, s.mid) ||
                !maplayer::Build(pk, v, 0, terrOnly, t, sc, terr.data(), nullptr, nullptr, s.mid)) {
                std::printf("RIG: Build refused Bend at %d mi (%s)\n", mi, mapterrain::Name(l));
                return 2;
            }
            std::vector<uint8_t> m((size_t)kS * kS);
            for (size_t p = 0; p < m.size(); ++p) m[p] = full[p] != terr[p];
            if (l == mapterrain::Level::Subtle)
                for (uint8_t b : m) linePx += b;
            masks.push_back(m);
            if (!outDir.empty()) {
                mapcompose::Map cm;
                mapcompose::Init(cm, Alloc, Free);
                mapcompose::Key k;
                k.view = v;
                k.radiusMi = mi;
                k.pkSeq = 1;
                k.terrainDrawn = s.drawn;
                k.terrainGain = s.gain;
                k.terrainMid = s.mid;
                mapcompose::Inputs in;
                in.build = &BuildFn;
                const uint16_t* pic = mapcompose::Frame(cm, &pk, k, nullptr, 0, in);
                char path[512];
                std::snprintf(path, sizeof path, "%s/s8-%03dmi-%s.ppm", outDir.c_str(), mi, mapterrain::Name(l));
                if (!pic || !WritePpm(path, pic)) {
                    std::printf("RIG: could not draw or write %s\n", path);
                    return 2;
                }
                mapcompose::Release(cm);
            }
        }
        if (linePx == 0) {
            std::printf("RIG: no line pixel at %d mi: the measure cannot see a line\n", mi);
            return 2;
        }
        int rowsSame = 0, colsSame = 0;
        for (int i = 0; i < kS; ++i) {
            bool r = true, c = true;
            const std::string r0 = Runs(masks[0], (size_t)i * kS, 1, kS), c0 = Runs(masks[0], (size_t)i, kS, kS);
            for (size_t j = 1; j < masks.size(); ++j) {
                r = r && Runs(masks[j], (size_t)i * kS, 1, kS) == r0;
                c = c && Runs(masks[j], (size_t)i, kS, kS) == c0;
            }
            rowsSame += r;
            colsSame += c;
        }
        const bool same = rowsSame == kS && colsSame == kS;
        off += same ? 0 : 1;
        std::printf("  %s S8C %3d mi: %ld line pixel(s) (Subtle); rows identical across the four values %d of %d, "
                    "columns %d of %d\n", same ? "ok  " : "FAIL", mi, linePx, rowsSame, kS, colsSame, kS);
        if (mi == 10)
            for (size_t j = 0; j < masks.size(); ++j)
                std::printf("  info corridor US 97, 10 mi, %-7s: centre row [%s]; centre column [%s]\n",
                            mapterrain::Name(lv[j]), Runs(masks[j], (size_t)(kS / 2) * kS, 1, kS).c_str(),
                            Runs(masks[j], (size_t)(kS / 2), kS, kS).c_str());
    }
    std::printf("test_map_s8: %s (%d zoom(s) off)\n", off ? "OFF-PREDICTION" : "as predicted", off);
    return off ? 1 : 0;
}
