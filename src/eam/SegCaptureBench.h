#pragma once

// SEVEN-SEGMENT CAPTURE BENCH -- env missileer-s3-128-segcapture ONLY. NEVER SHIPS, NOT FOR
// MERGE (branch bench/missileer-segcapture-2026-10-12).
//
// Daniel's ruling (2026-10-12): before / +#405 / +#406 framebuffer captures of the Clock, the
// ON WATCH count (the even-thickness zero diff), the drill face and the 1 at the ON WATCH
// sizes. The live screen will not hold still for that -- the time moves, ON WATCH is whatever
// the server counts, no drill is offered -- so this pins each input, in four 15 s phases that
// it announces on serial, and leaves everything else on the production draw path:
//   0  Clock at 18:51:47, ON WATCH "1",  label size 2 (the 1.28" ON WATCH size)
//   1  Clock at 11:11:11, ON WATCH "10", label size 2
//   2  Clock at 18:51:47, ON WATCH "1",  label size 3 (the 1.46" ON WATCH size, on this panel)
//   3  the drill face (DrawDrill, Idle), clock age 17 s
// Marker "[seg-capture]": scripts/check-no-bench-hooks.sh fails any image that carries it.
#if defined(SEVENSEG_CAPTURE_BENCH)

#include <Arduino.h>
#include <cstring>

namespace segcapture {

constexpr unsigned long kPhaseMs = 15000;
constexpr int kPhases = 4;
constexpr int kDrillAgeS = 17;

inline int Phase() { return (int)((millis() / kPhaseMs) % kPhases); }

inline const char* Name(int p)
{
    switch (p) {
        case 0: return "clock 18:51:47, ON WATCH 1, label 2";
        case 1: return "clock 11:11:11, ON WATCH 10, label 2";
        case 2: return "clock 18:51:47, ON WATCH 1, label 3";
        default: return "drill face, clock age 17";
    }
}

inline void Announce()
{
    static int last = -1;
    const int p = Phase();
    if (p != last) {
        last = p;
        Serial.printf("[seg-capture] phase=%d %s t=%lu\n", p, Name(p), (unsigned long)millis());
    }
}

inline bool DrillPhase() { return Phase() == 3; }

inline void PinClock(int* hh, int* mm, int* ss)
{
    if (Phase() == 1) { *hh = 11; *mm = 11; *ss = 11; }
    else              { *hh = 18; *mm = 51; *ss = 47; }
}

inline void OnWatchText(char* buf, unsigned n)
{
    const char* t = Phase() == 1 ? "10" : "1";
    if (n) { std::strncpy(buf, t, n - 1); buf[n - 1] = '\0'; }
}

inline int LabelSize(int normal) { return Phase() == 2 ? 3 : normal; }

}  // namespace segcapture

#endif  // SEVENSEG_CAPTURE_BENCH
