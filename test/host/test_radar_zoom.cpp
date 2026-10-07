// Radar swipe-to-zoom, the pure half: include/RadarZoom.h.
//
// The ladder is strictly increasing and unique for EVERY configured radius swept
// (0.5 .. 500 in 0.01 steps, plus the conversion-noise neighbours of each candidate),
// its top is the configured radius, and a step never wraps.
//
// SABOTAGE (b): admit the configured radius as a candidate too (`c <= configured`)
// and "strictly increasing, unique" goes red -- 50 configured gives 5,10,25,50,50.
#include <cstdio>
#include <cmath>
#include <initializer_list>
#include "../../include/RadarZoom.h"

static int failures = 0;
static void check(bool ok, const char* what)
{
    std::printf("  %s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++failures;
}

static bool Is(const radarzoom::Ladder& l, std::initializer_list<float> want)
{
    if ((int)want.size() != l.n) return false;
    int i = 0;
    for (float w : want) if (std::fabs(l.step[i++] - w) > 1e-4f) return false;
    return true;
}

static bool StrictlyIncreasingUnique(const radarzoom::Ladder& l)
{
    for (int i = 1; i < l.n; ++i)
        if (!(l.step[i] > l.step[i - 1] + radarzoom::SAME_STEP_EPS * 0.5f)) return false;
    return l.n >= 1;
}

int main()
{
    using namespace radarzoom;
    std::printf("radar zoom (RadarZoom.h)\n");

    check(Is(BuildLadder(100.0f), { 5, 10, 25, 50, 100 }), "100 -> 5,10,25,50,100");
    check(Is(BuildLadder(50.0f), { 5, 10, 25, 50 }), "50 -> 5,10,25,50 (50 once)");
    check(Is(BuildLadder(50.00002f), { 5, 10, 25, 50.00002f }), "50.00002 (conversion noise) -> one top, not 50 twice");
    check(Is(BuildLadder(49.99997f), { 5, 10, 25, 49.99997f }), "49.99997 -> one top");
    check(Is(BuildLadder(30.0f), { 5, 10, 25, 30 }), "30 -> 5,10,25,30");
    check(Is(BuildLadder(250.0f), { 5, 10, 25, 50, 250 }), "250 -> 5,10,25,50,250");
    check(Is(BuildLadder(5.0f), { 5 }), "5 -> 5 (nothing to zoom)");
    check(Is(BuildLadder(3.0f), { 3 }), "3 -> 3 (nothing to zoom)");

    {
        int bad = 0, cases = 0;
        for (int i = 50; i <= 50000; ++i) {                 // 0.5 .. 500 in 0.01 steps
            const float cfg = i / 100.0f;
            const Ladder l = BuildLadder(cfg);
            ++cases;
            if (!StrictlyIncreasingUnique(l) || std::fabs(l.step[l.Top()] - cfg) > 1e-6f) ++bad;
        }
        for (float c : CANDIDATES)                           // the noise right at each candidate
            for (float d : { -0.4f, -1e-4f, 0.0f, 1e-4f, 0.4f }) {
                const Ladder l = BuildLadder(c + d);
                ++cases;
                if (!StrictlyIncreasingUnique(l)) ++bad;
            }
        std::printf("  sweep: %d configured radii, %d ladders not strictly increasing+unique or top != configured\n", cases, bad);
        check(bad == 0, "strictly increasing, unique, top = configured, for every radius swept");
    }

    {
        const Ladder l = BuildLadder(100.0f);
        Edge e;
        int idx = l.Top();
        int steps = 0;
        while (true) { const int n = Step(idx, -1, l, e); ++steps; if (e != Edge::None) break; idx = n; }
        check(idx == 0 && e == Edge::Max && steps == 5, "zooming in from the default: 4 steps, then MAX at 5");
        check(Step(0, -1, l, e) == 0 && e == Edge::Max, "at max, another zoom-in stays at 5 (never wraps)");
        idx = 0;
        while (true) { const int n = Step(idx, +1, l, e); if (e != Edge::None) break; idx = n; }
        check(idx == l.Top() && e == Edge::Min, "zooming out ends at the configured radius with MIN");
        check(Step(l.Top(), +1, l, e) == l.Top() && e == Edge::Min, "at min, another zoom-out stays (never wraps to 5)");
        const Ladder one = BuildLadder(4.0f);
        check(Step(0, -1, one, e) == 0 && e == Edge::Max, "a one-step ladder: zoom-in says max");
        check(Step(0, +1, one, e) == 0 && e == Edge::Min, "a one-step ladder: zoom-out says min");
    }

    check(IdleExpired(30000, 0, 30000), "idle: exactly the timeout expires");
    check(!IdleExpired(29999, 0, 30000), "idle: a millisecond short does not");
    check(IdleExpired(0x00000FFFUL + 30000UL, 0xFFFFF000UL, 30000) , "idle: across the millis() wrap");
    check(!IdleExpired(999, 1000, 30000), "idle: a touch stamped a tick AFTER now is not 49 days old");

    std::printf(failures ? "FAILED (%d)\n" : "ok\n", failures);
    return failures ? 1 : 0;
}
