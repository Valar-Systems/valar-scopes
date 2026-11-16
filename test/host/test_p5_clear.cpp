// P5 1c's host rows (docs/speed-p5p6.md section 1c and amendment 4m, on weatherscope/speed-p5p6): the dirty-region
// restore (include/DirtyClear.h) against a full clear, on two alternating buffers, with LovyanGFX's raster as ported in
// test/host/LgfxPort.h.
//
//   test_p5_clear      exit 0 every row ok, 1 a row failed
//
// Rows (each prints "ok" or "FAIL" and its id; test/host/sabotage_p5_clear.py reads the ids):
//   CMP control   the comparison sees one changed pixel
//   MARK-COVER    every pixel the raster writes for a primitive (on screen, partly off, wholly off) lies inside its mark
//   SEQ2          600 frames on two alternating buffers, primitives moving, the layer rebuilt now and then: after every
//                 Restore the buffer equals the layer, pixel for pixel; and after the frame, the full-clear twin
//   IND-STOP      frames stopped before the indicator: its band shows the layer, never the last frame's dots
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "DirtyClear.h"
#include "LgfxPort.h"

namespace {

int failures = 0;
void Row(bool ok, const std::string& id, const std::string& what)
{
    if (!ok) ++failures;
    std::printf("  %s  %s  %s\n", ok ? "ok  " : "FAIL", id.c_str(), what.c_str());
}

constexpr int W = 240, H = 240;
const dirtyclear::Band kBand = {96, 219, 143, 229};   // the indicator's dots and their margin

struct Img {
    std::vector<uint16_t> p;
    Img() : p((size_t)W * H, 0) {}
    void Set(int32_t x, int32_t y, uint32_t c) { p[(size_t)y * W + (size_t)x] = (uint16_t)c; }
};
struct Bits {   // which pixels a primitive wrote
    std::vector<uint8_t> on;
    Bits() : on((size_t)W * H, 0) {}
    void Set(int32_t x, int32_t y, uint32_t) { on[(size_t)y * W + (size_t)x] = 1; }
};

// A canvas that marks every primitive before drawing it, as BandCanvas will on the device.
struct Tracked {
    lgfxport::Port<Img>& port;
    dirtyclear::Set* s;   // nullptr: the full-clear twin, which marks nothing
    void Tri(int a, int b, int c, int d, int e, int f, uint32_t k) { if (s) dirtyclear::MarkTriangle(*s, a, b, c, d, e, f); port.fillTriangle(a, b, c, d, e, f, k); }
    void Line(int a, int b, int c, int d, uint32_t k) { if (s) dirtyclear::MarkLine(*s, a, b, c, d); port.drawLine(a, b, c, d, k); }
    void Circle(int x, int y, int r, uint32_t k) { if (s) dirtyclear::MarkCircle(*s, x, y, r); port.drawCircle(x, y, r, k); }
    void Disc(int x, int y, int r, uint32_t k) { if (s) dirtyclear::MarkCircle(*s, x, y, r); port.fillCircle(x, y, r, k); }
    void Rect(int x, int y, int w, int h, uint32_t k)
    {
        if (s) dirtyclear::MarkRect(*s, x, y, w, h);
        int32_t xx = x, ww = w, yy = y, hh = h;
        lgfxport::Port<Img>::_adjust_abs(xx, ww);
        lgfxport::Port<Img>::_adjust_abs(yy, hh);
        port.color = k;
        port.writeFillRect(xx, yy, ww, hh);
    }
};

struct Lcg {
    uint32_t s;
    uint32_t Next() { s = s * 1664525u + 1013904223u; return s >> 8; }
    int Int(int lo, int hi) { return lo + (int)(Next() % (uint32_t)(hi - lo + 1)); }
};

// One frame's primitives, a function of (frame, i), so both twins draw the same; avoidBand keeps them off the band.
void DrawFrame(Tracked& t, int f, int count, bool avoidBand)
{
    for (int i = 0; i < count; ++i) {
        Lcg g{(uint32_t)(i * 7919 + 17)};
        const int x = (g.Int(-20, 259) + f * (1 + i % 3)) % 280 - 20;   // moving, now and then partly off screen
        int y = (g.Int(-20, 259) + f * (i % 2 ? 1 : 2)) % 280 - 20;
        if (avoidBand && y > 190) y = y % 190;
        const uint32_t k = 3000u + (uint32_t)(i * 31 + f) % 60000u;
        switch (i % 6) {
            case 0: t.Tri(x, y, x + g.Int(-12, 12), y + g.Int(-12, 12), x + g.Int(-12, 12), y + g.Int(-12, 12), k); break;
            case 1: t.Line(x, y, x + g.Int(-30, 30), y + g.Int(-20, 20), k); break;
            case 2: t.Circle(x, y, g.Int(0, 9), k); break;
            case 3: t.Disc(x, y, g.Int(0, 6), k); break;
            case 4: t.Rect(x, y, g.Int(-14, 14), g.Int(-9, 9), k); break;
            default: t.Rect(x, y, g.Int(1, 40), 1, k); break;   // a fast hline
        }
    }
}

// The indicator, drawn WITHOUT a mark (the worst case: DrawScreenIndicator's own writes), at fixed dot positions.
void Indicator(Img& img, int screen)
{
    for (int d = 0; d < 5; ++d)
        for (int yy = 222; yy <= 226; ++yy)
            for (int xx = 100 + 9 * d; xx <= 104 + 9 * d; ++xx) img.Set(xx, yy, d == screen % 5 ? 0xFFFFu : 0x8410u);
}

void Layer(Img& layer, uint32_t gen)
{
    for (size_t i = 0; i < layer.p.size(); ++i) layer.p[i] = (uint16_t)(1 + ((i * 2654435761u + gen * 40503u) >> 16) % 65000u);
}

long DiffPx(const Img& a, const Img& b)
{
    long n = 0;
    for (size_t i = 0; i < a.p.size(); ++i) n += a.p[i] != b.p[i];
    return n;
}

void MarkCover()
{
    long prims = 0, px = 0, outside = 0;
    for (int trial = 0; trial < 4000; ++trial) {
        Bits bits;
        lgfxport::Port<Bits> port(bits, 0, 0, W - 1, H - 1);
        dirtyclear::Set s;
        s.Init(W, H);
        Lcg g{(uint32_t)(trial * 2654435761u + 1)};
        const int x = g.Int(-30, 269), y = g.Int(-30, 269);
        switch (trial % 6) {
            case 0: { const int a = x + g.Int(-25, 25), b = y + g.Int(-25, 25), c = x + g.Int(-25, 25), d = y + g.Int(-25, 25);
                      dirtyclear::MarkTriangle(s, x, y, a, b, c, d); port.fillTriangle(x, y, a, b, c, d, 1u); break; }
            case 1: { const int a = x + g.Int(-60, 60), b = y + g.Int(-60, 60);
                      dirtyclear::MarkLine(s, x, y, a, b); port.drawLine(x, y, a, b, 1u); break; }
            case 2: { const int r = g.Int(-2, 20); dirtyclear::MarkCircle(s, x, y, r); port.drawCircle(x, y, r, 1u); break; }
            case 3: { const int r = g.Int(-2, 20); dirtyclear::MarkCircle(s, x, y, r); port.fillCircle(x, y, r, 1u); break; }
            case 4: { const int w = g.Int(-30, 30), h = g.Int(-30, 30); dirtyclear::MarkRect(s, x, y, w, h);
                      int32_t xx = x, ww = w, yy = y, hh = h; lgfxport::Port<Bits>::_adjust_abs(xx, ww);
                      lgfxport::Port<Bits>::_adjust_abs(yy, hh); port.color = 1u; port.writeFillRect(xx, yy, ww, hh); break; }
            default: { const int w = g.Int(-40, 40); dirtyclear::MarkHLine(s, x, y, w); port.drawFastHLine(x, y, w, 1u); break; }
        }
        ++prims;
        for (int yy = 0; yy < H; ++yy)
            for (int xx = 0; xx < W; ++xx) {
                if (!bits.on[(size_t)yy * W + (size_t)xx]) continue;
                ++px;
                if (xx < s.lo[yy] || xx > s.hi[yy]) ++outside;
            }
    }
    Row(outside == 0 && px > 0, "MARK-COVER",
        std::to_string(prims) + " primitives, " + std::to_string(px) + " pixels written, " + std::to_string(outside) +
            " outside their mark (want 0)");
}

void Seq2()
{
    Img layer, oldBuf[2], newBuf[2];
    dirtyclear::Set sets[2];
    sets[0].Init(W, H);
    sets[1].Init(W, H);
    uint32_t gen = 1;
    Layer(layer, gen);
    long restoreBad = 0, frameBad = 0, fulls = 0, partials = 0;
    unsigned long long partialPx = 0;
    for (int f = 0; f < 600; ++f) {
        if (f > 0 && f % 97 == 0) Layer(layer, ++gen);   // the layer rebuilt
        const int b = f % 2;
        const uint32_t copied = dirtyclear::Restore(newBuf[b].p.data(), layer.p.data(), sets[b], gen, kBand);
        if (copied == (uint32_t)(W * H)) ++fulls; else { ++partials; partialPx += copied; }
        restoreBad += DiffPx(newBuf[b], layer);
        oldBuf[b].p = layer.p;   // the full clear
        lgfxport::Port<Img> pn(newBuf[b], 0, 0, W - 1, H - 1), po(oldBuf[b], 0, 0, W - 1, H - 1);
        Tracked tn{pn, &sets[b]}, to{po, nullptr};   // SEQ2's twins: this buffer's own set
        DrawFrame(tn, f, 40, false);
        DrawFrame(to, f, 40, false);
        Indicator(newBuf[b], f / 10);
        Indicator(oldBuf[b], f / 10);
        frameBad += DiffPx(newBuf[b], oldBuf[b]);
    }
    const bool control = fulls > 0 && partials > 0;
    Row(restoreBad == 0 && frameBad == 0 && control, "SEQ2",
        std::to_string(restoreBad) + " px off the layer after Restore, " + std::to_string(frameBad) +
            " off the full-clear twin after the frame (want 0, 0); full restores " + std::to_string(fulls) +
            ", partial " + std::to_string(partials) + " averaging " +
            std::to_string(partials ? (long)(partialPx / (unsigned long long)partials) : 0L) + " px (control: both above 0: " +
            (control ? "yes" : "NO") + ")");
}

void IndStop()
{
    Img layer, oldBuf[2], newBuf[2];
    dirtyclear::Set sets[2];
    sets[0].Init(W, H);
    sets[1].Init(W, H);
    Layer(layer, 7);
    long bandBad = 0, checked = 0;
    for (int f = 0; f < 200; ++f) {
        const int b = f % 2;
        const bool stopped = f >= 4 && f % 5 == 0;   // now and then a frame ends before the indicator
        dirtyclear::Restore(newBuf[b].p.data(), layer.p.data(), sets[b], 7, kBand);
        oldBuf[b].p = layer.p;
        lgfxport::Port<Img> pn(newBuf[b], 0, 0, W - 1, H - 1), po(oldBuf[b], 0, 0, W - 1, H - 1);
        Tracked tn{pn, &sets[b]}, to{po, nullptr};
        DrawFrame(tn, f, 40, true);
        DrawFrame(to, f, 40, true);
        if (!stopped) {
            Indicator(newBuf[b], f / 7);
            Indicator(oldBuf[b], f / 7);
            continue;
        }
        ++checked;
        for (int y = kBand.y0; y <= kBand.y1; ++y)
            for (int x = kBand.x0; x <= kBand.x1; ++x)
                bandBad += newBuf[b].p[(size_t)y * W + (size_t)x] != oldBuf[b].p[(size_t)y * W + (size_t)x];
    }
    Row(bandBad == 0 && checked > 0, "IND-STOP",
        std::to_string(checked) + " frames stopped before the indicator; " + std::to_string(bandBad) +
            " band pixels differ from the full clear's (want 0)");
}

}  // namespace

int main()
{
    {
        Img a, b;
        b.p[17] = 9;
        Row(DiffPx(a, b) == 1, "CMP control", "one changed pixel reads " + std::to_string(DiffPx(a, b)) + " (want 1)");
    }
    MarkCover();
    Seq2();
    IndStop();
    std::printf("%s\n", failures ? "P5 CLEAR TESTS FAILED" : "all P5 clear checks passed");
    return failures ? 1 : 0;
}
