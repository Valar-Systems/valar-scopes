#include "NetWatchdog.h"
#include "OtaUpdater.h"

#include <Arduino.h>
#include <WiFi.h>
#include <atomic>

namespace netwatch {
namespace {

// Cumulative outcome counters. Written from ANY task, read only by Tick() on the
// loop task. Relaxed ordering is right: these are counters whose exact interleave
// does not matter -- Step() works on DELTAS, so a count observed one tick late is
// simply attributed to the next tick, and the ladder's thresholds are minutes
// wide. What matters is that they never tear, which is what atomic buys.
std::atomic<uint32_t> g_okTotal{0};
std::atomic<uint32_t> g_failTotal{0};

State    g_state;                    // loop task only
Stage    g_rebootedBy = Stage::Healthy;
uint32_t g_lastTickMs = 0;

// Tick cadence. The ladder's shortest interval is the 90 s stage grace, so a
// 5 s tick is far more resolution than it needs -- and Step() is a handful of
// integer comparisons, so the cost is noise. Cheap enough that there is no
// reason to make it adaptive and one more thing to be wrong.
constexpr uint32_t TICK_MS = 5000;

void ActReconnect()
{
    // Cheapest rung: keep the radio and the driver up, drop the association and
    // take it again. Fixes the common case where the AP has forgotten us or the
    // DHCP lease went stale, and costs a second or two of downtime.
    WiFi.disconnect(/*wifioff=*/false);
    delay(100);
    WiFi.reconnect();
}

void ActRadioReset()
{
    // Everything the previous rung keeps, this one throws away: the driver, the
    // supplicant state and the IP stack's idea of the interface. This is the rung
    // that clears an "associated but unroutable" wedge where the association
    // itself is fine and something below it is not -- which is precisely the
    // state the rehearsal produced.
    WiFi.disconnect(/*wifioff=*/true, /*eraseap=*/false);
    delay(500);
    WiFi.mode(WIFI_OFF);
    delay(500);
    WiFi.mode(WIFI_STA);
    delay(100);
    WiFi.begin(); // saved credentials; never erases them
}

} // namespace

void RecordOutcome(bool transportOk)
{
    if (transportOk) g_okTotal.fetch_add(1, std::memory_order_relaxed);
    else             g_failTotal.fetch_add(1, std::memory_order_relaxed);
}

void Begin()
{
    const uint8_t cause = ConsumeDeferredRebootCause();
    if (cause == REBOOT_CAUSE_NET_WEDGE) {
        g_rebootedBy = Stage::Reboot;
        Serial.println("[netwd] this boot was armed by the REACHABILITY watchdog "
                       "(the network was unreachable for the full ladder)");
    }

    // THE LADDER IS PRINTED IN FULL, EVERY BOOT, WHETHER OR NOT IT EVER FIRES.
    //
    // Without this, "we have never seen a radio-reset in the fleet" is
    // ambiguous between a healthy fleet and a rung that cannot fire -- and those
    // want opposite responses. Printing what the ladder CONTAINS makes the
    // absence of a rung in the logs a fact about the world rather than a fact
    // about the build. Costs four lines once per boot.
    Serial.printf("[netwd] armed: trigger >=%u consecutive transport failures AND >=%lus; "
                  "ladder:\n",
                  (unsigned)MIN_FAIL_RUN, (unsigned long)(MIN_WEDGE_MS / 1000));
    Serial.printf("[netwd]   1 %-12s  2 %-12s  3 %-12s  then %s every %lus\n",
                  StageName(Stage::Reconnect), StageName(Stage::RadioReset),
                  StageName(Stage::Reboot), StageName(Stage::Backoff),
                  (unsigned long)(BACKOFF_RETRY_MS / 1000));
    Serial.printf("[netwd]   stage grace %lus; the reboot rung shares the OTA 24 h cap "
                  "and BACKS OFF when refused\n",
                  (unsigned long)(STAGE_GRACE_MS / 1000));
}

void Tick()
{
    const uint32_t now = millis();
    if (now - g_lastTickMs < TICK_MS) return;
    g_lastTickMs = now;

    // WL_CONNECTED is read HERE and passed in, and the policy does not consult
    // it -- see the header of NetWatchPolicy.h. It exists in the record so that
    // "associated but unroutable" (this defect) can be told from "fell off the
    // AP" (the case the old supervisor in main.cpp still handles).
    const bool associated = WiFi.status() == WL_CONNECTED;
    const uint32_t ok   = g_okTotal.load(std::memory_order_relaxed);
    const uint32_t fail = g_failTotal.load(std::memory_order_relaxed);

    const Action a = Step(g_state, ok, fail, now, associated);
    if (a == Action::None) return;

    // EVERY escalation prints the counts that caused it. A stage name alone says
    // what happened; the counts say whether the trigger was reasonable, which is
    // the question anyone reading this log a month from now will actually have.
    Serial.printf("[netwd] %s: failRun=%u run=%lus assoc=%d cycles=%u\n",
                  ActionName(a), (unsigned)g_state.failRun,
                  (unsigned long)((now - g_state.runStartMs) / 1000),
                  associated ? 1 : 0, (unsigned)g_state.cycles);

    switch (a) {
        case Action::Reconnect:
            ActReconnect();
            break;

        case Action::RadioReset:
            ActRadioReset();
            break;

        case Action::Reboot:
            // Shares the OTA 24 h cap. Returns false when refused -- by the cap,
            // an unsynced clock or unavailable NVS -- and control comes straight
            // back here. That is not an error: it is the designed path into
            // Backoff, and it is what stops an unreachable network from becoming
            // a reboot loop. Says so out loud, because a silent refusal here
            // would look exactly like a reboot that did not work.
            if (!DeferRebootWithCause(REBOOT_CAUSE_NET_WEDGE, ESP.getMaxAllocHeap()))
                Serial.println("[netwd] reboot refused (cap/clock/NVS) -- backing off instead");
            break;

        case Action::EnterBackoff:
            // The board cannot fix a router with no upstream. Stop spending rungs
            // on it, keep the display running on whatever data it has, and re-arm
            // slowly. Nothing here touches the UI: not drawing is the point.
            Serial.printf("[netwd] ladder exhausted; slow retry every %lus, display keeps running\n",
                          (unsigned long)(BACKOFF_RETRY_MS / 1000));
            break;

        case Action::Recovered:
            Serial.println("[netwd] traffic recovered; ladder stood down");
            break;

        case Action::None:
            break;
    }
}

Stage RebootedByStage() { return g_rebootedBy; }
Stage CurrentStage()    { return g_state.stage; }
uint16_t FailRun()      { return g_state.failRun; }

} // namespace netwatch
