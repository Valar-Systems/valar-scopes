#pragma once

// KeyTouchSampler — touch at the panel's own report rate, for the key turn.
//
// THE KEY TURN IS SCORED AT A SAMPLE'S TIMESTAMP. HandleTouch reads the panel once
// per render pass, and on the S3-128 a pass is ~48 ms ([health] frame avg), so a
// deviation measured there cannot be finer than a frame whatever the server's
// bucket is. This reads touch on its own task instead, once per CST816 REPORT:
// the chip pulses INT (GPIO11) low for each report, an ISR stamps the edge with
// esp_timer and wakes the task, and the task reads the coordinates. The sample's
// time is the edge's, not the read's.
//
// WHY ON THE REPORT, NOT A FASTER TIMER. The panel cannot produce data faster than
// it reports, so reading faster only re-reads the last report -- and on this chip
// revision a read BETWEEN reports intermittently returns zero contacts mid-touch
// (include/variants/s3_128.h, BLIPSCOPE_TOUCH_PIN_INT: the phantom-release bug of
// the blind-polling build). The report edge is the fastest real rate there is.
//
// READ ONLY ON INT. The first bench run (2026-09-24, COM15) found the chip's IrqCtl in
// change-only mode -- 0-2 INT edges per touch -- so a 20 ms timer fallback produced
// every sample and "measured" its own period. Begin() now logs the chip's IrqCtl,
// scan period and self-reset timers, then sets IrqCtl to a pulse per report
// (EnTouch|EnChange). There is no timer read at all: a touched finger that goes
// kSilenceMs without a report is COUNTED (silences), never polled.
//
// ONE READER AT A TIME. The touch I2C transaction is not safe from two tasks, so
// every read -- the loop's ordinary HandleTouch poll included -- goes through
// ReadDirect()/the task under one mutex. Active only during the key window
// (Armed/Window); the rest of the time the task sleeps and the loop reads as before.
//
// FEATURE_EAM_GAME only.

#if defined(FEATURE_EAM_GAME)

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include "LGFX.h"
#include "../game/TouchCadence.h"

class KeyTouchSampler {
public:
    struct Sample {
        bool touched;
        int16_t x, y;
        uint64_t tUs;     // esp_timer time of the report's INT edge
        bool onEdge;      // always true now (no timer reads); kept for the log/consumers
    };

    void Begin(LGFX& tft);

    // Loop: open or close the key window. Closing logs the window's cadence.
    void SetActive(bool on);
    bool Active() const { return active; }

    // Loop: the ordinary once-per-pass read, serialized with the task.
    bool ReadDirect(int32_t& x, int32_t& y);

    // Loop: the oldest sample with tUs <= upToUs, if any. Samples arrive in order.
    bool Next(uint64_t upToUs, Sample& out);

#if defined(KEYTOUCH_BENCH)
    // Loop, bench only: the run prompt. Before each run Daniel answers A (30 s
    // continuous drag, no lifts) or B (ten deliberate lifts); E ends the run. Every
    // release and window line is tagged with the run it belongs to (run=A#1, ...;
    // run=-#0 outside a run), so the log says which run is which. The release
    // debounce (KeyTurnParams::rejoin_us) is set from these runs, never from bench
    // run 1's data (docs/missileer-game-design.md, "Release debounce").
    void BenchPollSerial();
#endif

private:
    static constexpr uint32_t kSilenceMs = 100;
    static constexpr int kQueueDepth = 64;

    LGFX* tft = nullptr;
    TaskHandle_t task = nullptr;
    QueueHandle_t queue = nullptr;
    SemaphoreHandle_t busMutex = nullptr;
    volatile bool active = false;

    // Stats for the current window. Written by the task only; read by the loop
    // after SetActive(false) has handed them over under the mutex.
    game::TouchCadence edgeCadence;   // report edge to report edge, while touched
    uint32_t edges = 0, silences = 0, drops = 0, touchedSamples = 0;
    uint8_t irqWanted = 0;
    uint64_t readUsSum = 0;
    uint32_t readUsMax = 0;
    uint64_t windowStartUs = 0;
#if defined(KEYTOUCH_BENCH)
    volatile char benchRun = 0;        // 'A', 'B', or 0 outside a run
    volatile uint16_t benchRunSeq = 0; // runs started since boot
    uint16_t benchReleases = 0;        // releases logged in the current run (task only)
    void BenchPrompt();
#endif

    static void IRAM_ATTR OnIntEdge(void* arg);
    static void Trampoline(void* arg);
    void Run();
    void LogWindow();
    void ResetStats();
    void ConfigureChip();
};

#endif // FEATURE_EAM_GAME
