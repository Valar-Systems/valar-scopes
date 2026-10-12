// Seven-segment shapes: include/SevenSegmentShapes.h, rasterized on a pixel grid.
//
// A segment is a body rectangle plus a triangular cap at each end. Before 2026-10-12 the
// body was `len - t` long, which for an ODD thickness stopped one pixel short of the far
// cap: a 1 px seam before every right and bottom tip, plain in the Missileer Clock's
// product photos. This draws the shipping SegH/SegV for every thickness 2..15 and length
// up to 48 and requires, of each segment:
//   - the centre line is lit from tip to tip, with no gap;
//   - every row (column) of the body is lit from one cap base to the other;
//   - nothing is drawn outside [start, start+len] x [0, t);
//   - the shape is mirror-symmetric end for end (its two caps are the same triangle).
//
// CONTROLS: the pre-fix shape (a copy of the old body length, below) must FAIL the gap
// check at every odd thickness -- the check can see a seam -- and must be pixel-identical
// to the shipping shape at every even thickness, so even-thickness digits did not move.
//
// SABOTAGE: put `len - t` back in SegBodyLen and the "tip to tip" and "symmetric" lines
// go red for every odd t.
#include <cstdio>
#include <cstring>
#include <algorithm>
#include "../../include/SevenSegmentShapes.h"

static int failures = 0;
static void check(bool ok, const char* what)
{
    std::printf("  %s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++failures;
}

// A 64x64 grid with the two primitives the shapes use. fillRect is LovyanGFX's
// [x, x+w) x [y, y+h). fillTriangle lights every pixel whose centre lies inside the
// closed triangle, row by row from the exact edge intersections -- symmetric under
// mirroring, so the symmetry check grades the geometry and not the rasterizer.
struct Grid {
    static const int N = 64;
    unsigned char p[N][N];
    Grid() { std::memset(p, 0, sizeof p); }
    void set(int x, int y) { if (x >= 0 && x < N && y >= 0 && y < N) p[y][x] = 1; }
    bool at(int x, int y) const { return x >= 0 && x < N && y >= 0 && y < N && p[y][x]; }
    void fillRect(int x, int y, int w, int h, int)
    {
        for (int yy = y; yy < y + h; ++yy)
            for (int xx = x; xx < x + w; ++xx) set(xx, yy);
    }
    void fillTriangle(int x0, int y0, int x1, int y1, int x2, int y2, int)
    {
        const int xs[3] = { x0, x1, x2 }, ys[3] = { y0, y1, y2 };
        const int ylo = std::min(y0, std::min(y1, y2)), yhi = std::max(y0, std::max(y1, y2));
        for (int y = ylo; y <= yhi; ++y) {
            double lo = 1e9, hi = -1e9;
            for (int e = 0; e < 3; ++e) {
                const int ax = xs[e], ay = ys[e], bx = xs[(e + 1) % 3], by = ys[(e + 1) % 3];
                if (ay == by) {
                    if (ay == y) { lo = std::min(lo, (double)std::min(ax, bx)); hi = std::max(hi, (double)std::max(ax, bx)); }
                    continue;
                }
                if (y < std::min(ay, by) || y > std::max(ay, by)) continue;
                const double x = ax + (double)(y - ay) * (bx - ax) / (by - ay);
                lo = std::min(lo, x); hi = std::max(hi, x);
            }
            for (int x = (int)(lo + 0.999999); x <= (int)(hi + 1e-6); ++x) set(x, y);
        }
    }
};

// The shape as it was before the fix, kept ONLY as the control.
static void OldSegH(Grid& c, int x, int y, int len, int t)
{
    const int h2 = t / 2;
    if (len <= t) return;
    c.fillRect(x + h2, y, len - t, t, 1);
    c.fillTriangle(x + h2, y, x + h2, y + t - 1, x, y + h2, 1);
    c.fillTriangle(x + len - h2, y, x + len - h2, y + t - 1, x + len, y + h2, 1);
}

// Properties of one horizontal segment drawn at (X, Y). `vert` reads the grid transposed,
// so the same checks grade SegV.
struct Verdict { bool tipToTip, bodyRows, contained, symmetric; };
static Verdict Grade(const Grid& g, int X, int Y, int len, int t, bool vert)
{
    auto at = [&](int a, int b) { return vert ? g.at(b, a) : g.at(a, b); };   // a = along, b = across
    const int h2 = t / 2;
    Verdict v = { true, true, true, true };
    for (int a = X; a <= X + len; ++a) if (!at(a, Y + h2)) v.tipToTip = false;
    for (int b = Y; b < Y + t; ++b)
        for (int a = X + h2; a <= X + len - h2; ++a) if (!at(a, b)) v.bodyRows = false;
    for (int b = 0; b < Grid::N; ++b)
        for (int a = 0; a < Grid::N; ++a)
            if (at(a, b) && (a < X || a > X + len || b < Y || b >= Y + t)) v.contained = false;
    for (int b = Y; b < Y + t; ++b)
        for (int a = X; a <= X + len; ++a)
            if (at(a, b) != at(2 * X + len - a, b)) v.symmetric = false;
    return v;
}

int main()
{
    std::printf("seven-segment shapes (SevenSegmentShapes.h)\n");
    const int X = 4, Y = 6;
    int cases = 0, badH = 0, badV = 0, oddSeamCaught = 0, odd = 0, evenSame = 0, even = 0;
    for (int t = 2; t <= 15; ++t) {
        for (int len = t + 1; len <= 48; ++len) {
            ++cases;
            Grid h; sevenseg::SegH(h, X, Y, len, t, 1);
            const Verdict vh = Grade(h, X, Y, len, t, false);
            if (!(vh.tipToTip && vh.bodyRows && vh.contained && vh.symmetric)) ++badH;

            Grid v; sevenseg::SegV(v, Y, X, len, t, 1);   // x = Y across, y = X along
            const Verdict vv = Grade(v, X, Y, len, t, true);
            if (!(vv.tipToTip && vv.bodyRows && vv.contained && vv.symmetric)) ++badV;

            Grid old; OldSegH(old, X, Y, len, t);
            if (t % 2) {
                ++odd;
                if (!Grade(old, X, Y, len, t, false).tipToTip) ++oddSeamCaught;
            } else {
                ++even;
                if (std::memcmp(old.p, h.p, sizeof h.p) == 0) ++evenSame;
            }
        }
    }
    std::printf("  %d segments per orientation (t 2..15, len t+1..48)\n", cases);
    check(badH == 0, "SegH: tip to tip unbroken, body meets both caps, contained, symmetric");
    check(badV == 0, "SegV: the same, vertically");
    check(odd > 0 && oddSeamCaught == odd, "CONTROL: the pre-fix body (len - t) leaves a seam at EVERY odd t");
    check(even > 0 && evenSame == even, "CONTROL: at every even t the fix is pixel-identical to the old shape");
    {
        // The case in the product photos: the 1.28" Clock's digit thickness is odd.
        Grid g; sevenseg::SegH(g, X, Y, 30, 7, 1);
        const int h2 = 7 / 2;
        check(g.at(X + 30 - h2 - 1, Y + h2), "t=7: the pixel before the far cap is lit (it was the seam)");
    }
    std::printf(failures ? "FAILED (%d)\n" : "ok\n", failures);
    return failures ? 1 : 0;
}
