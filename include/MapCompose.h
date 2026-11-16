// Step B's composer and stale test (docs/blipscope-map-client-steps.md, "MapCompose.h (CB1-CB5, CB9, CB11): the run,
// registered before its code"). Pure C++11, no Arduino: the caller gives the allocator, Build and the airport boxes,
// so the host test counts every allocation and every Build call from outside.
//
// S3'S ORDER: Build's output, byte-swapped to the canvas's order; the place names over it; then every non-zero weather
// pixel over that; 0 off the disc.
//
// THE KEY is everything a rebuild reads: the six wxlayer::View fields the weather layer is built from (CD3), compared
// as the weather layer's own stale test compares them (AircraftManager.cpp:3599-3601); radiusMi, the ladder step (Cap
// and Inner); pkSeq, which the caller changes at every accepted package; airportsGen, which the caller changes when the
// airport list or its filter changes; and the eight other fixed boxes BY VALUE, since a ring label's width follows the
// distance-unit setting, which no view field carries. The theme and the layer mask are constants here, not inputs: a
// runtime theme would have to change a signature, which is a visible act.
//
// MEMORY (Q-MB1, Q-MC14): four buffers through the caller's allocator (the device's is PSRAM), every one attempted
// before any is checked; one failure frees them all and refuses the map. No half composite (CB9). The refusal lasts until
// Forget, which the caller calls at a config change, where step A clears mapAllocFailed ("a new config earns a new
// allocation attempt"). A package needing more scratch than the buffers hold releases them and allocates again, at its
// own size.
//
// Nothing here prints: a nearby town's name says where the device is (CD2).
#pragma once

#include <new>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "BasemapPackage.h"
#include "MapLayer.h"
#include "MapPlaces.h"
#include "WxLayer.h"
#include "../src/weatherscope/WxRaster.h"   // the Weatherscope's label primitives: Fill and Text, behind a Sink

namespace mapcompose {

using Box = mapplaces::Box;

constexpr int kSize = maplayer::kSize;
constexpr size_t kPixels = (size_t)kSize * kSize;
constexpr int kFixedMax = 8;        // the three ring labels, the four cardinals and the indicator row
constexpr int kAirportsMax = 128;   // airports a rebuild can avoid; more refuses the names (CB11)
constexpr int kObstacles = kFixedMax + 2 * kAirportsMax;
constexpr uint8_t kLayers = maplayer::kLayersAll;

typedef void* (*AllocFn)(size_t bytes);
typedef void (*FreeFn)(void* p);
typedef bool (*BuildFn)(const bmpk::Package&, const wxlayer::View&, uint32_t, uint8_t, const maplayer::Theme&,
                        const maplayer::Scratch&, uint16_t*, maplayer::BuildInfo*, maplayer::ClockUs);
// The airports DrawAirports draws at this view, by its own cull: the two boxes (mapplaces::AirportObstacles) of at most
// `maxAirports` of them into `out`, returning how many there are in all, which may be more.
typedef int (*AirportsFn)(void* ctx, Box* out, int maxAirports);

struct Key {
    wxlayer::View view;
    int radiusMi = 0;
    uint32_t pkSeq = 0;
    uint32_t airportsGen = 0;
    Box fixed[kFixedMax] = {};
    int nFixed = 0;
};

struct Inputs {
    BuildFn build = nullptr;
    AirportsFn airports = nullptr;
    void* ctx = nullptr;
    uint32_t askedMm = 0;               // reaches only BuildInfo (F4), so it is not in the key
    maplayer::ClockUs clock = nullptr;
};

struct Map {
    AllocFn alloc = nullptr;
    FreeFn freeFn = nullptr;
    uint16_t* layer = nullptr;       // the map with its names, in the canvas's byte order
    uint16_t* composite = nullptr;   // the layer with the weather over it
    uint8_t* idx = nullptr;          // Build's index image
    uint8_t* scratch = nullptr;      // Build's vertices, the obstacles and the place list, in one block
    uint8_t* slice = nullptr;        // step C: the sweep's slice per pixel (wxsweep::Rasterise), zero between frames
    int16_t* spans = nullptr;        // step C: each row's first and last marked column, 2 x kSize
    maplayer::Scratch sc;
    Box* obstacles = nullptr;
    mapplaces::List* places = nullptr;
    uint32_t placesSeq = 0;
    bool placesLoaded = false;
    bool allocated = false;
    bool refused = false;    // an allocation failed: the map stays off until Forget (the wiring logs mapAllocFailed)
    bool attempted = false;  // a rebuild was attempted for `built`
    bool ready = false;      // a layer is built for `built`: "ready" is a built layer, not allocated buffers
    Key built;
    bool layerFresh = false;     // rebuilt since the last compose
    bool lastShown = false;      // the last frame returned the composite
    uint32_t composedWxGen = 0;
    mapplaces::Placed placed[mapplaces::kMaxLabels];
    int nPlaced = 0;
    uint32_t composes = 0, namesRefused = 0, buildFails = 0;
    maplayer::BuildInfo info;
};

static_assert(sizeof(Map) < 1024, "Q-MC14: the Map struct stays internal, under 1 KB");

inline uint16_t Swap(uint16_t v) { return (uint16_t)((v << 8) | (v >> 8)); }

// ------------------------------------------------------------------------------------------------------ the key

inline bool SameBox(const Box& a, const Box& b) { return a.x0 == b.x0 && a.y0 == b.y0 && a.x1 == b.x1 && a.y1 == b.y1; }

// Everything but the rotation.
inline bool SameRest(const Key& a, const Key& b)
{
    if (a.view.homeLat != b.view.homeLat || a.view.homeLon != b.view.homeLon) return false;
    if (a.view.rLat != b.view.rLat || a.view.rLon != b.view.rLon || a.view.size != b.view.size) return false;
    if (a.radiusMi != b.radiusMi || a.pkSeq != b.pkSeq || a.airportsGen != b.airportsGen) return false;
    if (a.nFixed != b.nFixed) return false;
    for (int i = 0; i < a.nFixed && i < kFixedMax; ++i)
        if (!SameBox(a.fixed[i], b.fixed[i])) return false;
    return true;
}

inline bool Same(const Key& a, const Key& b)
{
    if (a.view.rotSin != b.view.rotSin || a.view.rotCos != b.view.rotCos) return false;   // THE ROTATION
    return SameRest(a, b);
}

// --------------------------------------------------------------------------------------------------- the memory

// The scratch block, Q-MC14's price: Build's x, y and crossings (double) and qx, qy (int32) for maxVertices + 1, the
// obstacles and the place list. The allocator returns memory aligned for a double, so the doubles go first: the host's
// malloc; on the device heap_caps_aligned_alloc(8, n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT). THE CAP IS THE PRICE'S
// PREMISE: without MALLOC_CAP_SPIRAM an aligned alloc may take internal RAM. The wiring's source row checks it.
inline size_t ScratchBytes(uint32_t maxVertices)
{
    const size_t cap = (size_t)maxVertices + 1;
    return cap * (3 * sizeof(double) + 2 * sizeof(int32_t)) + kObstacles * sizeof(Box) + sizeof(mapplaces::List);
}
static_assert(3 * sizeof(double) + 2 * sizeof(int32_t) == 32, "Q-MC14: 32 B per vertex");
static_assert(kObstacles * sizeof(Box) == 4224, "Q-MC14: 264 boxes, 4,224 B");
static_assert(sizeof(mapplaces::List) == 13320, "Q-MC14: the place list, 13,320 B");

// Step C's two (Q-C3, +58,560 B): the sweep's slice, one byte a pixel, and its spans, two int16 a row.
constexpr size_t kSweepBytes = kPixels + 2 * (size_t)kSize * sizeof(int16_t);
static_assert(kSweepBytes == 58560, "Q-C3: the sweep's slice and spans, 58,560 B");

// The six buffers' price, in their allocation order: the layer, the composite, the index, the scratch, the slice,
// the spans.
inline size_t Price(uint32_t maxVertices)
{
    return 2 * kPixels * sizeof(uint16_t) + kPixels + ScratchBytes(maxVertices) + kSweepBytes;
}

// Every buffer freed and the attempt forgotten. The refusal is left as it is.
inline void Release(Map& m)
{
    if (m.freeFn) {
        if (m.layer) m.freeFn(m.layer);
        if (m.composite) m.freeFn(m.composite);
        if (m.idx) m.freeFn(m.idx);
        if (m.scratch) m.freeFn(m.scratch);
        if (m.slice) m.freeFn(m.slice);
        if (m.spans) m.freeFn(m.spans);
    }
    m.layer = nullptr;
    m.composite = nullptr;
    m.idx = nullptr;
    m.scratch = nullptr;
    m.slice = nullptr;
    m.spans = nullptr;
    m.sc = maplayer::Scratch();
    m.obstacles = nullptr;
    m.places = nullptr;
    m.placesLoaded = false;
    m.allocated = false;
    m.attempted = false;
    m.ready = false;
    m.layerFresh = false;
    m.lastShown = false;
    m.nPlaced = 0;
}

inline void Fail(Map& m)
{
    Release(m);
    m.refused = true;
}

inline void AllocFixed(Map& m)
{
    if (!m.layer) m.layer = (uint16_t*)m.alloc(kPixels * sizeof(uint16_t));
    if (!m.composite) m.composite = (uint16_t*)m.alloc(kPixels * sizeof(uint16_t));
    if (!m.idx) m.idx = (uint8_t*)m.alloc(kPixels);
}

// All six or none: every one is attempted before any is checked.
inline bool Allocate(Map& m, uint32_t maxVertices)
{
    AllocFixed(m);
    if (!m.scratch) m.scratch = (uint8_t*)m.alloc(ScratchBytes(maxVertices));
    if (!m.slice) m.slice = (uint8_t*)m.alloc(kPixels);
    if (!m.spans) m.spans = (int16_t*)m.alloc(2 * (size_t)kSize * sizeof(int16_t));
    if (!m.layer || !m.composite || !m.idx || !m.scratch || !m.slice || !m.spans) {
        Fail(m);   // CB9: one failure frees every buffer and refuses the map
        return false;
    }
    const size_t cap = (size_t)maxVertices + 1;
    double* d = (double*)(void*)m.scratch;
    m.sc.x = d;
    m.sc.y = d + cap;
    m.sc.cross = d + 2 * cap;
    int32_t* q = (int32_t*)(d + 3 * cap);
    m.sc.qx = q;
    m.sc.qy = q + cap;
    m.sc.cap = (uint32_t)cap;
    m.sc.idx = m.idx;
    m.obstacles = (Box*)(q + 2 * cap);
    m.places = new (m.obstacles + kObstacles) mapplaces::List();
    memset(m.slice, 0, kPixels);   // step C: no pixel is in the wedge until Rasterise marks it (the second path's
                                   // "the slice is clean", as AllocateWeatherBuffers does for the weather's)
    m.placesLoaded = false;
    m.allocated = true;
    return true;
}

inline void Init(Map& m, AllocFn alloc, FreeFn freeFn)
{
    m.alloc = alloc;
    m.freeFn = freeFn;
}

// At a config change: every buffer freed and the refusal cleared, so the next package allocates again at its own size.
inline void Forget(Map& m)
{
    Release(m);
    m.refused = false;
}

// ----------------------------------------------------------------------------------------------- the drawing

// The canvas's byte order, through the theme's one RGB565 rule.
class LayerSink : public wxraster::Sink {
public:
    explicit LayerSink(uint16_t* px) : px_(px) {}
    void Put(int x, int y, wxraster::Rgb c) override
    {
        const maplayer::Rgb8 c8 = {c.r, c.g, c.b};
        px_[(size_t)y * kSize + x] = Swap(maplayer::To565(c8));
    }

private:
    uint16_t* px_;
};

// One label as the Weatherscope draws one (WxScreens.h:788-789): the 3 x 3 dot, then the text with a 1 px halo. The
// theme's place grey, the halo in its land colour (Q-MB5).
inline void DrawLabel(uint16_t* layer, const mapplaces::Placed& p, const char* label)
{
    const maplayer::Theme& t = maplayer::kThemes[maplayer::kThemeDark];
    const wxraster::Rgb ink = {t.place.r, t.place.g, t.place.b}, halo = {t.land.r, t.land.g, t.land.b};
    LayerSink s(layer);
    wxraster::Fill(s, p.ix - 1, p.iy - 1, p.ix + 1, p.iy + 1, ink);
    wxraster::Text(s, p.tx, p.ty, label, 1, ink, &halo);
}

inline void DrawNames(Map& m, const bmpk::Package& pk, const Key& k, AirportsFn airports, void* ctx)
{
    m.nPlaced = 0;
    int n = 0;
    for (int i = 0; i < k.nFixed && i < kFixedMax; ++i) m.obstacles[n++] = k.fixed[i];
    int nAp = airports ? airports(ctx, m.obstacles + n, kAirportsMax) : 0;
    if (nAp < 0 || nAp > kAirportsMax) {
        ++m.namesRefused;   // CB11: a cut obstacle list would let a name cover an airport nobody listed
        return;
    }
    n += 2 * nAp;
    m.nPlaced = mapplaces::Place(*m.places, pk, k.view, k.radiusMi, m.obstacles, n, m.placed);
    for (int i = 0; i < m.nPlaced; ++i) DrawLabel(m.layer, m.placed[i], m.places->t[m.placed[i].town].label);
}

// S3's last step: the layer, then every non-zero weather pixel over it; 0 off the disc.
inline void Compose(const uint16_t* layer, const uint16_t* wx, uint16_t* out)
{
    for (int y = 0; y < kSize; ++y)
        for (int x = 0; x < kSize; ++x) {
            const size_t p = (size_t)y * kSize + x;
            out[p] = wxlayer::OffDisc(x, y, kSize) ? 0 : (wx[p] != 0 ? wx[p] : layer[p]);
        }
}

// ------------------------------------------------------------------------------------------- the rebuild

// The map for key k: the place list (once per package), Build, the swap, the names.
inline bool Rebuild(Map& m, const bmpk::Package& pk, const Key& k, const Inputs& in)
{
    m.ready = false;
    m.built = k;
    m.attempted = true;   // A FAILED ATTEMPT TOO: a Build that fails waits for the key to change, never per frame
    if (!m.placesLoaded || m.placesSeq != k.pkSeq) {
        mapplaces::Load(pk, m.places);   // more than 256 places refuses the towns (Q-MB6)
        m.placesSeq = k.pkSeq;
        m.placesLoaded = true;
    }
    if (!in.build || !in.build(pk, k.view, in.askedMm, kLayers, maplayer::kThemes[maplayer::kThemeDark], m.sc, m.layer,
                               &m.info, in.clock)) {
        ++m.buildFails;
        return false;
    }
    for (size_t i = 0; i < kPixels; ++i) m.layer[i] = Swap(m.layer[i]);   // to the canvas's byte order
    DrawNames(m, pk, k, in.airports, in.ctx);
    m.ready = true;
    m.layerFresh = true;
    return true;
}

inline bool Stale(const Map& m, const Key& k)
{
    return !m.attempted || !Same(m.built, k);
}

// ------------------------------------------------------------------------------------------------ the frame

// The buffer the base draw copies (kPixels, in the canvas's byte order), or nullptr for today's path. `wx` is the
// weather layer when it is shown this frame, else nullptr; `wxGen` changes whenever the weather layer is rebuilt.
inline const uint16_t* Frame(Map& m, const bmpk::Package* pk, const Key& k, const uint16_t* wx, uint32_t wxGen,
                             const Inputs& in)
{
    if (pk == nullptr || pk->data == nullptr || m.refused || m.alloc == nullptr) return nullptr;
    if (m.allocated && (size_t)pk->maxVertices + 1 > m.sc.cap) Release(m);   // a larger package: all again, at its size
    if (!m.allocated && !Allocate(m, pk->maxVertices)) return nullptr;
    if (Stale(m, k)) Rebuild(m, *pk, k, in);
    if (!m.ready) return nullptr;
    if (wx == nullptr) {
        m.lastShown = false;
        return m.layer;
    }
    if (m.layerFresh || !m.lastShown || wxGen != m.composedWxGen) {
        Compose(m.layer, wx, m.composite);
        m.composedWxGen = wxGen;
        m.layerFresh = false;
        ++m.composes;
    }
    m.lastShown = true;
    return m.composite;
}

}  // namespace mapcompose
