#include "ClockSync.h"

#include <Arduino.h>
#include <esp_sntp.h>
#include <esp_timer.h>

namespace clocksync {

namespace {

portMUX_TYPE gMux = portMUX_INITIALIZER_UNLOCKED;
uint64_t gLastSyncMonoUs = 0;
bool gBegun = false;

// On the SNTP task. Records the monotonic instant only; no logging, no allocation.
void OnSync(struct timeval* /*tv*/)
{
    const uint64_t now = (uint64_t)esp_timer_get_time();
    portENTER_CRITICAL(&gMux);
    gLastSyncMonoUs = now;
    portEXIT_CRITICAL(&gMux);
}

} // namespace

void Begin()
{
    if (gBegun) return;
    gBegun = true;
    // A SINGLE callback slot in ESP-IDF's SNTP. Nothing else in the EAM build registers
    // one (the gametest harness does, but it is a separate firmware image).
    sntp_set_time_sync_notification_cb(OnSync);
    // A sync that completed before this registration (configTime() runs earlier in
    // setup) would otherwise go unrecorded until the next one, ~1 h later.
    if (sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED) OnSync(nullptr);
}

uint64_t LastSyncMonoUs()
{
    portENTER_CRITICAL(&gMux);
    const uint64_t v = gLastSyncMonoUs;
    portEXIT_CRITICAL(&gMux);
    return v;
}

bool HaveSync() { return LastSyncMonoUs() != 0; }

} // namespace clocksync
