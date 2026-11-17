// Board addendum 2's L/rung pick (docs/speed-p5p6.md sections 7 and 7d, ruling A3 (a)): the anchor scenes' rung and
// level, chosen on the HOST before the window, from the device's own arithmetic ported exactly:
//   - the table: fbtable::At(home, i, kTblT) (include/FrameBenchTable.h, the header the device compiles);
//   - the wire and its parse: lat4 = Q4(lat), then (float)lat4 / 1e4f (src/CloudFeed.cpp); track = (float)lround(track);
//   - the box: R mi x 1.609344 km, radLat = clamp(km / 111.0), radLon = clamp(km / (111.0 cos(lat DEG_TO_RAD)))
//     (AircraftManager's radius block), and the view factor exactly 1.0 at the top of the zoom ladder (ApplyZoom);
//   - the screen: radarproj::Project (include/RadarProjection.h), up 0 (the anchor scenes override radar-up to 0).
// The home and kTblT are READ from include/FrameBench.h, never retyped here; the home is never printed.
//
// THE RULE, pre-registered (7d's message to the reviewer, 01:51Z): for R = 8, 7, 6, 5, 4 mi in that order, the first
// R where some L in 20..60 has >= 1 overlapping pair and >= 1 edge-clipped contact (anchor_core's definitions); at
// that R the smallest such L. None -> A3 (b), named.
//
//   anchor_pick <repo-root>     exit 0 a pick, 3 no R qualifies (A3 (b)), 2 the rig
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "FrameBenchTable.h"
#include "RadarProjection.h"
#include "anchor_core.h"

namespace {

constexpr double kDegToRad = 0.017453292519943295769236907684886;   // Arduino's DEG_TO_RAD
constexpr double kKmPerMile = 1.609344;                              // units::KM_PER_MILE
constexpr double kKmPerDegree = 111.0, kMinDeg = 0.001, kMaxDeg = 2.0;

bool Between(const std::string& text, const std::string& open, const std::string& close, std::string& out)
{
    const size_t a = text.find(open);
    if (a == std::string::npos || text.find(open, a + 1) != std::string::npos) return false;   // exactly once
    const size_t b = text.find(close, a + open.size());
    if (b == std::string::npos) return false;
    out = text.substr(a + open.size(), b - a - open.size());
    return true;
}

struct Pick {   // an aggregate (C++11: no default member initializers), so Pick{...} below compiles
    int r, l, onScreen, pairs, clipped;
};

}  // namespace

int main(int argc, char** argv)
{
    if (argc != 2) {
        std::printf("usage: anchor_pick <repo-root>\n");
        return 2;
    }
    std::ifstream in(std::string(argv[1]) + "/include/FrameBench.h");
    std::stringstream ss;
    ss << in.rdbuf();
    const std::string fb = ss.str();
    std::string sLat, sLon, sT;
    if (!Between(fb, "kHomeLat = \"", "\"", sLat) || !Between(fb, "kHomeLon = \"", "\"", sLon) ||
        !Between(fb, "constexpr uint32_t kTblT = ", ";", sT)) {
        std::printf("RIG: include/FrameBench.h does not carry kHomeLat, kHomeLon and kTblT exactly once each\n");
        return 2;
    }
    const double lat0 = std::atof(sLat.c_str()), lon0 = std::atof(sLon.c_str());   // OnCloudPoll's and the device's
    const double t = (double)std::strtoul(sT.c_str(), nullptr, 10);
    std::printf("kTblT %.0f (read from include/FrameBench.h); home read from there, not printed\n", t);

    const int kMaxL = 60;
    for (int R = 8; R >= 4; --R) {
        const double km = R * kKmPerMile;
        double cosLat = std::cos(lat0 * kDegToRad);
        if (cosLat < 0.01) cosLat = 0.01;
        const double radLat = std::min(std::max(km / kKmPerDegree, kMinDeg), kMaxDeg);
        const double radLon = std::min(std::max(km / (kKmPerDegree * cosLat), kMinDeg), kMaxDeg);
        anchor::Frame f;
        f.up = 0;
        f.zoomed = false;
        for (int i = 0; i < kMaxL; ++i) {
            const fbtable::Row row = fbtable::At(lat0, lon0, i, t);
            const float la = (float)fbtable::Q4(row.lat) / 1e4f, lo = (float)fbtable::Q4(row.lon) / 1e4f;
            const std::pair<int, int> p = radarproj::Project(la, lo, lat0, lon0, radLat, radLon, 0.0f, 1.0f, anchor::W);
            f.k.push_back(anchor::Contact{fbtable::kCategoryA3, (float)lround(row.track), p.first, p.second, false,
                                          0xFFFFu});
        }
        std::vector<std::vector<uint8_t> > fp;
        std::vector<int> on(kMaxL), clip(kMaxL);
        for (int i = 0; i < kMaxL; ++i) {
            fp.push_back(anchor::FootprintOf(f, i));
            on[i] = 0;
            for (int j = 0; j < anchor::W * anchor::W && !on[i]; ++j) on[i] = fp[i][j] != 0;
            clip[i] = anchor::EdgeClipped(f, i);
        }
        Pick best = {0, 0, 0, 0, 0};
        for (int L = 20; L <= kMaxL && !best.l; ++L) {
            int pairs = 0, clipped = 0, onScreen = 0;
            for (int a = 0; a < L; ++a) {
                clipped += clip[a];
                onScreen += on[a];
                for (int b = a + 1; b < L; ++b) pairs += anchor::Overlap(fp[a], fp[b]);
            }
            if (pairs >= 1 && clipped >= 1) best = Pick{R, L, onScreen, pairs, clipped};
        }
        if (!best.l) {
            std::printf("R %d mi: no L in 20..60 qualifies\n", R);
            continue;
        }
        std::printf("R %d mi: L %d qualifies (contacts %d, on screen %d, overlapping pairs %d, edge-clipped %d)\n", R,
                    best.l, best.l, best.onScreen, best.pairs, best.clipped);
        std::printf("PICK: rung %d mi, L %d. The contacts (table order; the device prints its own draw order):\n", R,
                    best.l);
        for (int i = 0; i < best.l; ++i)
            std::printf("  c%d x %d y %d trk %ld cat %d on %d clipped %d\n", i, f.k[i].x, f.k[i].y,
                        lround(f.k[i].track * 10.0f), f.k[i].cat, on[i], clip[i]);
        std::printf("overlapping pairs:");
        for (int a = 0; a < best.l; ++a)
            for (int b = a + 1; b < best.l; ++b)
                if (anchor::Overlap(fp[a], fp[b])) std::printf(" (c%d, c%d)", a, b);
        std::printf("\n");
        return 0;
    }
    std::printf("NO R in 8..4 qualifies: A3 (b), the edge untested on glass, goes to the reviewer\n");
    return 3;
}
