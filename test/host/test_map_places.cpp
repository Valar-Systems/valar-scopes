// Step B's place names (docs/blipscope-map-client-steps.md, "MapPlaces.h (CB6-CB8), registered before its code"):
// include/MapPlaces.h held to CB6 and CB7 over KJAC, KSFO and Bend, every ladder step and 8 rotations.
//     test_map_places <test/fixtures/wx>
// Exit 0 all pass, 1 any fail, 2 the rig (a fixture missing or refused, or a case count other than 32). Prints no
// coordinate and no name: a nearby town's name says where a device is (CD2), and the fixtures are no exception.
//
// The counts are the registered table, computed by an independent scratch reading before any of this code. The
// property rows follow that reading's own definitions (the rank row's "considered" set is the TRUE greedy's, with the
// true anchors and the true obstacles), so a plant's red set here is the one the reading computed under it.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "Airports.h"
#include "MapPlaces.h"
#include "RadarProjection.h"
#include "RadarZoom.h"
#include "WxLogic.h"
#include "WxScreens.h"

using mapplaces::Box;

static int g_checks = 0, g_fails = 0;
static bool g_rig = false;

static void Check(bool ok, const std::string& name)
{
    ++g_checks;
    if (ok) {
        std::printf("  ok   %s\n", name.c_str());
    } else {
        ++g_fails;
        std::printf("  FAIL %s\n", name.c_str());
    }
}

static std::vector<uint8_t> Read(const std::string& path)
{
    std::vector<uint8_t> b;
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) {
        std::printf("the RIG: cannot read %s\n", path.c_str());
        g_rig = true;
        return b;
    }
    uint8_t buf[65536];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) b.insert(b.end(), buf, buf + n);
    std::fclose(f);
    return b;
}

// The device's own View at a ladder step (default radius 100 mi, unit mi), transcribed from
// src/AircraftManager.cpp:874-904 (radLat/radLon, rangeRadiusDisplay through float FromKm, the ladder), :5506-5510
// (f = step / top) and :1447-1449 (rotRad = radians((float)radarUpDeg), cosf, sinf). viewRadiusDisplay as :5511.
struct DevView {
    wxlayer::View v;
    double viewRadiusDisplay = 0;
};

static bool DeviceView(double lat, double lon, double stepMi, int up, DevView* out)
{
    const double DEG_TO_RAD = 0.017453292519943295769236907684886;
    const double kmPerUnit = 1.609344;
    const double distanceKm = 100.0 * kmPerUnit;
    double cosLat = std::cos(lat * DEG_TO_RAD);
    if (cosLat < 0.01) cosLat = 0.01;
    const double radLat = std::min(std::max(distanceKm / 111.0, 0.001), 2.0);
    const double radLon = std::min(std::max(distanceKm / (111.0 * cosLat), 0.001), 2.0);
    volatile float km = (float)(radLat * 111.0);
    volatile float shown = km / (float)kmPerUnit;
    const double rrd = shown;
    volatile float configured = (float)rrd;
    const radarzoom::Ladder l = radarzoom::BuildLadder(configured);
    int idx = -1;
    for (int i = 0; i < l.n; ++i)
        if ((double)l.step[i] == stepMi) idx = i;
    if (idx < 0) return false;
    const double top = l.step[l.Top()];
    const double f = (idx == l.Top() || top <= 0.0) ? 1.0 : l.step[idx] / top;
    const float rotRad = (float)((float)up * DEG_TO_RAD);
    out->v.homeLat = lat;
    out->v.homeLon = lon;
    out->v.rLat = radLat * f;
    out->v.rLon = radLon * f;
    out->v.rotCos = cosf(rotRad);
    out->v.rotSin = sinf(rotRad);
    out->v.size = 240;
    out->viewRadiusDisplay = rrd * f;
    return true;
}

// The registered table: placed labels per rotation 0..315, per step 5 / 10 / 25 / 50 / 100 mi.
struct Expected {
    const char* name;
    int counts[5][8];
};
static const Expected kTable[3] = {
    {"kjac", {{0, 0, 0, 0, 0, 0, 0, 0}, {0, 0, 0, 0, 0, 0, 0, 0}, {2, 2, 1, 2, 2, 2, 1, 1}, {6, 4, 3, 3, 6, 4, 3, 3},
              {8, 8, 7, 8, 8, 6, 6, 8}}},
    {"ksfo", {{3, 3, 4, 5, 3, 3, 2, 3}, {4, 5, 5, 3, 5, 5, 5, 5}, {5, 5, 5, 5, 5, 5, 5, 5}, {6, 6, 6, 6, 6, 6, 6, 6},
              {8, 8, 8, 8, 8, 8, 8, 8}}},
    {"bend", {{0, 0, 0, 0, 0, 0, 0, 0}, {0, 0, 0, 0, 0, 0, 0, 0}, {1, 1, 1, 0, 1, 1, 1, 0}, {3, 4, 4, 3, 3, 4, 4, 3},
              {3, 3, 4, 4, 3, 4, 4, 2}}},
};
static const double kSteps[5] = {5.0, 10.0, 25.0, 50.0, 100.0};

// The test's own copies of the rule's tests, so a property row does not trust the code it grades.
static bool Over(const Box& a, const Box& b, int pad)
{
    return !(a.x1 + pad < b.x0 || b.x1 + pad < a.x0 || a.y1 + pad < b.y0 || b.y1 + pad < a.y0);
}
static bool Corners(const Box& b)
{
    const int xs[2] = {b.x0, b.x1}, ys[2] = {b.y0, b.y1};
    for (int i = 0; i < 2; ++i)
        for (int j = 0; j < 2; ++j)
            if ((xs[i] - 120.0) * (xs[i] - 120.0) + (ys[j] - 120.0) * (ys[j] - 120.0) > 116.0 * 116.0) return false;
    return true;
}
static bool Clear(const Box& b, double inner)
{
    const double nx = std::min(std::max(120.0, (double)b.x0), (double)b.x1);
    const double ny = std::min(std::max(120.0, (double)b.y0), (double)b.y1);
    return (nx - 120.0) * (nx - 120.0) + (ny - 120.0) * (ny - 120.0) >= inner * inner;
}
static int TrueCap(double stepMi) { return stepMi >= 60 ? 8 : (stepMi >= 30 ? 6 : 5); }

struct Frame {
    std::vector<Box> airports, others;
    std::vector<mapplaces::Placed> placed;
    wxlayer::View v;
};

static void BuildFrame(const bmpk::Package& pk, const mapplaces::List& list, double stepMi, int up, Frame* fr)
{
    const double lat = pk.lat0Micro / 1e6, lon = pk.lon0Micro / 1e6;
    DevView dv;
    if (!DeviceView(lat, lon, stepMi, up, &dv)) {
        std::printf("the RIG: no ladder step %.0f\n", stepMi);
        g_rig = true;
        return;
    }
    fr->v = dv.v;
    std::vector<Box> fixed(2 * AIRPORT_COUNT + 8);
    int nFixed = 0;
    for (size_t i = 0; i < AIRPORT_COUNT; ++i) {
        const Airport& ap = AIRPORTS[i];
        if (fabsf(ap.lat - (float)lat) > (float)dv.v.rLat || fabsf(ap.lon - (float)lon) > (float)dv.v.rLon) continue;
        const std::pair<int, int> xy = radarproj::Project(ap.lat, ap.lon, lat, lon, dv.v.rLat, dv.v.rLon, dv.v.rotSin,
                                                          dv.v.rotCos, 240);
        Box mark, code;
        radarbox::Airport(xy.first, xy.second, (int)std::strlen(ap.code), &mark, &code);
        fr->airports.push_back(mark);
        fr->airports.push_back(code);
        nFixed += mapplaces::AirportObstacles(xy.first, xy.second, ap.code, &fixed[nFixed]);
    }
    Box ring[3], card[4];
    mapplaces::RingLabels(dv.viewRadiusDisplay, "mi", ring);
    mapplaces::Cardinals(up, card);
    for (int i = 0; i < 3; ++i) fr->others.push_back(ring[i]);
    for (int i = 0; i < 4; ++i) fr->others.push_back(card[i]);
    fr->others.push_back(mapplaces::IndicatorRow());
    for (const Box& b : fr->others) fixed[nFixed++] = b;
    std::vector<mapplaces::Placed> out(mapplaces::kMaxPlaces);
    const int n = mapplaces::Place(list, pk, dv.v, (int)std::lround(dv.viewRadiusDisplay), fixed.data(), nFixed,
                                   out.data());
    out.resize(n);
    fr->placed = out;
}

// The TRUE greedy's considered towns (rank order, true anchors, true obstacles, the true cap), as the reading defines
// the rank row: written here, apart from MapPlaces.h, so a plant there cannot reach it.
static void TrueConsidered(const bmpk::Package& pk, const mapplaces::List& list, const Frame& fr, double stepMi,
                           std::vector<std::pair<int, std::pair<double, double>>>* cons, std::vector<int>* rankPos)
{
    std::vector<int> order(list.n);
    for (int i = 0; i < list.n; ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
        const uint32_t pa = list.t[a].pop ? list.t[a].pop : 1000u, pb = list.t[b].pop ? list.t[b].pop : 1000u;
        return pa > pb;
    });
    rankPos->assign(list.n, 0);
    for (int k = 0; k < list.n; ++k) (*rankPos)[order[k]] = k;
    const maplayer::Affine a = maplayer::AffineFor(pk, pk.radiusMm, fr.v);
    const double inner = 0.17 * 120.0 + 4;
    std::vector<Box> placed;
    const int cap = TrueCap(stepMi);
    for (int k = 0; k < list.n && (int)placed.size() < cap; ++k) {
        const mapplaces::Town& t = list.t[order[k]];
        const double x = a.ax + a.bx * t.qx + a.cx * t.qy, y = a.ay + a.by * t.qx + a.cy * t.qy;
        cons->push_back({order[k], {x, y}});
        if ((x - 120) * (x - 120) + (y - 120) * (y - 120) < inner * inner) continue;
        if (std::fabs(x - 120) > 1e6 || std::fabs(y - 120) > 1e6) continue;
        const int ix = (int)x, iy = (int)y, w = (int)std::strlen(t.label) * 6 - 1;
        Box c[2] = {{ix - 1, iy - 4, ix + 4 + w, iy + 4}, {ix - 5 - w, iy - 4, ix + 2, iy + 4}};
        if (x < 120) std::swap(c[0], c[1]);
        for (int s = 0; s < 2; ++s) {
            bool ok = Corners(c[s]) && Clear(c[s], inner);
            for (const Box& f : fr.airports) ok = ok && !Over(c[s], f, 4);
            for (const Box& f : fr.others) ok = ok && !Over(c[s], f, 4);
            for (const Box& p : placed) ok = ok && !Over(c[s], p, 4);
            if (ok) {
                placed.push_back(c[s]);
                break;
            }
        }
    }
}

// A synthetic package with n places (and an empty PROVENANCE), for CB6 256.
static void PutVarint(std::vector<uint8_t>& b, uint64_t v)
{
    do {
        uint8_t x = v & 0x7F;
        v >>= 7;
        if (v) x |= 0x80;
        b.push_back(x);
    } while (v);
}
static void Put32(std::vector<uint8_t>& b, size_t at, uint32_t v)
{
    for (int i = 0; i < 4; ++i) b[at + i] = (uint8_t)(v >> (8 * i));
}
static std::vector<uint8_t> SynPlaces(int n)
{
    std::vector<uint8_t> body;
    for (int i = 0; i < n; ++i) {
        PutVarint(body, (uint32_t)((i << 1) ^ (i >> 31)));   // qx = i, zigzag
        PutVarint(body, 0);                                   // qy = 0
        PutVarint(body, (uint32_t)(n - i));                   // pop
        char name[16];
        std::snprintf(name, sizeof name, "P%d", i);
        body.push_back((uint8_t)std::strlen(name));
        body.insert(body.end(), name, name + std::strlen(name));
    }
    const uint32_t dirEnd = bmpk::kHeaderBytes + 2 * bmpk::kEntryBytes;
    std::vector<uint8_t> p(dirEnd, 0);
    p[0] = 'B'; p[1] = 'M'; p[2] = 'P'; p[3] = 'K';
    p[4] = 1;
    p[5] = 2;
    Put32(p, 16, 10000000);   // radiusMm: 10 km
    Put32(p, 20, 1000);       // toleranceMm
    p[32] = 5;                // PLACES
    Put32(p, 36, (uint32_t)n);
    Put32(p, 40, dirEnd);
    Put32(p, 44, (uint32_t)body.size());
    p[48] = 7;                // PROVENANCE, empty: 0 lines
    Put32(p, 52, 0);
    Put32(p, 56, dirEnd + (uint32_t)body.size());
    Put32(p, 60, 0);
    p.insert(p.end(), body.begin(), body.end());
    Put32(p, 24, (uint32_t)p.size());
    Put32(p, 28, bmpk::detail::Crc32ZeroingField(p.data(), (uint32_t)p.size()));
    return p;
}

int main(int argc, char** argv)
{
    if (argc != 2) {
        std::printf("usage: test_map_places <test/fixtures/wx>\n");
        return 2;
    }
    const std::string root = argv[1];
    static mapplaces::List lists[3];
    std::vector<uint8_t> bytes[3];
    bmpk::Package pks[3];
    for (int f = 0; f < 3; ++f) {
        bytes[f] = Read(root + "/basemap/" + kTable[f].name + ".bmpk");
        if (g_rig) return 2;
        if (bmpk::Validate(bytes[f].data(), bytes[f].size(), &pks[f]) != bmpk::Status::kOk) {
            std::printf("the RIG: a fixture is refused\n");
            return 2;
        }
        mapplaces::Load(pks[f], &lists[f]);
    }

    bool cap = true, rank = true, centre = true, corners = true, airports = true, fixedOk = true, labels = true;
    bool cb7[8] = {true, true, true, true, true, true, true, true};
    const double inner = 0.17 * 120.0 + 4;
    for (int f = 0; f < 3; ++f) {
        const bmpk::Package& pk = pks[f];
        const mapplaces::List& list = lists[f];
        const double lat0 = pk.lat0Micro / 1e6, lon0 = pk.lon0Micro / 1e6;
        const double c0 = std::max(std::cos(lat0 * (3.14159265358979323846 / 180)), 0.01);
        const double kmPerQ = pk.radiusMm / 1e6 / 32767.0;
        for (int s = 0; s < 5; ++s) {
            bool row = true;
            for (int r = 0; r < 8; ++r) {
                const int up = r * 45;
                Frame fr;
                BuildFrame(pk, list, kSteps[s], up, &fr);
                if (g_rig) return 2;
                if ((int)fr.placed.size() != kTable[f].counts[s][r]) row = false;
                if ((int)fr.placed.size() > TrueCap(kSteps[s])) cap = false;
                std::vector<int> placedTowns;
                for (size_t i = 0; i < fr.placed.size(); ++i) {
                    const Box& b = fr.placed[i].box;
                    placedTowns.push_back(fr.placed[i].town);
                    if (!Corners(b)) corners = false;
                    if (!Clear(b, inner)) centre = false;
                    for (const Box& a : fr.airports)
                        if (Over(b, a, 4)) airports = false;
                    for (const Box& o : fr.others)
                        if (Over(b, o, 4)) fixedOk = false;
                    for (size_t j = i + 1; j < fr.placed.size(); ++j)
                        if (Over(b, fr.placed[j].box, 4)) labels = false;
                }
                // CB6 rank, as the reading defines it.
                std::vector<std::pair<int, std::pair<double, double>>> cons;
                std::vector<int> pos;
                TrueConsidered(pk, list, fr, kSteps[s], &cons, &pos);
                for (const auto& c : cons) {
                    const int t = c.first;
                    const double x = c.second.first, y = c.second.second;
                    if (std::find(placedTowns.begin(), placedTowns.end(), t) != placedTowns.end()) continue;
                    if ((x - 120) * (x - 120) + (y - 120) * (y - 120) < inner * inner) continue;
                    if (std::fabs(x - 120) > 1e6 || std::fabs(y - 120) > 1e6) continue;
                    std::vector<Box> above;
                    for (const auto& p : fr.placed)
                        if (pos[p.town] < pos[t]) above.push_back(p.box);
                    const int ix = (int)x, iy = (int)y, w = (int)std::strlen(list.t[t].label) * 6 - 1;
                    const Box cand[2] = {{ix - 1, iy - 4, ix + 4 + w, iy + 4}, {ix - 5 - w, iy - 4, ix + 2, iy + 4}};
                    for (int k = 0; k < 2; ++k) {
                        bool ok = Corners(cand[k]) && Clear(cand[k], inner);
                        for (const Box& a : fr.airports) ok = ok && !Over(cand[k], a, 4);
                        for (const Box& o : fr.others) ok = ok && !Over(cand[k], o, 4);
                        for (const Box& a : above) ok = ok && !Over(cand[k], a, 4);
                        if (ok) rank = false;
                    }
                }
                // CB7: every town's anchor against Build's float mapping and against radarproj::Project.
                const maplayer::Affine a = mapplaces::AnchorAffine(pk, fr.v);
                const maplayer::AffineF ff = maplayer::Fast(maplayer::AffineFor(pk, pk.radiusMm, fr.v));
                for (int i = 0; i < list.n; ++i) {
                    double x, y;
                    mapplaces::Anchor(a, list.t[i].qx, list.t[i].qy, &x, &y);
                    float fx, fy;
                    maplayer::ToScreen(ff, list.t[i].qx, list.t[i].qy, &fx, &fy);
                    if (std::fabs(x - fx) > 0.5 || std::fabs(y - fy) > 0.5) cb7[r] = false;
                    const double lat = lat0 - list.t[i].qy * kmPerQ / 111.0;
                    const double lon = lon0 + list.t[i].qx * kmPerQ / (111.0 * c0);
                    const std::pair<int, int> pr = radarproj::Project((float)lat, (float)lon, lat0, lon0, fr.v.rLat,
                                                                      fr.v.rLon, fr.v.rotSin, fr.v.rotCos, 240);
                    // The Project half, as ruled (A): what is DRAWN, the label's integer anchor, against Project's int
                    // result, at most 1 apart per axis. Two truncations of nearly equal values differ by at most 1, so
                    // the bound is exact; the first run compared a double with a float's truncation, which it is not.
                    if (std::fabs(x) < 1e5 && std::fabs(y) < 1e5 &&
                        (std::abs((int)x - pr.first) > 1 || std::abs((int)y - pr.second) > 1))
                        cb7[r] = false;
                }
            }
            char nm[48];
            std::snprintf(nm, sizeof nm, "CB6 %s %.0fmi", kTable[f].name, kSteps[s]);
            Check(row, nm);
        }
    }
    Check(cap, "CB6 cap");
    Check(rank, "CB6 rank");
    Check(centre, "CB6 centre");
    Check(corners, "CB6 corners");
    Check(airports, "CB6 airports");
    Check(fixedOk, "CB6 fixed");
    Check(labels, "CB6 labels");

    // CB6 256: 256 places accepted, 257 refused whole (no towns).
    {
        static mapplaces::List l256, l257;
        const std::vector<uint8_t> p256 = SynPlaces(256), p257 = SynPlaces(257);
        bmpk::Package a, b;
        const bool v256 = bmpk::Validate(p256.data(), p256.size(), &a) == bmpk::Status::kOk;
        const bool v257 = bmpk::Validate(p257.data(), p257.size(), &b) == bmpk::Status::kOk;
        if (!v256 || !v257) {
            std::printf("the RIG: a synthetic package is refused by Validate\n");
            return 2;
        }
        const bool ok256 = mapplaces::Load(a, &l256) && l256.n == 256 && !l256.refused;
        const bool ok257 = !mapplaces::Load(b, &l257) && l257.n == 0 && l257.refused;
        Check(ok256 && ok257, "CB6 256");
    }

    // CB6 eq: the repeated arithmetic equals the Weatherscope's over every case they share.
    {
        bool eq = true;
        // Rank against RankOrder: ties, zeros, and a long pseudo-random list.
        static mapplaces::List l;
        uint32_t seed = 12345;
        for (int len : {1, 2, 10, 64, 256}) {
            l.n = len;
            std::vector<uint32_t> pops(len);
            for (int i = 0; i < len; ++i) {
                seed = seed * 1103515245u + 12345u;
                const uint32_t r = (seed >> 16) % 7;
                pops[i] = r == 0 ? 0u : (r == 1 ? 1000u : (seed >> 8) % 50000u);
                l.t[i].pop = pops[i];
            }
            std::vector<int> a(len), b(len);
            mapplaces::Rank(l, a.data());
            wxscreens::RankOrder(pops.data(), len, b.data());
            if (a != b) eq = false;
        }
        for (double R : {1.0, 5.0, 10.0, 15.0, 25.0, 30.0, 50.0, 60.0, 100.0, 222.0})
            if (mapplaces::Inner(R) != wxscreens::Inner(R)) eq = false;
        for (int x0 = -10; x0 < 250; x0 += 13)
            for (int y0 = -10; y0 < 250; y0 += 17)
                for (int w : {0, 5, 40})
                    for (int h : {0, 8}) {
                        const Box m{x0, y0, x0 + w, y0 + h};
                        const wxscreens::Box ws{x0, y0, x0 + w, y0 + h};
                        if (mapplaces::InsideCircle(m) != wxscreens::InsideCircle(ws)) eq = false;
                        if (mapplaces::ClearsCentre(m, 24.4) != wxscreens::ClearsCentre(ws, 24.4)) eq = false;
                        const Box o{100, 100, 130, 108};
                        const wxscreens::Box wo{100, 100, 130, 108};
                        for (int pad : {0, 4})
                            if (mapplaces::Overlaps(m, o, pad) != wxscreens::Overlaps(ws, wo, pad)) eq = false;
                    }
        // The candidates against PlaceTown, with no obstacle, where the Weatherscope's y band does not decide.
        const char* labels6[3] = {"A", "Bend", "Wonderful Long Town"};
        for (const char* lab : labels6)
            for (double x = 20.5; x < 220; x += 7.25)
                for (double y = 60.5; y < 180; y += 9.5) {
                    const wxscreens::Placed p = wxscreens::PlaceTown(lab, x, y, 24.4, nullptr, 0, nullptr, 0);
                    Box c[2];
                    int txs[2];
                    char sides[2];
                    mapplaces::Candidates(x, y, lab, c, txs, sides);
                    int pick = -1;
                    if ((x - 120) * (x - 120) + (y - 120) * (y - 120) >= 24.4 * 24.4)
                        for (int k = 0; k < 2 && pick < 0; ++k)
                            if (mapplaces::InsideCircle(c[k]) && mapplaces::ClearsCentre(c[k], 24.4) &&
                                c[k].y0 >= 26 && c[k].y1 <= 192)
                                pick = k;
                    if (p.placed != (pick >= 0)) eq = false;
                    if (pick >= 0 && (p.box.x0 != c[pick].x0 || p.box.y0 != c[pick].y0 || p.box.x1 != c[pick].x1 ||
                                      p.box.y1 != c[pick].y1 || p.side != sides[pick] || p.tx != txs[pick]))
                        eq = false;
                }
        // LabelText against wxlogic::LabelText.
        for (const char* nm : {"Bend city", "Bend", "x city", " city", "city", "Salt Lake City city",
                               "A very long place name that runs past thirty-nine bytes city"}) {
            char a[40], b[40];
            mapplaces::LabelText(nm, (uint32_t)std::strlen(nm), a);
            wxlogic::LabelText(nm, b, sizeof b);
            if (std::strcmp(a, b) != 0) eq = false;
        }
        Check(eq, "CB6 eq");
    }

    for (int r = 0; r < 8; ++r) {
        char nm[24];
        std::snprintf(nm, sizeof nm, "CB7 r%d", r * 45);
        Check(cb7[r], nm);
    }

    std::printf("%d checks, %d failures\n", g_checks, g_fails);
    if (g_checks != 32) {
        std::printf("the RIG: %d cases, not 32\n", g_checks);
        return 2;
    }
    return g_fails ? 1 : 0;
}
