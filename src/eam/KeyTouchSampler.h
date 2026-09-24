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
// With no edge for kIdleReadMs the task reads anyway, so a static finger (which the
// chip may stop reporting) and a lift are still seen.
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
        uint64_t tUs;     // esp_timer time of the report edge (or of the read, on an idle read)
        bool onEdge;      // true: read on an INT report edge; false: the idle fallback read
    };

    void Begin(LGFX& tft);

    // Loop: open or close the key window. Closing logs the window's cadence.
    void SetActive(bool on);
    bool Active() const { return active; }

    // Loop: the ordinary once-per-pass read, serialized with the task.
    bool ReadDirect(int32_t& x, int32_t& y);

    // Loop: the oldest sample with tUs <= upToUs, if any. Samples arrive in order.
    bool Next(uint64_t upToUs, Sample& out);

private:
    static constexpr uint32_t kIdleReadMs = 20;
    static constexpr int kQueueDepth = 64;

    LGFX* tft = nullptr;
    TaskHandle_t task = nullptr;
    QueueHandle_t queue = nullptr;
    SemaphoreHandle_t busMutex = nullptr;
    volatile bool active = false;

    // Stats for the current window. Written by the task only; read by the loop
    // after SetActive(false) has handed them over under the mutex.
    game::TouchCadence edgeCadence;   // report edge to report edge, while touched
    game::TouchCadence sampleCadence; // every sample the key turn saw, while touched
    uint32_t edges = 0, idleReads = 0, drops = 0, touchedSamples = 0;
    uint64_t readUsSum = 0;
    uint32_t readUsMax = 0;
    uint64_t windowStartUs = 0;

    static void IRAM_ATTR OnIntEdge(void* arg);
    static void Trampoline(void* arg);
    void Run();
    void LogWindow();
};

#endif // FEATURE_EAM_GAME
