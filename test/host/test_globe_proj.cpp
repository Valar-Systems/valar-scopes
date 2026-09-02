// Host test for include/GlobeProjection.h -- the orthographic globe extracted
// from src/anim/FlightAnimation.cpp.
//
// =============================================================================
// THIS TEST IS THE REASON THE EXTRACTION WAS SAFE TO DO
//
// The projection was moved out of a shipping picture. "Behaviour-preserving" is
// the kind of claim that is easy to make and, on a graphic, nearly impossible to
// check by eye: a basis wrong in the third decimal moves a coastline by a
// pixel, which looks exactly like the coastline.
//
// So the expected values below were PRINTED BY THE PRE-EXTRACTION CODE. A copy
// of the original functions -- the anonymous-namespace UnitVec/Norm3/Cross3/
// Dot3/BuildGlobeBasis/GlobePt, verbatim, with the GOLF-07 scenario's launch and
// aim -- was compiled and run on 2026-08-27, and its output is transcribed here
// to nine decimal places. Every assertion is against that output, not against a
// hand-derived expectation of what the maths ought to do.
//
// That distinction matters: a test written from first principles would pass
// against a correct-but-different implementation, and "different" is precisely
// what must not have happened.
//
// =============================================================================
// AND EVERY PIN HAS A CONTROL
//
// A golden-value test passes trivially if the thing under test never varies. So
// each block below is paired with an assertion that some OTHER input produces a
// materially different answer -- otherwise nine matching numbers prove only that
// nine constants were typed twice.
#include <cmath>
#include <cstdio>

#include "../../include/GlobeProjection.h"

static int failures = 0;
static int checks   = 0;

static void check(bool ok, const char* what)
{
    ++checks;
    if (!ok) { std::printf("  FAIL: %s\n", what); ++failures; }
}

static bool near(float a, float b, float eps) { return std::fabs(a - b) <= eps; }

using namespace globeproj;

int main()
{
    std::printf("== GlobeProjection (extracted from src/anim) ==\n");

    // GOLF-07: F.E. Warren AFB -> the South Pacific pole of inaccessibility, the
    // animation's default scenario, at its fixed 30 degree tilt.
    const float LLON = -104.87f, LLAT =  41.15f;
    const float ALON = -123.39f, ALAT = -48.87f;
    const Basis g = MakeBasis(LLON, LLAT, ALON, ALAT, 30.0f);

    std::printf("  ---- the basis, against what the old code printed\n");
    // Tolerance is 1e-6: these came from the same float arithmetic, so they
    // should agree to the last digit that was printed, not merely be close.
    check(near(g.v[0],  0.110419028f, 1e-6f) &&
          near(g.v[1], -0.984292865f, 1e-6f) &&
          near(g.v[2], -0.137750462f, 1e-6f), "view vector matches the original");
    check(near(g.r[0],  0.993766487f, 1e-6f) &&
          near(g.r[1],  0.111481786f, 1e-6f) &&
          near(g.r[2],  0.000000000f, 1e-6f), "right vector matches the original");
    check(near(g.u[0],  0.015356667f, 1e-6f) &&
          near(g.u[1], -0.136891797f, 1e-6f) &&
          near(g.u[2],  0.990466952f, 1e-6f), "up vector matches the original");

    // Orthonormality, which the golden values cannot check on their own: three
    // transcribed vectors are three transcribed vectors.
    check(near(Dot3(g.v, g.r), 0.0f, 1e-5f) &&
          near(Dot3(g.v, g.u), 0.0f, 1e-5f) &&
          near(Dot3(g.r, g.u), 0.0f, 1e-5f), "the basis is orthogonal");
    check(near(Dot3(g.v, g.v), 1.0f, 1e-5f) &&
          near(Dot3(g.r, g.r), 1.0f, 1e-5f) &&
          near(Dot3(g.u, g.u), 1.0f, 1e-5f), "and normalised");

    std::printf("  ---- real coastline vertices, through the same c and R\n");
    // Nine vertices with the screen positions the ORIGINAL GlobePt gave them at
    // c=120, R=119.
    //
    // THEY ARE FIXED INPUTS, NOT A SAMPLE OF THE SHIPPED DATA -- and that
    // distinction only became visible when the data changed under them. They
    // were copied from the head of the 0.50 deg set, which is no longer what
    // ships (the density moved to 0.15 on 2026-08-30). Nothing about this block
    // needs updating for that: it grades the PROJECTION, and the projection is
    // behaviour-preserving or not regardless of which vertices happen to be in
    // the file today. Re-pinning them to the new data's head would swap a
    // pre-extraction golden for a post-extraction one and quietly delete the
    // only reason the block exists.
    struct Golden { int16_t x, y, z; float sx, sy; bool near_; };
    static const Golden kGolden[] = {
        {  -110, -11476, 30691, 114.956730f,  3.902812f, true },
        {   587, -12685, 30206, 116.982761f,  5.007469f, true },
        {   880, -11236, 30768, 118.626872f,  3.689939f, true },
        {  1463, -11296, 30724, 120.706664f,  3.785868f, true },
        {  1909, -12601, 30187, 121.787953f,  5.043845f, true },
        {  6960, -14647, 28473, 139.188976f,  9.910375f, true },
        { 10239, -15969, 26718, 150.487862f, 15.383148f, true },
        {  4211, -27637, 17092, 124.008414f, 44.544090f, true },
        {  5047, -28788, 14815, 126.559588f, 52.115800f, true },
    };
    for (const Golden& gd : kGolden) {
        float sx = 0.0f, sy = 0.0f;
        const bool vis = Project(g, gd.x * VEC_INV, gd.y * VEC_INV, gd.z * VEC_INV,
                                 120.0f, 119.0f, sx, sy);
        check(near(sx, gd.sx, 1e-3f) && near(sy, gd.sy, 1e-3f),
              "a coastline vertex lands where it landed before the extraction");
        check(vis == gd.near_, "... and on the same side of the limb");
    }

    // The two scenario endpoints, likewise pinned.
    {
        float w[3], sx = 0.0f, sy = 0.0f;
        UnitVec(LLON, LLAT, w);
        Project(g, w[0], w[1], w[2], 120.0f, 119.0f, sx, sy);
        check(near(sx, 87.493179f, 1e-3f) && near(sy, 30.938223f, 1e-3f),
              "the launch point projects where it did");
        UnitVec(ALON, ALAT, w);
        Project(g, w[0], w[1], w[2], 120.0f, 119.0f, sx, sy);
        check(near(sx, 69.905365f, 1e-3f) && near(sy, 200.493607f, 1e-3f),
              "and so does the aim point");
    }

    // THE CONTROL FOR ALL OF THE ABOVE. Nine matching pairs prove nothing if the
    // projection ignores its basis, so a DIFFERENT basis must move them.
    {
        const Basis other = MakeBasis(0.0f, 0.0f, 90.0f, 0.0f, 30.0f);
        float sx = 0.0f, sy = 0.0f;
        Project(other, kGolden[0].x * VEC_INV, kGolden[0].y * VEC_INV,
                kGolden[0].z * VEC_INV, 120.0f, 119.0f, sx, sy);
        check(std::fabs(sx - kGolden[0].sx) > 10.0f ||
              std::fabs(sy - kGolden[0].sy) > 10.0f,
              "CONTROL: another basis puts the same vertex somewhere else");
    }

    std::printf("  ---- the basis is a parameter now, and §9's example proves it\n");
    // The extraction's one design change: a basis per route rather than one
    // file-global built from missile globals. §9 computed DEN->DEL's midpoint by
    // hand at 83.9 N, 88.6 E -- so with NO tilt the view vector must point
    // exactly there, which ties the parameterised basis to a number derived
    // independently of this code.
    {
        const Basis den2del = MakeBasis(-104.67f, 39.86f, 77.10f, 28.57f, 0.0f);
        float mid[3];
        UnitVec(88.6f, 83.9f, mid);
        const float cosang = Dot3(den2del.v, mid);
        check(cosang > 0.99985f,   // within ~1 degree of the spec's midpoint
              "an untilted DEN->DEL basis looks at §9's hand-computed midpoint");

        // Both endpoints must be on the near hemisphere, or the globe crops the
        // route it exists to show.
        float a[3], b[3], sx = 0.0f, sy = 0.0f;
        UnitVec(-104.67f, 39.86f, a);
        UnitVec(77.10f, 28.57f, b);
        check(Project(den2del, a[0], a[1], a[2], 120.0f, 94.0f, sx, sy),
              "DEN is on the visible hemisphere");
        check(Project(den2del, b[0], b[1], b[2], 120.0f, 94.0f, sx, sy),
              "so is DEL");

        // CONTROL: a basis aimed at the wrong hemisphere must NOT see them, or
        // the two assertions above would pass for any basis at all.
        const Basis wrong = MakeBasis(-88.6f, -83.9f, -88.0f, -83.0f, 0.0f);
        check(!Project(wrong, a[0], a[1], a[2], 120.0f, 94.0f, sx, sy),
              "CONTROL: a basis pointed at the antipode does not see DEN");
    }

    std::printf("  ---- the degenerate guard, which arbitrary routes can reach\n");
    // The original's comment said the pole case "this tilt cannot produce" --
    // true of one fixed scenario, not of arbitrary routes. A route whose view
    // lands on the pole must still yield a finite orthonormal basis.
    {
        const Basis polar = MakeBasis(0.0f, 89.999f, 180.0f, 89.999f, 0.0f);
        check(std::isfinite(polar.r[0]) && std::isfinite(polar.r[1]) &&
              std::isfinite(polar.r[2]), "a polar view still yields a finite right vector");
        check(near(Dot3(polar.r, polar.r), 1.0f, 1e-4f),
              "... and a normalised one");
        check(near(Dot3(polar.v, polar.r), 0.0f, 1e-4f),
              "... still orthogonal to the view");
    }

    std::printf("  ---- the coastline data survived the move\n");
    {
        const Coastline* c = Coastlines();
        const int n = CoastlineCount();
        // THE TRIPWIRE, AND IT HAS FIRED ONCE IN ANGER. These numbers exist so
        // that regenerating the data cannot happen silently -- "if this moves,
        // the globe's cost figures moved with it". On 2026-08-30 a docs commit
        // swept a candidate .inc into the branch and this is what said so.
        //
        // 0.15 deg, adopted 2026-08-30 after the two candidates were compared on
        // glass. Was 84 rings / 1,306 vertices at 0.50 deg. If you are changing
        // these numbers, you are changing what ships: re-measure the globe face
        // and re-read the residual, because the previous change spent all of the
        // model's cushion (see DrawRouteGlobe).
        check(n == 105, "105 rings, as generated at 0.15 deg");
        int verts = 0;
        for (int i = 0; i < n; ++i) verts += c[i].n;
        check(verts == 5286, "5,286 vertices, as measured 2026-08-30");
        check(c[0].v[0].x == -770 && c[0].v[0].y == -10224 && c[0].v[0].z == 31121,
              "and the first vertex is the shipped data's own, read back from it");
        // Every vertex should be a unit vector x32767, within rounding.
        bool allUnit = true;
        for (int i = 0; i < n; ++i)
            for (int j = 0; j < c[i].n; ++j) {
                const float x = c[i].v[j].x * VEC_INV;
                const float y = c[i].v[j].y * VEC_INV;
                const float z = c[i].v[j].z * VEC_INV;
                if (!near(std::sqrt(x * x + y * y + z * z), 1.0f, 0.002f)) allUnit = false;
            }
        check(allUnit, "every stored vertex is a unit vector");
    }

    std::printf("\n%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
