#include "KeyTouchSampler.h"

#if defined(FEATURE_EAM_GAME)

#include <esp_timer.h>

// KEYTOUCH_BENCH: the sampler runs outside the key window too, and logs the
// cadence of every touch when the finger lifts -- so the report rate can be
// measured with a finger drag, without working a drill to its key window.
// Bench builds only: `PLATFORMIO_BUILD_FLAGS=-DKEYTOUCH_BENCH pio run -e missileer-s3-128`.

namespace {
volatile uint64_t gLastEdgeUs = 0;
}

void IRAM_ATTR KeyTouchSampler::OnIntEdge(void* arg)
{
    KeyTouchSampler* self = static_cast<KeyTouchSampler*>(arg);
    if (!self->active || self->task == nullptr) return;
    gLastEdgeUs = (uint64_t)esp_timer_get_time();
    BaseType_t woken = pdFALSE;
    vTaskNotifyGiveFromISR(self->task, &woken);
    if (woken) portYIELD_FROM_ISR();
}

void KeyTouchSampler::Begin(LGFX& panel)
{
    if (task != nullptr) return;
    tft = &panel;
    busMutex = xSemaphoreCreateMutex();
    queue = xQueueCreate(kQueueDepth, sizeof(Sample));
    // Core 0 with the network, at priority 2: above eam_fetch (1), far below the
    // Wi-Fi and lwIP tasks. Core 1 is the render loop, which this must not slow --
    // a read is an I2C transaction of a few hundred microseconds, once per report.
    xTaskCreatePinnedToCore(Trampoline, "key_touch", 4096, this, 2, &task, 0);
#if defined(BLIPSCOPE_TOUCH_PIN_INT)
    if (BLIPSCOPE_TOUCH_PIN_INT >= 0)
        attachInterruptArg(digitalPinToInterrupt(BLIPSCOPE_TOUCH_PIN_INT), OnIntEdge, this, FALLING);
#endif
#if defined(KEYTOUCH_BENCH)
    Serial.println("[keytouch] BENCH build: sampling every touch; drag a finger, lift, read the line");
    SetActive(true);
#endif
}

void KeyTouchSampler::SetActive(bool on)
{
#if defined(KEYTOUCH_BENCH)
    on = true;  // the bench keeps it running; windows are logged per touch instead
#endif
    if (task == nullptr || on == active) return;
    xSemaphoreTake(busMutex, portMAX_DELAY);
    if (on) {
        edgeCadence.Reset();
        sampleCadence.Reset();
        edges = idleReads = drops = touchedSamples = 0;
        readUsSum = 0;
        readUsMax = 0;
        windowStartUs = (uint64_t)esp_timer_get_time();
        xQueueReset(queue);
        active = true;
        xSemaphoreGive(busMutex);
        xTaskNotifyGive(task);
        return;
    }
    active = false;
    LogWindow();
    xSemaphoreGive(busMutex);
}

bool KeyTouchSampler::ReadDirect(int32_t& x, int32_t& y)
{
    if (busMutex == nullptr) return tft->getTouch(&x, &y);
    xSemaphoreTake(busMutex, portMAX_DELAY);
    const bool touched = tft->getTouch(&x, &y);
    xSemaphoreGive(busMutex);
    return touched;
}

bool KeyTouchSampler::Next(uint64_t upToUs, Sample& out)
{
    if (queue == nullptr) return false;
    Sample s;
    if (xQueuePeek(queue, &s, 0) != pdTRUE) return false;
    if (s.tUs > upToUs) return false;  // later than the drill's "now": next pass
    xQueueReceive(queue, &out, 0);
    return true;
}

void KeyTouchSampler::Trampoline(void* arg)
{
    static_cast<KeyTouchSampler*>(arg)->Run();
}

void KeyTouchSampler::Run()
{
    bool wasTouched = false;
    for (;;) {
        if (!active) {
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            wasTouched = false;
            continue;
        }
        const uint32_t n = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(kIdleReadMs));
        if (!active) continue;
        const bool onEdge = n > 0;

        int32_t x = 0, y = 0;
        xSemaphoreTake(busMutex, portMAX_DELAY);
        const uint64_t t = onEdge ? gLastEdgeUs : (uint64_t)esp_timer_get_time();
        const uint64_t r0 = (uint64_t)esp_timer_get_time();
        const bool touched = tft->getTouch(&x, &y);
        const uint32_t readUs = (uint32_t)((uint64_t)esp_timer_get_time() - r0);

        readUsSum += readUs;
        if (readUs > readUsMax) readUsMax = readUs;
        if (onEdge) edges += n; else idleReads += 1;
        if (touched) {
            touchedSamples += 1;
            sampleCadence.Add(t);
            if (onEdge) edgeCadence.Add(t);
        } else {
            sampleCadence.Break();
            edgeCadence.Break();
        }
        xSemaphoreGive(busMutex);

        const Sample s{touched, (int16_t)x, (int16_t)y, t, onEdge};
        if (xQueueSend(queue, &s, 0) != pdTRUE) drops += 1;

#if defined(KEYTOUCH_BENCH)
        if (wasTouched && !touched && touchedSamples >= 2) {
            xSemaphoreTake(busMutex, portMAX_DELAY);
            LogWindow();
            edgeCadence.Reset();
            sampleCadence.Reset();
            edges = idleReads = drops = touchedSamples = 0;
            readUsSum = 0;
            readUsMax = 0;
            windowStartUs = (uint64_t)esp_timer_get_time();
            xSemaphoreGive(busMutex);
        }
#endif
        wasTouched = touched;
    }
}

void KeyTouchSampler::LogWindow()
{
    // THE NUMBER THE KEY TURN IS SCORED AT. `report` is the panel's own cadence
    // (INT edge to INT edge while touched); `sample` is every sample the key turn
    // saw while touched, idle reads included.
    const uint32_t reads = edges + idleReads;
    const uint64_t spanMs = ((uint64_t)esp_timer_get_time() - windowStartUs) / 1000ull;
    Serial.printf("[keytouch] window %llums: touched samples=%u | report interval avg=%.2fms min=%.2f "
                  "p95=%.2f max=%.2f (n=%u) | sample interval avg=%.2fms p95=%.2f max=%.2f (n=%u) | "
                  "edges=%u idle reads=%u drops=%u | i2c read avg=%uus max=%uus\n",
                  (unsigned long long)spanMs, (unsigned)touchedSamples,
                  edgeCadence.MeanUs() / 1000.0, edgeCadence.MinUs() / 1000.0,
                  edgeCadence.PercentileUs(950) / 1000.0, edgeCadence.MaxUs() / 1000.0,
                  (unsigned)edgeCadence.Count(),
                  sampleCadence.MeanUs() / 1000.0, sampleCadence.PercentileUs(950) / 1000.0,
                  sampleCadence.MaxUs() / 1000.0, (unsigned)sampleCadence.Count(),
                  (unsigned)edges, (unsigned)idleReads, (unsigned)drops,
                  reads ? (unsigned)(readUsSum / reads) : 0u, (unsigned)readUsMax);
}

#endif // FEATURE_EAM_GAME
