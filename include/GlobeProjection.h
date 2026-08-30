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

/// The high-density set, for zoomed-in views. Natural Earth 1:50m at 0.043 deg
/// -- 13,512 vertices against the coarse set's 1,306. See LOD_DENSE_ABOVE_R.
const Coastline* CoastlinesDense();
int CoastlineDenseCount();

/// Above this projection radius the coarse set's 0.52 deg decimation becomes
/// visible as straight segments, and the dense set earns its cost.
///
/// DERIVED, NOT CHOSEN. The coarse data is decimated at 0.52 deg, so its drawn
/// segment is 0.52*pi/180*R pixels: 1.1 px at R=119, 3.0 px at R=331, 9.7 px at
/// R=1068. Three pixels is where straightness starts to read on this panel, so
/// the crossover is 330 -- the same arithmetic that fixed the old 4,000 km
/// threshold, applied to a scale instead of a distance.
///
/// The first version of this constant was 200, picked by feel before the
/// arithmetic was done, and it put LHR-JFK (R=228) on the dense set for no
/// visible gain at 17 ms of cost. Long routes zoom OUT to a small R and belong
/// on the coarse set; short routes zoom IN and are exactly where the detail
/// shows.
constexpr float LOD_DENSE_ABOVE_R = 330.0f;

/// Which coastline set should a view at this radius draw?
inline const Coastline* CoastlinesFor(float R, int& countOut)
{
    if (R > LOD_DENSE_ABOVE_R) { countOut = CoastlineDenseCount(); return CoastlinesDense(); }
    countOut = CoastlineCount();
    return Coastlines();
}

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
