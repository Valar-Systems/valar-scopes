// Step D's town-name rows (docs/blipscope-map-client-steps.md, "Step D's other half", (a)): the composer with the
// names switch, on the pinned Bend package.
//
//   DNP1  over D3's sweep (10, 50 and 150 mi; rotations 0 and 90): names OFF is byte-equal to maplayer::Build's output
//         swapped to the canvas's order, called directly with no names step; names ON differs from it at every view
//         where nPlaced > 0. Both counts are printed; no view with a name placed is the rig (the row could not see).
//         Its plant (S-DNP1 in test/host/sabotage_map_settings.py): Rebuild ignores `names` and draws them anyway: red.
//   DNP2  the composer, names on -> off -> on with frames between the changes: exactly one rebuild at each change and 0
//         on every other frame. Its plant (S-DNP2): `names` left out of Same(): 0 rebuilds at the changes, red.
//
//     test_map_names <fixtures dir: test/fixtures/wx>     exit 0 all pass; 1 a failure; 2 the rig
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "MapCompose.h"

namespace {

int failures = 0;

void check(bool ok, const char* row, const char* what)
{
    std::printf("  %s %-4s %s\n", ok ? "ok  " : "FAIL", row, what);
    if (!ok) ++failures;
}

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

int g_builds = 0;
bool BuildCounted(const bmpk::Package& pk, const wxlayer::View& v, uint32_t asked, uint8_t layers,
                  const maplayer::Theme& t, const maplayer::Scratch& sc, uint16_t* out, maplayer::BuildInfo* info,
                  maplayer::ClockUs clock, int mid)
{
    ++g_builds;
    return maplayer::Build(pk, v, asked, layers, t, sc, out, info, clock, mid);
}

constexpr double kDeg = 0.017453292519943295769236907684886;
constexpr int kS = mapcompose::kSize;

wxlayer::View RadarView(double lat, double lon, double km, int rotDeg)
{
    wxlayer::View v;
    double c = std::cos(lat * kDeg);
    if (c < 0.01) c = 0.01;
    v.homeLat = lat;
    v.homeLon = lon;
    v.rLat = std::min(std::max(km / 111.0, 0.001), 2.0);
    v.rLon = std::min(std::max(km / (111.0 * c), 0.001), 2.0);
    const float r = (float)((float)rotDeg * kDeg);
    v.rotCos = cosf(r);
    v.rotSin = sinf(r);
    v.size = kS;
    return v;
}

}  // namespace

int main(int argc, char** argv)
{
    if (argc != 2) {
        std::printf("usage: test_map_names <fixtures dir>\n");
        return 2;
    }
    const std::vector<uint8_t> bytes = Read(std::string(argv[1]) + "/basemap/bend.bmpk");
    bmpk::Package pk;
    if (bytes.empty() || bmpk::Validate(bytes.data(), bytes.size(), &pk) != bmpk::Status::kOk) {
        std::printf("RIG: cannot read %s/basemap/bend.bmpk\n", argv[1]);
        return 2;
    }
    mapcompose::Inputs in;
    in.build = &BuildCounted;
    char buf[256];

    // ---- DNP1
    {
        const size_t cap = (size_t)pk.maxVertices + 1;
        std::vector<int32_t> qx(cap), qy(cap);
        std::vector<double> x(cap), y(cap), cr(cap);
        std::vector<uint8_t> idx((size_t)kS * kS);
        std::vector<uint16_t> ref((size_t)kS * kS);
        maplayer::Scratch sc;
        sc.qx = qx.data(); sc.qy = qy.data(); sc.x = x.data(); sc.y = y.data(); sc.cross = cr.data();
        sc.cap = (uint32_t)cap; sc.idx = idx.data();
        const int miles[] = {10, 50, 150}, rots[] = {0, 90};
        int views = 0, offEqual = 0, withNames = 0, onDiffers = 0;
        for (int mi : miles)
            for (int rot : rots) {
                ++views;
                mapcompose::Key k;
                k.view = RadarView(pk.lat0Micro / 1e6, pk.lon0Micro / 1e6, mi * 1.609344, rot);
                k.radiusMi = mi;
                k.pkSeq = 1;
                if (!maplayer::Build(pk, k.view, 0, maplayer::kLayersAll, maplayer::kThemes[maplayer::kThemeDark], sc,
                                     ref.data())) {
                    std::printf("RIG: Build refused Bend at %d mi, rotation %d\n", mi, rot);
                    return 2;
                }
                // Each state in a FRESH Map, so DNP1 does not lean on Same() (DNP2's subject): S-DNP2 turns DNP2 red
                // and leaves DNP1 able to see.
                mapcompose::Map m, mo;
                mapcompose::Init(m, Alloc, Free);
                mapcompose::Init(mo, Alloc, Free);
                k.names = false;
                const uint16_t* off = mapcompose::Frame(m, &pk, k, nullptr, 0, in);
                bool eq = off != nullptr;
                for (size_t p = 0; eq && p < (size_t)kS * kS; ++p) eq = off[p] == mapcompose::Swap(ref[p]);
                offEqual += eq;
                k.names = true;
                const uint16_t* on = mapcompose::Frame(mo, &pk, k, nullptr, 0, in);
                if (on && mo.nPlaced > 0) {
                    ++withNames;
                    bool differs = false;
                    for (size_t p = 0; !differs && p < (size_t)kS * kS; ++p) differs = on[p] != mapcompose::Swap(ref[p]);
                    onDiffers += differs;
                }
                std::printf("  info DNP1 %3d mi rot %2d: names off %s; names on placed %d\n", mi, rot,
                            eq ? "byte-equal" : "DIFFERS", on ? mo.nPlaced : -1);
                mapcompose::Release(m);
                mapcompose::Release(mo);
            }
        if (withNames == 0) {
            std::printf("RIG: no view placed a name: DNP1 cannot see the names\n");
            return 2;
        }
        std::snprintf(buf, sizeof buf, "names off byte-equal to Build's own output, swapped: %d of %d views", offEqual,
                      views);
        check(offEqual == views, "DNP1", buf);
        std::snprintf(buf, sizeof buf, "names on differs from it at %d of the %d view(s) with nPlaced > 0", onDiffers,
                      withNames);
        check(onDiffers == withNames, "DNP1", buf);
    }

    // ---- DNP2
    {
        mapcompose::Map m;
        mapcompose::Init(m, Alloc, Free);
        mapcompose::Key k;
        k.view = RadarView(pk.lat0Micro / 1e6, pk.lon0Micro / 1e6, 50 * 1.609344, 0);
        k.radiusMi = 50;
        k.pkSeq = 1;
        const bool seq[] = {true, true, true, false, false, false, true, true, true};
        std::string got, want;
        for (int i = 0; i < 9; ++i) {
            k.names = seq[i];
            g_builds = 0;
            mapcompose::Frame(m, &pk, k, nullptr, 0, in);
            got += (char)('0' + g_builds);
            want += (i == 0 || seq[i] != seq[i - 1]) ? '1' : '0';
        }
        mapcompose::Release(m);
        std::snprintf(buf, sizeof buf, "rebuilds per frame, names on/off/on: %s (want %s: one at each change, 0 else)",
                      got.c_str(), want.c_str());
        check(got == want, "DNP2", buf);
    }

    std::printf("test_map_names: %s (%d failure(s))\n", failures ? "FAILED" : "all checks passed", failures);
    return failures ? 1 : 0;
}
