// WD1 (docs/wx-device.md): radarproj::Project (include/RadarProjection.h) is bit-identical to the body
// AircraftManager::ProjectAt had before the extraction (test/host/ref_project_at.h, cut verbatim at 788ad44).
//
// Sweep, as frozen: homes lat 20-55 by 2.5 x lon -125 to -65 by 5; view radii 0.009-2.0 deg (5 steps);
// rotations 0, 37, 90, 180, 253 deg; sizes 240, 412, 466, 480; targets on a 21 x 21 grid across +-1.2 radii.
// Exit: 0 ok, 1 a check failed.
#include <cmath>
#include <cstdio>

#include "RadarProjection.h"
#include "ref_project_at.h"

static const double kRadii[5] = {0.009, 0.1, 0.5, 1.2, 2.0};
static const int kRot[5] = {0, 37, 90, 180, 253};

template <int S>
static long Sweep(bool flipSin, long* n)
{
    refproj::Projector<S> ref;
    long diff = 0;
    for (int a = 0; a <= 14; ++a) for (int b = 0; b <= 12; ++b) {
        ref.lat = 20.0 + a * 2.5;
        ref.lon = -125.0 + b * 5.0;
        double c = std::cos(ref.lat * 3.14159265358979323846 / 180.0);
        if (c < 0.01) c = 0.01;
        for (double rLat : kRadii) {
            const double rLon = std::fmin(rLat / c, 2.0);
            for (int rot : kRot) {
                const float rad = rot * 3.14159265f / 180.0f;
                ref.rotCos = rot == 0 ? 1.0f : std::cos(rad);
                ref.rotSin = rot == 0 ? 0.0f : std::sin(rad);
                for (int i = 0; i <= 20; ++i) for (int j = 0; j <= 20; ++j) {
                    const float tLat = (float)(ref.lat + (i / 10.0 - 1.0) * 1.2 * rLat);
                    const float tLon = (float)(ref.lon + (j / 10.0 - 1.0) * 1.2 * rLon);
                    const std::pair<int, int> want = ref.ProjectAt(tLat, tLon, rLat, rLon);
                    const std::pair<int, int> got = radarproj::Project(tLat, tLon, ref.lat, ref.lon, rLat, rLon,
                                                                       flipSin ? -ref.rotSin : ref.rotSin, ref.rotCos, S);
                    ++*n;
                    if (got != want) ++diff;
                }
            }
        }
    }
    return diff;
}

int main()
{
    std::printf("RadarProjection.h vs ProjectAt's body at 788ad44\n");
    long n = 0, d = 0;
    d += Sweep<240>(false, &n); d += Sweep<412>(false, &n); d += Sweep<466>(false, &n); d += Sweep<480>(false, &n);
    const bool ok = n == 8599500 && d == 0;
    std::printf("  %s  WD1  %ld projections, %ld different (predicted 8,599,500, 0)\n", ok ? "ok  " : "FAIL", n, d);
    long n2 = 0, d2 = 0;
    d2 += Sweep<240>(true, &n2); d2 += Sweep<480>(true, &n2);
    const bool sab = d2 > 0;
    std::printf("  %s  WD1  SABOTAGE rotSin sign flipped: %ld of %ld different (must be > 0)\n", sab ? "ok  " : "FAIL", d2, n2);
    std::printf(ok && sab ? "all radar projection checks passed\n" : "RADAR PROJECTION TESTS FAILED\n");
    return ok && sab ? 0 : 1;
}
