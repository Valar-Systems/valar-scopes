#pragma once

// BENCH ONLY: makes a healthy touch controller look DEAD, so the touch-wedge cap can be
// tested on a board whose chip is fine (TouchWedgePolicy.h, docs/v15-touch-wedge-cap.md).
//
// While Dead(): TouchWatchdog's Probe() never gets an answer (so the supervisor declares
// the wedge and walks its ladder exactly as for a real dead chip), and the touch poll
// reports no touch (so nothing advances the idle clock). Dead FROM BOOT, because the chain
// spans reboots; serial key `w` revives the chip for the reset tests.
//
// Compiled only with -DTOUCH_WEDGE_BENCH (env:blipscope-s3-128-touchbench and
// env:weatherscope-touchbench-s3-128). Every line it prints carries "[touch-bench]", and
// scripts/check-no-bench-hooks.sh refuses that marker in every shipping image.
//
// The keys and the 30 s summary are shared by both editions' bench images (A12.119): each
// edition calls Announce and Summary from its loop and offers every serial byte to OnKey.
#ifndef TOUCH_WEDGE_BENCH
#error "TouchBench.h is bench-only (TOUCH_WEDGE_BENCH)"
#endif

#include <Arduino.h>
#include "TouchWedgeCap.h"

namespace touchbench {
inline bool& Dead()
{
    static bool dead = true;
    return dead;
}

// Once: the chip's state and the keys. `extraKeys` names an edition's own (the radar's " n=next screen").
inline void Announce(const char* extraKeys)
{
    static bool announced = false;
    if (announced) return;
    announced = true;
    Serial.printf("[touch-bench] chip %s from boot; keys: w=toggle dead/alive r=state%s "
                  "(idle %lus, healthy %lus)\n", Dead() ? "DEAD" : "alive", extraKeys,
                  (unsigned long)(touchwedge::IDLE_IN_FORCE_MS / 1000UL),
                  (unsigned long)(touchwedge::HEALTHY_IN_FORCE_MS / 1000UL));
}

// The boot summary again at 30 s: the bench capture reattaches only once a rebooted board is
// past its boot window (a reopen inside it resets the chip, rst:0x15), so it misses the boot
// line; this repeats what that line said.
inline void Summary(const touchwedge::Cap& cap, const char* resetName)
{
    static bool summarised = false;
    if (summarised || millis() < 30000UL) return;
    summarised = true;
    Serial.printf("[touch-bench] t=%lu boot summary: reset=%s run=%u stored=%u unavailable=%d chip=%s\n",
                  millis(), resetName, (unsigned)cap.St().run, (unsigned)cap.Stored(),
                  (int)cap.St().unavailable, Dead() ? "DEAD" : "alive");
}

// `w` toggles the chip, `r` prints the cap's state. True if the byte was one of them.
inline bool OnKey(int ch, const touchwedge::Cap& cap)
{
    if (ch == 'w') {
        Dead() = !Dead();
        Serial.printf("[touch-bench] t=%lu chip now %s\n", millis(), Dead() ? "DEAD" : "alive");
        return true;
    }
    if (ch == 'r') {
        Serial.printf("[touch-bench] t=%lu run=%u stored=%u unavailable=%d wedgeSeen=%d chip=%s\n", millis(),
                      (unsigned)cap.St().run, (unsigned)cap.Stored(), (int)cap.St().unavailable,
                      (int)cap.WedgeSeen(), Dead() ? "DEAD" : "alive");
        return true;
    }
    return false;
}
} // namespace touchbench
