// WD6 (docs/wx-device.md): the sweep over the weather, looks 3 (see-through) and 4 (lit), include/WxSweep.h.
// Rows are tagged [W1] [W2] [W3-out] [W3-in] [W5a] [W5b] so test/host/sabotage_wx_sweep.py can require exactly the
// sabotaged rows to go red. Exit: 0 ok, 1 a check failed.
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>

#include "WxSweep.h"

using namespace wxsweep;

static int failures = 0;
static void row(bool ok, const char* tag, const char* what, long bad)
{
    std::printf("  %s  [%s] %s -- %ld mismatches\n", ok ? "ok  " : "FAIL", tag, what, bad);
    if (!ok) ++failures;
}

static int R5(uint16_t c) { return c >> 11; }
static int G6(uint16_t c) { return (c >> 5) & 0x3F; }
static int B5(uint16_t c) { return c & 0x1F; }

// W1: look 3 over every colour and level.
static void W1()
{
    long bad = 0, unchangedAt0 = 0;
    for (uint32_t c = 0; c < 65536; ++c)
        for (int lvl = 0; lvl <= kPeak; ++lvl) {
            const uint16_t out = SeeThrough565((uint16_t)c, (uint8_t)lvl);
            int g8 = G6((uint16_t)c) * 4 + lvl; if (g8 > 255) g8 = 255;
            if (R5(out) != R5((uint16_t)c) || B5(out) != B5((uint16_t)c) || G6(out) != (g8 >> 2)) ++bad;
            if (lvl == 0 && out != (uint16_t)c) ++unchangedAt0;
        }
    row(bad == 0 && unchangedAt0 == 0, "W1", "see-through: R and B bits kept, green + level clamped, level 0 changes nothing", bad + unchangedAt0);
}

// W2: look 4 over every code, both layer looks and every level.
static void W2()
{
    long bad = 0;
    const wxs2::Look looks[2] = {wxs2::Look::A, wxs2::Look::B};
    for (const wxs2::Look l : looks) {
        const int pct = wxs2::Percent(l);
        for (int code = 0; code <= 15; ++code)
            for (int lvl = 0; lvl <= kPeak; ++lvl) {
                const uint16_t out = Lit565((uint8_t)code, pct, (uint8_t)lvl);
                if (code >= 1 && code <= 13) {
                    // From the code's own percent (WD7, written here independently: light rain x 55/40, half up).
                    const int base = code == 1 ? (pct * 55 + 20) / 40 : pct;
                    const int p = base + ((100 - base) * lvl + kPeak / 2) / kPeak;
                    if (out != wxs2::To565(wxs2::Pack(wxs2::EchoAt((uint8_t)code, p)))) ++bad;
                    if (lvl == 0 && out != wxs2::To565(wxs2::Pixel888((uint8_t)code, 0, 0, l))) ++bad;   // the layer's own
                    if (lvl == kPeak && out != wxs2::To565(wxs2::Pack(wxs2::EchoAt((uint8_t)code, 100)))) ++bad;
                } else if (out != Green565((uint8_t)lvl)) {
                    ++bad;
                }
            }
    }
    row(bad == 0, "W2", "lit: an echo code raised from its own percent to 100 % at the beam (its own colour at the tail); others the wedge green", bad);
}

// W3: the buffer render, as the device runs it.
static void W3()
{
    const int W = 240, H = 240, C = 119, RAD = 120;
    std::vector<uint8_t> codes(W * H), slice(W * H, 0), marks(W * H);
    std::vector<int16_t> lo(H), hi(H);
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) codes[y * W + x] = (uint8_t)(((x / 6) + 3 * (y / 6)) % 16);
    long outBad = 0, inBad = 0, cells = 0, pixels = 0, runs = 0;
    const wxs2::Look looks[2] = {wxs2::Look::A, wxs2::Look::B};
    for (const wxs2::Look l : looks) {
        const int pct = wxs2::Percent(l);
        std::vector<uint16_t> layer(W * H);
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x) layer[y * W + x] = wxs2::To565(wxs2::Pixel888(codes[y * W + x], x, y, l));
        for (int k = 0; k < 16; ++k) {
            const float angle = k * 0.3926991f;   // 16 beam angles round the disc
            for (const Look sweep : {Look::SeeThrough, Look::Lit}) {
                std::vector<uint16_t> frame = layer;
                int y0 = 0, y1 = 0;
                const uint32_t n = Rasterise(C, C, RAD, angle, slice.data(), W, H, &y0, &y1, lo.data(), hi.data());
                marks = slice;
                const uint32_t blended = Blend(sweep, pct, slice.data(), W, y0, y1, lo.data(), hi.data(),
                    [&](int x, int y) { return frame[y * W + x]; },
                    [&](int x, int y, uint16_t v) { frame[y * W + x] = v; },
                    [&](int x, int y) { return codes[y * W + x]; });
                pixels += n; ++runs;
                if (blended != n) ++inBad;                                       // once per pixel, every pixel
                for (int i = 0; i < W * H; ++i) {
                    if (slice[i] != 0) ++inBad;                                  // the marks are cleared
                    if (marks[i] == 0) { if (frame[i] != layer[i]) ++outBad; continue; }
                    ++cells;
                    const uint8_t lvl = SliceLevel(marks[i]);
                    const uint16_t want = sweep == Look::SeeThrough ? SeeThrough565(layer[i], lvl) : Lit565(codes[i], pct, lvl);
                    if (frame[i] != want) ++inBad;
                    if (sweep == Look::SeeThrough && (R5(frame[i]) != R5(layer[i]) || B5(frame[i]) != B5(layer[i]))) ++inBad;
                }
            }
        }
    }
    std::printf("  info [W3] the wedge: %ld pixels on average (%ld runs)\n", runs ? pixels / runs : 0, runs);
    row(outBad == 0, "W3-out", "outside the wedge every pixel is exactly the layer's colour", outBad);
    row(inBad == 0 && cells > 0, "W3-in", "inside: look 3 keeps R and B and adds green; look 4 is Lit565; once per pixel", inBad);
}

// W5 (the B1 fix, docs/wx-device.md): the span-only Blend against the whole-row BlendRows, at 3,600 angles, both sweep
// looks, over a frame of varied pixels and codes. W5a: the same frame, count and cleared slice; W5b: each row's span
// holds only marked pixels (span bytes = marked), averaging the 7,279 sweep_geom measured.
static void W5()
{
    const int W = 240, H = 240, C = 119, RAD = 120, STEPS = 3600;
    std::vector<uint8_t> codes(W * H), sa(W * H, 0), sb(W * H, 0);
    std::vector<int16_t> lo(H), hi(H);
    std::vector<uint16_t> base(W * H);
    for (int i = 0; i < W * H; ++i) {
        codes[i] = (uint8_t)(((i % W) * 7 + (i / W) * 13) % 16);
        base[i] = (uint16_t)(((uint32_t)i * 2654435761u) >> 16);
    }
    const int pct = wxs2::Percent(wxs2::Look::A);
    long bad = 0, spanBad = 0;
    double spanSum = 0, markSum = 0;
    for (const Look sweep : {Look::SeeThrough, Look::Lit})
        for (int k = 0; k < STEPS; ++k) {
            const float angle = (float)(k * 6.283185307179586 / STEPS);
            int y0 = 0, y1 = -1;
            const uint32_t n = Rasterise(C, C, RAD, angle, sa.data(), W, H, &y0, &y1, lo.data(), hi.data());
            sb = sa;
            long span = 0;
            for (int y = y0; y <= y1; ++y) if (hi[y] >= lo[y]) span += hi[y] - lo[y] + 1;
            if (span != (long)n) ++spanBad;
            if (sweep == Look::Lit) { spanSum += span; markSum += n; }
            std::vector<uint16_t> fa = base, fb = base;
            const uint32_t na = Blend(sweep, pct, sa.data(), W, y0, y1, lo.data(), hi.data(),
                [&](int x, int y) { return fa[y * W + x]; }, [&](int x, int y, uint16_t v) { fa[y * W + x] = v; },
                [&](int x, int y) { return codes[y * W + x]; });
            const uint32_t nb = BlendRows(sweep, pct, sb.data(), W, y0, y1,
                [&](int x, int y) { return fb[y * W + x]; }, [&](int x, int y, uint16_t v) { fb[y * W + x] = v; },
                [&](int x, int y) { return codes[y * W + x]; });
            if (na != nb || fa != fb || sa != sb) ++bad;
            std::fill(sa.begin(), sa.end(), (uint8_t)0);   // so one angle's fault cannot leak into the next
        }
    const double avg = spanSum / STEPS, avgMarked = markSum / STEPS;
    std::printf("  info [W5] over %d angles: span bytes %.1f, marked %.1f on average\n", STEPS, avg, avgMarked);
    row(bad == 0, "W5a", "span-only Blend: the same frame, count and cleared slice as BlendRows at every angle, both looks", bad);
    row(spanBad == 0 && (long)(avg + 0.5) == 7279, "W5b", "each row's span holds only marked pixels (span bytes = marked; average 7,279)", spanBad);
}

int main()
{
    std::printf("WxSweep.h: the sweep over the weather (looks 3 and 4)\n");
    W1();
    W2();
    W3();
    W5();
    std::printf(failures ? "WX SWEEP TESTS FAILED\n" : "all WX sweep checks passed\n");
    return failures ? 1 : 0;
}
