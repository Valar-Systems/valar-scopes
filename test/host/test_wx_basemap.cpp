// Weatherscope 3.5b's device code (docs/weatherscope-stage3.md A10.11, as A12.39 restates it after the rulings since):
// the BMPK reader (include/BasemapPackage.h), the basemap rasteriser (src/weatherscope/WxBasemap.h) and the fold, the
// basemap key and the peaks (src/weatherscope/WxScreens.h), held to 96 named cases in 9 families, and nothing else:
//   PK 30 (A10.11's 28 + A12.15's 2), TX 5, TR 5, FL 7, LN 9, BF 9, BK 7, BW 8 (A12.53: the key's reason), PE 12.
// Its sabotages: python -I test/host/sabotage_wx_basemap.py (16, each failing exactly its set).
//
//     test_wx_basemap <test/fixtures/wx>
//
// Exit 0 all pass, 1 any fail, 2 the rig (a fixture missing, or a case count other than 96). Prints no coordinate.

#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "BasemapPackage.h"
#include "WxBasemap.h"
#include "WxCrop.h"
#include "WxPalette.h"
#include "WxRaster.h"
#include "WxRender.h"
#include "WxScreens.h"

namespace {

using wxraster::Rgb;
using wxscreens::Box;

constexpr int kCases = 96;
constexpr int kS = wxraster::kSize;

int checks = 0, failures = 0;
void check(const char* name, bool ok, const std::string& what)
{
    ++checks;
    if (!ok) ++failures;
    std::printf("  %s %s %s\n", ok ? "ok  " : "FAIL", name, what.c_str());
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

std::string RgbS(Rgb c) { return fmt("(%d, %d, %d)", c.r, c.g, c.b); }
bool Eq(Rgb a, Rgb b) { return a.r == b.r && a.g == b.g && a.b == b.b; }
std::string BoxS(const Box& b) { return fmt("(%d, %d, %d, %d)", b.x0, b.y0, b.x1, b.y1); }
bool Eq(const Box& a, const Box& b) { return a.x0 == b.x0 && a.y0 == b.y0 && a.x1 == b.x1 && a.y1 == b.y1; }

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

// A view at radius `mi` around (lat, lon), as wxrender::BuildViewKm builds one (home, rLat, rLon; no crop rows).
wxrender::View ViewAt(double lat, double lon, double mi)
{
    wxrender::View v;
    const double km = mi * 1.609344;
    v.homeLat = lat;
    v.homeLon = lon;
    v.rLat = km / wxrender::kKmPerDeg;
    double c = std::cos(lat * (wxraster::kPi / 180.0));
    if (c < 0.01) c = 0.01;
    v.rLon = km / (wxrender::kKmPerDeg * c);
    return v;
}

bmpk::Package Origin(uint32_t radiusMm)
{
    bmpk::Package p;
    p.lat0Micro = 0;
    p.lon0Micro = 0;
    p.radiusMm = radiusMm;
    return p;
}

// ------------------------------------------------------------------------------------------------ PK (30)
void PK()
{
    struct Neg {
        const char* name;
        const char* file;
        bmpk::Status want;
    };
    const Neg kNeg[] = {
        {"PK-short", "neg-short", bmpk::Status::kHeader},
        {"PK-magic", "neg-magic", bmpk::Status::kMagic},
        {"PK-version", "neg-version", bmpk::Status::kVersion},
        {"PK-flags", "neg-flags", bmpk::Status::kFlags},
        {"PK-total", "neg-total", bmpk::Status::kTotal},
        {"PK-crc", "neg-crc", bmpk::Status::kCrc},
        {"PK-nsec0", "neg-nsec0", bmpk::Status::kSectionCount},
        {"PK-nsec33", "neg-nsec33", bmpk::Status::kSectionCount},
        {"PK-bounds", "neg-bounds", bmpk::Status::kSectionBounds},
        {"PK-overlap", "neg-overlap", bmpk::Status::kOverlap},
        {"PK-gap", "neg-gap", bmpk::Status::kGap},
        {"PK-dup", "neg-dup", bmpk::Status::kDuplicate},
        {"PK-noprov", "neg-noprov", bmpk::Status::kNoProvenance},
        {"PK-varint6", "neg-varint6", bmpk::Status::kVarint},
        {"PK-overlong", "neg-overlong", bmpk::Status::kVarint},
        {"PK-walk", "neg-walk", bmpk::Status::kVertexRange},
        {"PK-ring2", "neg-ring2", bmpk::Status::kVertexCount},
        {"PK-line1", "neg-line1", bmpk::Status::kVertexCount},
        {"PK-count", "neg-count", bmpk::Status::kBody},
        {"PK-text", "neg-text", bmpk::Status::kText},
        {"PK-rivername", "neg-rivername", bmpk::Status::kRiverName},
        {"PK-roadclass", "neg-roadclass", bmpk::Status::kRoadClass},
        {"PK-terrainlen", "neg-terrainlen", bmpk::Status::kTerrainLength},
        {"PK-terrainN0", "neg-terrainN0", bmpk::Status::kTerrainLength},
        {"PK-reserved", "neg-reserved", bmpk::Status::kReserved},
        {"PK-terraincount", "neg-terrain-count", bmpk::Status::kTerrainCount},
        {"PK-provcount", "neg-prov-count", bmpk::Status::kProvenanceCount},
    };
    bmpk::Package pm, pu, pb;
    const std::vector<uint8_t> bmin = Read("basemap/syn-min.bmpk");
    const bmpk::Status smin = bmpk::Validate(bmin.data(), bmin.size(), &pm);
    check("PK-min", smin == bmpk::Status::kOk, fmt("syn-min: %s", bmpk::Name(smin)));
    const std::vector<uint8_t> bun = Read("basemap/syn-unknown.bmpk");
    const bmpk::Status sun = bmpk::Validate(bun.data(), bun.size(), &pu);
    bool same = sun == bmpk::Status::kOk && smin == bmpk::Status::kOk;
    int known = 0;
    for (int i = 0; same && i < pu.nSections; ++i) {   // every known section as syn-min's, byte for byte
        const bmpk::Section& s = pu.sections[i];
        if (s.type < 1 || s.type > 7) continue;
        const bmpk::Section* m = pm.Find(s.type);
        same = m && m->count == s.count && m->length == s.length &&
               std::memcmp(pu.data + s.offset, pm.data + m->offset, s.length) == 0;
        ++known;
    }
    check("PK-unknown", same && known == pm.nSections, fmt("syn-unknown: %s; %d known sections equal syn-min's",
                                                           bmpk::Name(sun), known));
    const std::vector<uint8_t> bb = Read("basemap/bend.bmpk");
    const bmpk::Status sb = bmpk::Validate(bb.data(), bb.size(), &pb);
    check("PK-bend", sb == bmpk::Status::kOk, fmt("bend.bmpk: %s", bmpk::Name(sb)));
    for (const Neg& n : kNeg) {
        const std::vector<uint8_t> b = Read(std::string("basemap/") + n.file + ".bmpk");
        bmpk::Package p;
        const bmpk::Status s = bmpk::Validate(b.data(), b.size(), &p);
        check(n.name, s == n.want && p.data == nullptr,
              fmt("%s: %s (want %s)", n.file, bmpk::Name(s), bmpk::Name(n.want)));
    }
}

// ------------------------------------------------------------------------------------------------ TX (5)
void TX()
{
    struct Tx {
        const char* name;
        double homeLat, homeLon, mi;
        int32_t qx, qy;
        double x, y;
    };
    const Tx kTx[] = {
        {"TX1", 0, 0, 60, 0, 0, 120.0, 120.0},
        {"TX2", 0, 0, 60, 32767, 0, 240.546011294, 120.0},
        {"TX3", 0, 0, 60, 0, 32767, 120.0, 240.546011294},
        {"TX4", 0, 0, 15, -4000, 2000, 61.137846592, 149.431076704},
        {"TX5", 0.004999, 0.004999, 15, 0, 0, 117.241663695, 122.758336316},
    };
    const bmpk::Package pk = Origin(97000000);
    for (const Tx& t : kTx) {
        const wxrender::View v = ViewAt(t.homeLat, t.homeLon, t.mi);
        const wxbase::Mapper m(pk, v);
        double x, y;
        m.ToScreen(t.qx, t.qy, &x, &y);
        check(t.name, std::fabs(x - t.x) < 1e-6 && std::fabs(y - t.y) < 1e-6,
              fmt("(%.9f, %.9f), want (%.9f, %.9f)", x, y, t.x, t.y));
    }
}

// ------------------------------------------------------------------------------------------------ TR (5)
bool OnDisc(int x, int y)
{
    const int dx2 = 2 * x + 1 - 240, dy2 = 2 * y + 1 - 240;
    return dx2 * dx2 + dy2 * dy2 <= 4 * 120 * 120;
}

void TR()
{
    const bmpk::Package pk = Origin(97000000);
    {
        const wxrender::View v = ViewAt(0, 0, 60);
        const wxbase::Mapper m(pk, v);
        int i1, j1, i2, j2;
        m.LevelPixel(0, 120, 98974656, 246, &i1, &j1);
        m.LevelPixel(239, 24, 98974656, 246, &i2, &j2);
        check("TR1", i1 == 3 && j1 == 123 && i2 == 242 && j2 == 27,
              fmt("(0, 120) -> (%d, %d); (239, 24) -> (%d, %d); want (3, 123), (242, 27)", i1, j1, i2, j2));
    }
    {
        const wxrender::View v = ViewAt(0.004999, 0.004999, 15);
        const wxbase::Mapper m(pk, v);
        int i, j;
        m.LevelPixel(120, 120, 24743664, 246, &i, &j);
        check("TR2", i == 126 && j == 120, fmt("(120, 120) -> (%d, %d); want (126, 120)", i, j));
    }
    const int kMi[3] = {15, 30, 60};
    const double kHome[5][2] = {{0, 0}, {0.004999, 0.004999}, {0.004999, -0.004999}, {-0.004999, 0.004999},
                                {-0.004999, -0.004999}};
    long out246 = 0, out240 = 0, swept = 0;
    for (int z = 0; z < 3; ++z)
        for (int h = 0; h < 5; ++h) {
            const wxrender::View v = ViewAt(kHome[h][0], kHome[h][1], kMi[z]);
            const wxbase::Mapper m(pk, v);
            for (int y = 0; y < kS; ++y)
                for (int x = 0; x < kS; ++x) {
                    if (!OnDisc(x, y)) continue;
                    int i, j;
                    ++swept;
                    if (!m.LevelPixel(x, y, wxbase::LevelMm(kMi[z]), 246, &i, &j)) ++out246;
                    if (!m.LevelPixel(x, y, (uint32_t)(kMi[z] * 1609344), 240, &i, &j)) ++out240;
                }
        }
    check("TR3", out246 == 0, fmt("%ld of %ld disc pixels outside [0, 246)", out246, swept));
    check("TR3c", out240 > 0, fmt("the control (N = 240, R_l = the view's radius): %ld outside (> 0; the calculator "
                                  "found 1,216)", out240));
    const int r0 = wxbase::RampIndex(0), r7 = wxbase::RampIndex(7), r8 = wxbase::RampIndex(8),
              r128 = wxbase::RampIndex(128), r255 = wxbase::RampIndex(255);
    const Rgb p1 = wxtheme::kDark.base[1], p37 = wxtheme::kDark.base[37];   // PaletteRgb retired into the theme (A12.47)
    check("TR4", r0 == 1 && r7 == 1 && r8 == 2 && r128 == 19 && r255 == 37 && Eq(p1, Rgb{10, 10, 12}) &&
                     Eq(p37, Rgb{46, 46, 48}),
          fmt("RampIndex 0, 7, 8, 128, 255 -> %d, %d, %d, %d, %d; kDark.base[1] %s, [37] %s", r0, r7, r8, r128, r255,
              RgbS(p1).c_str(), RgbS(p37).c_str()));
}

// ------------------------------------------------------------------------------------------------ FL (11)
typedef std::set<std::pair<int, int>> Px;
typedef std::vector<std::pair<double, double>> Ring;

// FL7c and FL8c's CONTROL (docs/weatherscope-stage4.md S4.21): FillRing as it was before the bridge fix, VERBATIM but for
// its name, with every crossing computed from the edge's FIRST endpoint. A zero-width bridge is crossed once each way, the
// two doubles can straddle a pixel centre, and the pixel is lost. Kept here so each bridge case is shown able to fail.
int FillRingFirstEndpoint(const double* xs, const double* ys, int n, double* cross, uint8_t* img, uint8_t value)
{
    if (n < 3) return 0;
    double lo = ys[0], hi = ys[0];
    for (int k = 1; k < n; ++k) {
        lo = fmin(lo, ys[k]);
        hi = fmax(hi, ys[k]);
    }
    int y0 = (int)floor(lo) - 1, y1 = (int)ceil(hi) + 2;
    if (y0 < 0) y0 = 0;
    if (y1 > kS) y1 = kS;
    int written = 0;
    for (int y = y0; y < y1; ++y) {
        const double py = y + 0.5;
        int m = 0;
        for (int k = 0; k < n; ++k) {
            const int k2 = (k + 1) % n;
            const double xa = xs[k], ya = ys[k], xb = xs[k2], yb = ys[k2];
            if ((ya > py) != (yb > py)) cross[m++] = xa + (py - ya) * (xb - xa) / (yb - ya);
        }
        for (int a = 1; a < m; ++a) {
            const double t = cross[a];
            int b = a - 1;
            while (b >= 0 && cross[b] > t) {
                cross[b + 1] = cross[b];
                --b;
            }
            cross[b + 1] = t;
        }
        for (int a = 0; a + 1 < m; a += 2) {
            int x0 = (int)ceil(cross[a] - 0.5), x1 = (int)ceil(cross[a + 1] - 0.5);
            if (x0 < 0) x0 = 0;
            if (x1 > kS) x1 = kS;
            for (int x = x0; x < x1; ++x) {
                img[y * kS + x] = value;
                ++written;
            }
        }
    }
    return written;
}

// An independent reference for FL7: a pixel is in when its CENTRE is inside the ring by even-odd, tested pixel by pixel,
// with none of FillRing's scanline code, so a plant in FillRing can't make the two agree.
Px CentreFill(const Ring& r)
{
    Px s;
    for (int y = 0; y < kS; ++y)
        for (int x = 0; x < kS; ++x) {
            const double px = x + 0.5, py = y + 0.5;
            bool in = false;
            for (size_t i = 0, j = r.size() - 1; i < r.size(); j = i++) {
                const double xi = r[i].first, yi = r[i].second, xj = r[j].first, yj = r[j].second;
                if ((yi > py) != (yj > py) && px < (xj - xi) * (py - yi) / (yj - yi) + xi) in = !in;
            }
            if (in) s.insert(std::make_pair(x, y));
        }
    return s;
}

Px Filled(const std::vector<uint8_t>& img, uint8_t v)
{
    Px s;
    for (int y = 0; y < kS; ++y)
        for (int x = 0; x < kS; ++x)
            if (img[y * kS + x] == v) s.insert(std::make_pair(x, y));
    return s;
}

Px Rect(int x0, int x1, int y0, int y1)
{
    Px s;
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x) s.insert(std::make_pair(x, y));
    return s;
}

Px Fill(std::initializer_list<std::vector<std::pair<double, double>>> rings)
{
    std::vector<uint8_t> img(kS * kS + 64, 0);   // 64 guard bytes: a write past the image's end shows as a pixel
    for (const auto& r : rings) {
        std::vector<double> xs, ys, cross(r.size() + 4);
        for (const auto& p : r) {
            xs.push_back(p.first);
            ys.push_back(p.second);
        }
        wxbase::FillRing(xs.data(), ys.data(), (int)r.size(), cross.data(), img.data(), 1);
    }
    Px s = Filled(img, 1);
    for (int k = 0; k < 64; ++k)
        if (img[kS * kS + k]) s.insert(std::make_pair(-1, -1));
    return s;
}

// One ring through a given fill function: FL7c and FL8c's control path, with Fill's guard bytes.
Px FillWith(int (*fill)(const double*, const double*, int, double*, uint8_t*, uint8_t), const Ring& r)
{
    std::vector<uint8_t> img(kS * kS + 64, 0);
    std::vector<double> xs, ys, cross(r.size() + 4);
    for (const auto& p : r) {
        xs.push_back(p.first);
        ys.push_back(p.second);
    }
    fill(xs.data(), ys.data(), (int)r.size(), cross.data(), img.data(), 1);
    Px s = Filled(img, 1);
    for (int k = 0; k < 64; ++k)
        if (img[kS * kS + k]) s.insert(std::make_pair(-1, -1));
    return s;
}

void FL()
{
    const std::vector<std::pair<double, double>> sq1 = {{10.5, 10.5}, {14.5, 10.5}, {14.5, 14.5}, {10.5, 14.5}};
    const std::vector<std::pair<double, double>> sq2 = {{12.5, 12.5}, {16.5, 12.5}, {16.5, 16.5}, {12.5, 16.5}};
    Px a = Fill({sq1});
    check("FL1", a == Rect(10, 13, 10, 13), fmt("%zu px (want 16: x 10..13, y 10..13)", a.size()));
    a = Fill({{{20.0, 20.5}, {24.0, 20.5}, {24.0, 22.5}, {20.0, 22.5}}});
    check("FL2", a == Rect(20, 23, 20, 21), fmt("%zu px (want 8: x 20..23, y 20..21)", a.size()));
    a = Fill({{{30.5, 30.5}, {33.5, 33.5}, {30.5, 36.5}, {27.5, 33.5}}});
    Px d;
    const int rows[5][2] = {{29, 30}, {28, 31}, {27, 32}, {28, 31}, {29, 30}};
    for (int r = 0; r < 5; ++r) {
        const Px row = Rect(rows[r][0], rows[r][1], 31 + r, 31 + r);
        d.insert(row.begin(), row.end());
    }
    check("FL3", a == d, fmt("%zu px (want 18: none in row 30; rows 31-35 2, 4, 6, 4, 2)", a.size()));
    a = Fill({{{230, 100}, {250, 100}, {250, 110}, {230, 110}}});
    check("FL4", a == Rect(230, 239, 100, 109), fmt("%zu px (want 100: x 230..239, y 100..109; nothing past x 239)",
                                                     a.size()));
    a = Fill({{{50.1, 50.1}, {50.4, 50.1}, {50.1, 50.4}}});
    check("FL5a", a.empty(), fmt("%zu px (want 0)", a.size()));
    a = Fill({{{60.3, 60.3}, {60.9, 60.3}, {60.3, 60.9}}});
    check("FL5b", a == Rect(60, 60, 60, 60), fmt("%zu px (want 1: (60, 60))", a.size()));
    a = Fill({sq1, sq2});
    Px u = Rect(10, 13, 10, 13);
    const Px r2 = Rect(12, 15, 12, 15);
    u.insert(r2.begin(), r2.end());
    check("FL6", a == u, fmt("%zu px (want 28: the union)", a.size()));

    // FL7-FL8 (S4.21): a zero-width bridge through a pixel centre, map-server's cases by bytes (its kb1-case set,
    // SHA256SUMS dbed2d33...89f2cd). Synthetic screen px; no real place. The fix computes every crossing from the edge's
    // lower-y endpoint, so the bridge's two passes give one double and the pixel is kept.
    const Ring minRing = {{10.1, 10.1}, {229.9, 10.1}, {229.9, 229.9}, {10.1, 229.9}, {10.1, 10.1},
                          {150.3, 150.3}, {150.3, 170.7}, {170.7, 170.7}, {170.7, 150.3}, {150.3, 150.3}};
    Px ref = CentreFill({{10.1, 10.1}, {229.9, 10.1}, {229.9, 229.9}, {10.1, 229.9}});
    for (const auto& p : CentreFill({{150.3, 150.3}, {150.3, 170.7}, {170.7, 170.7}, {170.7, 150.3}})) ref.erase(p);
    a = Fill({minRing});
    check("FL7", a == ref, fmt("%zu px (want the outer square minus the island, %zu px, the bridge's pixels kept)", a.size(),
                               ref.size()));
    const Px lost = {{26, 26}, {31, 31}, {128, 128}, {133, 133}, {134, 134}};
    Px o = FillWith(FillRingFirstEndpoint, minRing), missing, extra;
    for (const auto& p : ref) if (!o.count(p)) missing.insert(p);
    for (const auto& p : o) if (!ref.count(p)) extra.insert(p);
    check("FL7c", missing == lost && extra.empty(),
          fmt("CONTROL: the first-endpoint crossing misses %zu px, adds %zu (want exactly the 5 bridge pixels, none added)",
              missing.size(), extra.size()));

    // view-162-90: the 22-vertex bridged sea ring from map-server's synthetic KB1 build, the same doubles as its round-trip
    // reprs. Three tiny values are spelled with the point moved one place (45.35...e-16 for 4.535...e-15, 90.70...e-17 for
    // 9.070...e-16): the same decimal, so the same double, but no 16-hex-character run for the device-id guard to read.
    const Ring v90 = {{45.92425305948058, 194.07574694051942}, {90.36896877956481, 149.6310312204352},
                      {90.36896877956481, 138.51985229041412}, {101.48014770958586, 138.51985229041412},
                      {123.70250556962797, 112.59132663960692}, {123.70250556962797, 106.6658528397473},
                      {129.6279793694876, 106.6658528397473}, {129.6279793694876, 112.59132663960692},
                      {123.70250556962797, 112.59132663960692}, {101.48014770958586, 138.51985229041412},
                      {101.48014770958586, 149.6310312204352}, {90.36896877956481, 149.6310312204352},
                      {45.92425305948058, 194.07574694051942}, {45.92425305948059, -45.35831319257823e-16},
                      {194.07574694051942, 45.35831319257823e-16}, {134.81368449964904, 90.70765653036976e-17},
                      {134.81368449964904, 8.888210699789427}, {120.0, 8.888210699789425},
                      {120.00000000000001, 0.0}, {134.81368449964904, 90.70765653036976e-17},
                      {194.07574694051942, 45.35831319257823e-16}, {194.07574694051942, 194.07574694051942}};
    const Px lost90 = {{81, 158}, {79, 160}, {74, 165}, {72, 167}, {70, 169}, {69, 170}, {63, 176},
                       {62, 177}, {60, 179}, {58, 181}, {57, 182}, {55, 184}, {53, 186}, {51, 188}};
    a = Fill({v90});
    size_t kept = 0;
    for (const auto& p : lost90) kept += a.count(p);
    check("FL8", a.size() == 28420 && kept == lost90.size(),
          fmt("%zu px, %zu of the 14 bridge pixels (want 28420, map-server's reference count, and all 14)", a.size(), kept));
    o = FillWith(FillRingFirstEndpoint, v90);
    size_t held = 0;
    for (const auto& p : lost90) held += o.count(p);
    check("FL8c", o.size() == 28406 && held == 0,
          fmt("CONTROL: the first-endpoint crossing fills %zu px, %zu of the 14 (want 28406, none of them)", o.size(), held));
}

// ------------------------------------------------------------------------------------------------ LN (9)
struct Scr {
    std::vector<int32_t> qx, qy;
    std::vector<double> x, y, cross;
    wxbase::Scratch s;
    explicit Scr(uint32_t n) : qx(n), qy(n), x(n), y(n), cross(n)
    {
        s.qx = qx.data();
        s.qy = qy.data();
        s.x = x.data();
        s.y = y.data();
        s.cross = cross.data();
        s.cap = n;
    }
};

void LN()
{
    const std::vector<uint8_t> b = Read("basemap/syn-order.bmpk");
    bmpk::Package pk;
    const bmpk::Status st = bmpk::Validate(b.data(), b.size(), &pk);
    const wxrender::View v = ViewAt(0, 0, 60);
    Scr sc(pk.maxVertices + 1);
    std::vector<uint8_t> all(kS * kS), off(kS * kS), noriv(kS * kS);
    const bool ok = st == bmpk::Status::kOk &&
                    wxbase::Build(pk, v, 60, wxbase::kMaskAll, true, sc.s, all.data()) &&
                    wxbase::Build(pk, v, 60, wxbase::kMaskAll, false, sc.s, off.data()) &&
                    wxbase::Build(pk, v, 60, wxbase::kMaskAll & ~wxbase::kMaskRivers, true, sc.s, noriv.data());
    auto at = [](const std::vector<uint8_t>& img, int x, int y) { return (int)img[y * kS + x]; };
    struct Ln {
        const char* name;
        const std::vector<uint8_t>* img;
        int x, y, want;
    };
    const Ln kLn[] = {{"LN1", &all, 120, 110, 40}, {"LN2", &all, 130, 110, 39}, {"LN3", &all, 130, 130, 38},
                      {"LN4", &all, 50, 120, 19},  {"LN5", &all, 120, 95, 40},  {"LN6", &all, 95, 110, 39}};
    for (const Ln& l : kLn)
        check(l.name, ok && at(*l.img, l.x, l.y) == l.want,
              fmt("(%d, %d) -> %d (want %d); syn-order %s", l.x, l.y, ok ? at(*l.img, l.x, l.y) : -1, l.want,
                  bmpk::Name(st)));
    check("LN7", ok && at(off, 50, 120) == 0 && at(off, 130, 130) == 38,
          fmt("terrain off: (50, 120) -> %d, (130, 130) -> %d (want 0, 38)", ok ? at(off, 50, 120) : -1,
              ok ? at(off, 130, 130) : -1));
    check("LN8", ok && at(all, 120, 130) == 40, fmt("(120, 130) -> %d (want 40: road over water)",
                                                     ok ? at(all, 120, 130) : -1));
    check("LN9", ok && at(noriv, 130, 110) == 38 && at(noriv, 95, 110) == 19,
          fmt("no rivers: (130, 110) -> %d, (95, 110) -> %d (want 38, 19)", ok ? at(noriv, 130, 110) : -1,
              ok ? at(noriv, 95, 110) : -1));
}

// ------------------------------------------------------------------------------------------------ BF (9)
class Rec : public wxraster::Sink {
public:
    std::vector<Rgb> px = std::vector<Rgb>(kS * kS, Rgb{1, 2, 3});
    std::vector<int> puts = std::vector<int>(kS * kS, 0);
    void Put(int x, int y, Rgb c) override
    {
        px[y * kS + x] = c;
        ++puts[y * kS + x];
    }
};

class Rows : public wxscreens::CodeRows {
public:
    std::vector<uint8_t> all;
    void Row(const uint8_t* codes, int n) override { all.insert(all.end(), codes, codes + n); }
};

void BF()
{
    std::vector<uint8_t> img(kS * kS, 38);
    const Rgb water{10, 34, 72}, black{0, 0, 0}, grey{40, 40, 40}, hatch{72, 72, 72};
    Rgb c = wxscreens::MapPixel(0, 50, 60, img.data(), false, wxtheme::kDark);
    check("BF1", Eq(c, water), "code 0, the image: " + RgbS(c));
    c = wxscreens::MapPixel(0, 50, 60, nullptr, false, wxtheme::kDark);
    check("BF2", Eq(c, black), "code 0, no image: " + RgbS(c));
    c = wxscreens::MapPixel(5, 50, 60, img.data(), false, wxtheme::kDark);
    const wxpalette::Rgb r5 = wxpalette::Code(5, false);
    check("BF3", Eq(c, Rgb{r5.r, r5.g, r5.b}), "code 5, the image: " + RgbS(c) + " (want Code(5, false), the radar)");
    c = wxscreens::MapPixel(14, 50, 60, img.data(), false, wxtheme::kDark);
    check("BF4", Eq(c, grey), "code 14, terrain off: " + RgbS(c));
    const Rgb a = wxscreens::MapPixel(14, 10, 10, img.data(), true, wxtheme::kDark),
              b = wxscreens::MapPixel(14, 10, 11, img.data(), true, wxtheme::kDark);
    check("BF5", Eq(a, hatch) && Eq(b, black), "code 14, terrain on: (10, 10) " + RgbS(a) + ", (10, 11) " + RgbS(b));
    const Rgb a0 = wxscreens::MapPixel(15, 10, 10, img.data(), false, wxtheme::kDark),
              b0 = wxscreens::MapPixel(15, 10, 11, img.data(), false, wxtheme::kDark);
    const Rgb a1 = wxscreens::MapPixel(15, 10, 10, img.data(), true, wxtheme::kDark),
              b1 = wxscreens::MapPixel(15, 10, 11, img.data(), true, wxtheme::kDark);
    check("BF6", Eq(a0, hatch) && Eq(b0, black) && Eq(a1, hatch) && Eq(b1, black),
          "code 15: off " + RgbS(a0) + " " + RgbS(b0) + ", on " + RgbS(a1) + " " + RgbS(b1));
    c = wxscreens::MapPixel(wxrender::kOffDisc, 0, 0, img.data(), false, wxtheme::kDark);
    check("BF7", Eq(c, black), "off the disc, the image: " + RgbS(c));

    const std::vector<uint8_t> clear = Read("loop/synthetic-clear.wxc");   // the Dallas loop's dry crop (ref35 CLEAR)
    wxcrop::Header h;
    std::vector<uint8_t> codes;
    bool ok = wxcrop::ParseHeader(clear.data(), clear.size(), &h) == wxcrop::Status::Ok;
    if (ok) {
        codes.resize(h.Cells());
        ok = wxcrop::DecodeInto(clear.data(), clear.size(), codes.data(), codes.size()) == wxcrop::Status::Ok;
    }
    wxrender::View v;
    if (ok) wxrender::BuildViewKm(h, &v, 60 * 1.609344);
    Rec s;
    Rows withImg, without;
    if (ok) wxscreens::DrawMap(s, wxtheme::kDark, h, codes.data(), v, &withImg, img.data(), false);
    long bandPuts = 0, onWater = 0, onOther = 0, offBlack = 0, offOther = 0;
    for (int y = 0; ok && y < kS; ++y)
        for (int x = 0; x < kS; ++x) {
            const int i = y * kS + x;
            if (y < wxraster::kTopBand || y >= wxraster::kBotBand) {
                bandPuts += s.puts[i];
                continue;
            }
            if (OnDisc(x, y)) (Eq(s.px[i], water) ? onWater : onOther)++;
            else (Eq(s.px[i], black) ? offBlack : offOther)++;
        }
    check("BF8", ok && bandPuts == 0 && onWater == 36828 && onOther == 0 && offOther == 0,
          fmt("band Puts %ld; on-disc map pixels water %ld (want 36,828), other %ld; off-disc black %ld, other %ld",
              bandPuts, onWater, onOther, offBlack, offOther));
    Rec s2;
    if (ok) wxscreens::DrawMap(s2, wxtheme::kDark, h, codes.data(), v, &without);
    check("BF9", ok && withImg.all.size() == 240u * 240u && withImg.all == without.all,
          fmt("CodeRows: %zu and %zu codes, %s", withImg.all.size(), without.all.size(),
              withImg.all == without.all ? "byte-identical" : "DIFFERENT"));
}

// ------------------------------------------------------------------------------------------------ BK (7)
void BK()
{
    const std::vector<uint8_t> dA = Read("loop/shsr-20240521-210400.wxc"), dB = Read("loop/shsr-20240521-220000.wxc"),
                               bend = Read("bend-loop/shsr-20240602-191000.wxc");
    wxcrop::Header a, b, c;
    const bool ok = wxcrop::ParseHeader(dA.data(), dA.size(), &a) == wxcrop::Status::Ok &&
                    wxcrop::ParseHeader(dB.data(), dB.size(), &b) == wxcrop::Status::Ok &&
                    wxcrop::ParseHeader(bend.data(), bend.size(), &c) == wxcrop::Status::Ok;
    const uint8_t all = wxbase::kMaskAll;
    const wxscreens::BaseKey k0 = wxscreens::MakeBaseKey(a, 0x1234ABCDu, 205093, 60, all, false);
    struct Bk {
        const char* name;
        wxscreens::BaseKey k;
        bool reused;
    };
    const Bk kBk[] = {
        {"BK1", wxscreens::MakeBaseKey(a, 0x1234ABCDu, 205093, 60, all, false), true},
        {"BK2", wxscreens::MakeBaseKey(a, 0x1234ABCDu, 205093, 30, all, false), false},
        {"BK3", wxscreens::MakeBaseKey(a, 0x0BADF00Du, 205094, 60, all, false), false},
        {"BK4", wxscreens::MakeBaseKey(a, 0x1234ABCDu, 205093, 60, all, true), false},
        {"BK5", wxscreens::MakeBaseKey(a, 0x1234ABCDu, 205093, 60, all & ~wxbase::kMaskRivers, false), false},
        {"BK6", wxscreens::MakeBaseKey(c, 0x1234ABCDu, 205093, 60, all, false), false},
        {"BK7", wxscreens::MakeBaseKey(b, 0x1234ABCDu, 205093, 60, all, false), true},
    };
    for (const Bk& k : kBk) {
        const bool same = wxscreens::SameBase(k0, k.k);
        check(k.name, ok && same == k.reused, fmt("%s (want %s)", same ? "reused" : "rebuilt", k.reused ? "reused" : "rebuilt"));
    }
}

// ------------------------------------------------------------------------------------------------ BW (8)
// The key's reason (A12.53): BaseWhy names the first field that differs, in the order package, place, zoom, mask,
// terrain; "first" with no valid image; null when the image is still right. Over BK's own keys and geometries.
void BW()
{
    const std::vector<uint8_t> dA = Read("loop/shsr-20240521-210400.wxc"), bend = Read("bend-loop/shsr-20240602-191000.wxc");
    wxcrop::Header a, c;
    const bool ok = wxcrop::ParseHeader(dA.data(), dA.size(), &a) == wxcrop::Status::Ok &&
                    wxcrop::ParseHeader(bend.data(), bend.size(), &c) == wxcrop::Status::Ok;
    const uint8_t all = wxbase::kMaskAll;
    const wxscreens::BaseKey k0 = wxscreens::MakeBaseKey(a, 0x1234ABCDu, 205093, 60, all, false);
    struct Bw {
        const char* name;
        bool valid;
        wxscreens::BaseKey now;
        const char* want;   // null: reused
    };
    const Bw kBw[] = {
        {"BW1", false, k0, "first"},
        {"BW2", true, k0, nullptr},
        {"BW3", true, wxscreens::MakeBaseKey(a, 0x0BADF00Du, 205094, 60, all, false), "package"},
        {"BW4", true, wxscreens::MakeBaseKey(c, 0x1234ABCDu, 205093, 60, all, false), "place"},
        {"BW5", true, wxscreens::MakeBaseKey(a, 0x1234ABCDu, 205093, 30, all, false), "zoom"},
        {"BW6", true, wxscreens::MakeBaseKey(a, 0x1234ABCDu, 205093, 60, all & ~wxbase::kMaskRivers, false), "mask"},
        {"BW7", true, wxscreens::MakeBaseKey(a, 0x1234ABCDu, 205093, 60, all, true), "terrain"},
        {"BW8", true, wxscreens::MakeBaseKey(a, 0x1234ABCDu, 205093, 30, all, true), "zoom"},
    };
    for (const Bw& w : kBw) {
        const char* got = wxscreens::BaseWhy(k0, w.valid, w.now);
        const bool same = got && w.want ? std::strcmp(got, w.want) == 0 : got == w.want;
        check(w.name, ok && same, fmt("%s (want %s)", got ? got : "null: reused", w.want ? w.want : "null: reused"));
    }
}

// ------------------------------------------------------------------------------------------------ PE (12)
class Peaks : public wxscreens::PeakSet {
public:
    struct P {
        const char* name;
        double x, y;
    };
    std::vector<P> p;
    int Count() const override { return (int)p.size(); }
    const char* Name(int i) const override { return p[i].name; }
    void At(int i, double* x, double* y) const override
    {
        *x = p[i].x;
        *y = p[i].y;
    }
};

class Towns : public wxscreens::TownSet {
public:
    struct T {
        const char* name;
        double x, y;
    };
    std::vector<T> t;
    const char* Name(int i) const override { return t[i].name; }
    void At(int i, double* x, double* y) const override
    {
        *x = t[i].x;
        *y = t[i].y;
    }
};

const Box kBands[2] = {{0, 0, kS - 1, wxraster::kTopBand - 1}, {0, wxraster::kBotBand, kS - 1, kS - 1}};

wxscreens::Placed One(const char* label, double x, double y)
{
    return wxscreens::PlacePeak(label, x, y, wxscreens::Inner(60), kBands, 2, nullptr, 0);
}

void PE()
{
    const double inner = wxscreens::Inner(60);
    wxscreens::Placed p = One("Ab", 170.0, 80.0);
    check("PE1", p.placed && p.side == 'R' && Eq(p.box, Box{167, 76, 186, 84}) && p.tx == 175 && p.ty == 77,
          fmt("%s %c %s text (%d, %d)", p.placed ? "placed" : "skipped", p.side ? p.side : '-', BoxS(p.box).c_str(), p.tx, p.ty));
    p = One("Ab", 70.0, 80.0);
    check("PE2", p.placed && p.side == 'L' && Eq(p.box, Box{53, 76, 74, 84}) && p.tx == 54 && p.ty == 77,
          fmt("%s %c %s text (%d, %d)", p.placed ? "placed" : "skipped", p.side ? p.side : '-', BoxS(p.box).c_str(), p.tx, p.ty));
    {
        Peaks pk;
        pk.p = {{"A", 140.0, 120.0}, {"B", 162.43, 77.57}, {"C", 162.43, 162.43},
                {"D", 77.57, 162.43}, {"E", 77.57, 77.57}, {"F", 180.0, 120.0}};
        Towns none;
        wxscreens::PlacedPeak out[wxscreens::kMaxPeaks];
        wxscreens::PlacedLabel towns[wxscreens::kMaxLabels];
        int np = 0;
        wxscreens::PlaceLabels(&pk, none, nullptr, 0, 60, inner, kBands, 2, out, &np, towns);
        std::string got;
        for (int i = 0; i < np; ++i) got += out[i].label;
        // the control: uncapped, every one of B-F places, so it is the cap that decides
        Box boxes[8];
        int nb = 0;
        std::string uncapped;
        for (const Peaks::P& q : pk.p) {
            const wxscreens::Placed u = wxscreens::PlacePeak(q.name, q.x, q.y, inner, kBands, 2, boxes, nb);
            if (u.placed) {
                boxes[nb++] = u.box;
                uncapped += q.name;
            }
        }
        check("PE3", got == "BCDE" && uncapped == "BCDEF",
              fmt("placed %s (want BCDE); uncapped %s (want BCDEF)", got.c_str(), uncapped.c_str()));
    }
    p = One("Ab", 144.0, 120.0);
    check("PE4", !p.placed, fmt("(144, 120), 24.0 px: %s (want skipped)", p.placed ? "placed" : "skipped"));
    {
        Peaks pk;
        pk.p = {{"Ab", 170.0, 80.0}};
        Towns t;
        t.t = {{"B", 172.0, 82.0}};
        const int order[1] = {0};
        wxscreens::PlacedPeak out[wxscreens::kMaxPeaks];
        wxscreens::PlacedLabel towns[wxscreens::kMaxLabels];
        int np = 0;
        const int nt = wxscreens::PlaceLabels(&pk, t, order, 1, 60, inner, kBands, 2, out, &np, towns);
        check("PE5", np == 1 && Eq(out[0].at.box, Box{167, 76, 186, 84}) && nt == 0,
              fmt("peaks %d %s; towns %d (want the peak at (167, 76, 186, 84), the town skipped)", np,
                  np ? BoxS(out[0].at.box).c_str() : "-", nt));
    }
    struct Mt {
        const char* name;
        const char* in;
        const char* want;
    };
    const Mt kMt[] = {{"PE6a", "Mount Bachelor", "Mt Bachelor"},
                      {"PE6b", "Broken Top", "Broken Top"},
                      {"PE6c", "Mountain View", "Mountain View"},
                      {"PE6d", "Little Mount Hope", "Little Mt Hope"}};
    for (const Mt& m : kMt) {
        char out[40];
        wxscreens::PeakLabel(m.in, out, sizeof out);
        check(m.name, std::strcmp(out, m.want) == 0, fmt("\"%s\" -> \"%s\" (want \"%s\")", m.in, out, m.want));
    }
    p = One("Ab", 232.0, 120.0);
    check("PE7", !p.placed, fmt("(232, 120): %s (want skipped: both sides' corners beyond 116 px)",
                                p.placed ? "placed" : "skipped"));
    p = One("Ab", 170.0, 185.0);
    check("PE8a", p.placed && p.side == 'R' && Eq(p.box, Box{167, 181, 186, 189}),
          fmt("(170, 185): %s %s (want right, (167, 181, 186, 189))", p.placed ? "placed" : "skipped", BoxS(p.box).c_str()));
    p = One("Ab", 170.0, 186.0);
    check("PE8b", !p.placed, fmt("(170, 186): %s (want skipped: y1 190 within 4 px of the band)",
                                 p.placed ? "placed" : "skipped"));
}

}  // namespace

int main(int argc, char** argv)
{
    if (argc != 2) {
        std::printf("usage: test_wx_basemap <test/fixtures/wx>\n");
        return 2;
    }
    root = argv[1];
    std::printf("== PK, the reader ==\n");
    PK();
    std::printf("== TX, the mapping ==\n");
    TX();
    std::printf("== TR, terrain sampling ==\n");
    TR();
    std::printf("== FL, the fill ==\n");
    FL();
    std::printf("== LN, the layers and their order ==\n");
    LN();
    std::printf("== BF, the fold ==\n");
    BF();
    std::printf("== BK, the basemap key ==\n");
    BK();
    std::printf("== BW, the basemap key's reason ==\n");
    BW();
    std::printf("== PE, the peaks ==\n");
    PE();
    std::printf("%d checks, %d failures\n", checks, failures);
    if (rig) return 2;
    if (checks != kCases) {
        std::printf("the RIG: %d cases ran, A12.39 froze %d\n", checks, kCases);
        return 2;
    }
    return failures ? 1 : 0;
}
