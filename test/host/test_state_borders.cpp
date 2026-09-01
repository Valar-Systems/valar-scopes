// Host acceptance test for the generated state/province/international borders.
//
// =============================================================================
// WHY THIS TEST EXISTS, AND WHY ITS FOUR CASES WERE NAMED BEFORE IT WAS WRITTEN
//
// The data this grades replaced 26 hand-authored endpoint PAIRS. Two points can
// express a meridian or a parallel and nothing else, so that table had INVERTED
// COVERAGE: it drew Colorado, Wyoming, Utah, Arizona and New Mexico cleanly and
// drew nothing at all west of the Cascades. The device sits in Oregon.
//
// The failure was invisible for weeks because a map with SOME lines on it looks
// like a map. Nothing about the rendered face says "there should be a line here"
// -- the missing Columbia looks exactly like ocean, and the missing Snake looks
// exactly like Idaho. That is this project's signature failure mode: the broken
// state and the working state produce the same observation.
//
// So the four features that the old representation COULD NOT express were named
// in advance, before this file existed, and each is asserted here:
//
//     the Columbia            OR/WA, a river
//     the Snake               OR/ID above 44.3 N, a river
//     the Bitterroot divide   ID/MT, a watershed crest
//     California's south edge absent from the old table entirely
//
// A test written after looking at the output would have picked whatever the
// output happened to contain.
//
// =============================================================================
// THE PROBES COME FROM THE OTHER SIDE OF THE CONTRACT
//
// Every probe below is a vertex of the RAW Natural Earth source -- the
// generator's INPUT -- transcribed with its coordinates. They are not read back
// out of the shipped table, which would assert only that a file equals itself,
// and they are not recalled from memory of where a river runs.
//
// This is the weaker, transcribed form of that rule: the strong form parses the
// geojson directly, and this rig deliberately has no filesystem or JSON
// dependency. What it therefore CANNOT catch is the source itself changing under
// a future Natural Earth release. What it CAN catch, which is the thing that has
// actually gone wrong: the decimation dropping a feature, the wrong layer being
// read, the two layers not being merged, a units or sign error, and the chunked
// cull silently discarding geometry.
//
// =============================================================================
// AND EVERY POSITIVE HAS A NEGATIVE BESIDE IT
//
// "Is this point near a border" passes trivially if the answer is always yes --
// a bug that returned 0.0 for everything would satisfy all sixteen positives. So
// four points known to be FAR from any boundary are asserted to be far, and the
// separation is reported: the worst positive is 0.053 deg and the nearest
// negative is 1.44 deg, a factor of 27.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "../../include/GlobeProjection.h"

static int failures = 0;
static int checks   = 0;

static void check(bool ok, const char* what)
{
    ++checks;
    if (!ok) { std::printf("  FAIL: %s\n", what); ++failures; }
}

using namespace globeproj;

// ---------------------------------------------------------------------------
// Spherical geometry the test owns, so it is not grading the shipped maths with
// the shipped maths.
// ---------------------------------------------------------------------------
// -std=c++11 is strict ISO, so kPi is not declared. Its own constant rather
// than _USE_MATH_DEFINES, which would be a compiler-specific escape hatch in a
// rig whose whole point is that it needs nothing.
static const double kPi = 3.14159265358979323846;

struct V3 { double x, y, z; };

static V3 U(double latDeg, double lonDeg)
{
    const double la = latDeg * kPi / 180.0, lo = lonDeg * kPi / 180.0;
    const double c = std::cos(la);
    V3 v = { c * std::cos(lo), c * std::sin(lo), std::sin(la) };
    return v;
}
static V3 fromStored(const GeoVec& g)
{
    V3 v = { g.x / 32767.0, g.y / 32767.0, g.z / 32767.0 };
    const double m = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    if (m > 1e-12) { v.x /= m; v.y /= m; v.z /= m; }
    return v;
}
static double dot(const V3& a, const V3& b) { return a.x*b.x + a.y*b.y + a.z*b.z; }
static V3 cross(const V3& a, const V3& b)
{
    V3 v = { a.y*b.z - a.z*b.y, a.z*b.x - a.x*b.z, a.x*b.y - a.y*b.x };
    return v;
}
static double angDeg(const V3& a, const V3& b)
{
    double d = dot(a, b);
    if (d >  1.0) d =  1.0;
    if (d < -1.0) d = -1.0;
    return std::acos(d) * 180.0 / kPi;
}

/// Great-circle distance from p to the SEGMENT ab, in degrees. Cross-track when
/// the foot of the perpendicular lies within the segment, endpoint distance
/// otherwise -- without that clamp a point beyond the end of a short border
/// scores as if the line continued forever, which would make the negative
/// controls pass for the wrong reason.
static double segDistDeg(const V3& p, const V3& a, const V3& b)
{
    V3 n = cross(a, b);
    const double m = std::sqrt(dot(n, n));
    if (m < 1e-12) return angDeg(p, a);
    n.x /= m; n.y /= m; n.z /= m;
    double s = dot(p, n);
    if (s >  1.0) s =  1.0;
    if (s < -1.0) s = -1.0;
    const double crossTrack = std::fabs(std::asin(s)) * 180.0 / kPi;

    V3 f = { p.x - dot(p, n) * n.x, p.y - dot(p, n) * n.y, p.z - dot(p, n) * n.z };
    const double fm = std::sqrt(dot(f, f));
    if (fm < 1e-12) return crossTrack;
    f.x /= fm; f.y /= fm; f.z /= fm;
    const double ab = angDeg(a, b);
    if (angDeg(a, f) <= ab + 1e-9 && angDeg(b, f) <= ab + 1e-9) return crossTrack;
    const double da = angDeg(p, a), db = angDeg(p, b);
    return da < db ? da : db;
}

/// Nearest approach of any shipped border segment to a point, in degrees.
static double minDistDeg(double lat, double lon)
{
    const V3 p = U(lat, lon);
    const Coastline* L = Borders();
    const int n = BorderCount();
    double best = 1e9;
    for (int i = 0; i < n; ++i)
        for (int j = 0; j + 1 < L[i].n; ++j) {
            const double d = segDistDeg(p, fromStored(L[i].v[j]),
                                           fromStored(L[i].v[j + 1]));
            if (d < best) best = d;
        }
    return best;
}

// ---------------------------------------------------------------------------
// The drawn-segment list, with and without the chunk cull.
// ---------------------------------------------------------------------------
struct Seg { int x0, y0, x1, y1; };
static bool operator==(const Seg& a, const Seg& b)
{ return a.x0 == b.x0 && a.y0 == b.y0 && a.x1 == b.x1 && a.y1 == b.y1; }

static const int SS = 240;   // the panel this models

/// `culled` selects the shipped path (chunk caps) or the reference path (every
/// polyline, every segment). They must agree EXACTLY.
static std::vector<Seg> render(const Basis& b, float R, bool culled)
{
    std::vector<Seg> out;
    const Coastline* L = Borders();
    const float cx = SS / 2.0f, cy = SS / 2.0f;

    const float halfDiag = (SS / 2.0f) * 1.41421356f;
    const float viewRad = std::asin(std::fmin(1.0f, halfDiag / R)) + 0.0175f;
    const float cosV = std::cos(viewRad), sinV = std::sin(viewRad);

    const auto emit = [&](const GeoVec* v, int first, int count) {
        float px = 0.0f, py = 0.0f;
        bool pv = false;
        for (int s = 0; s <= count; ++s) {
            const GeoVec& p = v[first + s];
            float x = 0.0f, y = 0.0f;
            const bool vis = Project(b, p.x * VEC_INV, p.y * VEC_INV, p.z * VEC_INV,
                                     cx, cy, R, x, y);
            if (vis && pv && !SegmentOffPanel(px, py, x, y, SS, SS)) {
                Seg sg = { (int)px, (int)py, (int)x, (int)y };
                out.push_back(sg);
            }
            px = x; py = y; pv = vis;
        }
    };

    if (culled) {
        int nc = 0;
        const ChunkCap* ch = BorderChunks(nc);
        for (int c = 0; c < nc; ++c) {
            if (!CapMayBeVisible(ch[c].cap, b.v, cosV, sinV)) continue;
            emit(L[ch[c].ring].v, ch[c].first, ch[c].count);
        }
    } else {
        for (int i = 0; i < BorderCount(); ++i)
            if (L[i].n >= 2) emit(L[i].v, 0, L[i].n - 1);
    }
    return out;
}

/// Set-equality, because the cull visits chunks in a different order than the
/// reference visits whole lines. Sorting would hide a duplicate; counting
/// membership both ways does not.
static bool sameSegments(std::vector<Seg> a, std::vector<Seg> b)
{
    if (a.size() != b.size()) return false;
    std::vector<bool> used(b.size(), false);
    for (size_t i = 0; i < a.size(); ++i) {
        bool hit = false;
        for (size_t j = 0; j < b.size(); ++j)
            if (!used[j] && a[i] == b[j]) { used[j] = true; hit = true; break; }
        if (!hit) return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// ASCII render, for the half of this that no assertion covers: whether the
// shapes look like the country.
// ---------------------------------------------------------------------------
static void ascii(const char* title, float alon, float alat, float blon, float blat,
                  float R)
{
    const int W = 78, H = 39;
    static char fb[64][128];
    for (int y = 0; y < H; ++y) { std::memset(fb[y], ' ', W); fb[y][W] = 0; }

    const Basis b = MakeBasis(alon, alat, blon, blat, 0.0f);
    const float cx = SS / 2.0f, cy = SS / 2.0f;
    const auto plot = [&](float sx, float sy, char c) {
        const int x = (int)(sx * W / SS), y = (int)(sy * H / SS);
        if (x >= 0 && x < W && y >= 0 && y < H && (fb[y][x] == ' ' || c != '#'))
            fb[y][x] = c;
    };

    const std::vector<Seg> segs = render(b, R, true);
    for (size_t i = 0; i < segs.size(); ++i) {
        const Seg& s = segs[i];
        const int n = (int)std::fmax(std::fabs((float)(s.x1 - s.x0)),
                                     std::fabs((float)(s.y1 - s.y0))) + 1;
        for (int k = 0; k <= n; ++k) {
            const float t = (float)k / n;
            plot(s.x0 + (s.x1 - s.x0) * t, s.y0 + (s.y1 - s.y0) * t, '#');
        }
    }
    // The endpoints, so the picture can be located.
    float w[3], x = 0.0f, y = 0.0f;
    UnitVec(alon, alat, w);
    if (Project(b, w[0], w[1], w[2], cx, cy, R, x, y)) plot(x, y, 'A');
    UnitVec(blon, blat, w);
    if (Project(b, w[0], w[1], w[2], cx, cy, R, x, y)) plot(x, y, 'B');

    std::printf("\n  %s   (R=%.0f, %zu segments drawn)\n", title, R, segs.size());
    for (int yy = 0; yy < H; ++yy) std::printf("  |%s|\n", fb[yy]);
}

// ---------------------------------------------------------------------------
struct Probe { const char* name; double lat, lon; };

int main()
{
    std::printf("== state / province / international borders ==\n");

    // ---- the data itself ---------------------------------------------------
    {
        const int n = BorderCount();
        int verts = 0;
        for (int i = 0; i < n; ++i) verts += Borders()[i].n;
        std::printf("  %d polylines, %d vertices, %d B of flash\n",
                    n, verts, verts * 6);
        check(n == 148, "148 polylines, as generated at 0.15 deg from both layers");
        check(verts == 537, "537 vertices, measured -- NOT estimated");

        bool allUnit = true;
        for (int i = 0; i < n; ++i)
            for (int j = 0; j < Borders()[i].n; ++j) {
                const GeoVec& g = Borders()[i].v[j];
                const double m = std::sqrt((double)g.x*g.x + (double)g.y*g.y
                                         + (double)g.z*g.z) / 32767.0;
                if (std::fabs(m - 1.0) > 0.002) allUnit = false;
            }
        check(allUnit, "every stored vertex is a unit vector x32767");

        // THE EXTENT, which is the only assertion here that covers vertices no
        // probe visits. A point-probe can only say "nothing is near THIS spot";
        // a bound says "nothing is south of here at all", and that is the shape
        // of the failure a geographic selection produces -- a whole extra
        // country arriving intact.
        double laMin = 90.0, laMax = -90.0, loMin = 180.0, loMax = -180.0;
        for (int i = 0; i < n; ++i)
            for (int j = 0; j < Borders()[i].n; ++j) {
                const V3 v = fromStored(Borders()[i].v[j]);
                const double la = std::asin(v.z) * 180.0 / kPi;
                const double lo = std::atan2(v.y, v.x) * 180.0 / kPi;
                if (la < laMin) laMin = la;
                if (la > laMax) laMax = la;
                if (lo < loMin) loMin = lo;
                if (lo > loMax) loMax = lo;
            }
        std::printf("  extent  lat %.2f..%.2f  lon %.2f..%.2f\n",
                    laMin, laMax, loMin, loMax);
        // 25.96 N is the Rio Grande at Brownsville -- the southernmost point of
        // the US/Mexico border, and therefore of this whole dataset. Anything
        // south of 25 N means the admin-0 box let a part through.
        check(laMin > 25.0, "nothing south of 25 N -- no Central American part");
        check(laMin < 26.5, "and the Rio Grande mouth IS present (not over-cropped)");
        // 141.00 W is the Alaska/Yukon meridian; -57 is Newfoundland's approach.
        check(loMin > -142.0 && loMin < -140.0, "west edge is the 141 W meridian");
        check(loMax < -55.0, "nothing east of the Atlantic seaboard");

        int nc = 0;
        BorderChunks(nc);
        std::printf("  %d chunks built (table is BORDER_CHUNK_COUNT)\n", nc);
        // 0 is what BuildChunks returns on overflow. It cannot happen here --
        // the table is sized by the generator's own count -- so a 0 means the
        // .inc and the table have come apart.
        check(nc > 0, "the chunk table did not overflow");
        check(nc == 148, "148 chunks, matching the generator's BORDER_CHUNK_COUNT");
    }

    // ---- the four features the old table could not express -----------------
    //
    // Threshold is the generator's own tolerance: Douglas-Peucker at 0.15 deg
    // guarantees the kept polyline stays within 0.15 deg of the original, so a
    // raw source vertex further than that from every shipped segment means the
    // feature was DROPPED, not merely simplified.
    static const Probe kPositive[] = {
        // The Columbia, OR/WA -- raw admin-1 vertices.
        { "Columbia at Portland",      45.6274, -122.6498 },
        { "Columbia at Hood River",    45.7118, -121.4021 },
        { "Columbia at Cathlamet",     46.1788, -123.1209 },
        // The Snake, OR/ID above 44.3 N.
        { "Snake at Brownlee",         44.7701, -116.9490 },
        { "Snake in Hells Canyon",     45.4567, -116.5492 },
        { "Snake below Lewiston",      45.9585, -116.8692 },
        // The Bitterroot crest, ID/MT.
        { "Bitterroot south",          45.8909, -114.3968 },
        { "Bitterroot at Lost Trail",  46.4530, -114.3802 },
        { "Bitterroot at Lolo",        47.0846, -115.1266 },
        // California's southern edge -- raw admin-0 vertices. ABSENT from the
        // admin-1 layer, which is why this generator reads two files.
        { "CA south, inland of Tijuana", 32.5548, -116.8421 },
        { "CA south, midpoint",          32.6190, -115.9837 },
        { "CA south, west of Yuma",      32.7047, -114.8391 },
        // Alaska / Yukon on the 141 W meridian. The OTHER thing admin-1 left
        // out, found by the same probe that found California and fixed by the
        // same layer -- and it went into the first version of this test as a
        // remark rather than a case, which is how a fix stops being checked.
        { "Alaska/Yukon 141 W, south",   60.3002, -141.0021 },
        { "Alaska/Yukon 141 W, north",   69.6508, -141.0021 },
        // The sanity anchors: the projection and the data agree about these or
        // the disagreement is in the rig, not the map.
        { "ANCHOR 42 N, the OR/CA line", 42.0006, -122.9010 },
        { "ANCHOR the WA/ID stub, 117 W",47.2307, -117.0292 },
        { "ANCHOR 49 N above Idaho",     48.9931, -117.0391 },
        { "ANCHOR 49 N above N. Dakota", 48.9931, -104.0339 },
    };
    // The negatives are two different claims and the second is the one that was
    // missing. "Central Nevada is not on a border" grades the DISTANCE function.
    // "The Mexico/Guatemala border is not in this data" grades the SELECTION --
    // and nothing in the first version of this test would have failed if an
    // extra Central American line had come along for the ride. It would simply
    // have been a line on the map: plausible, wrong, and passing.
    //
    // The admin-0 layer has no country attribution at all -- every NAME is null
    // -- so it is selected by FEATURECLA plus a box, and the box's 24 N floor is
    // the only thing keeping Central America out. Measured: drop that floor to
    // 12 N and seven more parts / 251 vertices sweep in (Mexico/Guatemala,
    // Guatemala/Belize, Honduras/Nicaragua, El Salvador, Haiti/Dominican
    // Republic). Those seven are what these probes are aimed at.
    static const Probe kNegative[] = {
        // grading the distance function
        { "central Nevada",             39.0000, -117.0000 },
        { "central Kansas",             38.5000,  -98.5000 },
        { "Hudson Bay",                 59.0000,  -86.0000 },
        { "the mid Pacific",            30.0000, -150.0000 },
        // grading the SELECTION -- raw vertices of parts the box must exclude
        { "Mexico/Guatemala",           16.0700,  -90.1000 },
        { "Guatemala/Belize",           17.8100,  -89.1400 },
        { "Honduras/Nicaragua",         14.0000,  -85.0000 },
        { "Haiti/Dominican Republic",   19.0000,  -71.7500 },
    };

    std::printf("\n  ---- named features, against the raw source's own vertices\n");
    double worstPos = 0.0;
    for (size_t i = 0; i < sizeof(kPositive)/sizeof(kPositive[0]); ++i) {
        const double d = minDistDeg(kPositive[i].lat, kPositive[i].lon);
        if (d > worstPos) worstPos = d;
        std::printf("      %-32s %7.4f deg\n", kPositive[i].name, d);
        check(d < 0.15, kPositive[i].name);
    }

    std::printf("\n  ---- the control: points that must NOT be near a border\n");
    double nearestNeg = 1e9;
    for (size_t i = 0; i < sizeof(kNegative)/sizeof(kNegative[0]); ++i) {
        const double d = minDistDeg(kNegative[i].lat, kNegative[i].lon);
        if (d < nearestNeg) nearestNeg = d;
        std::printf("      %-32s %7.4f deg\n", kNegative[i].name, d);
        check(d > 0.5, kNegative[i].name);
    }
    std::printf("      worst positive %.4f, nearest negative %.4f -- %.0fx apart\n",
                worstPos, nearestNeg, nearestNeg / (worstPos > 0 ? worstPos : 1));
    // Without this the two sets could both pass while overlapping, which is the
    // state a half-broken distance function produces.
    check(nearestNeg > worstPos * 5.0,
          "the positive and negative populations are cleanly separated");

    // ---- the chunked cull draws exactly what brute force draws -------------
    //
    // This is what catches the wiring rather than the data. A border polyline is
    // OPEN, so it has n-1 segments and no closing wrap; the coastline's builder
    // assumes closed rings. Getting that wrong drops the last segment of every
    // line, or invents one across the map from a line's end to its start, and
    // neither is visible on a 240 px disc among 148 lines.
    std::printf("\n  ---- chunked cull vs brute force, four views\n");
    struct View { const char* n; float alon, alat, blon, blat, R; };
    static const View kViews[] = {
        { "SEA -> BUR",   -122.3088f, 47.4502f, -118.3587f, 34.2007f, 500.0f },
        { "PDX -> LAX",   -122.5960f, 45.5887f, -118.4085f, 33.9416f, 500.0f },
        { "ORD -> YYZ",    -87.9048f, 41.9786f,  -79.6306f, 43.6777f, 500.0f },
        { "the Atlantic",  -30.0000f, 40.0000f,  -25.0000f, 42.0000f, 500.0f },
    };
    for (size_t i = 0; i < sizeof(kViews)/sizeof(kViews[0]); ++i) {
        const Basis b = MakeBasis(kViews[i].alon, kViews[i].alat,
                                  kViews[i].blon, kViews[i].blat, 0.0f);
        const std::vector<Seg> culled = render(b, kViews[i].R, true);
        const std::vector<Seg> all    = render(b, kViews[i].R, false);
        std::printf("      %-14s culled %4zu   brute %4zu\n",
                    kViews[i].n, culled.size(), all.size());
        check(sameSegments(culled, all), kViews[i].n);
    }
    // The cull has to actually cull, or the equality above is trivially true.
    {
        const Basis b = MakeBasis(-122.3088f, 47.4502f, -118.3587f, 34.2007f, 0.0f);
        int nc = 0;
        const ChunkCap* ch = BorderChunks(nc);
        const float halfDiag = (SS / 2.0f) * 1.41421356f;
        const float vr = std::asin(std::fmin(1.0f, halfDiag / 500.0f)) + 0.0175f;
        int kept = 0;
        for (int c = 0; c < nc; ++c)
            if (CapMayBeVisible(ch[c].cap, b.v, std::cos(vr), std::sin(vr))) ++kept;
        std::printf("      SEA -> BUR keeps %d of %d chunks\n", kept, nc);
        check(kept < nc / 2, "the cull discards most chunks at a regional view");
        check(kept > 0,      "and is not discarding all of them");
    }

    // ---- and the picture ---------------------------------------------------
    ascii("SEA -> BUR, the view the owner looks at daily",
          -122.3088f, 47.4502f, -118.3587f, 34.2007f, 500.0f);
    ascii("the Columbia, the Snake and the Bitterroot",
          -123.1209f, 46.1788f, -114.3802f, 46.4530f, 500.0f);
    ascii("California's southern edge",
          -117.1283f, 32.5334f, -114.7248f, 32.7153f, 500.0f);

    std::printf("\n%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
