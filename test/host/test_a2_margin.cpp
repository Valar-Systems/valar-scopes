// Board addendum 2's margin probe (docs/speed-p5p6.md section 7): framebench::MarginSearch, on the host.
// The probe asks "the largest L on the 64-B grid in [lo, hi] for which ok(L) holds". Each row fixes the largest size
// that fits (X), so ok(L) = L <= X, and checks the answer and that the search stays inside its bisection bound.
#include <cstdio>
#include <cstdlib>
#include "FrameBenchMargin.h"

static int g_checks = 0, g_fail = 0;
static void check(const char* name, bool ok)
{
    ++g_checks;
    if (!ok) ++g_fail;
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", name);
}

int main()
{
    const uint32_t lo = 16717, hi = framebench::kMarginHi;
    const uint32_t steps = (hi - lo) / 64;   // the grid's last index
    int calls = 0;
    auto run = [&](uint32_t X) {
        calls = 0;
        return framebench::MarginSearch(lo, hi, [&](uint32_t L) { ++calls; return L <= X; });
    };
    // the bisection makes at most 1 + ceil(log2(steps + 1)) calls
    int bound = 1;
    for (uint32_t n = steps + 1; n > 1; n = (n + 1) / 2) ++bound;

    check("M-NONE: nothing fits (X < lo) reads 0", run(lo - 1) == 0);
    check("M-LO: only lo fits (X = lo) reads lo", run(lo) == lo);
    check("M-BELOW: X one byte under a grid point reads the point below", run(lo + 64 * 100 - 1) == lo + 64 * 99);
    check("M-ON: X on a grid point reads that point", run(lo + 64 * 100) == lo + 64 * 100);
    check("M-ALL: everything fits reads the grid's last point", run(0xFFFFFFFFu) == lo + 64 * steps);
    run(lo + 64 * 1234 + 17);
    check("M-CALLS: one search stays inside the bisection bound", calls <= bound);
    bool every = true;   // a sweep over the whole grid, each answer exact
    for (uint32_t k = 0; k <= steps; k += 7) every = every && run(lo + 64 * k + 63) == lo + 64 * k;
    check("M-SWEEP: every 7th grid point, X = point + 63, reads that point", every);
    std::printf("%d checks, %d failures\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
