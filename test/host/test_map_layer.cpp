// The Blipscope's map layer, step M's host rows (docs/blipscope-map-client-steps.md): include/MapLayer.h held to
// CM1-CM8, CM10 and CM11, frozen before the code (2026-11-07, accepted ~19:08Z; CM2/CM3's cases, CM10, the count
// and CM7's view registered at 21:26Z and 21:33Z, before any code; CM7's water-only build and CM11 after the ~23:23Z
// ruling). CM9 is check_map_layer.py's. Its sabotages: python -I
// test/host/sabotage_map_layer.py, each failing exactly its set.
//
//     test_map_layer <test/fixtures/wx>
//
// Exit 0 all pass, 1 any fail, 2 the rig (a fixture missing, or a case count other than 160). Prints no coordinate:
// the packages' origins are read from their headers and used, never printed.

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <string>
#include <tuple>
#include <vector>

#include "BasemapPackage.h"
#include "MapLayer.h"
#include "RadarProjection.h"
#include "WxBasemap.h"

namespace {

constexpr int kCases = 160;
constexpr int kS = maplayer::kSize;
constexpr double kDegToRad = 0.017453292519943295769236907684886;   // Arduino's DEG_TO_RAD, as radians() uses it

int cases = 0, failures = 0;
void check(const std::string& name, bool ok, const std::string& what)
{
    ++cases;
    if (!ok) ++failures;
    std::printf("  %s %s %s\n", ok ? "ok  " : "FAIL", name.c_str(), what.c_str());
}

std::string fmt(const char* f, ...)
{
    char s[512];
    va_list ap;
    va_start(ap, f);
    std::vsnprintf(s, sizeof s, f, ap);
    va_end(ap);
    return s;
}

std::string root;
bool rig = false;

std::vector<uint8_t> Read(const std::string& rel)
{
    std::vector<uint8_t> b;
    FILE* f = std::fopen((root + "/" + rel).c_str(), "rb");
    if (!f) {
        std::printf("the RIG: cannot read %s\n", rel.c_str());
        rig = true;
        return b;
    }
    uint8_t buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) b.insert(b.end(), buf, buf + n);
    std::fclose(f);
    return b;
}

// The radar's view, as AircraftManager::Initialise and ApplyZoom make it (the 0.01 cos floor, the 0.001 and 2.0 degree
// clamps, radians() through DEG_TO_RAD) and its window-up rotation (radarUpDeg -> rotRad -> cosf/sinf), transcribed.
wxlayer::View RadarView(double homeLat, double homeLon, double radiusKm, int rotDeg)
{
    wxlayer::View v;
    double cosLat = std::cos(homeLat * kDegToRad);
    if (cosLat < 0.01) cosLat = 0.01;
    v.homeLat = homeLat;
    v.homeLon = homeLon;
    v.rLat = std::min(std::max(radiusKm / 111.0, 0.001), 2.0);
    v.rLon = std::min(std::max(radiusKm / (111.0 * cosLat), 0.001), 2.0);
    const float rotRad = (float)((float)rotDeg * kDegToRad);
    v.rotCos = cosf(rotRad);
    v.rotSin = sinf(rotRad);
    v.size = kS;
    return v;
}

constexpr double kMi = 1.609344;

// Scratch for a package, sized from it.
struct Buffers {
    std::vector<int32_t> qx, qy;
    std::vector<double> x, y, cross;
    std::vector<uint8_t> idx;
    std::vector<uint16_t> out;
    maplayer::Scratch sc;
    explicit Buffers(const bmpk::Package& pk)
        : qx(pk.maxVertices + 1), qy(pk.maxVertices + 1), x(pk.maxVertices + 1), y(pk.maxVertices + 1),
          cross(pk.maxVertices + 1), idx((size_t)kS * kS), out((size_t)kS * kS)
    {
        sc.qx = qx.data();
        sc.qy = qy.data();
        sc.x = x.data();
        sc.y = y.data();
        sc.cross = cross.data();
        sc.cap = pk.maxVertices + 1;
        sc.idx = idx.data();
    }
};

const maplayer::Theme& Dark() { return maplayer::kThemes[maplayer::kThemeDark]; }

// Every vertex of every ring, river and road, in walking order.
std::vector<std::pair<int32_t, int32_t>> AllVertices(const bmpk::Package& pk)
{
    std::vector<std::pair<int32_t, int32_t>> all;
    Buffers b(pk);
    const auto take = [&](int n) {
        for (int k = 0; k < n; ++k) all.push_back(std::make_pair(b.qx[k], b.qy[k]));
    };
    bmpk::ForEachRing(pk, b.sc.qx, b.sc.qy, b.sc.cap, [&](int n) { take(n); });
    bmpk::ForEachRiver(pk, b.sc.qx, b.sc.qy, b.sc.cap, [&](int, int n) { take(n); });
    bmpk::ForEachRoad(pk, b.sc.qx, b.sc.qy, b.sc.cap, [&](int, int n) { take(n); });
    return all;
}

// CM2's and CM10's cases: views x rotations x home offsets from the package's origin (degrees, lat then lon).
const int kViewsMi[] = {5, 10, 25, 50, 100};
const int kRots[] = {0, 30, 90, 211};
const double kOffsets[][2] = {{0, 0}, {0.0049, -0.0049}, {-0.0049, 0.0031}};

// ------------------------------------------------------------------------------------------------------------ CM1
void Cm1()
{
    struct Neg {
        const char* file;
        bmpk::Status why;
    };
    // The same 27 reasons as test_wx_basemap.cpp's PK table (the README's set, A10.11 and A12.15).
    const Neg kNeg[] = {
        {"neg-short", bmpk::Status::kHeader},           {"neg-magic", bmpk::Status::kMagic},
        {"neg-version", bmpk::Status::kVersion},        {"neg-flags", bmpk::Status::kFlags},
        {"neg-total", bmpk::Status::kTotal},            {"neg-crc", bmpk::Status::kCrc},
        {"neg-nsec0", bmpk::Status::kSectionCount},     {"neg-nsec33", bmpk::Status::kSectionCount},
        {"neg-bounds", bmpk::Status::kSectionBounds},   {"neg-overlap", bmpk::Status::kOverlap},
        {"neg-gap", bmpk::Status::kGap},                {"neg-dup", bmpk::Status::kDuplicate},
        {"neg-noprov", bmpk::Status::kNoProvenance},    {"neg-varint6", bmpk::Status::kVarint},
        {"neg-overlong", bmpk::Status::kVarint},        {"neg-walk", bmpk::Status::kVertexRange},
        {"neg-ring2", bmpk::Status::kVertexCount},      {"neg-line1", bmpk::Status::kVertexCount},
        {"neg-count", bmpk::Status::kBody},             {"neg-text", bmpk::Status::kText},
        {"neg-rivername", bmpk::Status::kRiverName},    {"neg-roadclass", bmpk::Status::kRoadClass},
        {"neg-terrainlen", bmpk::Status::kTerrainLength}, {"neg-terrainN0", bmpk::Status::kTerrainLength},
        {"neg-reserved", bmpk::Status::kReserved},      {"neg-terrain-count", bmpk::Status::kTerrainCount},
        {"neg-prov-count", bmpk::Status::kProvenanceCount},
    };
    for (const Neg& n : kNeg) {
        const std::vector<uint8_t> b = Read(std::string("basemap/") + n.file + ".bmpk");
        bmpk::Package pk;
        const maplayer::Verdict vd = maplayer::Load(b.data(), b.size(), &pk);
        // Drawn only past the gate, as the client draws; the out buffer starts as a sentinel and must keep it.
        std::vector<uint16_t> out((size_t)kS * kS, 0xA5A5);
        int written = 0;
        if (vd.accepted) {
            Buffers buf(pk);
            maplayer::Build(pk, RadarView(0, 0, 60 * kMi, 0), 0, maplayer::kLayersAll, Dark(), buf.sc, out.data());
        }
        for (uint16_t p : out) written += p != 0xA5A5;
        check(std::string("CM1-") + n.file, !vd.accepted && vd.why == n.why && written == 0,
              fmt("%s (want %s), %d pixels written", bmpk::Name(vd.why), bmpk::Name(n.why), written));
    }
    for (const char* name : {"bend", "syn-min"}) {
        const std::vector<uint8_t> b = Read(std::string("basemap/") + name + ".bmpk");
        bmpk::Package pk;
        const maplayer::Verdict vd = maplayer::Load(b.data(), b.size(), &pk);
        bool built = false;
        if (vd.accepted) {
            Buffers buf(pk);
            built = maplayer::Build(pk, RadarView(pk.lat0Micro / 1e6, pk.lon0Micro / 1e6, 60 * kMi, 0), 0,
                                    maplayer::kLayersAll, Dark(), buf.sc, buf.out.data());
        }
        check(std::string("CM1-") + name, vd.accepted && built, fmt("%s, built %d", bmpk::Name(vd.why), (int)built));
    }
}

// ------------------------------------------------------------------------------------------------------------ CM2
// The reference: wxbase::Mapper (the Weatherscope's section 9, north-up, in double), then Project's rotation in double.
void Reference(const bmpk::Package& pk, const wxlayer::View& v, int32_t qx, int32_t qy, double* x, double* y)
{
    wxrender::View wv;
    wv.homeLat = v.homeLat;
    wv.homeLon = v.homeLon;
    wv.rLat = v.rLat;
    wv.rLon = v.rLon;
    const wxbase::Mapper m(pk, wv);
    double nx, ny;
    m.ToScreen(qx, qy, &nx, &ny);
    const double C = v.size / 2.0, c = v.rotCos, s = v.rotSin;
    *x = C + (nx - C) * c + (ny - C) * s;
    *y = C - (nx - C) * s + (ny - C) * c;
}

void Cm2(const bmpk::Package& pk)
{
    const std::vector<std::pair<int32_t, int32_t>> all = AllVertices(pk);
    const double lat0 = pk.lat0Micro / 1e6, lon0 = pk.lon0Micro / 1e6;
    for (int mi : kViewsMi)
        for (int rot : kRots)
            for (int o = 0; o < 3; ++o) {
                const wxlayer::View v = RadarView(lat0 + kOffsets[o][0], lon0 + kOffsets[o][1], mi * kMi, rot);
                const maplayer::AffineF f = maplayer::Fast(maplayer::AffineFor(pk, pk.radiusMm, v));
                double worst = 0;
                for (const auto& q : all) {
                    float fx, fy;
                    double rx, ry;
                    maplayer::ToScreen(f, q.first, q.second, &fx, &fy);
                    Reference(pk, v, q.first, q.second, &rx, &ry);
                    worst = std::max(worst, std::max(std::fabs(fx - rx), std::fabs(fy - ry)));
                }
                check(fmt("CM2-%dmi-r%d-o%d", mi, rot, o), worst <= 0.01,
                      fmt("%d vertices, max error %.6f px (<= 0.01)", (int)all.size(), worst));
            }
}

// ------------------------------------------------------------------------------------------------------------ CM3
uint32_t Lcg(uint32_t* s)
{
    *s = *s * 1664525u + 1013904223u;
    return *s;
}

void Cm3(const bmpk::Package& pk)
{
    const double lat0 = pk.lat0Micro / 1e6, lon0 = pk.lon0Micro / 1e6;
    const double c0 = std::fmax(std::cos(lat0 * (maplayer::kPi / 180)), 0.01), rMm = pk.radiusMm;
    // The 20 zero-offset cases, then the 16 offset cases: the two offsets at 5 and 10 mi (registered ~21:40Z).
    std::vector<std::tuple<int, int, int>> list;
    for (int mi : kViewsMi)
        for (int rot : kRots) list.push_back(std::make_tuple(mi, rot, 0));
    for (int mi : {5, 10})
        for (int rot : kRots)
            for (int o = 1; o < 3; ++o) list.push_back(std::make_tuple(mi, rot, o));
    uint32_t seed = 20261107u;
    for (const auto& cs : list) {
        const int mi = std::get<0>(cs), rot = std::get<1>(cs), o = std::get<2>(cs);
        const wxlayer::View v = RadarView(lat0 + kOffsets[o][0], lon0 + kOffsets[o][1], mi * kMi, rot);
        const maplayer::AffineF f = maplayer::Fast(maplayer::AffineFor(pk, pk.radiusMm, v));
        int kept = 0, tries = 0, worst = 0;
        while (kept < 10000 && tries < 400000) {
            ++tries;
            const double u = Lcg(&seed) / 4294967296.0, w = Lcg(&seed) / 4294967296.0;
            const double lat = v.homeLat + (2 * u - 1) * v.rLat, lon = v.homeLon + (2 * w - 1) * v.rLon;
            const std::pair<int, int> pl = radarproj::Project((float)lat, (float)lon, v.homeLat, v.homeLon, v.rLat,
                                                              v.rLon, v.rotSin, v.rotCos, kS);
            if (pl.first < 0 || pl.first >= kS || pl.second < 0 || pl.second >= kS ||
                wxlayer::OffDisc(pl.first, pl.second, kS))
                continue;
            // The point as a builder would store it (format section 2): the package's own frame, rounded.
            const double e = (lon - lon0) * maplayer::kKmPerDeg * c0, s = (lat0 - lat) * maplayer::kKmPerDeg;
            const double qxd = std::floor(e * 1e6 / rMm * 32767 + 0.5), qyd = std::floor(s * 1e6 / rMm * 32767 + 0.5);
            if (std::fabs(qxd) > 32767 || std::fabs(qyd) > 32767) continue;   // outside the package's square
            float fx, fy;
            maplayer::ToScreen(f, (int32_t)qxd, (int32_t)qyd, &fx, &fy);
            const int dx = std::abs(maplayer::Rnd(fx) - pl.first), dy = std::abs(maplayer::Rnd(fy) - pl.second);
            worst = std::max(worst, std::max(dx, dy));
            ++kept;
        }
        check(fmt("CM3-%dmi-r%d-o%d", mi, rot, o), kept == 10000 && worst <= 1,
              fmt("%d points, worst |d| %d px (<= 1)", kept, worst));
    }
}

// ------------------------------------------------------------------------------------------------------------ CM4
void Cm4(const bmpk::Package& pk)
{
    struct Row {
        int mi, primary, secondary;
    };
    const Row kRows[] = {{5, 0, 67}, {10, 0, 74}, {25, 4, 173}, {50, 4, 308}, {60, 4, 324}};
    const uint16_t road = maplayer::To565(Dark().road);
    for (const Row& r : kRows) {
        Buffers b(pk);
        maplayer::BuildInfo bi;
        const wxlayer::View v = RadarView(pk.lat0Micro / 1e6, pk.lon0Micro / 1e6, r.mi * kMi, 0);
        const bool ok = maplayer::Build(pk, v, pk.radiusMm, maplayer::kLayersAll, Dark(), b.sc, b.out.data(), &bi);
        int roadPx = 0;
        for (int y = 0; y < kS; ++y)
            for (int x = 0; x < kS; ++x)
                if (!wxlayer::OffDisc(x, y, kS) && b.out[(size_t)y * kS + x] == road) ++roadPx;
        const bool want2 = r.primary == 0;
        check(fmt("CM4-%dmi", r.mi),
              ok && bi.primaryInView == r.primary && bi.secondaryInView == r.secondary && bi.secondaryDrawn == want2 &&
                  (want2 ? bi.secondary > 0 && roadPx > 0 : bi.secondary == 0),
              fmt("primary/secondary in view %d/%d (want %d/%d); secondary %s (want %s); %d secondary drawn, %d road px",
                  bi.primaryInView, bi.secondaryInView, r.primary, r.secondary, bi.secondaryDrawn ? "draws" : "doesn't",
                  want2 ? "draws" : "doesn't", bi.secondary, roadPx));
    }
}

// ------------------------------------------------------------------------------------------------------------ CM5
void Cm5()
{
    std::set<uint32_t> c8, c565;
    for (int v = 0; v < 256; ++v) {
        const maplayer::Rgb8 c = maplayer::TerrainRgb(Dark(), (uint8_t)v);
        c8.insert(((uint32_t)c.r << 16) | ((uint32_t)c.g << 8) | c.b);
        c565.insert(maplayer::To565(c));
    }
    const int want[4][4] = {{0, 0, 0, 0}, {128, 6, 8, 10}, {180, 10, 12, 14}, {255, 16, 18, 20}};
    bool samples = true;
    std::string got;
    for (const auto& w : want) {
        const maplayer::Rgb8 c = maplayer::TerrainRgb(Dark(), (uint8_t)w[0]);
        samples = samples && c.r == w[1] && c.g == w[2] && c.b == w[3];
        got += fmt(" %d->(%d,%d,%d)", w[0], c.r, c.g, c.b);
    }
    check("CM5", c8.size() == 21 && c565.size() == 9 && samples,
          fmt("%d 8-bit and %d RGB565 colours (want 21, 9);%s", (int)c8.size(), (int)c565.size(), got.c_str()));
}

// ------------------------------------------------------------------------------------------------------------ CM6
void Cm6(const bmpk::Package& pk)
{
    const wxlayer::View v = RadarView(0, 0, 96.56064, 0);   // syn-order: origin = home, R = the 60 mi view
    const auto build = [&](uint8_t layers, std::vector<uint16_t>* out) {
        Buffers b(pk);
        const bool ok = maplayer::Build(pk, v, pk.radiusMm, layers, Dark(), b.sc, b.out.data());
        *out = b.out;
        return ok;
    };
    std::vector<uint16_t> full, roads, rivers, water, bare;
    bool ok = build(maplayer::kLayersAll, &full);
    ok = build(maplayer::kLayerTerrain | maplayer::kLayerRoads, &roads) && ok;
    ok = build(maplayer::kLayerTerrain | maplayer::kLayerRivers, &rivers) && ok;
    ok = build(maplayer::kLayerTerrain | maplayer::kLayerWater, &water) && ok;
    ok = build(maplayer::kLayerTerrain, &bare) && ok;
    const uint16_t cRoad = maplayer::To565(Dark().road), cRiver = maplayer::To565(Dark().river),
                   cWater = maplayer::To565(Dark().water);
    int bad = 0, roadRiver = 0, roadWater = 0, riverWater = 0;
    for (size_t p = 0; p < full.size(); ++p) {
        const bool r = roads[p] == cRoad, v2 = rivers[p] == cRiver, w = water[p] == cWater;
        roadRiver += r && v2;
        roadWater += r && w;
        riverWater += v2 && w;
        const uint16_t want = r ? cRoad : v2 ? cRiver : w ? cWater : bare[p];
        bad += full[p] != want;
    }
    // The control: the fixture must overlap each pair, or the order could not show.
    check("CM6", ok && bad == 0 && roadRiver > 0 && roadWater > 0 && riverWater > 0,
          fmt("%d pixels off S3's order; overlaps road/river %d, road/water %d, river/water %d", bad, roadRiver,
              roadWater, riverWater));
}

// ------------------------------------------------------------------------------------------------------------ CM7
void Cm7(std::vector<uint8_t> b)
{
    // syn-min with its header's radius halved and its CRC recomputed: a valid package covering a smaller disc.
    const uint32_t asked = (uint32_t)b[16] | ((uint32_t)b[17] << 8) | ((uint32_t)b[18] << 16) | ((uint32_t)b[19] << 24);
    const uint32_t half = asked / 2;
    for (int k = 0; k < 4; ++k) b[16 + k] = (uint8_t)(half >> (8 * k));
    const uint32_t crc = bmpk::detail::Crc32ZeroingField(b.data(), (uint32_t)b.size());
    for (int k = 0; k < 4; ++k) b[28 + k] = (uint8_t)(crc >> (8 * k));
    bmpk::Package pk;
    const maplayer::Verdict vd = maplayer::Load(b.data(), b.size(), &pk);
    if (!vd.accepted) {
        check("CM7", false, fmt("the halved package was refused: %s", bmpk::Name(vd.why)));
        return;
    }
    // Zoomed to the asked radius / 16 (about 6 km), so the ring covers thousands of pixels and a 2x scale shows.
    const wxlayer::View v = RadarView(pk.lat0Micro / 1e6, pk.lon0Micro / 1e6, asked / 1e6 / 16, 0);
    Buffers bu(pk);
    maplayer::BuildInfo bi;
    // Terrain and water only (Q-MC9, ruled ~23:23Z): S3 draws the river and the road over the ring, and they are not
    // what CM7 grades.
    const bool ok = maplayer::Build(pk, v, asked, maplayer::kLayerTerrain | maplayer::kLayerWater, Dark(), bu.sc,
                                    bu.out.data(), &bi);
    // The reference ring, from the header's radius, filled by the same centre rule: its pixels must be the layer's water.
    std::vector<double> rx, ry, cross;
    bmpk::ForEachRing(pk, bu.sc.qx, bu.sc.qy, bu.sc.cap, [&](int n) {
        for (int k = 0; k < n; ++k) {
            double x, y;
            Reference(pk, v, bu.sc.qx[k], bu.sc.qy[k], &x, &y);
            rx.push_back(x);
            ry.push_back(y);
        }
    });
    cross.resize(rx.size() + 1);
    std::vector<uint8_t> ref((size_t)kS * kS, 0);
    wxbase::FillRing(rx.data(), ry.data(), (int)rx.size(), cross.data(), ref.data(), 1);
    const uint16_t cWater = maplayer::To565(Dark().water);
    int refPx = 0, layerPx = 0, both = 0;
    for (int y = 0; y < kS; ++y)
        for (int x = 0; x < kS; ++x) {
            if (wxlayer::OffDisc(x, y, kS)) continue;
            const size_t p = (size_t)y * kS + x;
            refPx += ref[p] == 1;
            layerPx += bu.out[p] == cWater;
            both += ref[p] == 1 && bu.out[p] == cWater;
        }
    // Float against double may move an edge pixel: within 2 % of the reference, and the reference's area.
    const bool near = refPx > 0 && std::abs(layerPx - refPx) * 50 <= refPx && std::abs(both - refPx) * 50 <= refPx;
    check("CM7", ok && bi.headerMm == half && bi.askedMm == asked && near,
          fmt("header %u mm, asked %u mm; water %d px against the header-radius reference %d px (%d shared)",
              (unsigned)bi.headerMm, (unsigned)bi.askedMm, layerPx, refPx, both));
}

// ------------------------------------------------------------------------------------------------------------ CM8
void Cm8(const bmpk::Package& pk)
{
    Buffers b(pk);
    const wxlayer::View v = RadarView(pk.lat0Micro / 1e6, pk.lon0Micro / 1e6, 60 * kMi, 0);
    const bool ok = maplayer::Build(pk, v, pk.radiusMm, maplayer::kLayersAll, Dark(), b.sc, b.out.data());
    int on = 0, off = 0, offLit = 0;
    for (int y = 0; y < kS; ++y)
        for (int x = 0; x < kS; ++x) {
            if (wxlayer::OffDisc(x, y, kS)) {
                ++off;
                offLit += b.out[(size_t)y * kS + x] != 0;
            } else {
                ++on;
            }
        }
    check("CM8", ok && on == 45244 && off == 12356 && offLit == 0,
          fmt("%d on the disc, %d off it (want 45,244 and 12,356); %d off-disc pixels not 0", on, off, offLit));
}

// ----------------------------------------------------------------------------------------------------------- CM10
void Cm10(const bmpk::Package& pk)
{
    struct Row {
        int mi;
        uint32_t levelMm;
    };
    const Row kRows[] = {{5, 24743664}, {10, 24743664}, {25, 49487328}, {50, 98974656}, {60, 98974656}, {100, 98974656}};
    const double lat0 = pk.lat0Micro / 1e6, lon0 = pk.lon0Micro / 1e6;
    const double c0 = std::fmax(std::cos(lat0 * (maplayer::kPi / 180)), 0.01);
    bmpk::Level levels[8];
    const int nl = bmpk::Levels(pk, levels, 8);
    for (const Row& r : kRows) {
        int wrongLevel = 0, mismatch = 0;
        long band = 0, offLevel = 0;
        for (int rot : kRots)
            for (int o = 0; o < 3; ++o) {
                const wxlayer::View v = RadarView(lat0 + kOffsets[o][0], lon0 + kOffsets[o][1], r.mi * kMi, rot);
                bmpk::Level got;
                bool covers = false;
                const int k = maplayer::ChooseLevel(pk, v, &got, &covers);
                if (k < 0 || got.radiusMm != r.levelMm) ++wrongLevel;
                // The reference samples the EXPECTED level, independently of the layer's choice.
                const bmpk::Level* want = nullptr;
                for (int i = 0; i < nl && i < 8; ++i)
                    if (levels[i].radiusMm == r.levelMm) want = &levels[i];
                if (!want || k < 0) {
                    ++mismatch;
                    continue;
                }
                const double C = kS / 2.0, c = v.rotCos, s = v.rotSin, pix = 2.0 * want->radiusMm / 1e6 / want->n;
                maplayer::ForEachLevelPixel(pk, got, v, [&](int x, int y, int32_t i, int32_t j) {
                    if (wxlayer::OffDisc(x, y, kS)) return;
                    const double X = x + 0.5 - C, Y = y + 0.5 - C;
                    const double u = c * X - s * Y, w = s * X + c * Y;
                    const double lon = v.homeLon + u * v.rLon / C, lat = v.homeLat - w * v.rLat / C;
                    const double e = (lon - lon0) * maplayer::kKmPerDeg * c0, sk = (lat0 - lat) * maplayer::kKmPerDeg;
                    const double fi = e / pix + want->n / 2.0, fj = sk / pix + want->n / 2.0;
                    if (std::fabs(fi - std::floor(fi + 0.5)) < 1e-6 || std::fabs(fj - std::floor(fj + 0.5)) < 1e-6) {
                        ++band;
                        return;
                    }
                    const int ri = (int)std::floor(fi), rj = (int)std::floor(fj);
                    const bool rin = ri >= 0 && ri < want->n && rj >= 0 && rj < want->n;
                    const bool gin = i >= 0 && i < got.n && j >= 0 && j < got.n;
                    offLevel += !rin;
                    if (rin != gin || (rin && (ri != i || rj != j))) ++mismatch;
                });
            }
        check(fmt("CM10-%dmi", r.mi), wrongLevel == 0 && mismatch == 0,
              fmt("level %u mm; %d of 12 cases chose another; %d pixel mismatches; printed, not graded: band %ld, "
                  "off-level %ld",
                  (unsigned)r.levelMm, wrongLevel, mismatch, band, offLevel));
    }
}

// ----------------------------------------------------------------------------------------------------------- CM11
// The layer's line against wxraster::Line, pixel for pixel, where both draw: rows 24-193, on pixels inside both disc
// tests (the layer's pixel-centre OffDisc, the Weatherscope's InMap). Ruled ~23:23Z, so the copy cannot drift.
class Lit : public wxraster::Sink {
public:
    std::vector<uint8_t> img;
    Lit() : img((size_t)kS * kS, 0) {}
    void Put(int x, int y, wxraster::Rgb) override { img[(size_t)y * kS + x] = 1; }
};

void Cm11(const bmpk::Package& pk)
{
    for (int mi : kViewsMi)
        for (int rot : kRots) {
            const wxlayer::View v = RadarView(pk.lat0Micro / 1e6, pk.lon0Micro / 1e6, mi * kMi, rot);
            const maplayer::AffineF f = maplayer::Fast(maplayer::AffineFor(pk, pk.radiusMm, v));
            Buffers b(pk);
            std::vector<uint8_t> mine((size_t)kS * kS, 0);
            Lit theirs;
            const auto segs = [&](int n) {
                float x0, y0, x1, y1;
                maplayer::ToScreen(f, b.qx[0], b.qy[0], &x0, &y0);
                for (int k = 1; k < n; ++k, x0 = x1, y0 = y1) {
                    maplayer::ToScreen(f, b.qx[k], b.qy[k], &x1, &y1);
                    maplayer::LineIdx(mine.data(), x0, y0, x1, y1, 1);
                    wxraster::Line(theirs, x0, y0, x1, y1, wxraster::Rgb{0, 0, 0});
                }
            };
            bmpk::ForEachRiver(pk, b.sc.qx, b.sc.qy, b.sc.cap, [&](int, int n) { segs(n); });
            bmpk::ForEachRoad(pk, b.sc.qx, b.sc.qy, b.sc.cap, [&](int, int n) { segs(n); });
            int lit = 0, differ = 0;
            for (int y = wxraster::kTopBand; y < wxraster::kBotBand; ++y)
                for (int x = 0; x < kS; ++x) {
                    if (wxlayer::OffDisc(x, y, kS) || !wxraster::InMap(x, y)) continue;
                    const size_t p = (size_t)y * kS + x;
                    lit += theirs.img[p];
                    differ += (mine[p] != 0) != (theirs.img[p] != 0);
                }
            check(fmt("CM11-%dmi-r%d", mi, rot), lit > 0 && differ == 0,
                  fmt("%d pixels lit by wxraster::Line in the shared area, %d differ", lit, differ));
        }
}

}  // namespace

int main(int argc, char** argv)
{
    if (argc < 2) {
        std::printf("usage: test_map_layer <test/fixtures/wx>\n");
        return 2;
    }
    root = argv[1];
    const std::vector<uint8_t> bend = Read("basemap/bend.bmpk");
    const std::vector<uint8_t> order = Read("basemap/syn-order.bmpk");
    const std::vector<uint8_t> synMin = Read("basemap/syn-min.bmpk");
    if (rig) return 2;
    bmpk::Package pBend, pOrder;
    if (!maplayer::Load(bend.data(), bend.size(), &pBend).accepted ||
        !maplayer::Load(order.data(), order.size(), &pOrder).accepted) {
        std::printf("the RIG: bend or syn-order did not pass the gate\n");
        return 2;
    }
    std::printf("CM1 the gate\n");
    Cm1();
    std::printf("CM2 the fast path against section 9\n");
    Cm2(pBend);
    std::printf("CM3 registration with the planes\n");
    Cm3(pBend);
    std::printf("CM4 S1 on Bend\n");
    Cm4(pBend);
    std::printf("CM5 S2's ramp\n");
    Cm5();
    std::printf("CM6 S3's order\n");
    Cm6(pOrder);
    std::printf("CM7 F4, the header's disc\n");
    Cm7(synMin);
    std::printf("CM8 the round screen\n");
    Cm8(pBend);
    std::printf("CM10 terrain sampling and the level\n");
    Cm10(pBend);
    std::printf("CM11 the line against wxraster::Line\n");
    Cm11(pBend);
    if (rig) return 2;
    std::printf("%d checks, %d failures\n", cases, failures);
    if (cases != kCases) {
        std::printf("the RIG: %d cases, not %d\n", cases, kCases);
        return 2;
    }
    return failures ? 1 : 0;
}
