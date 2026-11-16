#pragma once
// P5 1a, the weather cut's outline (docs/speed-p5p6.md section 1a, on weatherscope/speed-p5p6): ONE dilated black
// edge per contact instead of 8 black copies of its marker, and byte-identical to the 8 copies BY CONSTRUCTION.
//
// Why the two are equal. Today the outline is the marker drawn in black at the 8 one-pixel offsets, then once in
// colour. If the marker drawn at (x+ox, y+oy) is exactly the marker drawn at (x, y) moved by (ox, oy), the union of
// the 8 black copies minus the marker is the marker's 8-neighbour dilation minus the marker, which is what DrawEdge
// writes. That holds for LovyanGFX's integer raster: fillTriangle, drawLine (its clip walks the same Bresenham path),
// drawCircle and fillCircle work only on differences of their integer arguments, and clip per span.
//
// What can break it is the step BEFORE the raster: a dart's vertices are floats truncated to int32_t at the call, and
// truncation of n + f is not n + truncation of f when the value crosses zero (a marker past the sprite's top or left
// edge), nor always under float rounding. So DrawEdge computes the integer vertices at all 8 offsets first, and uses
// the mask only when every one is the centre's moved by its offset. Otherwise that contact gets today's 8 copies.
// Either way the pixels are today's.
//
// ONE COMPILED BODY. Heading() and Build() are DrawAircraftTriangle's own arithmetic, moved here unchanged and marked
// noinline, and DrawAircraftTriangle calls them. So the colour marker, the 8-copy bench path, the guard and the mask
// all get their vertices from the same instructions, whatever the compiler decides about fusing a multiply-add.
//
// The scratch. M is taken once, by the same drawing calls, on a small 1-bit scratch (the device's is an LGFX_Sprite,
// the host's a port of LovyanGFX in test/host/LgfxPort.h), at an integer shift, which the raster cannot see.
// The edge's writes go through the canvas's own drawFastHLine, so they are clipped exactly as LovyanGFX clips: the
// sprite's bounds, and BandCanvas's band offset if banding returns.
//
// Header-only and free of Arduino/LovyanGFX, so test/host/test_p5_outline.cpp compiles this file as the device does.
#include <cmath>
#include <cstdint>

namespace outlinemask {

#if defined(__GNUC__)
#define OUTLINEMASK_NOINLINE __attribute__((noinline))
#else
#define OUTLINEMASK_NOINLINE
#endif

// Arduino's DEG_TO_RAD, spelled out so the host computes what radians() computes: a DOUBLE multiply.
constexpr double kDegToRad = 0.017453292519943295769236907684886;
#ifdef DEG_TO_RAD
static_assert(kDegToRad == DEG_TO_RAD, "outlinemask's constant must be Arduino's DEG_TO_RAD");
#endif

// The heading's unit vector (forward) and its perpendicular (the right "wing").
struct Dir {
    float dx, dy, px, py;
};

enum PrimKind : uint8_t { kTri, kLine, kFillCircle, kCircle };
struct Prim {
    uint8_t kind;
    int32_t v[6];   // kTri: x0 y0 x1 y1 x2 y2; kLine: x0 y0 x1 y1; kFillCircle, kCircle: x y r
};
struct Marker {
    Prim p[3];
    uint8_t n = 0;
    void Add(uint8_t kind, int32_t a, int32_t b, int32_t c, int32_t d = 0, int32_t e = 0, int32_t f = 0)
    {
        Prim& q = p[n++];
        q.kind = kind;
        q.v[0] = a; q.v[1] = b; q.v[2] = c; q.v[3] = d; q.v[4] = e; q.v[5] = f;
    }
};

// Window-up subtracts the screen-top bearing so darts stay aligned with the rotated picture (positions rotate inside
// ProjectCoordinateToScreen). radians(deg) is ((deg) * DEG_TO_RAD), so the subtraction is float and the multiply double.
inline OUTLINEMASK_NOINLINE Dir Heading(float trueTrack, int radarUpDeg)
{
    const float headingScreen = (trueTrack - (float)radarUpDeg) * kDegToRad;
    const float dx = std::sin(headingScreen);
    const float dy = -std::cos(headingScreen);
    const float px = -dy;
    const float py = dx;
    return Dir{dx, dy, px, py};
}

// Type-aware marker, keyed off the ADS-B emitter category (normalised to OpenSky's numbering for both the cloud and
// local feeds). Heading-less types (rotorcraft, balloons) get a fixed glyph; everything else is a heading dart whose
// size grows with the aircraft's weight class so a heavy reads differently from a light single at a glance. Each
// float is converted to int32_t exactly where DrawAircraftTriangle's call converted it.
inline OUTLINEMASK_NOINLINE Marker Build(int cat, const Dir& d, int x, int y)
{
    Marker m;
    // rotorcraft: a hub with two crossed rotor blades. Non-directional, since helicopters routinely hover and yaw
    // independently of their ground track.
    if (cat == 8) {
        m.Add(kLine, x - 5, y - 5, x + 5, y + 5);
        m.Add(kLine, x - 5, y + 5, x + 5, y - 5);
        m.Add(kFillCircle, x, y, 2);
        return m;
    }
    // lighter-than-air (balloon / airship): a simple ring, also non-directional.
    if (cat == 10) {
        m.Add(kCircle, x, y, 4);
        return m;
    }
    // a dart pointing along the heading: tip ahead of the point, base behind it
    auto dart = [&](float tip, float base, float halfWidth) {
        const float tipX  = x + d.dx * tip,                     tipY  = y + d.dy * tip;
        const float leftX = x - d.dx * base + d.px * halfWidth, leftY = y - d.dy * base + d.py * halfWidth;
        const float rightX= x - d.dx * base - d.px * halfWidth, rightY= y - d.dy * base - d.py * halfWidth;
        m.Add(kTri, (int32_t)tipX, (int32_t)tipY, (int32_t)leftX, (int32_t)leftY, (int32_t)rightX, (int32_t)rightY);
    };
    // glider: a long, slender dart.
    if (cat == 9) {
        dart(9.0f, 2.0f, 1.0f);
        return m;
    }
    // heavy / large jets (Large, High-vortex large, Heavy): a bigger dart with a stub cross-wing so the airliners
    // stand out from light traffic.
    const bool heavy = (cat == 4 || cat == 5 || cat == 6);
    if (heavy) {
        dart(8.0f, 4.0f, 2.5f);
        m.Add(kLine, (int32_t)(x + d.px * 4.0f), (int32_t)(y + d.py * 4.0f), (int32_t)(x - d.px * 4.0f),
              (int32_t)(y - d.py * 4.0f));
        return m;
    }
    // everything else (light, small, high-performance, unknown): the standard dart
    dart(6.0f, 3.0f, 1.5f);
    return m;
}

// The dot marker (triangles off): fillCircle(x, y, 3).
inline Marker BuildDot(int x, int y)
{
    Marker m;
    m.Add(kFillCircle, x, y, 3);
    return m;
}

// Draws a marker on any canvas with LovyanGFX's signatures (BandCanvas, the scratch, the host's port).
template <class C, class Color>
inline void Draw(C& c, const Marker& m, const Color& color)
{
    for (int i = 0; i < m.n; ++i) {
        const int32_t* v = m.p[i].v;
        switch (m.p[i].kind) {
            case kTri: c.fillTriangle(v[0], v[1], v[2], v[3], v[4], v[5], color); break;
            case kLine: c.drawLine(v[0], v[1], v[2], v[3], color); break;
            case kFillCircle: c.fillCircle(v[0], v[1], v[2], color); break;
            default: c.drawCircle(v[0], v[1], v[2], color); break;
        }
    }
}

// The number of coordinates in a primitive that are positions (a circle's third value is its radius).
inline int Coords(uint8_t kind) { return kind == kTri ? 6 : kind == kLine ? 4 : 2; }

// a == b moved by (ox, oy), exactly.
inline bool Same(const Marker& a, const Marker& b, int ox, int oy)
{
    if (a.n != b.n) return false;
    for (int i = 0; i < a.n; ++i) {
        if (a.p[i].kind != b.p[i].kind) return false;
        const int k = Coords(a.p[i].kind);
        for (int j = 0; j < k; ++j)
            if (a.p[i].v[j] != b.p[i].v[j] + ((j & 1) ? oy : ox)) return false;
        if (k == 2 && a.p[i].v[2] != b.p[i].v[2]) return false;
    }
    return true;
}

inline Marker Shift(const Marker& m, int ox, int oy)
{
    Marker s = m;
    for (int i = 0; i < s.n; ++i) {
        const int k = Coords(s.p[i].kind);
        for (int j = 0; j < k; ++j) s.p[i].v[j] += (j & 1) ? oy : ox;
    }
    return s;
}

// The scratch is kSide x kSide with the contact at (kHalf, kHalf). A marker fits when every point it can raster lies
// within kReach of the contact, so its 8-neighbour edge stays inside the scratch with a row and a column to spare.
constexpr int kSide = 32;
constexpr int kHalf = 16;
constexpr int kReach = 13;

inline bool Fits(const Marker& m, int x, int y)
{
    for (int i = 0; i < m.n; ++i) {
        const Prim& q = m.p[i];
        const int k = Coords(q.kind);
        const int r = k == 2 ? q.v[2] : 0;
        if (r < 0) return false;
        for (int j = 0; j < k; ++j) {
            const int32_t off = q.v[j] - ((j & 1) ? y : x);
            if (off - r < -kReach || off + r > kReach) return false;
        }
    }
    return true;
}

// The 8 one-pixel offsets, in the order today's loop draws them (oy outer, ox inner).
constexpr int8_t kOff[8][2] = {{-1, -1}, {0, -1}, {1, -1}, {-1, 0}, {1, 0}, {-1, 1}, {0, 1}, {1, 1}};

// The black edge for the contact at (x, y) whose marker is m0 (Build or BuildDot at x, y). buildAt(ax, ay) builds the
// same contact's marker at another position. Returns true when the mask drew it, false when it fell back to today's 8
// copies (the guard: an offset whose integer vertices are not the centre's moved, a marker past the scratch, or no
// scratch). The caller draws the colour marker afterwards, as today.
//
// Scratch: Begin() clears it and returns false if it has no buffer; LovyanGFX's fillTriangle/drawLine/fillCircle/
// drawCircle with an int "on" colour; Row(r) returns row r with bit j set for column j.
template <class C, class S, class BuildAt, class Color>
inline bool DrawEdge(C& c, S& scratch, const Marker& m0, BuildAt buildAt, int x, int y, const Color& black)
{
    bool exact = Fits(m0, x, y);
    for (int k = 0; exact && k < 8; ++k)
        exact = Same(buildAt(x + kOff[k][0], y + kOff[k][1]), m0, kOff[k][0], kOff[k][1]);
    if (exact) exact = scratch.Begin();
    if (!exact) {
        for (int k = 0; k < 8; ++k) Draw(c, buildAt(x + kOff[k][0], y + kOff[k][1]), black);
        return false;
    }
    const int bx = x - kHalf, by = y - kHalf;   // the scratch's (0, 0) on the canvas
    Draw(scratch, Shift(m0, -bx, -by), scratch.On());
    uint32_t m[kSide + 2];
    m[0] = m[kSide + 1] = 0;
    for (int r = 0; r < kSide; ++r) m[r + 1] = scratch.Row(r);
    for (int r = 0; r < kSide; ++r) {
        const uint32_t v = m[r] | m[r + 1] | m[r + 2];   // the rows above, at and below
        const uint32_t d = v | (v << 1) | (v >> 1);       // and the columns either side: the 8-neighbourhood
        uint32_t e = d & ~m[r + 1];
        while (e) {
            const int j = __builtin_ctz(e);
            const uint32_t t = ~(e >> j);
            const int len = t ? __builtin_ctz(t) : kSide - j;
            c.drawFastHLine(bx + j, by + r, len, black);
            e &= ~((len >= kSide ? 0xFFFFFFFFu : ((1u << len) - 1u)) << j);
        }
    }
    return true;
}

}  // namespace outlinemask
