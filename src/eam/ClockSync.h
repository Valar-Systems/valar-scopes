#pragma once

#include <stdint.h>

// ClockSync -- WHEN the device's clock was last set by SNTP, on the monotonic clock.
//
// The drill may arm (and PR 2's client may vote) only with a sync of KNOWN age younger
// than the served maximum (Fable, 2026-09-23). EamManager's old `time(nullptr) >
// 1600000000` test answers "is it after 2020?", which a clock that synced once, days ago,
// and has drifted since still passes. This records the instant of each real sync so the
// age is a measurement, not an assumption.
//
// The sync callback runs on the SNTP (lwIP) task, so the timestamp is published under a
// spinlock: a 64-bit store is two 32-bit stores on the S3, and a torn read would report a
// sync from the far future or the distant past.
namespace clocksync {

// Register the SNTP sync callback. Call once, after configTime(). Idempotent.
void Begin();

// True once any sync has completed since boot.
bool HaveSync();

// esp_timer_get_time() at the most recent sync, in microseconds (0 = never).
uint64_t LastSyncMonoUs();

} // namespace clocksync
