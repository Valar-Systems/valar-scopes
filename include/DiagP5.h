// P5-DIAG (weatherscope; REG-diag-images.md f020896cbfe1, accepted 2026-11-20): BENCH ONLY, never toward firmware.
// Compiled only in the diag env (FRAME_PROFILE_GAINS == 7). Two arms on top of G5' (bench/gains-p5b d5623de606bb):
//   1a-SUB      a TIMED COPY of outlinemask::DrawEdge (OutlineMask.h :204-233; the shipping template is not edited):
//               cycle stamps at the code's own cut points -- guard (Fits + the 8 Same checks), begin (the scratch
//               fill), raster, readback, dilate+runs (interleaved per row in the code, so timed TOGETHER, the
//               registration's named change) -- and the refusals counted by cause. Its total is NOT NEW's: the copy
//               is extra code and pays for its own stamps. It splits A2 into parts and gives A1's refusal rate.
//   1a-DISCARD  the SHIPPING DrawEdge run in full against a canvas that keeps nothing (each run's length goes into a
//               volatile sink, so the compiler cannot delete the work), then today's 8 copies: NEW's code, OLD's
//               pixels. Its per-contact footprint is the UNION of both paths.
// Every number is printed ONCE per scene at its measure end, on its own [diag-p5] line; ProfRec is untouched.
#pragma once

#if defined(FRAME_PROFILE) && defined(FRAME_PROFILE_GAINS) && FRAME_PROFILE_GAINS == 7
#define DIAG_P5_ON 1

#include <Arduino.h>
#include <esp_cpu.h>
#include "OutlineMask.h"

namespace diagp5 {

struct Totals {
    uint32_t contacts = 0;                        // DrawEdgeTimed calls while the scene MEASURES
    uint32_t refFits = 0, refSame = 0, refBegin = 0;   // its refusals, by the first cause that fired
    uint64_t guard = 0, begin = 0, raster = 0, readback = 0, runs = 0;   // cycles
};
inline Totals g_totals;
inline volatile uint32_t g_sink = 0;   // DISCARD's runs and primitives land here, so nothing is optimised away

inline void Reset() { g_totals = Totals(); }

// DrawEdge (OutlineMask.h :204-233), line for line, with the stamps. `count` is false outside a measure window.
template <class C, class S, class BuildAt, class Color>
inline bool DrawEdgeTimed(C& c, S& scratch, const outlinemask::Marker& m0, BuildAt buildAt, int x, int y,
                          const Color& black, bool count)
{
    using namespace outlinemask;
    const uint32_t t0 = esp_cpu_get_cycle_count();
    const bool fits = Fits(m0, x, y);
    bool exact = fits;
    for (int k = 0; exact && k < 8; ++k)
        exact = Same(buildAt(x + kOff[k][0], y + kOff[k][1]), m0, kOff[k][0], kOff[k][1]);
    const bool same = exact;
    const uint32_t t1 = esp_cpu_get_cycle_count();
    if (exact) exact = scratch.Begin();
    const uint32_t t2 = esp_cpu_get_cycle_count();
    if (!exact) {
        for (int k = 0; k < 8; ++k) Draw(c, buildAt(x + kOff[k][0], y + kOff[k][1]), black);
        if (count) {
            ++g_totals.contacts;
            if (!fits) ++g_totals.refFits;
            else if (!same) ++g_totals.refSame;
            else ++g_totals.refBegin;
            g_totals.guard += t1 - t0;
            g_totals.begin += t2 - t1;
        }
        return false;
    }
    const int bx = x - kHalf, by = y - kHalf;
    Draw(scratch, Shift(m0, -bx, -by), scratch.On());
    const uint32_t t3 = esp_cpu_get_cycle_count();
    uint32_t m[kSide + 2];
    m[0] = m[kSide + 1] = 0;
    for (int r = 0; r < kSide; ++r) m[r + 1] = scratch.Row(r);
    const uint32_t t4 = esp_cpu_get_cycle_count();
    for (int r = 0; r < kSide; ++r) {
        const uint32_t v = m[r] | m[r + 1] | m[r + 2];
        const uint32_t d = v | (v << 1) | (v >> 1);
        uint32_t e = d & ~m[r + 1];
        while (e) {
            const int j = __builtin_ctz(e);
            const uint32_t t = ~(e >> j);
            const int len = t ? __builtin_ctz(t) : kSide - j;
            c.drawFastHLine(bx + j, by + r, len, black);
            e &= ~((len >= kSide ? 0xFFFFFFFFu : ((1u << len) - 1u)) << j);
        }
    }
    const uint32_t t5 = esp_cpu_get_cycle_count();
    if (count) {
        ++g_totals.contacts;
        g_totals.guard += t1 - t0;
        g_totals.begin += t2 - t1;
        g_totals.raster += t3 - t2;
        g_totals.readback += t4 - t3;
        g_totals.runs += t5 - t4;
    }
    return true;
}

// The canvas 1a-DISCARD hands the shipping DrawEdge: LovyanGFX's signatures, nothing drawn, every call summed into
// g_sink (a volatile), so the run computation and the fallback's primitives all still execute.
struct DiscardCanvas {
    template <class T> void drawFastHLine(int32_t x, int32_t y, int32_t w, const T&) { g_sink = g_sink + (uint32_t)(x + y + w); }
    template <class T> void fillTriangle(int32_t x0, int32_t y0, int32_t, int32_t, int32_t, int32_t, const T&) { g_sink = g_sink + (uint32_t)(x0 + y0); }
    template <class T> void drawLine(int32_t x0, int32_t y0, int32_t, int32_t, const T&) { g_sink = g_sink + (uint32_t)(x0 + y0); }
    template <class T> void fillCircle(int32_t x, int32_t y, int32_t r, const T&) { g_sink = g_sink + (uint32_t)(x + y + r); }
    template <class T> void drawCircle(int32_t x, int32_t y, int32_t r, const T&) { g_sink = g_sink + (uint32_t)(x + y + r); }
};

// One line per scene, at its measure end. Microseconds from cycles at the CPU clock; sums over a 120 s window fit 32 bits.
inline void Print(int scene)
{
    const uint32_t mhz = getCpuFrequencyMhz();
    auto us = [mhz](uint64_t cyc) { return (unsigned long)(mhz ? cyc / mhz : 0); };
    const Totals& t = g_totals;
    Serial.printf("[diag-p5] sub S%d contacts=%lu guard=%lu begin=%lu raster=%lu readback=%lu dilate_runs=%lu "
                  "refuse_fits=%lu refuse_same=%lu refuse_begin=%lu mhz=%lu\n",
                  scene, (unsigned long)t.contacts, us(t.guard), us(t.begin), us(t.raster), us(t.readback), us(t.runs),
                  (unsigned long)t.refFits, (unsigned long)t.refSame, (unsigned long)t.refBegin, (unsigned long)mhz);
}

}  // namespace diagp5

#endif  // FRAME_PROFILE_GAINS == 7
