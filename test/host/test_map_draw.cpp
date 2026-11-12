// Step B's composer and stale test (docs/blipscope-map-client-steps.md, "MapCompose.h (CB1-CB5, CB9, CB11)" and its
// Q-MC14 re-freeze): include/MapCompose.h held to its 26 registered rows over the KJAC fixture.
//     test_map_draw <test/fixtures/wx>
// Exit 0 all pass, 1 any fail, 2 the rig (the fixture missing or refused, a free of a block this test never gave out, or
// a case count other than 26). Prints no coordinate and no name (CD2): counts only.
//
// Everything the composer is graded on is counted from OUTSIDE it: the allocator here gives out and takes back every
// block (and fails the one it is told to), and every Build call goes through a counting wrapper. Each row builds its own
// Map, reads the live bytes against its own start, and releases the Map at its end, so a plant that leaks in one row
// cannot reach the next.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "Airports.h"
#include "MapCompose.h"
#include "RadarProjection.h"
#include "RadarZoom.h"

using mapcompose::Box;
using mapcompose::kPixels;

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

// ------------------------------------------------------------------------------------- the allocator, counted

static std::map<void*, size_t> g_blocks;
static size_t g_live = 0;
static int g_calls = 0, g_failAt = -1;

static void* Alloc(size_t n)
{
    const int i = g_calls++;
    if (i == g_failAt) return nullptr;
    void* p = std::malloc(n);
    if (p) {
        g_blocks[p] = n;
        g_live += n;
    }
    return p;
}

static void Free(void* p)
{
    std::map<void*, size_t>::iterator it = g_blocks.find(p);
    if (it == g_blocks.end()) {
        std::printf("the RIG: a free of a block this test never gave out\n");
        g_rig = true;
        return;
    }
    g_live -= it->second;
    g_blocks.erase(it);
    std::free(p);
}

// A row's Map: allocations counted from its creation, the failure (if any) armed before Init.
static size_t Start(mapcompose::Map& m, int failAt)
{
    g_calls = 0;
    g_failAt = failAt;
    const size_t live0 = g_live;
    mapcompose::Init(m, Alloc, Free);
    return live0;
}

static void End(mapcompose::Map& m)
{
    g_failAt = -1;
    mapcompose::Release(m);
}

// ------------------------------------------------------------------------------------------ Build, counted

static int g_builds = 0;
static const uint16_t kM = 0x6B6B;   // byte-symmetric: CB1 reads order alone, CB2 reads the byte order

static bool BuildReal(const bmpk::Package& pk, const wxlayer::View& v, uint32_t asked, uint8_t layers,
                      const maplayer::Theme& t, const maplayer::Scratch& sc, uint16_t* out, maplayer::BuildInfo* info,
                      maplayer::ClockUs clock)
{
    ++g_builds;
    return maplayer::Build(pk, v, asked, layers, t, sc, out, info, clock);
}

static bool BuildM(const bmpk::Package&, const wxlayer::View&, uint32_t, uint8_t, const maplayer::Theme&,
                   const maplayer::Scratch&, uint16_t* out, maplayer::BuildInfo*, maplayer::ClockUs)
{
    ++g_builds;
    for (int y = 0; y < mapcompose::kSize; ++y)
        for (int x = 0; x < mapcompose::kSize; ++x)
            out[(size_t)y * mapcompose::kSize + x] = wxlayer::OffDisc(x, y, mapcompose::kSize) ? 0 : kM;
    return true;
}

static bool BuildFail(const bmpk::Package&, const wxlayer::View&, uint32_t, uint8_t, const maplayer::Theme&,
                      const maplayer::Scratch&, uint16_t*, maplayer::BuildInfo*, maplayer::ClockUs)
{
    ++g_builds;
    return false;
}

// ----------------------------------------------------------------------------------------------- the airports

// DrawAirports' own cull and projection over the baked table (as test_map_places.cpp's frame does).
static int AirportsBaked(void* ctx, Box* out, int maxAirports)
{
    const wxlayer::View& v = *(const wxlayer::View*)ctx;
    int n = 0;
    for (size_t i = 0; i < AIRPORT_COUNT; ++i) {
        const Airport& ap = AIRPORTS[i];
        if (std::fabs(ap.lat - (float)v.homeLat) > (float)v.rLat || std::fabs(ap.lon - (float)v.homeLon) > (float)v.rLon)
            continue;
        if (n < maxAirports) {
            const std::pair<int, int> xy =
                radarproj::Project(ap.lat, ap.lon, v.homeLat, v.homeLon, v.rLat, v.rLon, v.rotSin, v.rotCos, 240);
            mapplaces::AirportObstacles(xy.first, xy.second, ap.code, &out[2 * n]);
        }
        ++n;
    }
    return n;
}

// 129 airports, all far off the screen: none can block a name, so only the count can refuse them.
static int Airports129(void*, Box* out, int maxAirports)
{
    for (int i = 0; i < 129 && i < maxAirports; ++i) mapplaces::AirportObstacles(-1000, -1000, "ZZZ", &out[2 * i]);
    return 129;
}

// ------------------------------------------------------------------------------------------------- the view

// The device's own View at a ladder step, transcribed from test_map_places.cpp's DeviceView (default radius 100 mi,
// unit mi): src/AircraftManager.cpp:874-904, :5506-5516 and :1447-1449.
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

// The key a device at the package's centre holds at 100 mi, north up: the view, the step, the eight fixed boxes.
static bool MakeKey(const bmpk::Package& pk, mapcompose::Key* k)
{
    DevView dv;
    if (!DeviceView(pk.lat0Micro / 1e6, pk.lon0Micro / 1e6, 100.0, 0, &dv)) return false;
    k->view = dv.v;
    k->radiusMi = (int)std::lround(dv.viewRadiusDisplay);
    k->pkSeq = 1;
    k->airportsGen = 1;
    Box ring[3], card[4];
    mapplaces::RingLabels(dv.viewRadiusDisplay, "mi", ring);
    mapplaces::Cardinals(0, card);
    k->nFixed = 0;
    for (int i = 0; i < 3; ++i) k->fixed[k->nFixed++] = ring[i];
    for (int i = 0; i < 4; ++i) k->fixed[k->nFixed++] = card[i];
    k->fixed[k->nFixed++] = mapplaces::IndicatorRow();
    return true;
}

static bool OffDisc(size_t p)
{
    return wxlayer::OffDisc((int)(p % mapcompose::kSize), (int)(p / mapcompose::kSize), mapcompose::kSize);
}

int main(int argc, char** argv)
{
    if (argc < 2) {
        std::printf("usage: test_map_draw <test/fixtures/wx>\n");
        return 2;
    }
    const std::string root = argv[1];
    const std::vector<uint8_t> bytes = Read(root + "/basemap/kjac.bmpk");
    bmpk::Package pk;
    if (g_rig || bmpk::Validate(bytes.data(), bytes.size(), &pk) != bmpk::Status::kOk) {
        std::printf("the RIG: the fixture package is missing or refused\n");
        return 2;
    }
    mapcompose::Key K;
    if (!MakeKey(pk, &K)) {
        std::printf("the RIG: no 100 mi ladder step\n");
        return 2;
    }
    wxlayer::View ctxView = K.view;
    mapcompose::Inputs real;
    real.build = BuildReal;
    real.airports = AirportsBaked;
    real.ctx = &ctxView;
    mapcompose::Inputs synth = real;
    synth.build = BuildM;
    mapcompose::Inputs failing = real;
    failing.build = BuildFail;

    std::vector<uint16_t> wx1(kPixels, 0), wx2(kPixels, 0), zero(kPixels, 0);
    for (size_t p = 0; p < kPixels; ++p) {
        if (p % 7 == 0) wx1[p] = 0x0F0F;
        if (p % 5 == 0) wx2[p] = 0x3C3C;
    }

    // ---------------------------------------------------------------------------------------------- CB1 compose
    {
        std::vector<uint16_t> L(kPixels), W(kPixels, 0), out(kPixels);
        const uint16_t P = 0x1234, Wv = 0x0F0F;
        for (int y = 0; y < mapcompose::kSize; ++y)
            for (int x = 0; x < mapcompose::kSize; ++x) {
                const size_t p = (size_t)y * mapcompose::kSize + x;
                L[p] = ((x + y) % 3 == 0) ? P : kM;
                if (x >= 200 || (y >= 100 && y < 110)) W[p] = Wv;   // across the disc's edge, and over name pixels
            }
        mapcompose::Compose(L.data(), W.data(), out.data());
        bool same = true;
        int weatherOverName = 0, offDisc = 0, layerShown = 0;
        for (size_t p = 0; p < kPixels; ++p) {
            const uint16_t want = OffDisc(p) ? 0 : (W[p] != 0 ? W[p] : L[p]);
            if (out[p] != want) same = false;
            if (OffDisc(p)) ++offDisc;
            else if (W[p] != 0 && L[p] == P) ++weatherOverName;
            else if (W[p] == 0) ++layerShown;
        }
        Check(same && weatherOverName > 0 && offDisc > 0 && layerShown > 0, "CB1 compose");
    }

    // ------------------------------------------------------------------------------------------------ CB1 names
    {
        mapcompose::Map m;
        Start(m, -1);
        bool ok = mapcompose::Frame(m, &pk, K, nullptr, 0, real) != nullptr && m.nPlaced >= 1;
        if (ok) {
            const mapplaces::Town t = m.places->t[m.placed[0].town];
            m.places->n = 1;
            m.places->t[0] = t;
            ok = mapcompose::Rebuild(m, pk, K, synth) && m.nPlaced == 1;
        }
        int touched = 0;
        if (ok) {
            std::vector<uint16_t> e0(kPixels, 0), e1(kPixels, 0xFFFF);
            mapcompose::DrawLabel(e0.data(), m.placed[0], m.places->t[0].label);
            mapcompose::DrawLabel(e1.data(), m.placed[0], m.places->t[0].label);
            for (size_t p = 0; p < kPixels; ++p) {
                const bool label = e0[p] == e1[p];
                if (label) ++touched;
                const uint16_t want = label ? e0[p] : (OffDisc(p) ? 0 : kM);
                if (m.layer[p] != want) ok = false;
            }
        }
        Check(ok && touched > 0, "CB1 names");
        End(m);
    }

    // ------------------------------------------------------------------------------------------------------ CB2
    {
        mapcompose::Map m;
        Start(m, -1);
        std::vector<uint16_t> A(kPixels), B(kPixels), R(kPixels);
        const uint16_t* a = mapcompose::Frame(m, &pk, K, zero.data(), 1, real);
        if (a) std::memcpy(A.data(), a, kPixels * 2);
        // The same key, rebuilt directly with an empty list: CB2 reads the byte order, not the key.
        m.places->n = 0;
        const bool again = mapcompose::Rebuild(m, pk, K, real);
        const uint16_t* b = mapcompose::Frame(m, &pk, K, zero.data(), 1, real);
        if (b) std::memcpy(B.data(), b, kPixels * 2);
        // Build's own output, called directly with buffers of this test's own.
        const size_t cap = (size_t)pk.maxVertices + 1;
        std::vector<int32_t> qx(cap), qy(cap);
        std::vector<double> x(cap), y(cap), cr(cap);
        std::vector<uint8_t> idx(kPixels);
        maplayer::Scratch sc;
        sc.qx = qx.data();
        sc.qy = qy.data();
        sc.x = x.data();
        sc.y = y.data();
        sc.cross = cr.data();
        sc.cap = (uint32_t)cap;
        sc.idx = idx.data();
        const bool refOk = maplayer::Build(pk, K.view, 0, maplayer::kLayersAll, maplayer::kThemes[maplayer::kThemeDark],
                                           sc, R.data());
        int shared = 0, wrong = 0;
        for (size_t p = 0; p < kPixels; ++p) {
            if (A[p] != B[p]) continue;
            ++shared;
            if (A[p] != mapcompose::Swap(R[p])) ++wrong;
        }
        std::printf("       CB2: %d shared pixels, %d named\n", shared, (int)kPixels - shared);
        Check(a && again && b && refOk && shared > 0 && wrong == 0, "CB2");
        End(m);
    }

    // ------------------------------------------------------------------------------------------------------ CB3
    {
        mapcompose::Map m;
        const size_t live0 = Start(m, -1);
        g_builds = 0;
        std::vector<uint16_t> dst(kPixels), black(kPixels, 0);
        bool allNull = true, untouched = true;
        for (int i = 0; i < 200; ++i) {
            const bool shown = i < 100;
            const std::vector<uint16_t>& today = shown ? wx1 : black;
            dst = today;
            const uint16_t* b = mapcompose::Frame(m, nullptr, K, shown ? wx1.data() : nullptr, 1, real);
            if (b) {
                allNull = false;
                std::memcpy(dst.data(), b, kPixels * 2);
            }
            if (dst != today) untouched = false;
        }
        Check(g_live == live0 && g_calls == 0, "CB3 alloc");
        Check(g_builds == 0, "CB3 build");
        Check(allNull && untouched, "CB3 today");
        End(m);
    }

    // ------------------------------------------------------------------------------------------------------ CB4
    const std::vector<std::pair<const char*, std::function<void(mapcompose::Key&)>>> inputs = {
        {"CB4 homeLat", [](mapcompose::Key& k) { k.view.homeLat += 0.01; }},
        {"CB4 homeLon", [](mapcompose::Key& k) { k.view.homeLon += 0.01; }},
        {"CB4 rLat", [](mapcompose::Key& k) { k.view.rLat *= 0.5; }},
        {"CB4 rLon", [](mapcompose::Key& k) { k.view.rLon *= 0.5; }},
        {"CB4 rotSin", [](mapcompose::Key& k) { k.view.rotSin = 0.5f; }},
        {"CB4 rotCos", [](mapcompose::Key& k) { k.view.rotCos = 0.5f; }},
        {"CB4 radiusMi", [](mapcompose::Key& k) { k.radiusMi = 50; }},
        {"CB4 package", [](mapcompose::Key& k) { ++k.pkSeq; }},
        {"CB4 airports", [](mapcompose::Key& k) { ++k.airportsGen; }},
        {"CB4 fixed", [](mapcompose::Key& k) { k.fixed[0].x1 += 6; }},   // one ring label one character wider
    };
    for (const auto& in : inputs) {
        mapcompose::Map m;
        Start(m, -1);
        mapcompose::Frame(m, &pk, K, wx1.data(), 1, synth);
        mapcompose::Key K2 = K;
        in.second(K2);
        g_builds = 0;
        mapcompose::Frame(m, &pk, K2, wx1.data(), 1, synth);
        Check(g_builds == 1, in.first);
        End(m);
    }
    {
        mapcompose::Map m;
        Start(m, -1);
        mapcompose::Frame(m, &pk, K, wx1.data(), 1, synth);
        g_builds = 0;
        for (int i = 0; i < 1000; ++i) mapcompose::Frame(m, &pk, K, wx1.data(), 1, synth);
        Check(g_builds == 0, "CB4 steady");
        End(m);
    }
    {
        mapcompose::Map m;
        Start(m, -1);
        g_builds = 0;
        bool allNull = mapcompose::Frame(m, &pk, K, wx1.data(), 1, failing) == nullptr;
        for (int i = 0; i < 1000; ++i)
            if (mapcompose::Frame(m, &pk, K, wx1.data(), 1, failing) != nullptr) allNull = false;
        Check(g_builds == 1 && allNull, "CB4 buildfail");
        End(m);
    }

    // ------------------------------------------------------------------------------------------------------ CB5
    {
        mapcompose::Map m;
        Start(m, -1);
        mapcompose::Frame(m, &pk, K, wx1.data(), 1, synth);
        const uint32_t c0 = m.composes;
        g_builds = 0;
        mapcompose::Frame(m, &pk, K, wx2.data(), 2, synth);
        Check(m.composes == c0 + 1 && g_builds == 0, "CB5");
        End(m);
    }

    // ------------------------------------------------------------------------------------------------------ CB9
    // Step C (Q-C3) adds the sweep's slice and spans, allocated after the scratch: six buffers, all or none.
    const char* const kBuffers[6] = {"layer", "composite", "index", "scratch", "slice", "spans"};
    for (int k = 0; k < 6; ++k) {
        mapcompose::Map m;
        const size_t live0 = Start(m, k);
        g_builds = 0;
        std::vector<uint16_t> dst(kPixels);
        bool allNull = true, untouched = true;
        for (int i = 0; i < 10; ++i) {
            dst = wx1;
            const uint16_t* b = mapcompose::Frame(m, &pk, K, wx1.data(), 1, synth);
            if (b) {
                allNull = false;
                std::memcpy(dst.data(), b, kPixels * 2);
            }
            if (dst != wx1) untouched = false;
        }
        Check(g_live == live0 && m.refused && g_builds == 0 && allNull && untouched,
              std::string("CB9 ") + kBuffers[k]);
        End(m);
    }
    {
        mapcompose::Map m;
        const size_t live0 = Start(m, 0);
        const bool refusedFirst = mapcompose::Frame(m, &pk, K, wx1.data(), 1, synth) == nullptr && m.refused;
        g_failAt = -1;
        mapcompose::Forget(m);
        const uint16_t* b = mapcompose::Frame(m, &pk, K, wx1.data(), 1, synth);
        Check(refusedFirst && b != nullptr && g_live - live0 == mapcompose::Price(pk.maxVertices), "CB9 forget");
        End(m);
    }
    {
        mapcompose::Map m;
        const size_t live0 = Start(m, -1);
        const bool first = mapcompose::Frame(m, &pk, K, wx1.data(), 1, real) != nullptr;
        bmpk::Package bigger = pk;
        bigger.maxVertices = pk.maxVertices * 2 + 1;
        mapcompose::Key K2 = K;
        ++K2.pkSeq;
        const uint16_t* b = mapcompose::Frame(m, &bigger, K2, wx1.data(), 1, real);
        Check(first && b != nullptr && g_live - live0 == mapcompose::Price(bigger.maxVertices), "CB9 grow");
        End(m);
    }

    // ----------------------------------------------------------------------------------------------------- CB11
    {
        mapcompose::Map m;
        Start(m, -1);
        mapcompose::Inputs many = real;
        many.airports = Airports129;
        std::vector<uint16_t> A(kPixels), B(kPixels);
        const uint16_t* a = mapcompose::Frame(m, &pk, K, nullptr, 0, many);
        if (a) std::memcpy(A.data(), a, kPixels * 2);
        const uint32_t refusedOnce = m.namesRefused;
        m.places->n = 0;
        const bool again = mapcompose::Rebuild(m, pk, K, many);   // the same key, directly, with an empty list
        const uint16_t* b = mapcompose::Frame(m, &pk, K, nullptr, 0, many);
        if (b) std::memcpy(B.data(), b, kPixels * 2);
        Check(a && again && b && refusedOnce == 1 && A == B, "CB11 airports 129");
        End(m);
    }

    std::printf("%d checks, %d failures\n", g_checks, g_fails);
    if (g_rig) {
        std::printf("the RIG: see above\n");
        return 2;
    }
    if (g_checks != 26) {
        std::printf("the RIG: %d cases, not 26\n", g_checks);
        return 2;
    }
    return g_fails ? 1 : 0;
}
