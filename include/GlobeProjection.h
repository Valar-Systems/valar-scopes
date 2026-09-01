#pragma once

// The orthographic globe, extracted from src/anim/FlightAnimation.cpp so two
// products can share one implementation.
//
// =============================================================================
// WHY THIS FILE EXISTS
//
// §7.2 of docs/follow-mode-consolidated.md says of the animation module: "it was
// written to be lifted into the real flight director. Lift it." That turned out
// not to be a re-include. `GlobePt`, `GeoVec`, `kCoast` and the basis all lived
// inside an ANONYMOUS NAMESPACE in FlightAnimation.cpp -- internal linkage, so
// adding the module to a build compiled it and made nothing in it callable.
//
// So the maths and the data are exported here, and FlightAnimation.cpp now calls
// this header rather than its own copy. One implementation, two consumers, which
// is what §7.2 was actually asking for.
//
// =============================================================================
// THE BASIS IS A PARAMETER, AND THAT IS THE ONE DESIGN CHANGE
//
// The original built its basis into a file-global (`gGlobe`) behind a latch
// (`gGlobeReady`), from the missile scenario's launch/aim globals. That is
// reasonable for a screen showing one scenario at a time and impossible for
// Follow, which needs a basis per route.
//
// So MakeBasis() returns one and Project() takes one. No globals live in this
// header. FlightAnimation.cpp keeps its own cache of the result -- that is a
// cache, not a latch in the shared surface, and it preserves the animation's
// behaviour exactly.
//
// =============================================================================
// PURE, AND GRADED AGAINST THE CODE IT REPLACED
//
// No Arduino, no LovyanGFX, nothing but <cmath>. test/host/test_globe_proj.cpp
// pins this against values printed by the PRE-EXTRACTION implementation -- the
// GOLF-07 basis to nine decimals and nine real coastline vertices projected
// through it -- so "behaviour-preserving" is an assertion rather than a claim.
//
// The arithmetic below is moved VERBATIM. Not retyped, not tidied: 0.0174533f is
// the original's degrees-to-radians constant and keeps its original precision
// because there is no reason to change it, and a "while I'm here" improvement to
// a shipping picture is how a refactor stops being one.
//
// WHAT THE PINS ACTUALLY BITE ON, measured rather than assumed (four probes,
// 2026-08-27): flipping the screen-y sign fails 11 of 37; tilting the other way
// fails 14; truncating d2r to 0.01745 fails 14. Making d2r MORE accurate
// (0.017453292519943295) fails NOTHING -- it moves a coastline vertex by about
// 4e-5 px, three orders below the 1e-3 px tolerance these values are pinned at.
// So the test catches anything that moves the picture and is deliberately blind
// to anything that does not. Worth stating plainly, because the opposite claim
// -- that the constant is load-bearing for the test -- is the sort of thing that
// reads as true and would have been wrong.

#include <cmath>
#include <cstdint>

namespace globeproj {

// -----------------------------------------------------------------------------
// The coastline set: Natural Earth 1:110m land, spherical Douglas-Peucker at
// 0.5 deg, 1,306 vertices stored as int16 unit vectors x32767 (~7.6 KB).
//
// The decimation is sized to be ~1 px at r = 119. At the regional scale §9
// contemplates (r ~ 3,700) these same vertices are 32 px apart and Puget Sound
// renders as a triangle -- so this data is for the GLOBE, and a regional chart
// needs its own set.
// -----------------------------------------------------------------------------
struct GeoVec { int16_t x, y, z; };
struct Coastline { const GeoVec* v; int n; };

constexpr float VEC_SCALE = 32767.0f;
constexpr float VEC_INV   = 1.0f / VEC_SCALE;

/// The rings, defined once in src/anim/Coastlines.cpp. A build that wants the
/// globe includes `+<anim/>`; one that does not drops the data entirely under
/// --gc-sections.
const Coastline* Coastlines();
int CoastlineCount();

/// The state and province borders, defined once in src/anim/StateBorders.cpp.
///
/// Same GeoVec/Coastline shape as the coastline rings, and drawn by the same
/// chunk-culled loop -- but these are OPEN polylines rather than closed rings,
/// so a line of n vertices carries n-1 segments and nothing wraps. That one
/// difference is why BuildChunks below takes a `closed` flag instead of the
/// coastline's loop being copied and edited.
const Coastline* Borders();
int BorderCount();

// -----------------------------------------------------------------------------
// The projection
// -----------------------------------------------------------------------------

/// Camera basis: view direction, screen-right, screen-up. Orthonormal.
struct Basis {
    float v[3] = { 0.0f, 0.0f, 1.0f };
    float r[3] = { 1.0f, 0.0f, 0.0f };
    float u[3] = { 0.0f, 1.0f, 0.0f };
};

inline void UnitVec(float lonDeg, float latDeg, float* o)
{
    const float d2r = 0.0174533f;
    const float c = cosf(latDeg * d2r);
    o[0] = c * cosf(lonDeg * d2r);
    o[1] = c * sinf(lonDeg * d2r);
    o[2] = sinf(latDeg * d2r);
}

inline void Norm3(float* v)
{
    const float m = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (m > 1e-9f) { v[0] /= m; v[1] /= m; v[2] /= m; }
}

inline void Cross3(const float* a, const float* b, float* o)
{
    o[0] = a[1] * b[2] - a[2] * b[1];
    o[1] = a[2] * b[0] - a[0] * b[2];
    o[2] = a[0] * b[1] - a[1] * b[0];
}

inline float Dot3(const float* a, const float* b)
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

/// A camera looking at the great-circle midpoint of two places, tilted toward
/// the arc's normal by `tiltDeg` so the route bows across the disc rather than
/// running down its spine.
///
/// NORTH IS UP. The original's comment is worth carrying: a FOLLOWING camera --
/// subject centred, world sliding under it -- answers nothing, because the
/// marker never moves and a world sliding under a fixed dot reads as the world
/// moving. A fixed orientation with both endpoints on the visible hemisphere
/// answers "how far along am I" at a glance.
inline Basis MakeBasis(float lon0, float lat0, float lon1, float lat1, float tiltDeg)
{
    Basis g;
    float L[3], A[3];
    UnitVec(lon0, lat0, L);
    UnitVec(lon1, lat1, A);

    float n[3]; Cross3(L, A, n); Norm3(n);                 // great-circle normal
    float m[3] = { L[0] + A[0], L[1] + A[1], L[2] + A[2] }; // arc midpoint
    Norm3(m);

    const float phi = tiltDeg * 0.0174533f;
    const float cp = cosf(phi), sp = sinf(phi);
    for (int i = 0; i < 3; ++i) g.v[i] = m[i] * cp + n[i] * sp;
    Norm3(g.v);

    // right = worldNorth x view, up = view x right. Degenerate only looking
    // straight down a pole.
    //
    // THE DEGENERATE CASE IS REACHABLE HERE AND WAS NOT BEFORE. The original
    // note reads "which this tilt cannot produce" -- true of one fixed missile
    // scenario, false of arbitrary routes: DEN->DEL's midpoint is at 83.9 N
    // (§9's worked example), and a route whose tilted view lands on the pole
    // would leave `r` undefined. The original's guard is kept and now earns its
    // place rather than being dead code.
    float north[3] = { 0.0f, 0.0f, 1.0f };
    Cross3(north, g.v, g.r);
    if (Dot3(g.r, g.r) < 1e-6f) {
        g.r[0] = 1.0f; g.r[1] = 0.0f; g.r[2] = 0.0f;
    }
    Norm3(g.r);
    Cross3(g.v, g.r, g.u);
    Norm3(g.u);
    return g;
}

// A ROUTE-UP BASIS WAS ADDED HERE AND REMOVED, 2026-08-31. Recorded because the
// idea is a natural one to have twice.
//
// It rolled the camera so the great-circle normal was up, which put the route on
// the horizontal diameter. It looked deliberate and it was wrong for this
// product: THE FOLLOW MAP IS A MAP, and a tilted coastline is one nobody can
// place at a glance. Recognising the geography is most of what the face is for,
// so any rotation that buys composition at the cost of recognition is a bad
// trade here regardless of how well it frames.
//
// It was also justified by a claim that is simply false -- that the fit
// R = usable / sin(theta/2) only lands both ends on the usable circle if they
// lie on a diameter. Orthographic projection maps angular distance from the view
// centre to screen RADIUS, and a roll is a rotation about the view axis, so it
// cannot change a radius. Both endpoints are theta/2 from the midpoint in every
// basis and therefore land at `usable` in every basis. The roll only ever
// changed the ANGLE at which they fall.

/// World unit vector -> screen. Returns true on the near hemisphere.
///
/// `c` is the disc centre and `R` its radius, both in pixels. Callers that want
/// a non-square placement pass the two centres separately; this two-argument
/// form is the original's and is kept for it.
inline bool Project(const Basis& g, float x, float y, float z,
                    float cx, float cy, float R, float& sx, float& sy)
{
    const float zz = g.v[0] * x + g.v[1] * y + g.v[2] * z;
    sx = cx + (g.r[0] * x + g.r[1] * y + g.r[2] * z) * R;
    sy = cy - (g.u[0] * x + g.u[1] * y + g.u[2] * z) * R;
    return zz > 0.0f;
}

/// Square-disc form, identical to the original `GlobePt(x,y,z,c,R,...)`.
inline bool Project(const Basis& g, float x, float y, float z,
                    float c, float R, float& sx, float& sy)
{
    return Project(g, x, y, z, c, c, R, sx, sy);
}

/// Great-circle interpolation between two places, as a unit vector. The
/// animation's own `GreatCircle()` in world coordinates, parameterised.
/// Cohen-Sutherland outcode against a w x h panel.
inline int Outcode(float x, float y, int w, int h)
{
    int o = 0;
    if (x < 0.0f)            o |= 1;
    else if (x > (float)(w - 1)) o |= 2;
    if (y < 0.0f)            o |= 4;
    else if (y > (float)(h - 1)) o |= 8;
    return o;
}

/// Can this segment be thrown away without drawing it?
///
/// VISIBILITY IS A VIEWPORT QUESTION, NOT A HEMISPHERE ONE (#274 step 2).
/// Project() returns `zz > 0`, which asks "is this point on the near side of the
/// sphere" -- the right question when the sphere fills the panel and a wildly
/// wrong one once it does not. Under route framing R reaches ~1350 px on a
/// 240 px panel, so almost everything on the near hemisphere is off screen and
/// still reached drawLine.
///
/// Trivial reject only: both ends outside the SAME edge. A segment with both
/// ends off-panel on different sides may still cross it, and dropping those is
/// how a coastline develops holes at high zoom -- the bug this function exists
/// to avoid, not to cause.
inline bool SegmentOffPanel(float x0, float y0, float x1, float y1, int w, int h)
{
    return (Outcode(x0, y0, w, h) & Outcode(x1, y1, w, h)) != 0;
}

// =============================================================================
// BOUNDING CAPS -- SKIPPING GEOMETRY BEFORE IT IS PROJECTED
//
// The coastline draw projected every vertex of all 105 rings on every frame and
// only then asked SegmentOffPanel. That is 5,391 projections at 2.80 us =
// 15.1 ms, and it did not vary with zoom -- at the R=500 clamp the visible cap
// is 13.9 deg, which is 1.46 % of the sphere, so ~98 % of the work was thrown
// away after being paid for. It stayed invisible precisely BECAUSE it never
// varied: a constant cost reads as the floor rather than as waste.
//
// A cap is a centre unit vector and an angular radius containing every vertex of
// a ring. The ring cannot touch the panel unless its cap reaches the visible
// cap, and that is one dot product.
//
// WHY THE VIEW RADIUS IS THE PANEL'S HALF-DIAGONAL AND NOT ITS HALF-WIDTH.
// A point at angular distance a projects to screen radius R*sin(a), and the
// furthest a visible pixel can sit from the disc centre is the CORNER of the
// square panel -- half-width * sqrt(2). Using half-width would cull rings that
// are genuinely visible in the corners: a coastline that disappears only at
// certain view angles, which is the exact defect this optimisation could
// introduce and the reason the pixel-diff control exists.
//
// SAFETY, and why the convexity argument matters: a segment is drawn whenever
// its two endpoints straddle the panel, even with neither inside. But both
// endpoints lie in the ring's cap, the near-hemisphere image of a cap is convex
// (an ellipse interior intersected with the limb disc), and a straight screen
// segment between two points of a convex set stays inside it. So a cap that
// misses the panel cannot produce a segment that hits it.
struct Cap {
    float c[3]   = { 0.0f, 0.0f, 1.0f };
    float cosRad = -1.0f;   // cos of the angular radius
    float sinRad =  0.0f;   // sin of it, for the addition formula below
    bool  always = true;    // degenerate: never cull
};

/// Enclosing cap for a ring. Not the minimal one -- the centroid direction with
/// the max deviation is cheap, always valid, and only ever too generous.
inline Cap CapOfRing(const GeoVec* v, int n)
{
    Cap cap;
    if (!v || n <= 0) return cap;

    float s[3] = { 0.0f, 0.0f, 0.0f };
    for (int i = 0; i < n; ++i) {
        s[0] += (float)v[i].x; s[1] += (float)v[i].y; s[2] += (float)v[i].z;
    }
    const float m = sqrtf(s[0]*s[0] + s[1]*s[1] + s[2]*s[2]);
    // A ring whose vertices cancel out (a band around the globe) has no
    // meaningful centre. Leave `always` set: correctness first, and there are
    // only a handful of such rings.
    if (m < 1e-3f) return cap;
    cap.c[0] = s[0]/m; cap.c[1] = s[1]/m; cap.c[2] = s[2]/m;

    float minDot = 1.0f;
    for (int i = 0; i < n; ++i) {
        float p[3] = { (float)v[i].x * VEC_INV, (float)v[i].y * VEC_INV,
                       (float)v[i].z * VEC_INV };
        const float pn = sqrtf(p[0]*p[0] + p[1]*p[1] + p[2]*p[2]);
        if (pn < 1e-6f) continue;
        const float d = (p[0]*cap.c[0] + p[1]*cap.c[1] + p[2]*cap.c[2]) / pn;
        if (d < minDot) minDot = d;
    }
    if (minDot <= -0.999f) return cap;        // spans more than a hemisphere
    cap.cosRad = minDot;
    cap.sinRad = sqrtf(1.0f - minDot * minDot);
    cap.always = false;
    return cap;
}

/// Could anything in `cap` land on the panel? `cosView`/`sinView` are the cosine
/// and sine of the view's angular radius.
///
/// cos(capRad + viewRad) = cosCap*cosView - sinCap*sinView, so the whole test is
/// two multiplies and a compare -- no trig in the per-ring loop.
inline bool CapMayBeVisible(const Cap& cap, const float* viewC,
                            float cosView, float sinView)
{
    if (cap.always) return true;
    const float thresh = cap.cosRad * cosView - cap.sinRad * sinView;
    if (thresh <= -1.0f) return true;         // reaches everywhere
    const float dot = cap.c[0]*viewC[0] + cap.c[1]*viewC[1] + cap.c[2]*viewC[2];
    return dot >= thresh;
}

// PER-RING IS NOT ENOUGH, MEASURED. A cap per ring recovered only 8.9 ms of the
// 15.1 at R=500, because the rings that dominate the vertex count are the
// CONTINENTS: North America's cap spans tens of degrees, so it intersects any
// North American view and all ~1,000 of its vertices get projected to draw the
// 20 % that are on screen. Culling whole rings only ever discards islands.
//
// So the unit is a CHUNK: a run of consecutive segments within a ring, with its
// own cap. Same test, finer granularity, and the continents become cullable
// along their length.
//
// THE OVERLAP IS LOAD-BEARING. A chunk of `count` segments touches `count + 1`
// vertices, and its cap must contain the LAST one -- the vertex it shares with
// the next chunk. Sizing the cap to only the chunk's own start vertices would
// leave the segment across each boundary uncovered, so a boundary segment could
// be visible while both neighbouring caps tested as missing the view. That is a
// one-vertex mistake that produces a dotted coastline at particular zooms, and
// nothing else would have caught it -- which is what the pixel-diff control is
// for.
struct ChunkCap {
    Cap     cap;
    int16_t ring  = 0;
    int16_t first = 0;   // index of the first vertex
    int16_t count = 0;   // number of SEGMENTS (so count+1 vertices, wrapping)
};

/// Chunked caps over every ring, built once on first use.
// THE CHUNK BUDGET, shared by both datasets and checked at COMPILE time.
//
// A ChunkCap is 32 B of .bss on a board whose internal heap is the scarce
// resource, so these are budgeted rather than rounded up:
//
//     coastline : 182 chunks in use, table 256   (8.0 KB)
//     borders   : 148 chunks in use, table 148   (4.6 KB, sized EXACTLY)
//
// The border table is sized by BORDER_CHUNK_COUNT, which the generator computes
// from the data it just emitted -- so for borders there is no overflow branch to
// take and no chunk that can be dropped. The coastline table keeps its budget
// and its no-cull fallback, because its count is a property of a generated file
// this header cannot see.
//
// 512 rather than a rounder number, and deliberately not 768: 768 came out of an
// estimate that put the admin-1 border set at 60-90 KB of flash when it measured
// 3.1 KB -- 20-30x wrong. A constant carried forward from a bad estimate is a bad
// estimate that has stopped being visible, so the budget is set from the measured
// 404 with headroom rather than from the guess.
//
// src/anim/StateBorders.cpp static_asserts COAST_MAX_CH + BORDER_CHUNK_COUNT
// against MAX_CH. It FAILS THE BUILD; it does not clamp. A clamped table renders
// a map that is almost right, which is the one failure that does not show on
// glass.
constexpr int CHUNK_SEGMENTS = 48;   // segments per chunk
constexpr int MAX_CH         = 512;  // the shared budget
constexpr int COAST_MAX_CH   = 256;  // the coastline table's share of it

/// Build chunk caps over a set of rings (closed) or polylines (open). Returns
/// the number written, or 0 if `cap` was too small -- and 0 disables the cull at
/// the call site, which is slow and correct rather than fast and wrong.
inline int BuildChunks(const Coastline* rings, int nr, bool closed,
                       ChunkCap* out, int cap)
{
    int w = 0;
    for (int i = 0; i < nr; ++i) {
        const int n = rings[i].n;
        if (n < 2) continue;
        // A closed ring of n vertices has n segments, the last of which returns
        // to vertex 0. An open polyline has n-1 and stops.
        const int nseg = closed ? n : n - 1;
        for (int s = 0; s < nseg; s += CHUNK_SEGMENTS) {
            if (w >= cap) return 0;
            const int segs = (nseg - s < CHUNK_SEGMENTS) ? (nseg - s)
                                                         : CHUNK_SEGMENTS;
            // Gather this chunk's vertices INCLUDING the shared last one.
            GeoVec tmp[CHUNK_SEGMENTS + 1];
            for (int k = 0; k <= segs; ++k) {
                int idx = s + k;
                if (idx >= n) idx -= n;         // only reachable when closed
                tmp[k] = rings[i].v[idx];
            }
            out[w].cap   = CapOfRing(tmp, segs + 1);
            out[w].ring  = (int16_t)i;
            out[w].first = (int16_t)s;
            out[w].count = (int16_t)segs;
            ++w;
        }
    }
    return w;
}

inline const ChunkCap* CoastlineChunks(int& count)
{
    static ChunkCap chunks[COAST_MAX_CH];
    static int built = -1;
    if (built < 0)
        built = BuildChunks(Coastlines(), CoastlineCount(), /*closed=*/true,
                            chunks, COAST_MAX_CH);
    count = built;
    return chunks;
}

/// Chunked caps over the border polylines. Defined in src/anim/StateBorders.cpp
/// rather than inline here because its table is sized exactly by
/// BORDER_CHUNK_COUNT, and that macro lives in the generated .inc which only
/// that translation unit includes.
const ChunkCap* BorderChunks(int& count);

/// Caps for the coastline rings, built once on first use.
///
/// Built at RUNTIME rather than baked into Coastlines.inc on purpose: that file
/// has two live regeneration traps (see scripts/gen_coastlines.py), and a change
/// that has to touch it to be correct is a change that can be silently half
/// applied. One pass over 5,286 vertices at boot costs microseconds and keeps
/// this entirely inside code.
inline const Cap* CoastlineCaps(int& count)
{
    constexpr int MAX_RINGS = 192;
    static Cap caps[MAX_RINGS];
    static int built = -1;
    if (built < 0) {
        const Coastline* rings = Coastlines();
        const int n = CoastlineCount();
        built = (n <= MAX_RINGS) ? n : 0;     // too many: 0 disables culling
        for (int i = 0; i < built; ++i)
            caps[i] = CapOfRing(rings[i].v, rings[i].n);
    }
    count = built;
    return caps;
}

inline void GreatCirclePoint(float lon0, float lat0, float lon1, float lat1,
                             float f, float* o)
{
    float v1[3], v2[3];
    UnitVec(lon0, lat0, v1);
    UnitVec(lon1, lat1, v2);
    float d = Dot3(v1, v2);
    if (d >  1.0f) d =  1.0f;
    if (d < -1.0f) d = -1.0f;
    const float ang = acosf(d);
    const float s = sinf(ang);
    if (s < 1e-6f) { o[0] = v1[0]; o[1] = v1[1]; o[2] = v1[2]; return; }
    const float a = sinf((1.0f - f) * ang) / s;
    const float b = sinf(f * ang) / s;
    for (int i = 0; i < 3; ++i) o[i] = a * v1[i] + b * v2[i];
    Norm3(o);
}

} // namespace globeproj
