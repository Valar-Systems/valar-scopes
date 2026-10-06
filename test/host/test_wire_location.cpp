// Precise on device, coarse on the wire: include/WireLocation.h.
//
// 1. Center() writes exactly 2 dp, half away from zero, never "-0.00".
// 2. THE TILE CLAIM. The Worker snaps /blips' centre to a 0.05 deg tile with JavaScript's
//    Math.round (proxy/src/schema.ts quantizeTile). A 2-dp centre must land in the SAME tile
//    as the 4-dp centre it came from, or the request would hit a different cache entry.
//    Swept over every 4-dp latitude; the only permitted disagreement is a value sitting
//    EXACTLY on a tile edge (an odd multiple of 0.025), where the 4-dp request is itself a tie.
// 3. THE BOX CLAIM. OpenSkyBox() built from the ROUNDED centre must contain the TRUE box for
//    every centre and radius swept -- no aircraft near the true edge is lost -- and every edge
//    is on the 2-dp grid.
//    CONTROL: the same box WITHOUT the one-step widening must fail containment somewhere,
//    so the containment check can fail.
//
// Exit: 0 ok, 1 a claim failed.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "../../include/WireLocation.h"

static int failures = 0;
static void check(bool ok, const char* what)
{
    std::printf("  %s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++failures;
}

// proxy/src/schema.ts: (Math.round(v / TILE_DEG) * TILE_DEG).toFixed(2). JS Math.round is
// floor(x + 0.5) -- NOT C's round() -- so it is written out here (CLAUDE.md, the rounding
// entry: a port of Worker math pins its rounding).
static long jsTileIndex(double v) { return (long)std::floor(v / 0.05 + 0.5); }

static bool on2dp(double e) { return std::fabs(e * 100.0 - std::round(e * 100.0)) < 1e-6; }

int main()
{
    std::printf("wire location (WireLocation.h)\n");

    // ---- 1. Center ------------------------------------------------------------------
    check(std::strcmp(wireloc::Center(44.0582).c_str(), "44.06") == 0, "44.0582 -> 44.06");
    check(std::strcmp(wireloc::Center(-121.3153).c_str(), "-121.32") == 0, "-121.3153 -> -121.32");
    check(std::strcmp(wireloc::Center(-0.0040).c_str(), "0.00") == 0, "-0.0040 -> 0.00, never -0.00");
    check(std::strcmp(wireloc::Center(90.0).c_str(), "90.00") == 0, "90 -> 90.00");
    check(std::strcmp(wireloc::Center(-180.0).c_str(), "-180.00") == 0, "-180 -> -180.00");
    {
        int bad = 0;
        for (long i = -1800000; i <= 1800000; i += 7) {         // every 7th 4-dp longitude
            const double v = i / 10000.0;
            const String str = wireloc::Center(v);              // keep it alive: c_str() borrows
            const char* s = str.c_str();
            const char* dot = std::strchr(s, '.');
            if (!dot || std::strlen(dot + 1) != 2) ++bad;
            if (std::fabs(std::atof(s) - v) > 0.005 + 1e-9) ++bad;
        }
        std::printf("  2-dp sweep: %d malformed or off by more than half a step\n", bad);
        check(bad == 0, "every Center() is 2 dp and within half a step");
    }

    // ---- 2. the tile claim ----------------------------------------------------------
    {
        long total = 0, diff = 0, diffAtEdge = 0;
        for (long i = -899999; i <= 899999; ++i) {
            const double v4 = i / 10000.0;                      // the 4-dp value the old wire sent
            const double v2 = std::atof(wireloc::Center(v4).c_str());
            ++total;
            if (jsTileIndex(v4) != jsTileIndex(v2)) {
                ++diff;
                const double k = v4 * 40.0;                     // odd multiple of 0.025 <=> tile edge
                const long kr = std::lround(k);
                if (std::fabs(k - kr) < 1e-6 && (kr % 2 != 0)) ++diffAtEdge;
            }
        }
        std::printf("  tile sweep: %ld latitudes, %ld change tile, %ld of them exactly on a tile edge\n",
                    total, diff, diffAtEdge);
        check(diff == diffAtEdge, "a 2-dp centre stays in the 4-dp centre's Worker tile (ties excepted)");
    }

    // ---- 3. the box claim -----------------------------------------------------------
    {
        long cases = 0, lost = 0, offGrid = 0, ctlLost = 0, clamped = 0;
        const double kms[] = { 1.0, 5.0, 25.0, 100.0, 220.0 };
        for (double lat = -65.0; lat <= 65.0; lat += 0.3713) {
            for (double lon = -179.0; lon <= 179.0; lon += 1.1371) {
                for (double km : kms) {
                    double c = std::cos(lat * (3.14159265358979323846 / 180.0));
                    if (c < 0.01) c = 0.01;
                    // the device's own half-extents (AircraftManager: clamp 0.001 .. 2.0 deg)
                    const double radLat = std::fmin(std::fmax(km / 111.0, 0.001), 2.0);
                    const double radLon = std::fmin(std::fmax(km / (111.0 * c), 0.001), 2.0);
                    const wireloc::Box b = wireloc::OpenSkyBox(lat, lon, radLat, radLon);
                    ++cases;
                    // The TRUE box, clamped to where aircraft can exist. Near the antimeridian
                    // the device's own box runs past +-180; nothing flies there, and the wire
                    // box is clamped to the valid range, so compare like with like -- and count
                    // the cases this applied to rather than hide them.
                    const double t0 = std::fmax(lat - radLat, -90.0), t1 = std::fmin(lat + radLat, 90.0);
                    const double t2 = std::fmax(lon - radLon, -180.0), t3 = std::fmin(lon + radLon, 180.0);
                    if (t0 != lat - radLat || t1 != lat + radLat || t2 != lon - radLon || t3 != lon + radLon) ++clamped;
                    if (b.lamin > t0 + 1e-12 || b.lamax < t1 - 1e-12 ||
                        b.lomin > t2 + 1e-12 || b.lomax < t3 - 1e-12) ++lost;
                    if (!on2dp(b.lamin) || !on2dp(b.lamax) || !on2dp(b.lomin) || !on2dp(b.lomax)) ++offGrid;
                    // CONTROL: rounded centre, NO widening, edges at 2 dp (nearest) -- must lose some
                    const double cl = wireloc::Round2(lat), co = wireloc::Round2(lon);
                    const double n0 = wireloc::Round2(cl - radLat), n1 = wireloc::Round2(cl + radLat);
                    const double n2 = wireloc::Round2(co - radLon), n3 = wireloc::Round2(co + radLon);
                    if (n0 > lat - radLat || n1 < lat + radLat || n2 > lon - radLon || n3 < lon + radLon) ++ctlLost;
                }
            }
        }
        std::printf("  box sweep: %ld centre x radius cases (%ld past the antimeridian, clamped); "
                    "true box escapes the wire box in %ld; edges off the 2-dp grid %ld; "
                    "CONTROL (no widening) escapes in %ld\n",
                    cases, clamped, lost, offGrid, ctlLost);
        check(lost == 0, "the widened 2-dp box always contains the TRUE box");
        check(offGrid == 0, "every box edge is on the 2-dp grid");
        check(ctlLost > 0, "CONTROL: without the widening step the true box DOES escape");
    }

    // ---- InTrueBox ------------------------------------------------------------------
    check(wireloc::InTrueBox(44.5, -121.0, 44.0, -121.0, 0.9, 1.2), "inside is inside");
    check(!wireloc::InTrueBox(45.0, -121.0, 44.0, -121.0, 0.9, 1.2), "outside is outside");

    std::printf(failures ? "FAILED (%d)\n" : "ok\n", failures);
    return failures ? 1 : 0;
}
