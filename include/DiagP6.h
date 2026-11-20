// P6-DIAG (weatherscope; REG-diag-images.md f020896cbfe1, accepted 2026-11-20): BENCH ONLY, never toward firmware.
// Compiled only in the diag env (FRAME_PROFILE_GAINS == 8), on top of G6 (bench/gains-p6 10d4517fc7c9):
//   6-X1  NEW, plus labelcache::LayoutToday run and DISCARDED inside LabelLayout (so inside A_LAYOUT): OLD's layout
//         format() work, 2 x (5 + 3) = 16 format() a contact at the bench defaults. OLD's 3 draw-side format() calls
//         (A_DRAW) are not added: the registration says "inside A_LAYOUT".
//   6-X2  NEW, plus a loop that builds and frees Strings of a FIXED length table per LabelLayout call, with no
//         format() code: the approximation (named in the registration) of what LayoutToday's format calls build.
//         A FINDING made while writing this, reported, not acted on: on this framework (arduino-esp32 3.3.9,
//         cores/esp32/WString.h :319-327) a String holds up to 14 characters INLINE (SSO), and every length below
//         is 8 or fewer. So neither OLD's format() strings nor X2's touch the heap; X2 measures String churn, not
//         heap allocation.
//   heap  the internal heap's largest free block and free total, at each scene's measure start and end.
#pragma once

#if defined(FRAME_PROFILE) && defined(FRAME_PROFILE_GAINS) && FRAME_PROFILE_GAINS == 8
#define DIAG_P6_ON 1

#include <Arduino.h>
#include <esp_heap_caps.h>

namespace diagp6 {

inline volatile uint32_t g_sink = 0;   // X1's and X2's results land here, so nothing is optimised away
inline void Sink(uint32_t v) { g_sink = g_sink + v; }

// X2's table, per LabelLayout call: callsign 4, type 0, operator 0, speed 6 ("NNN kt"), baroalt 8 ("NNNNN ft"), and
// the two "+" temporaries (3, 5). Approximations from the field rules (AircraftInfoFields.cpp :19-25).
constexpr uint8_t kLens[] = {4, 0, 0, 6, 8, 3, 5};

inline void AllocLikeToday()
{
    for (uint8_t n : kLens) {
        String s;
        s.reserve(n);
        for (uint8_t i = 0; i < n; ++i) s += (char)('0' + i);
        Sink((uint32_t)s.length() + (n ? (uint8_t)s[0] : 0u));
    }
}

struct Heap {
    uint32_t largest = 0, free = 0;
};
inline Heap Read()
{
    Heap h;
    h.largest = (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    h.free = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    return h;
}
inline Heap g_start;
inline void MeasureStart() { g_start = Read(); }
inline void Print(int scene)
{
    const Heap e = Read();
    Serial.printf("[diag-p6] heap S%d start largest=%lu free=%lu end largest=%lu free=%lu\n", scene,
                  (unsigned long)g_start.largest, (unsigned long)g_start.free, (unsigned long)e.largest,
                  (unsigned long)e.free);
}

}  // namespace diagp6

#endif  // FRAME_PROFILE_GAINS == 8
