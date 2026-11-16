// P5 1b's host rows (docs/speed-p5p6.md section 1b and amendment 4k, on weatherscope/speed-p5p6): the table blend
// (include/WxSweepLut.h) against wxsweep::Blend, W5a's comparison carried over: the same frame, count and cleared
// slice, at all 3,600 beam angles, over a frame of varied pixels and codes.
//
//   test_p5_blend      exit 0 every row ok, 1 a row failed
//
// Rows (each prints "ok" or "FAIL" and its id; test/host/sabotage_p5_blend.py reads the ids):
//   CMP control   the comparison sees one changed pixel
//   W5c@A, W5c@B  each look: the table blend against Blend at every angle, codes 0-39 and the layer's kOffDisc (255),
//                 so both the table and its direct path run (the control: both above 0 pixels)
//   W5c-PCT       one table across frames whose look alternates A, B, A, ...: a table built for the wrong percent shows
#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "WxSweepLut.h"

namespace {

int failures = 0;
void Row(bool ok, const std::string& id, const std::string& what)
{
    if (!ok) ++failures;
    std::printf("  %s  %s  %s\n", ok ? "ok  " : "FAIL", id.c_str(), what.c_str());
}

constexpr int W = 240, H = 240, C = 119, RAD = 120, STEPS = 3600;

struct Scene {
    std::vector<uint8_t> codes;
    std::vector<uint16_t> base;
    Scene() : codes(W * H), base(W * H)
    {
        for (int i = 0; i < W * H; ++i) {
            const int x = i % W, y = i / W;
            codes[(size_t)i] = (x + y) % 97 == 0 ? (uint8_t)255 : (uint8_t)((x * 7 + y * 13) % 40);   // 255: kOffDisc
            base[(size_t)i] = (uint16_t)(((uint32_t)i * 2654435761u) >> 16);
        }
    }
};

struct Tally {
    long angles = 0, bad = 0, tablePx = 0, directPx = 0;
};

// One angle: Blend and the table blend from the same marks; true when frame, count and cleared slice all agree.
bool OneAngle(const Scene& s, const wxsweeplut::Table& t, int pct, float angle, Tally& tally)
{
    static std::vector<uint8_t> sa(W * H, 0), sb(W * H, 0);
    static std::vector<int16_t> lo(H), hi(H);
    int y0 = 0, y1 = -1;
    std::fill(sa.begin(), sa.end(), (uint8_t)0);
    wxsweep::Rasterise(C, C, RAD, angle, sa.data(), W, H, &y0, &y1, lo.data(), hi.data());
    sb = sa;
    for (int y = y0; y <= y1; ++y)
        for (int x = lo[(size_t)y]; x <= hi[(size_t)y]; ++x)
            if (sa[(size_t)y * W + x]) ++(s.codes[(size_t)y * W + x] < wxsweeplut::kCodes ? tally.tablePx : tally.directPx);
    std::vector<uint16_t> fa = s.base, fb = s.base;
    const uint32_t na = wxsweep::Blend(pct, sa.data(), W, y0, y1, lo.data(), hi.data(),
                                       [&](int x, int y, uint16_t v) { fa[(size_t)y * W + x] = v; },
                                       [&](int x, int y) { return s.codes[(size_t)y * W + x]; });
    const uint32_t nb = wxsweeplut::Blend(t, sb.data(), W, y0, y1, lo.data(), hi.data(),
                                          [&](int x, int y, uint16_t v) { fb[(size_t)y * W + x] = v; },
                                          [&](int x, int y) { return s.codes[(size_t)y * W + x]; });
    ++tally.angles;
    const bool same = na == nb && fa == fb && sa == sb;
    if (!same) ++tally.bad;
    return same;
}

void Look(const Scene& s, wxs2::Look l, const char* id)
{
    const int pct = wxs2::Percent(l);
    wxsweeplut::Table t;
    wxsweeplut::For(t, pct);
    Tally tally;
    for (int k = 0; k < STEPS; ++k) OneAngle(s, t, pct, (float)(k * 6.283185307179586 / STEPS), tally);
    const bool control = tally.tablePx > 0 && tally.directPx > 0;
    Row(tally.bad == 0 && control, id,
        std::to_string(tally.bad) + " of " + std::to_string(tally.angles) + " angles differ (want 0); pixels from the " +
            "table " + std::to_string(tally.tablePx) + ", direct " + std::to_string(tally.directPx) +
            " (control: both above 0: " + (control ? "yes" : "NO") + ")");
}

void Alternating(const Scene& s)
{
    const int pa = wxs2::Percent(wxs2::Look::A), pb = wxs2::Percent(wxs2::Look::B);
    wxsweeplut::Table t;   // ONE table, as the device keeps
    Tally tally;
    for (int k = 0; k < 360; ++k) {
        const int pct = (k / 3) % 2 ? pb : pa;   // the look changes every third frame
        OneAngle(s, wxsweeplut::For(t, pct), pct, (float)(k * 6.283185307179586 / 360), tally);
    }
    Row(tally.bad == 0 && pa != pb, "W5c-PCT",
        std::to_string(tally.bad) + " of " + std::to_string(tally.angles) + " frames differ (want 0), one table, the " +
            "look alternating " + std::to_string(pa) + "% / " + std::to_string(pb) + "%");
}

}  // namespace

int main()
{
    {
        std::vector<uint16_t> a(16, 7), b(16, 7);
        b[5] = 8;
        Row(a != b, "CMP control", std::string("one changed pixel ") + (a != b ? "reads a difference" : "reads the same"));
    }
    const Scene s;
    Look(s, wxs2::Look::A, "W5c@A");
    Look(s, wxs2::Look::B, "W5c@B");
    Alternating(s);
    std::printf("%s\n", failures ? "P5 BLEND TESTS FAILED" : "all P5 blend checks passed");
    return failures ? 1 : 0;
}
