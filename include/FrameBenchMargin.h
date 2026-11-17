// Board addendum 2's margin probe, free of Arduino so the host can test it (test/host/test_a2_margin.cpp).
#pragma once
#include <stdint.h>

namespace framebench {

// A2's margin probe (docs/speed-p5p6.md section 7, rule 3(i)): the largest L on the 64-B grid from lo up to hi for which
// ok(L) holds, by bisection; 0 when ok(lo) itself fails. It assumes ok is monotone (if L fits, anything smaller fits),
// which is true of one contiguous allocation. Host-tested (test/host/test_a2_margin.cpp). Called ONLY while a margin
// scene measures.
constexpr uint32_t kMarginHi = 196608;
constexpr int kMarginProbes = 3;
constexpr uint32_t kMarginEveryMs = 30000;
template <typename Ok>
inline uint32_t MarginSearch(uint32_t lo, uint32_t hi, Ok ok)
{
    if (!ok(lo)) return 0;
    uint32_t a = 0, b = (hi - lo) / 64;   // invariant: ok(lo + 64 * a) holds; the answer's index is in [a, b]
    while (a < b) {
        const uint32_t m = a + (b - a + 1) / 2;
        if (ok(lo + 64 * m))
            a = m;
        else
            b = m - 1;
    }
    return lo + 64 * a;
}

}  // namespace framebench
