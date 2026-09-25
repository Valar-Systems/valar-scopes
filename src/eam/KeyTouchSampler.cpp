#include "KeyTouchSampler.h"

#if defined(FEATURE_EAM_GAME)

#include <esp_timer.h>

// KEYTOUCH_BENCH: the sampler runs outside the key window too, and logs the
// cadence of every touch when the finger lifts -- so the report rate can be
// measured with a finger drag, without working a drill to its key window.
// Bench builds only: `PLATFORMIO_BUILD_FLAGS="-DKEYTOUCH_BENCH -UFEATURE_USB_OPEN"`
// (the second flag keeps a held finger from typing into the host).

namespace {
volatile uint64_t gLastEdgeUs = 0;

// CST816 configuration registers (see src/probe/TouchProbe.cpp DumpConfig).
constexpr uint8_t kRegNorScanPer = 0xEE;  // normal-mode scan period
constexpr uint8_t kRegIrqCtl     = 0xFA;  // interrupt mode
constexpr uint8_t kRegAutoReset  = 0xFB;  // touch held with no gesture -> chip self-resets (s)
constexpr uint8_t kRegLongPress  = 0xFC;  // long press -> chip self-reset (s)
constexpr uint8_t kRegDisAutoSlp = 0xFE;  // non-zero: never auto-sleep

// IrqCtl bits: EnTest 0x80, EnTouch 0x40 (a pulse per report while touched),
// EnChange 0x20 (a pulse on touch state change), EnMotion 0x10, OnceWLP 0x01.
// The first bench run (2026-09-24, COM15) saw 0-2 edges per touch: change-only.
constexpr uint8_t kIrqReportPerSample = 0x40 | 0x20;

int ReadReg(uint8_t reg)
{
#if defined(BLIPSCOPE_TOUCH_I2C_PORT) && defined(BLIPSCOPE_TOUCH_I2C_ADDR)
    auto r = lgfx::i2c::readRegister8(BLIPSCOPE_TOUCH_I2C_PORT, BLIPSCOPE_TOUCH_I2C_ADDR, reg,
                                      BLIPSCOPE_TOUCH_FREQ);
    return r.has_value() ? (int)r.value() : -1;
#else
    (void)reg;
    return -1;
#endif
}

bool WriteReg(uint8_t reg, uint8_t v)
{
#if defined(BLIPSCOPE_TOUCH_I2C_PORT) && defined(BLIPSCOPE_TOUCH_I2C_ADDR)
    return lgfx::i2c::writeRegister8(BLIPSCOPE_TOUCH_I2C_PORT, BLIPSCOPE_TOUCH_I2C_ADDR, reg, v, 0,
                                     BLIPSCOPE_TOUCH_FREQ).has_value();
#else
    (void)reg; (void)v;
    return false;
#endif
}
}  // namespace

void IRAM_ATTR KeyTouchSampler::OnIntEdge(void* arg)
{
    KeyTouchSampler* self = static_cast<KeyTouchSampler*>(arg);
    if (!self->active || self->task == nullptr) return;
    gLastEdgeUs = (uint64_t)esp_timer_get_time();
    BaseType_t woken = pdFALSE;
    vTaskNotifyGiveFromISR(self->task, &woken);
    if (woken) portYIELD_FROM_ISR();
}

void KeyTouchSampler::ConfigureChip()
{
    // WHAT THE CHIP IS SET TO, logged before anything is changed: the interrupt mode,
    // the scan period, and the two self-reset timers a long hold could run into.
    const int irq = ReadReg(kRegIrqCtl), scan = ReadReg(kRegNorScanPer);
    const int autoReset = ReadReg(kRegAutoReset), longPress = ReadReg(kRegLongPress);
    const int noSleep = ReadReg(kRegDisAutoSlp);
    Serial.printf("[keytouch] chip: IrqCtl(0xFA)=0x%02X NorScanPer(0xEE)=%d AutoReset(0xFB)=%ds "
                  "LongPressTime(0xFC)=%ds DisAutoSleep(0xFE)=%d (-1 = read failed)\n",
                  irq, scan, autoReset, longPress, noSleep);
    // REPORT PER SAMPLE: INT pulses once per report while touched (EnTouch), and on the
    // touch/release edges (EnChange). The sampler reads ONLY on these pulses.
    const bool ok = WriteReg(kRegIrqCtl, kIrqReportPerSample);
    irqWanted = kIrqReportPerSample;
    Serial.printf("[keytouch] IrqCtl <- 0x%02X: %s, reads back 0x%02X\n", kIrqReportPerSample,
                  ok ? "written" : "WRITE FAILED", ReadReg(kRegIrqCtl));
}

void KeyTouchSampler::Begin(LGFX& panel)
{
    if (task != nullptr) return;
    tft = &panel;
    busMutex = xSemaphoreCreateMutex();
    queue = xQueueCreate(kQueueDepth, sizeof(Sample));
    xSemaphoreTake(busMutex, portMAX_DELAY);
    ConfigureChip();
    xSemaphoreGive(busMutex);
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

void KeyTouchSampler::ResetStats()
{
    edgeCadence.Reset();
    edges = silences = drops = touchedSamples = 0;
    readUsSum = 0;
    readUsMax = 0;
    windowStartUs = (uint64_t)esp_timer_get_time();
}

void KeyTouchSampler::SetActive(bool on)
{
#if defined(KEYTOUCH_BENCH)
    on = true;  // the bench keeps it running; windows are logged per touch instead
#endif
    if (task == nullptr || on == active) return;
    xSemaphoreTake(busMutex, portMAX_DELAY);
    if (on) {
        ResetStats();
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

// " run=A#1" in a bench build, "" otherwise: which run a log line belongs to.
#if defined(KEYTOUCH_BENCH)
#define RUN_TAG_DECL char runTag[16]; snprintf(runTag, sizeof runTag, " run=%c#%u", benchRun ? benchRun : '-', (unsigned)benchRunSeq)
#else
#define RUN_TAG_DECL const char* runTag = ""
#endif

void KeyTouchSampler::Trampoline(void* arg)
{
    static_cast<KeyTouchSampler*>(arg)->Run();
}

void KeyTouchSampler::Run()
{
    // THE RELEASE LOG. Every release is logged with how long the finger had been down,
    // the gap to the next touch, and whether the very next read showed the finger again.
    // That is the data a release-debounce constant would come from; there is no such
    // constant until it has been measured. IrqCtl is re-read at each release: a chip
    // that reset itself mid-hold (AutoReset/LongPressTime) comes back at its default.
    bool wasTouched = false;
    uint64_t pressUs = 0;
    bool pending = false;          // a release awaiting its gap
    uint64_t releaseUs = 0;
    uint32_t heldMs = 0;
    int nextRead = -1;             // -1 unknown, 0 untouched, 1 touched
    int irqAtRelease = -1;
    for (;;) {
        if (!active) {
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            wasTouched = false;
            pending = false;
            continue;
        }
        const uint32_t n = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(kSilenceMs));
        if (!active) continue;
        const uint64_t nowUs = (uint64_t)esp_timer_get_time();

        if (n == 0) {
            // NO READ WITHOUT AN INT. A finger that is down and silent this long is
            // counted, not polled: polling between reports is what reads phantom lifts.
            if (wasTouched) silences += 1;
            if (pending && nowUs - releaseUs >= 1000000ull) {
                RUN_TAG_DECL;
                Serial.printf("[keytouch] release: held=%ums gap=>1000ms next_read=%s irq_after=0x%02X%s\n",
                              (unsigned)heldMs, nextRead == 1 ? "touched" : nextRead == 0 ? "untouched" : "none",
                              irqAtRelease & 0xFF, runTag);
                pending = false;
#if defined(KEYTOUCH_BENCH)
                xSemaphoreTake(busMutex, portMAX_DELAY);
                LogWindow();
                ResetStats();
                xSemaphoreGive(busMutex);
#endif
            }
            continue;
        }

        int32_t x = 0, y = 0;
        xSemaphoreTake(busMutex, portMAX_DELAY);
        const uint64_t t = gLastEdgeUs;
        const uint64_t r0 = (uint64_t)esp_timer_get_time();
        const bool touched = tft->getTouch(&x, &y);
        const uint32_t readUs = (uint32_t)((uint64_t)esp_timer_get_time() - r0);
        readUsSum += readUs;
        if (readUs > readUsMax) readUsMax = readUs;
        edges += n;
        if (touched) {
            touchedSamples += 1;
            edgeCadence.Add(t);
        } else {
            edgeCadence.Break();
        }
        if (wasTouched && !touched) irqAtRelease = ReadReg(kRegIrqCtl);
        xSemaphoreGive(busMutex);

        if (touched && !wasTouched) {
            if (pending) {
                if (nextRead < 0) nextRead = 1;
                RUN_TAG_DECL;
                Serial.printf("[keytouch] release: held=%ums gap=%ums next_read=%s irq_after=0x%02X%s\n",
                              (unsigned)heldMs, (unsigned)((t - releaseUs) / 1000ull),
                              nextRead == 1 ? "touched" : "untouched", irqAtRelease & 0xFF, runTag);
                pending = false;
            }
            pressUs = t;
        } else if (!touched && wasTouched) {
#if defined(KEYTOUCH_BENCH)
            if (benchRun) benchReleases += 1;
#endif
            pending = true;
            releaseUs = t;
            heldMs = (uint32_t)((t - pressUs) / 1000ull);
            nextRead = -1;
        } else if (pending && nextRead < 0) {
            nextRead = touched ? 1 : 0;
        }

        const Sample s{touched, (int16_t)x, (int16_t)y, t, true};
        if (xQueueSend(queue, &s, 0) != pdTRUE) drops += 1;
        wasTouched = touched;
    }
}

void KeyTouchSampler::LogWindow()
{
    // THE NUMBER THE KEY TURN IS SCORED AT: the panel's own cadence, INT edge to INT
    // edge while touched. There is no timer fallback; `silences` counts the times a
    // touched finger went kSilenceMs without a report.
    const uint32_t reads = edges;
    const uint64_t spanMs = ((uint64_t)esp_timer_get_time() - windowStartUs) / 1000ull;
    RUN_TAG_DECL;
    Serial.printf("[keytouch] window %llums: touched samples=%u | report interval avg=%.2fms min=%.2f "
                  "p95=%.2f max=%.2f (n=%u) | edges=%u silences=%u drops=%u | i2c read avg=%uus max=%uus%s\n",
                  (unsigned long long)spanMs, (unsigned)touchedSamples,
                  edgeCadence.MeanUs() / 1000.0, edgeCadence.MinUs() / 1000.0,
                  edgeCadence.PercentileUs(950) / 1000.0, edgeCadence.MaxUs() / 1000.0,
                  (unsigned)edgeCadence.Count(), (unsigned)edges, (unsigned)silences, (unsigned)drops,
                  reads ? (unsigned)(readUsSum / reads) : 0u, (unsigned)readUsMax, runTag);
}

#if defined(KEYTOUCH_BENCH)
void KeyTouchSampler::BenchPrompt()
{
    Serial.println("[keytouch] RUN? type A + Enter = run A (30 s continuous drag, NO lifts); "
                   "B + Enter = run B (ten deliberate lifts); E + Enter ends a run");
}

void KeyTouchSampler::BenchPollSerial()
{
    static bool prompted = false;
    if (!prompted) { prompted = true; BenchPrompt(); }
    while (Serial.available()) {
        int c = Serial.read();
        if (c >= 'a' && c <= 'z') c -= 32;
        if (c != 'A' && c != 'B' && c != 'E') continue;   // newlines and anything else
        if (benchRun) {
            Serial.printf("[keytouch] RUN %c#%u END: releases=%u\n", benchRun, (unsigned)benchRunSeq,
                          (unsigned)benchReleases);
            benchRun = 0;
        }
        if (c == 'E') { BenchPrompt(); continue; }
        benchReleases = 0;
        benchRunSeq = benchRunSeq + 1;
        benchRun = (char)c;
        Serial.printf("[keytouch] RUN %c#%u START: %s -- every line until E is tagged run=%c#%u\n",
                      (char)c, (unsigned)benchRunSeq,
                      c == 'A' ? "30 s continuous drag, NO lifts" : "ten deliberate lifts",
                      (char)c, (unsigned)benchRunSeq);
    }
}
#endif

#endif // FEATURE_EAM_GAME
