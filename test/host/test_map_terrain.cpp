// Step D's terrain rows (docs/blipscope-map-client-steps.md, Step D: D1 and D3).
//
//   D1   mapterrain::Resolve: each known value maps to its own row and no two collapse; Subtle is the Dark row's own
//        gain at mid 128 (today's map); every planted bad value (unset, "", "STRONG", "subtle ", "loud") reads Strong.
//        Its plant (S-D1 in test/host/sabotage_map_settings.py: the unknown branch returns Subtle) reads red.
//   D3   pixels: today's MapLayer.h (test/host/ref/MapLayer_main.h, main at 5d0c19f16682) and the new one at Subtle
//        build the pinned Bend package over the host sweep the CBF3-C thresholds used (10, 50 and 150 mi; rotations 0
//        and 90): BYTE-EQUAL images, and the count compared is printed. Off is byte-equal to today's build with
//        kLayerTerrain cleared. Strong and Extra strong are REPORTED (distinct colours, the share of disc pixels at 0),
//        never graded: the look is the owner's.
//        Its two controls run every time, each graded AS REGISTERED -- it turns D3 red, i.e. at least one of the six
//        images differs; the per-view counts are printed as information (the orchestrator, ~01:36Z 11-19): Subtle at
//        gain 0.0751; Off with the terrain layer left on. A control that turns nothing red makes the run the rig
//        (exit 2): the comparison could not see a change.
//        Off draws ZERO terrain pixels (the orchestrator's added row): Build reports no terrain level, and no pixel of
//        Off's image is a terrain ramp colour (the ramp at Off's own inputs, less the theme's own colours).
//
//     test_map_terrain <fixtures dir: test/fixtures/wx>     exit 0 all pass; 1 a failure; 2 the rig
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <set>
#include <string>
#include <vector>

#include "BasemapPackage.h"
#include "MapLayer.h"
#include "MapTerrain.h"
#include "ref/MapLayer_main.h"

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

constexpr double kDeg = 0.017453292519943295769236907684886;
constexpr int kS = maplayer::kSize;

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

struct Bufs {
    std::vector<int32_t> qx, qy;
    std::vector<double> x, y, cross;
    std::vector<uint8_t> idx;
    explicit Bufs(uint32_t maxV)
        : qx(maxV + 1), qy(maxV + 1), x(maxV + 1), y(maxV + 1), cross(maxV + 1), idx((size_t)kS * kS) {}
    maplayer::Scratch New()
    {
        maplayer::Scratch s;
        s.qx = qx.data(); s.qy = qy.data(); s.x = x.data(); s.y = y.data(); s.cross = cross.data();
        s.cap = (uint32_t)qx.size(); s.idx = idx.data();
        return s;
    }
    maplayer_ref::Scratch Ref()
    {
        maplayer_ref::Scratch s;
        s.qx = qx.data(); s.qy = qy.data(); s.x = x.data(); s.y = y.data(); s.cross = cross.data();
        s.cap = (uint32_t)qx.size(); s.idx = idx.data();
        return s;
    }
};

// Step D's build at a resolved level: the gain in a copy of the Dark row, Off clearing the terrain layer -- exactly
// what mapcompose::Rebuild passes.
bool BuildAt(const bmpk::Package& pk, const wxlayer::View& v, const mapterrain::Shading& s, Bufs& b,
             std::vector<uint16_t>& out, bool forceTerrain = false, maplayer::BuildInfo* info = nullptr)
{
    maplayer::Theme t = maplayer::kThemes[maplayer::kThemeDark];
    t.terrainGain = s.gain;
    const uint8_t layers = (s.drawn || forceTerrain) ? maplayer::kLayersAll
                                                      : (uint8_t)(maplayer::kLayersAll & ~maplayer::kLayerTerrain);
    return maplayer::Build(pk, v, pk.radiusMm, layers, t, b.New(), out.data(), info, nullptr, s.mid);
}

// The terrain ramp's colours at a shading's own inputs, less the theme's own colours and 0: a pixel holding one of
// these can only have come from S2.
std::set<uint16_t> RampOnly(const mapterrain::Shading& s)
{
    maplayer::Theme t = maplayer::kThemes[maplayer::kThemeDark];
    t.terrainGain = s.gain;
    std::set<uint16_t> r;
    for (int k = 0; k < 256; ++k) r.insert(maplayer::To565(maplayer::TerrainRgb(t, (uint8_t)k, s.mid)));
    for (const maplayer::Rgb8 c : {t.land, t.water, t.river, t.road}) r.erase(maplayer::To565(c));
    r.erase(0);
    return r;
}

long CountIn(const std::vector<uint16_t>& img, const std::set<uint16_t>& colours)
{
    long n = 0;
    for (uint16_t p : img) n += colours.count(p) ? 1 : 0;
    return n;
}

bool BuildRef(const bmpk::Package& pk, const wxlayer::View& v, uint8_t layers, Bufs& b, std::vector<uint16_t>& out)
{
    return maplayer_ref::Build(pk, v, pk.radiusMm, layers, maplayer_ref::kThemes[maplayer_ref::kThemeDark], b.Ref(),
                               out.data());
}

}  // namespace

int main(int argc, char** argv)
{
    if (argc != 2) {
        std::printf("usage: test_map_terrain <fixtures dir>\n");
        return 2;
    }
    char buf[256];

    // ---- D1
    using mapterrain::Level;
    const mapterrain::Shading off = mapterrain::Resolve("off"), subtle = mapterrain::Resolve("subtle"),
                              strong = mapterrain::Resolve("strong"), xstrong = mapterrain::Resolve("xstrong");
    check(!off.drawn && off.gain == maplayer::kThemes[maplayer::kThemeDark].terrainGain && off.mid == 128, "D1",
          "off: the terrain layer is not drawn (Q-T2); Subtle's inputs, never read");
    check(subtle.drawn && subtle.gain == maplayer::kThemes[maplayer::kThemeDark].terrainGain && subtle.mid == 128, "D1",
          "subtle: drawn, the Dark row's own gain (0.075), mid 128 -- today's map");
    check(strong.drawn && strong.gain == 0.45 && strong.mid == 180, "D1", "strong: drawn, gain 0.45, mid 180");
    check(xstrong.drawn && xstrong.gain == 0.45 && xstrong.mid == 128, "D1", "xstrong: drawn, gain 0.45, mid 128");
    {
        const mapterrain::Shading all[] = {off, subtle, strong, xstrong};
        int collapsed = 0;
        for (int i = 0; i < 4; ++i)
            for (int j = i + 1; j < 4; ++j)
                if (all[i].drawn == all[j].drawn && all[i].gain == all[j].gain && all[i].mid == all[j].mid) ++collapsed;
        check(collapsed == 0, "D1", "no two of the four values collapse to one row");
    }
    const char* bad[] = {nullptr, "", "STRONG", "Strong", "subtle ", " off", "loud", "xstrong\n", "1"};
    int badStrong = 0;
    for (const char* s : bad) {
        const mapterrain::Shading r = mapterrain::Resolve(s);
        badStrong += (r.drawn == strong.drawn && r.gain == strong.gain && r.mid == strong.mid);
    }
    std::snprintf(buf, sizeof buf, "%d of %d planted bad values read Strong", badStrong, (int)(sizeof bad / sizeof bad[0]));
    check(badStrong == (int)(sizeof bad / sizeof bad[0]), "D1", buf);
    const Level lvls[] = {Level::Off, Level::Subtle, Level::Strong, Level::Xstrong};
    int round = 0;
    for (Level l : lvls) round += mapterrain::Classify(mapterrain::Name(l)) == l;
    check(round == 4, "D1", "each level's Name() classifies back to itself");

    // ---- D3
    const std::vector<uint8_t> bytes = Read(std::string(argv[1]) + "/basemap/bend.bmpk");
    bmpk::Package pk;
    if (bytes.empty() || bmpk::Validate(bytes.data(), bytes.size(), &pk) != bmpk::Status::kOk) {
        std::printf("RIG: cannot read %s/basemap/bend.bmpk\n", argv[1]);
        return 2;
    }
    Bufs b(pk.maxVertices);
    std::vector<uint16_t> ref((size_t)kS * kS), mine((size_t)kS * kS);
    const int miles[] = {10, 50, 150}, rots[] = {0, 90};
    int compared = 0, equalSubtle = 0, equalOff = 0, ctlGain = 0, ctlOff = 0, offLevel = 0;
    long offRamp = 0, subtleRamp = 0;
    const std::set<uint16_t> rampOff = RampOnly(off), rampSubtle = RampOnly(subtle);
    for (int mi : miles)
        for (int rot : rots) {
            const wxlayer::View v = RadarView(pk.lat0Micro / 1e6, pk.lon0Micro / 1e6, mi * 1.609344, rot);
            ++compared;
            // Subtle against today's map
            if (!BuildRef(pk, v, maplayer_ref::kLayersAll, b, ref) || !BuildAt(pk, v, subtle, b, mine)) {
                std::printf("RIG: a build refused Bend at %d mi, rotation %d\n", mi, rot);
                return 2;
            }
            equalSubtle += ref == mine;
            subtleRamp += CountIn(mine, rampSubtle);
            // CONTROL: Subtle at gain 0.0751
            mapterrain::Shading nudged = subtle;
            nudged.gain = 0.0751;
            BuildAt(pk, v, nudged, b, mine);
            const bool dGain = ref != mine;
            ctlGain += dGain;
            // Off against today's build with the terrain layer cleared, and Off's own terrain: none
            BuildRef(pk, v, (uint8_t)(maplayer_ref::kLayersAll & ~maplayer_ref::kLayerTerrain), b, ref);
            maplayer::BuildInfo oi;
            BuildAt(pk, v, off, b, mine, false, &oi);
            equalOff += ref == mine;
            offLevel += oi.level == -1;
            offRamp += CountIn(mine, rampOff);
            // CONTROL: Off with the terrain layer left on
            BuildAt(pk, v, off, b, mine, true);
            const bool dOff = ref != mine;
            ctlOff += dOff;
            std::printf("  info controls %3d mi rot %2d: gain 0.0751 %s; Off with terrain on %s\n", mi, rot,
                        dGain ? "differs" : "equal", dOff ? "differs" : "equal");
            // Strong and Extra strong: reported, never graded
            for (const auto& lv : {std::make_pair("strong", strong), std::make_pair("xstrong", xstrong)}) {
                BuildAt(pk, v, lv.second, b, mine);
                std::set<uint16_t> colours(mine.begin(), mine.end());
                long zero = 0, disc = 0;
                for (int y = 0; y < kS; ++y)
                    for (int x = 0; x < kS; ++x)
                        if (!wxlayer::OffDisc(x, y, kS)) {
                            ++disc;
                            zero += mine[(size_t)y * kS + x] == 0;
                        }
                std::printf("  info %-7s %3d mi rot %2d: %zu distinct colours; %.2f%% of disc pixels at 0\n", lv.first,
                            mi, rot, colours.size(), 100.0 * zero / (disc ? disc : 1));
            }
        }
    // Each control graded as registered: it turns D3 red, i.e. at least one image differs.
    if (ctlGain < 1 || ctlOff < 1) {
        std::printf("RIG: a control turned nothing red (gain 0.0751: %d of %d differ; Off with terrain on: %d of %d) -- "
                    "the comparison cannot see a change\n", ctlGain, compared, ctlOff, compared);
        return 2;
    }
    std::snprintf(buf, sizeof buf, "Off draws zero terrain pixels: no terrain level in %d of %d builds; %ld ramp-only "
                  "pixel(s) (Subtle's images: %ld)", offLevel, compared, offRamp, subtleRamp);
    check(offLevel == compared && offRamp == 0 && subtleRamp > 0, "D3", buf);
    std::snprintf(buf, sizeof buf, "Subtle byte-equal to today's map: %d of %d images (controls: %d and %d of %d differ, "
                  "each needs >= 1)", equalSubtle, compared, ctlGain, ctlOff, compared);
    check(equalSubtle == compared && compared == 6, "D3", buf);
    std::snprintf(buf, sizeof buf, "Off byte-equal to today's build with kLayerTerrain cleared: %d of %d images", equalOff,
                  compared);
    check(equalOff == compared, "D3", buf);

    std::printf("test_map_terrain: %s (%d failure(s))\n", failures ? "FAILED" : "all checks passed", failures);
    return failures ? 1 : 0;
}
