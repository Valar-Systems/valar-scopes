#pragma once

// BENCH ONLY: makes a healthy touch controller look DEAD, so the touch-wedge cap can be
// tested on a board whose chip is fine (TouchWedgePolicy.h, docs/v15-touch-wedge-cap.md).
//
// While Dead(): TouchWatchdog's Probe() never gets an answer (so the supervisor declares
// the wedge and walks its ladder exactly as for a real dead chip), and the touch poll
// reports no touch (so nothing advances the idle clock). Dead FROM BOOT, because the chain
// spans reboots; serial key `w` revives the chip for the reset tests.
//
// Compiled only with -DTOUCH_WEDGE_BENCH (env:blipscope-s3-128-touchbench). Every line it
// prints carries "[touch-bench]", and scripts/check-no-bench-hooks.sh refuses that marker
// in every shipping image.
#ifndef TOUCH_WEDGE_BENCH
#error "TouchBench.h is bench-only (TOUCH_WEDGE_BENCH)"
#endif

namespace touchbench {
inline bool& Dead()
{
    static bool dead = true;
    return dead;
}
} // namespace touchbench
