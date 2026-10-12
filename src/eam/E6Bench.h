#pragma once

// E6 LONG-MESSAGE BENCH -- env missileer-s3-128-e6bench ONLY. NEVER SHIPS.
//
// ROADMAP E6 asks what a long EAM costs on the device: the host test
// (test/host/test_eam_long_message.cpp) counts every byte the parse and the merge
// ask for and converts it to device heap by a DERIVED rule; this measures the real
// heap, so the two can be compared. Procedure and verdict table:
// docs/e6-long-message-check.md.
//
// What the bench build changes, all of it in RAM, none of it saved:
//   * the feed base is the LAN fixture server (E6_FIXTURE_BASE, from the build
//     environment), WHATEVER NVS `eam-base-url` holds -- a build-flag default only
//     reaches a key that was never saved, and a configured board has saved it;
//   * no ntfy alert is sent (the topic is blanked), so fixture "EAMs" never reach
//     a phone;
//   * the EAM logbook (NVS `eam-log`) is never written, so fixture ids do not
//     become "seen" EAMs on the restored image;
//   * the poll cadence is the Normal one (interval scale 1), so a run takes the
//     time the procedure says whatever `eam-refresh` is set to.
// The fixture server 404s /api/v1/missileer/config, so no game config is ever
// held and no drill can be offered (DrillPolicy.h: NoConfig); this env has no
// GAME_API_BASE, so no vote is ever sent either.
//
// THE MARKER is "[e6-bench]": every line printed here carries it, and
// scripts/check-no-bench-hooks.sh fails any shipping Missileer image that
// contains it (CI scans missileer-s3-128 and missileer-s3-146 on every build).
#if defined(MISSILEER_E6_BENCH)

#if !defined(FEATURE_EAM)
#error "MISSILEER_E6_BENCH is a Missileer bench hook: build it only into a missileer-* env"
#endif

#include <Arduino.h>
#include <esp_heap_caps.h>

#include "HeapHealth.h"

#ifndef E6_FIXTURE_BASE
#define E6_FIXTURE_BASE ""
#endif

namespace e6bench {

// Where the feed goes. Never empty and never production: with no fixture base
// compiled in, the feed points at a name that cannot resolve, and says so.
inline String FeedBase()
{
    const String b = E6_FIXTURE_BASE;
    return b.length() ? b : String("http://e6-fixture-base-not-set.invalid");
}

// One heap reading. Taken FIRST and printed afterwards, so the print's own
// allocation (Print::printf mallocs a line over 64 bytes) is never inside it.
struct Sample {
    uint32_t intFree, intLargest, intMin, psramFree;
    bool tls16k;
};

inline Sample Take()
{
    const uint32_t caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;   // HeapHealth's kTlsCaps
    Sample s;
    s.intFree = (uint32_t)heap_caps_get_free_size(caps);
    // NOT a gate and NOT the headline: a max ACROSS regions, which latches onto
    // reserves nothing allocates from (HeapHealth.h). Printed because the brief
    // asks for it; read int_free and tls16k first.
    s.intLargest = (uint32_t)heap_caps_get_largest_free_block(caps);
    s.intMin = (uint32_t)heap_caps_get_minimum_free_size(caps);   // low-water mark since boot
    s.psramFree = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    // The question that matters: could a TLS handshake get its block right now?
    // Asked of the allocator, not inferred from a number (HeapHealth.h).
    s.tls16k = heaphealth::CanAllocate(heaphealth::TLS_HANDSHAKE_BYTES);
    return s;
}

// phase: pre (worker, before GetJson) | doc (worker, parsed, JsonDocument alive)
//        | res (worker, doc freed, result handed over) | merge (loop, MergeLatest's
//        peak: result + incoming + merged + old store) | kept (loop, result freed)
inline void Print(const char* phase, const Sample& s, int topLen, unsigned msgs)
{
    Serial.printf("[e6-bench] phase=%s len=%d msgs=%u int_free=%u int_largest=%u int_min=%u "
                  "psram_free=%u tls16k=%d t=%lu\n",
                  phase, topLen, msgs, (unsigned)s.intFree, (unsigned)s.intLargest, (unsigned)s.intMin,
                  (unsigned)s.psramFree, s.tls16k ? 1 : 0, (unsigned long)millis());
}

}  // namespace e6bench

#endif  // MISSILEER_E6_BENCH
