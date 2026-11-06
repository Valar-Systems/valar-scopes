// WD2 (docs/wx-device.md): the weather layer (include/WxLayer.h) registers with the planes.
//
//   test_wx_layer.exe <repo>/test/fixtures/wx/server
//
// Setup, as frozen: the 8 server-made crops (WS7); homes at each crop's centre +-0.005 deg; view radii from
// each crop's own r (ProjectAt's clamp) at zoom x1, x0.6, x0.3; rotations 0, 37, 90, 180, 253; sizes 240,
// 412, 466, 480.
//   (a) every pixel's cell against an INDEPENDENT double-precision inverse written here (no shared code):
//       0 mismatches outside a 1e-4-cell band around a cell boundary (the band count is printed);
//   (b) round trip: every crop cell centre that radarproj::Project (the planes' projection) puts on the
//       disc lands on a pixel whose layer cell is within |drow|, |dcol| <= 1 of it;
//   (c) every off-disc pixel is kOffDisc; (d) every on-disc pixel whose cell is outside the crop is 14.
// Sabotages: rotation ignored -> (a) > 0; rLat/rLon swapped -> (a) > 0; y flipped -> (b) > 0.
// Exit: 0 ok, 1 a check failed, 2 the rig is broken (fixtures unreadable).
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "RadarProjection.h"
#include "WxCrop.h"
#include "WxLayer.h"

typedef std::vector<uint8_t> Bytes;
static std::string g_dir;
static bool ReadFile(const std::string& name, Bytes* out)
{
    FILE* f = std::fopen((g_dir + "/" + name).c_str(), "rb");
    if (!f) return false;
    out->clear();
    uint8_t buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) out->insert(out->end(), buf, buf + n);
    std::fclose(f);
    return true;
}

struct Crop { std::string name; wxcrop::Header h; Bytes codes; double rKm; };

static const int kRot[5] = {0, 37, 90, 180, 253};
static const int kSizes[4] = {240, 412, 466, 480};
static const double kZoom[3] = {1.0, 0.6, 0.3};

// ProjectAt's view radii for range r at latitude lat (AircraftManager::Initialise's clamps).
static void Radii(double lat, double rKm, double zoom, double* rLat, double* rLon)
{
    double c = std::cos(lat * 3.14159265358979323846 / 180.0);
    if (c < 0.01) c = 0.01;
    *rLat = std::fmin(std::fmax(rKm / 111.0, 0.001), 2.0) * zoom;
    *rLon = std::fmin(std::fmax(rKm / (111.0 * c), 0.001), 2.0) * zoom;
}

struct Tally { long px = 0, mismatch = 0, band = 0, rtCells = 0, rtBad = 0, offBad = 0, nocovBad = 0,
               b1Cells = 0, b1Bad = 0, b2Cells = 0, b2Bad = 0, bothNeg = 0, ctlBad = 0; };

// One view. `layerView` is what the layer is GIVEN (sabotages change it); `truth` is the real view the
// reference and the planes use.
static void Grade(const Crop& k, const wxlayer::View& truth, const wxlayer::View& layerView, Tally* t, bool doRoundTrip)
{
    const int S = truth.size;
    std::vector<int32_t> idxR((size_t)S * S), idxC((size_t)S * S);
    std::vector<uint8_t> off((size_t)S * S), out((size_t)S * S);
    wxlayer::ForEachPixel(k.h, layerView, [&](int x, int y, bool o, int32_t r, int32_t c) {
        idxR[(size_t)y * S + x] = r; idxC[(size_t)y * S + x] = c; off[(size_t)y * S + x] = o;
    });
    wxlayer::Build(k.h, k.codes.data(), layerView, out.data());
    const double latNw = k.h.latNwMicro / 1e6, lonNw = k.h.lonNwMicro / 1e6;
    const double cLat = k.h.cellLatMicro / 1e6, cLon = k.h.cellLonMicro / 1e6;
    const double C = S / 2.0, cs = truth.rotCos, sn = truth.rotSin;
    for (int y = 0; y < S; ++y) for (int x = 0; x < S; ++x) {
        const size_t i = (size_t)y * S + x;
        // (c) and (d), on what Build wrote
        const double ddx = x + 0.5 - C, ddy = y + 0.5 - C;
        const bool refOff = ddx * ddx + ddy * ddy > C * C;
        if (refOff != (out[i] == wxlayer::kOffDisc)) ++t->offBad;
        if (refOff) continue;
        const bool outside = idxR[i] < 0 || idxR[i] >= k.h.rows || idxC[i] < 0 || idxC[i] >= k.h.cols;
        if (outside && out[i] != wxcrop::kCodeNoCoverage) ++t->nocovBad;
        // (a) the independent inverse: undo the rotation, undo the scale, then the cell
        const double ux = cs * ddx - sn * ddy, uy = sn * ddx + cs * ddy;
        const double lon = truth.homeLon + ux * (2.0 * truth.rLon / S);
        const double lat = truth.homeLat - uy * (2.0 * truth.rLat / S);
        const double fr = (latNw - lat) / cLat, fc = (lon - lonNw) / cLon;
        const long er = (long)std::floor(fr + 0.5), ec = (long)std::floor(fc + 0.5);
        ++t->px;
        if (er != idxR[i] || ec != idxC[i]) {
            const double br = std::fabs(fr + 0.5 - std::floor(fr + 0.5 + 0.5)), bc = std::fabs(fc + 0.5 - std::floor(fc + 0.5 + 0.5));
            if ((er != idxR[i] && br < 1e-4) || (ec != idxC[i] && bc < 1e-4)) ++t->band;
            else ++t->mismatch;
        }
    }
    if (!doRoundTrip) return;
    // (b) every crop cell centre, through the planes' projection, onto the layer's pixel
    for (int r = 0; r < k.h.rows; ++r) for (int c = 0; c < k.h.cols; ++c) {
        const float la = (float)(latNw - r * cLat), lo = (float)(lonNw + c * cLon);
        const std::pair<int, int> p = radarproj::Project(la, lo, truth.homeLat, truth.homeLon, truth.rLat, truth.rLon,
                                                         truth.rotSin, truth.rotCos, S);
        if (p.first < 0 || p.first >= S || p.second < 0 || p.second >= S) continue;
        const size_t i = (size_t)p.second * S + p.first;
        if (off[i]) continue;
        ++t->rtCells;
        const long dr = std::labs((long)idxR[i] - r), dc = std::labs((long)idxC[i] - c);
        if (dr > 1 || dc > 1) ++t->rtBad;                    // the frozen (b), kept as a printed record
        // (b'): classify by where Project's float x, y fell (its math, recomputed here only to classify;
        // the CONTROL below requires the recomputation to truncate to exactly what Project returned).
        const float fLon = lo - truth.homeLon, fLat = la - truth.homeLat;
        const float nLon = (fLon + truth.rLon) / (2.0f * truth.rLon), nLat = (fLat + truth.rLat) / (2.0f * truth.rLat);
        float fx = nLon * S, fy = S - (nLat * S);
        if (truth.rotSin != 0.0f || truth.rotCos != 1.0f) {
            const float Cc = S / 2.0f, px = fx - Cc, py = fy - Cc;
            fx = Cc + px * truth.rotCos + py * truth.rotSin;
            fy = Cc - px * truth.rotSin + py * truth.rotCos;
        }
        if ((int)fx != p.first || (int)fy != p.second) ++t->ctlBad;
        const double kRow = (2.0 * truth.rLat / S) / cLat, kCol = (2.0 * truth.rLon / S) / cLon;
        const bool negX = fx < 0.0f, negY = fy < 0.0f;
        if (negX && negY) ++t->bothNeg;
        const double reach = (negX || negY) ? std::sqrt(2.5) : std::sqrt(0.5);   // 1.5811 px truncated, 0.7071 px not
        const long bR = (long)std::floor(reach * kRow + 0.5), bC = (long)std::floor(reach * kCol + 0.5);
        if (negX || negY) { ++t->b2Cells; if (dr > bR || dc > bC) ++t->b2Bad; }
        else              { ++t->b1Cells; if (dr > bR || dc > bC) ++t->b1Bad; }
    }
}

int main(int argc, char** argv)
{
    if (argc != 2) { std::printf("usage: test_wx_layer <repo>/test/fixtures/wx/server\n"); return 2; }
    g_dir = argv[1];
    std::printf("WxLayer.h vs an independent inverse and the planes' projection\n");
    Bytes manB;
    if (!ReadFile("MANIFEST.json", &manB)) { std::printf("FAIL: cannot read %s/MANIFEST.json (rig)\n", g_dir.c_str()); return 2; }
    const std::string man(manB.begin(), manB.end());
    std::vector<Crop> crops;
    for (size_t p = man.find("\"name\": \""); p != std::string::npos; p = man.find("\"name\": \"", p + 1)) {
        Crop k;
        const size_t s = p + 9;
        k.name = man.substr(s, man.find('"', s) - s);
        const size_t rk = man.find("\"rKm\": ", p);
        k.rKm = std::strtod(man.c_str() + rk + 7, nullptr);
        Bytes b;
        if (!ReadFile(k.name, &b)) { std::printf("FAIL: cannot read %s (rig)\n", k.name.c_str()); return 2; }
        k.codes.assign(405 * 405, 0);
        if (wxcrop::DecodeInto(b.data(), b.size(), k.codes.data(), k.codes.size(), &k.h) != wxcrop::Status::Ok) {
            std::printf("FAIL: %s does not decode (rig)\n", k.name.c_str()); return 2;
        }
        crops.push_back(k);
    }
    if (crops.size() != 8) { std::printf("FAIL: %u crops in the manifest, not 8 (rig)\n", (unsigned)crops.size()); return 2; }

    Tally t, sabRot, sabSwap, sabFlip;
    long views = 0;
    for (const Crop& k : crops) {
        const double cLatC = k.h.latNwMicro / 1e6 - (k.h.rows - 1) / 2.0 * (k.h.cellLatMicro / 1e6);
        const double cLonC = k.h.lonNwMicro / 1e6 + (k.h.cols - 1) / 2.0 * (k.h.cellLonMicro / 1e6);
        for (double j : {-0.005, 0.005}) for (double z : kZoom) for (int rot : kRot) for (int S : kSizes) {
            wxlayer::View v;
            v.homeLat = cLatC + j; v.homeLon = cLonC + j; v.size = S;
            Radii(v.homeLat, k.rKm, z, &v.rLat, &v.rLon);
            const float rad = rot * 3.14159265f / 180.0f;
            v.rotCos = rot == 0 ? 1.0f : std::cos(rad);
            v.rotSin = rot == 0 ? 0.0f : std::sin(rad);
            ++views;
            Grade(k, v, v, &t, true);
            // the sabotages, on a subset of views (one per crop/rotation/size at zoom 1, +jitter)
            if (z == 1.0 && j > 0) {
                wxlayer::View noRot = v; noRot.rotCos = 1.0f; noRot.rotSin = 0.0f;
                if (rot != 0) Grade(k, v, noRot, &sabRot, true);
                wxlayer::View swap = v; swap.rLat = v.rLon; swap.rLon = v.rLat;
                Grade(k, v, swap, &sabSwap, true);
                wxlayer::View flip = v; flip.rLat = -v.rLat;           // a y flip
                Grade(k, v, flip, &sabFlip, true);
            }
        }
    }
    int fails = 0;
    auto line = [&](bool ok, const char* id, const std::string& what) { std::printf("  %s  %-4s %s\n", ok ? "ok  " : "FAIL", id, what.c_str()); if (!ok) ++fails; };
    char b[256];
    std::snprintf(b, sizeof b, "%ld views, %ld on-disc pixels: %ld mismatches outside the 1e-4 band (predicted 0), %ld inside it", views, t.px, t.mismatch, t.band);
    line(t.mismatch == 0 && views == 8 * 2 * 3 * 5 * 4, "a", b);
    // The frozen (b) -- 0 round trips more than one cell off -- FAILED (1,740) and stays a miss in the record
    // (docs/wx-device.md); by ruling it is re-registered as (b'1)/(b'2). Its count is printed, not asserted.
    std::printf("  ----  b    RECORD (frozen 0 missed): %ld of %ld round trips more than one cell off\n", t.rtBad, t.rtCells);
    std::snprintf(b, sizeof b, "CONTROL: the float x, y recomputed to classify truncate to Project's own pixel: %ld disagreements (any one voids the split)", t.ctlBad);
    line(t.ctlBad == 0, "b'", b);
    std::snprintf(b, sizeof b, "x, y >= 0: %ld round trips, %ld beyond floor(0.7071 k + 0.5) (predicted 0)", t.b1Cells, t.b1Bad);
    line(t.b1Bad == 0 && t.b1Cells > 0, "b'1", b);
    std::snprintf(b, sizeof b, "x or y in (-1, 0): %ld round trips (predicted >= 1,740), %ld beyond floor(1.5811 k + 0.5) (predicted 0)", t.b2Cells, t.b2Bad);
    line(t.b2Bad == 0 && t.b2Cells >= 1740, "b'2", b);
    std::snprintf(b, sizeof b, "both x and y in (-1, 0): %ld (predicted 0: pixel (0,0) is off the disc)", t.bothNeg);
    line(t.bothNeg == 0, "b'2", b);
    std::snprintf(b, sizeof b, "off-disc disagreements %ld (predicted 0)", t.offBad);
    line(t.offBad == 0, "c", b);
    std::snprintf(b, sizeof b, "outside-the-crop pixels not 14: %ld (predicted 0)", t.nocovBad);
    line(t.nocovBad == 0, "d", b);
    std::snprintf(b, sizeof b, "SABOTAGE rotation ignored: %ld mismatches (a), %ld (b'1) violations (both must be > 0)", sabRot.mismatch, sabRot.b1Bad);
    line(sabRot.mismatch > 0 && sabRot.b1Bad > 0, "sab", b);
    std::snprintf(b, sizeof b, "SABOTAGE rLat/rLon swapped: %ld mismatches (a), %ld (b'1) violations (both must be > 0)", sabSwap.mismatch, sabSwap.b1Bad);
    line(sabSwap.mismatch > 0 && sabSwap.b1Bad > 0, "sab", b);
    std::snprintf(b, sizeof b, "SABOTAGE y flipped: %ld (b'1) violations (must be > 0)", sabFlip.b1Bad);
    line(sabFlip.b1Bad > 0, "sab", b);
    std::printf(fails ? "WX LAYER TESTS FAILED\n" : "all WX layer checks passed\n");
    return fails ? 1 : 0;
}
