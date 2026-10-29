// Weatherscope 3.5b's device code (docs/weatherscope-stage3.md A10.11, as A12.39 restates it after the rulings since):
// the BMPK reader (include/BasemapPackage.h), the basemap rasteriser (src/weatherscope/WxBasemap.h) and the fold, the
// basemap key and the peaks (src/weatherscope/WxScreens.h), held to 84 named cases in 8 families, and nothing else:
//   PK 30 (A10.11's 28 + A12.15's 2), TX 5, TR 5, FL 7, LN 9, BF 9, BK 7, PE 12.
// Its sabotages: python -I test/host/sabotage_wx_basemap.py (14, each failing exactly its set).
//
//     test_wx_basemap <test/fixtures/wx>
//
// Exit 0 all pass, 1 any fail, 2 the rig (a fixture missing, or a case count other than 84). Prints no coordinate.

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

constexpr int kCases = 84;
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
    const Rgb p1 = wxbase::PaletteRgb(1), p37 = wxbase::PaletteRgb(37);
    check("TR4", r0 == 1 && r7 == 1 && r8 == 2 && r128 == 19 && r255 == 37 && Eq(p1, Rgb{10, 10, 12}) &&
                     Eq(p37, Rgb{46, 46, 48}),
          fmt("RampIndex 0, 7, 8, 128, 255 -> %d, %d, %d, %d, %d; PaletteRgb(1) %s, (37) %s", r0, r7, r8, r128, r255,
              RgbS(p1).c_str(), RgbS(p37).c_str()));
}

// ------------------------------------------------------------------------------------------------ FL (7)
typedef std::set<std::pair<int, int>> Px;

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
    Rgb c = wxscreens::MapPixel(0, 50, 60, img.data(), false);
    check("BF1", Eq(c, water), "code 0, the image: " + RgbS(c));
    c = wxscreens::MapPixel(0, 50, 60, nullptr, false);
    check("BF2", Eq(c, black), "code 0, no image: " + RgbS(c));
    c = wxscreens::MapPixel(5, 50, 60, img.data(), false);
    const wxpalette::Rgb r5 = wxpalette::Code(5, false);
    check("BF3", Eq(c, Rgb{r5.r, r5.g, r5.b}), "code 5, the image: " + RgbS(c) + " (want Code(5, false), the radar)");
    c = wxscreens::MapPixel(14, 50, 60, img.data(), false);
    check("BF4", Eq(c, grey), "code 14, terrain off: " + RgbS(c));
    const Rgb a = wxscreens::MapPixel(14, 10, 10, img.data(), true), b = wxscreens::MapPixel(14, 10, 11, img.data(), true);
    check("BF5", Eq(a, hatch) && Eq(b, black), "code 14, terrain on: (10, 10) " + RgbS(a) + ", (10, 11) " + RgbS(b));
    const Rgb a0 = wxscreens::MapPixel(15, 10, 10, img.data(), false), b0 = wxscreens::MapPixel(15, 10, 11, img.data(), false);
    const Rgb a1 = wxscreens::MapPixel(15, 10, 10, img.data(), true), b1 = wxscreens::MapPixel(15, 10, 11, img.data(), true);
    check("BF6", Eq(a0, hatch) && Eq(b0, black) && Eq(a1, hatch) && Eq(b1, black),
          "code 15: off " + RgbS(a0) + " " + RgbS(b0) + ", on " + RgbS(a1) + " " + RgbS(b1));
    c = wxscreens::MapPixel(wxrender::kOffDisc, 0, 0, img.data(), false);
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
    if (ok) wxscreens::DrawMap(s, h, codes.data(), v, &withImg, img.data(), false);
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
    if (ok) wxscreens::DrawMap(s2, h, codes.data(), v, &without);
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
