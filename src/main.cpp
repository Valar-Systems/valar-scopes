#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFiManager.h>
#include <esp_task_wdt.h>
// THE FRAME BENCH BEGIN
#ifdef FRAME_BENCH
#include "FrameBench.h"   // the frame-time bench, C1 (docs/frame-time-bench.md)
#endif
// THE FRAME BENCH END
// THE FRAME PROFILE BEGIN
#ifdef FRAME_PROFILE
#include "FrameProfile.h"   // the frame profile, P1 (docs/frame-profile.md)
#endif
// THE FRAME PROFILE END

#include "LGFX.h"
#include "Layout.h"
#include "FrameBuffer.h"
#ifdef FEATURE_WEATHERSCOPE
#include <atomic>
#include <cstring>
#include <esp_heap_caps.h>
#include "DiagText.h"          // diag only: /diag/wx (docs/weatherscope-stage3.md A9.42)
#endif
#include "BootScreen.h"
#include "SplashScreen.h"
#include "Board.h"
#include "BuildIdentity.h"
#include "DeviceIdentity.h"
#include "FactoryReset.h"
#include "ConfigMigration.h"
#include "NtfyTopic.h"
#include "TlsAllocator.h"
#include "HeapHealth.h"
#include "WiFiManagerHelpers.h"
#include "ConfigurationWebServer.h"
#include "HttpRequestManager.h"
#include "OpenSkyAuthTokenHandler.h"
#include "OtaUpdater.h"
#include "NetWatchdog.h"
#include "BrightnessCarry.h"
#include "BrightnessLog.h"
#include "LocalOffset.h"
#include "QuietHourPolicy.h"
#include "JoinFailure.h"      // ships: why a join failed, said on the glass
#ifdef BLIPSCOPE_JOIN_DIAG
#include "JoinDiagScreen.h"   // bench-only Wi-Fi join diagnostic; see the header
#endif
#include "NightOverride.h"

// JoinFailure.h duplicates the SDK's reason codes so it can stay pure and be
// host-tested. That duplication is a transcription, and a transcription is the
// weak form of a contract -- so it is asserted against the real enum HERE, where
// esp_wifi_types.h is in scope. If the IDF ever renumbers one, this fails the
// BUILD rather than quietly turning "wrong password" into the wrong sentence.
static_assert(joinfail::R_AUTH_EXPIRE            == WIFI_REASON_AUTH_EXPIRE, "");
static_assert(joinfail::R_4WAY_HANDSHAKE_TIMEOUT == WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT, "");
static_assert(joinfail::R_NO_AP_FOUND            == WIFI_REASON_NO_AP_FOUND, "");
static_assert(joinfail::R_AUTH_FAIL              == WIFI_REASON_AUTH_FAIL, "");
static_assert(joinfail::R_HANDSHAKE_TIMEOUT      == WIFI_REASON_HANDSHAKE_TIMEOUT, "");
// The active app is a compile-time choice: the radar (default), the FEATURE_EAM monitor, the
// FEATURE_SPACE (Spacescope) monitor, the FEATURE_SEISMIC earthquake radar, the FEATURE_BIRDING
// sightings radar, the FEATURE_FISHING (Reelscope) console, the FEATURE_CLAUDESCOPE usage gauge,
// the FEATURE_SPEED (Speedscope) speed-radar, or the FEATURE_WEATHERSCOPE (Weatherscope) weather
// radar. All expose the same
// Initialise()/Update()/Draw() surface, so loop() drives `appManager` without knowing which it is.
// The radar's sweep/models headers are radar-only.
#if defined(FEATURE_EAM)
#include "eam/EamManager.h"
#elif defined(FEATURE_SPACE)
#include "space/SpaceManager.h"
#elif defined(FEATURE_SEISMIC)
#include "seismic/SeismicManager.h"
#elif defined(FEATURE_BIRDING)
#include "birding/BirdingManager.h"
#elif defined(FEATURE_FISHING)
#include "fishing/FishingManager.h"
#elif defined(FEATURE_CLAUDESCOPE)
#include "claudescope/ClaudescopeManager.h"
#elif defined(FEATURE_SPEED)
#include "speed/SpeedManager.h"
#elif defined(FEATURE_WEATHERSCOPE)
#include "weatherscope/WeatherscopeManager.h"
#else
#include "AircraftManager.h"
#include "DrawHelpers.h"
#include "models/Aircraft.h"
#include "models/TrackedAircraft.h"
#endif

#ifdef SOAK_TEST
#include "SoakHarness.h" // realistic-duty 24 h soak
#endif

LGFX tft;
LGFX_Sprite backbuffer(&tft);
#if defined(BLIPSCOPE_PUSH_TASK)
LGFX_Sprite backbuffer2(&tft);   // M3: the second backbuffer (docs/m3-push-task.md, section 3)
#endif

// The borrowed view the /diag/fb handler reads. No second allocation: on every
// SKU BANDED_RENDER is false, so this sprite IS the whole 240x240 RGB565 frame
// already sitting in PSRAM. See include/FrameBuffer.h for why the sequence
// counter exists instead of a lock.
namespace {
LGFX_Sprite*           g_fbSprite = nullptr;
volatile uint32_t      g_fbSeq    = 0;
}  // namespace
namespace framebuf {
LGFX_Sprite* Backbuffer() { return g_fbSprite; }
uint32_t     Sequence()   { return g_fbSeq; }
void         FrameDone()  { ++g_fbSeq; }
void         Register(LGFX_Sprite* sprite) { g_fbSprite = sprite; }
// Where the loop is in the frame, the app's tag, and the clears (FrameBuffer.h; docs/weatherscope-stage3.md A9.22,
// A9.26): /diag/fb serves only a frame copied wholly between a Drawn store and the next Clearing store.
volatile uint8_t  g_fbPhase  = 0;
volatile uint32_t g_fbTag    = 0;
volatile uint32_t g_fbClears = 0;
void         SetPhase(uint8_t phase)
{
  if (phase == kPhaseClearing) g_fbClears = g_fbClears + 1;   // before the phase, so a reader never sees the
  g_fbPhase = phase;                                           // Clearing phase with the old count
}
uint8_t      Phase()                 { return g_fbPhase; }
uint32_t     Clears()                { return g_fbClears; }
void         SetTag(uint32_t tag)    { g_fbTag = tag; }
uint32_t     Tag()                   { return g_fbTag; }
}  // namespace framebuf

#if defined(BLIPSCOPE_PUSH_TASK)
// =====================================================================================================
// M3: THE PUSH ON ITS OWN TASK (docs/m3-push-task.md; design (i), DMA straight from the PSRAM buffer).
//
// Two backbuffers. The loop draws frame N into one, hands it over, and draws frame N+1 into the other
// while frame N goes out over SPI. Each buffer has a binary semaphore, "free": the loop takes it before
// drawing, and the push task gives it back only AFTER the send completes, so the loop can never draw
// into a buffer that is being sent, and a frame is handed over exactly once.
//
// What was read before this was written (the doc's section 2): the push held the loop for its whole
// 25.8 ms; LovyanGFX turns DMA OFF for PSRAM sprites on purpose ("DMA disable with use SPIRAM",
// LGFX_Sprite.hpp), which is why the cache is written back here first and why design (ii) exists; the
// S3's DMA path issues a long buffer segment by segment and yields between them; nothing the loop calls
// at run time writes the SPI bus, EXCEPT the two restart screens, which call Quiesce() first.
// =====================================================================================================
#include <esp_cache.h>
#if defined(M3_BENCH)
#include <esp_async_memcpy.h>
#endif
namespace pushtask {
constexpr size_t kBytes = (size_t)SCREEN_SIZE * SCREEN_SIZE * 2;   // one RGB565 frame
static_assert(!variant::BANDED_RENDER, "M3 needs whole-frame buffers: BANDED_RENDER must be false");
static_assert(kBytes % 64 == 0, "the cache write-back covers whole 64-byte lines");

LGFX_Sprite* g_buf[2] = {&backbuffer, &backbuffer2};
SemaphoreHandle_t g_free[2] = {nullptr, nullptr};
QueueHandle_t g_queue = nullptr;
int g_draw = 0;                  // the buffer the loop draws into next (loop task only)
volatile int g_complete = -1;    // the last buffer handed over complete: what /diag/fb reads

// The [m3] line's counters: written by the push task, printed by it every kReportEvery pushes.
uint32_t g_pushes = 0, g_syncErr = 0;
uint64_t g_pushUsSum = 0, g_syncUsSum = 0;
uint32_t g_pushUsMax = 0;
volatile uint32_t g_waitUsSum = 0, g_waitUsMax = 0, g_waits = 0;   // the loop's waits for a free buffer
constexpr uint32_t kReportEvery = 750;                              // ~30 s at 25 fps

#if defined(M3_BENCH)
// ---- M3-9's COHERENCY SELF-CHECK (bench only: env blipscope-s3-128-m3bench; docs/m3-push-task.md) ----------------
// Every kCheckEvery-th push, right after it, the same PSRAM frame is read again by a second DMA (esp_async_memcpy)
// into an internal buffer, chunk by chunk, and compared with what the CPU sees. A mismatch means the DMA read bytes
// the CPU had not written back: the fault behind a stale band. The plant (-DM3_SELFCHECK_PLANT) skips the cache
// write-back on exactly the checked frames, and must be caught, or the check is not believed.
constexpr uint32_t kCheckEvery = 30;
constexpr size_t kChunk = 4096;
async_memcpy_handle_t g_mcp = nullptr;
uint8_t* g_chunk = nullptr;
SemaphoreHandle_t g_chunkDone = nullptr;
uint32_t g_checks = 0, g_badFrames = 0, g_badChunks = 0, g_checkErr = 0;

static bool IRAM_ATTR OnChunkDone(async_memcpy_handle_t, async_memcpy_event_t*, void* arg)
{
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR((SemaphoreHandle_t)arg, &woken);
    return woken == pdTRUE;
}

void CheckInit()
{
    async_memcpy_config_t cfg = ASYNC_MEMCPY_DEFAULT_CONFIG();
    if (esp_async_memcpy_install(&cfg, &g_mcp) != ESP_OK) g_mcp = nullptr;
    g_chunk = (uint8_t*)heap_caps_aligned_alloc(64, kChunk, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    g_chunkDone = xSemaphoreCreateBinary();
    Serial.printf("[m3-bench] self-check: async memcpy %s, chunk %u B internal %s, every %lu pushes%s\n",
                  g_mcp ? "ok" : "FAILED", (unsigned)kChunk, g_chunk ? "ok" : "FAILED", (unsigned long)kCheckEvery,
#if defined(M3_SELFCHECK_PLANT)
                  "; PLANT: the checked frames skip the cache write-back"
#else
                  ""
#endif
    );
}

void Check(const uint8_t* frame)
{
    if (!g_mcp || !g_chunk || !g_chunkDone) { g_checkErr++; return; }
    bool bad = false;
    for (size_t off = 0; off < kBytes; off += kChunk) {
        const size_t n = (kBytes - off < kChunk) ? kBytes - off : kChunk;   // the last chunk is 2,512 B
        if (esp_async_memcpy(g_mcp, g_chunk, (void*)(frame + off), n, OnChunkDone, g_chunkDone) != ESP_OK ||
            xSemaphoreTake(g_chunkDone, pdMS_TO_TICKS(50)) != pdTRUE) {
            g_checkErr++;
            return;
        }
        if (memcmp(g_chunk, frame + off, n) != 0) { g_badChunks++; bad = true; }
    }
    g_checks++;
    if (bad) g_badFrames++;
}
#endif  // M3_BENCH


void Task(void*)
{
    for (;;) {
        uint8_t i;
        if (xQueueReceive(g_queue, &i, portMAX_DELAY) != pdTRUE) continue;
        uint8_t* const p = (uint8_t*)g_buf[i]->getBuffer();
        const uint32_t t0 = micros();
#if defined(M3_BENCH)
        const bool checkThis = (g_pushes + 1) % kCheckEvery == 0;   // M3-9: this push is re-read afterwards
#endif
        // Design (i): the DMA reads PSRAM directly, so the CPU's cached writes must reach PSRAM first.
#if defined(M3_BENCH) && defined(M3_SELFCHECK_PLANT)
        if (!checkThis)   // M3-9's control: the checked frames skip the write-back, and the check must catch them
#endif
        if (esp_cache_msync(p, kBytes, ESP_CACHE_MSYNC_FLAG_DIR_C2M) != ESP_OK) g_syncErr++;
        const uint32_t t1 = micros();
        tft.startWrite();
        tft.pushImageDMA(0, 0, SCREEN_SIZE, SCREEN_SIZE, (const lgfx::swap565_t*)p);
        // Bus_SPI::wait() busy-polls, so wait for the last segment by yielding instead of spinning.
        while (tft.dmaBusy()) vTaskDelay(1);
        tft.endWrite();
        // THE FRAME BENCH BEGIN
#ifdef FRAME_BENCH
        // M3-3: the planted push delay, by vTaskDelay (the push task outranks the loop on its core)
        if (const uint32_t d = framebench::PushDelayMs()) vTaskDelay(pdMS_TO_TICKS(d));
#endif
        // THE FRAME BENCH END
        const uint32_t t2 = micros();
#if defined(M3_BENCH)
        if (checkThis) Check(p);   // before the buffer is freed, so nothing redraws it during the re-read
#endif
        // FrameDone moves HERE (doc, section 5): map-client's p2p is timed from it, and it must stay
        // "present to present". Left in the loop it would time the loop, not the glass.
        framebuf::SetPhase(framebuf::kPhasePushed);
        framebuf::FrameDone();
        // THE FRAME BENCH BEGIN
#ifdef FRAME_BENCH
        framebench::Present(micros());   // M3-1: p2p at the push's completion, the glass
        framebench::PushTimes(t1 - t0, t2 - t0);   // M3-3 (b): the push's own times
#endif
        // THE FRAME BENCH END
        xSemaphoreGive(g_free[i]);

        g_pushes++;
        g_syncUsSum += t1 - t0;
        g_pushUsSum += t2 - t0;
        if (t2 - t0 > g_pushUsMax) g_pushUsMax = t2 - t0;
        if (g_pushes % kReportEvery == 0) {
            const uint32_t waits = g_waits ? g_waits : 1;
            Serial.printf("[m3] pushes=%lu push_avg_us=%lu push_max_us=%lu sync_avg_us=%lu sync_err=%lu "
                          "loop_wait_avg_us=%lu loop_wait_max_us=%lu\n",
                          (unsigned long)g_pushes, (unsigned long)(g_pushUsSum / g_pushes),
                          (unsigned long)g_pushUsMax, (unsigned long)(g_syncUsSum / g_pushes),
                          (unsigned long)g_syncErr, (unsigned long)(g_waitUsSum / waits),
                          (unsigned long)g_waitUsMax);
            g_pushUsMax = 0;
            g_waitUsMax = 0;
#if defined(M3_BENCH)
            Serial.printf("[m3-bench] checks=%lu bad_frames=%lu bad_chunks=%lu check_err=%lu\n",
                          (unsigned long)g_checks, (unsigned long)g_badFrames, (unsigned long)g_badChunks,
                          (unsigned long)g_checkErr);
#endif
        }
    }
}

// Both buffers 64-byte aligned in PSRAM, so the cache write-back covers whole lines. Allocated once, at
// boot, before Wi-Fi (doc, section 3). Returns the first buffer, or nullptr if either allocation failed.
void* CreateBuffers()
{
    const size_t internalBefore = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    void* a = heap_caps_aligned_alloc(64, kBytes, MALLOC_CAP_SPIRAM);
    void* b = heap_caps_aligned_alloc(64, kBytes, MALLOC_CAP_SPIRAM);
    if (!a || !b) {
        Serial.printf("[m3] buffers: ALLOC FAILED a=%p b=%p\n", a, b);
        return nullptr;
    }
    memset(a, 0, kBytes);
    memset(b, 0, kBytes);
    backbuffer.setColorDepth(16);
    backbuffer2.setColorDepth(16);
    backbuffer.setBuffer(a, SCREEN_SIZE, SCREEN_SIZE, 16);
    backbuffer2.setBuffer(b, SCREEN_SIZE, SCREEN_SIZE, 16);
    g_free[0] = xSemaphoreCreateBinary();
    g_free[1] = xSemaphoreCreateBinary();
    g_queue = xQueueCreate(1, sizeof(uint8_t));
    xSemaphoreGive(g_free[0]);
    xSemaphoreGive(g_free[1]);
#if defined(M3_BENCH)
    CheckInit();   // M3-9's self-check: its DMA channel and 4 KB internal buffer, before the task can push
#endif
    // One priority above the loop, on the loop's core: it mostly sleeps in the DMA's yields, and its
    // short bursts preempt the draw rather than Wi-Fi on core 0 (M3-5 measures the effect).
    const UBaseType_t prio = uxTaskPriorityGet(nullptr) + 1;
    const BaseType_t ok = xTaskCreatePinnedToCore(Task, "m3push", 4096, nullptr, prio, nullptr, xPortGetCoreID());
    const size_t internalAfter = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    Serial.printf("[m3] buffers: a=%p b=%p aligned64=%d bytes=%u each; task=%s core=%d prio=%u; "
                  "internal free %u -> %u (%d B)\n",
                  a, b, (int)(((uintptr_t)a % 64 == 0) && ((uintptr_t)b % 64 == 0)), (unsigned)kBytes,
                  ok == pdPASS ? "ok" : "FAILED", (int)xPortGetCoreID(), (unsigned)prio,
                  (unsigned)internalBefore, (unsigned)internalAfter,
                  (int)internalAfter - (int)internalBefore);
    return (ok == pdPASS && g_free[0] && g_free[1] && g_queue) ? a : nullptr;
}

// The loop takes the buffer it is about to draw into: it waits only if that buffer's push is still
// running (or /diag/fb is copying it).
LGFX_Sprite& TakeForDrawing()
{
    const uint32_t t0 = micros();
    xSemaphoreTake(g_free[g_draw], portMAX_DELAY);
    const uint32_t w = micros() - t0;
    g_waits = g_waits + 1;
    g_waitUsSum = g_waitUsSum + w;
    if (w > g_waitUsMax) g_waitUsMax = w;
    return *g_buf[g_draw];
}

// The loop hands the finished frame to the push task and moves to the other buffer.
void HandOver()
{
    const uint8_t i = (uint8_t)g_draw;
    g_complete = i;
    framebuf::Register(g_buf[i]);
    xQueueSend(g_queue, &i, portMAX_DELAY);
    g_draw ^= 1;
}

// Before the loop draws straight to the panel (the two restart screens): wait out any push in flight
// and keep both buffers, so nothing else touches the SPI bus. Bounded, so a stuck push cannot hang a
// restart: the restart proceeds either way.
void Quiesce()
{
    xSemaphoreTake(g_free[0], pdMS_TO_TICKS(200));
    xSemaphoreTake(g_free[1], pdMS_TO_TICKS(200));
}
}  // namespace pushtask

namespace framebuf {
LGFX_Sprite* AcquireComplete(uint32_t timeoutMs)
{
    const int i = pushtask::g_complete;
    if (i < 0) return nullptr;
    if (xSemaphoreTake(pushtask::g_free[i], pdMS_TO_TICKS(timeoutMs)) != pdTRUE) return nullptr;
    return pushtask::g_buf[i];
}
void ReleaseComplete(LGFX_Sprite* sprite)
{
    for (int i = 0; i < 2; ++i)
        if (pushtask::g_buf[i] == sprite) xSemaphoreGive(pushtask::g_free[i]);
}
}  // namespace framebuf
#endif  // BLIPSCOPE_PUSH_TASK

#if defined(M3_BENCH)
// ---- M3-2's GLASS TEST FRAME (bench only; docs/m3-push-task.md, section 4) -----------------------------------------
// Drawn over the radar every frame. The radar is still drawn under it, so the frame costs what it costs. Every row
// carries a distinct colour (the row number encoded into RGB565), a frame counter sits in 8 horizontal bands, and a
// white bar moves 4 px a frame. A stale band shows as a band whose counter is 2+ frames older than its neighbours;
// tearing as two counters in one frame; shear as a broken bar. Env -m3bench-base runs the same frame on today's push.
namespace m3bench {
// The test frame stops 24 rows short of the bottom, so the radar's own screen-indicator dots (DrawScreenIndicator,
// y = SCREEN_SIZE - 16) stay visible. map-client's frame-trust test reads those dots as proof of a finished frame, so
// the M3-1b plant below is graded in this same image (the orchestrator, 2026-11-12).
constexpr int kTestRows = SCREEN_SIZE - 24;
uint32_t g_frame = 0;
void DrawTestFrame(BandCanvas& c)
{
    ++g_frame;
    for (int y = 0; y < kTestRows; ++y)   // low 5 bits of y in red, high 3 in green: no two rows alike
        c.fillRect(0, y, SCREEN_SIZE, 1, lgfx::color565((y & 31) << 3, ((y >> 5) << 5) | 0x10, 0x40));
    c.fillRect((int)((g_frame * 4) % SCREEN_SIZE), 0, 8, kTestRows, lgfx::color565(255, 255, 255));
    char num[12];
    snprintf(num, sizeof(num), "%lu", (unsigned long)g_frame);
    c.setTextSize(2);
    c.setTextColor(lgfx::color565(255, 255, 255));
    const int band = kTestRows / 8;
    const int tw = (int)c.textWidth(num);
    for (int b = 0; b < 8; ++b) {
        const int y = b * band + (band - 16) / 2;
        c.fillRect((SCREEN_SIZE - tw) / 2 - 3, y - 2, tw + 6, 20, lgfx::color565(0, 0, 0));
        c.drawString(num, (SCREEN_SIZE - tw) / 2, y);
    }
}

// M3-1b's PLANT: one grab of the DRAW buffer taken right after the frame-start clear, before the sweep and the
// radar's Draw. So it has no screen-indicator dots. /diag/fb-middraw serves it labelled as a finished frame, and
// map-client's frame-trust test must still read it UNTRUSTWORTHY from its pixels.
volatile bool g_grabWanted = false;
volatile bool g_grabReady = false;
uint8_t* g_grab = nullptr;
void MaybeGrabMidDraw(LGFX_Sprite& s)
{
    if (!g_grabWanted || g_grabReady) return;
    const size_t n = (size_t)s.bufferLength();
    if (!g_grab) g_grab = (uint8_t*)heap_caps_malloc(n, MALLOC_CAP_SPIRAM);
    if (g_grab) memcpy(g_grab, s.getBuffer(), n);
    g_grabReady = (g_grab != nullptr);
}
const uint8_t* RequestMidDrawGrab(uint32_t timeoutMs)
{
    g_grabReady = false;
    g_grabWanted = true;
    const uint32_t t0 = millis();
    while (!g_grabReady && millis() - t0 < timeoutMs) vTaskDelay(1);
    g_grabWanted = false;
    return g_grabReady ? g_grab : nullptr;
}
}  // namespace m3bench
#endif

#ifdef FEATURE_WEATHERSCOPE
// DiagText.h's storage (docs/weatherscope-stage3.md A9.42): diag only, Weatherscope its one user. The route asks, the
// loop renders and publishes on its own task, the route copies. The atomics order the copy before the number.
namespace {
std::atomic<uint32_t> g_dtRequested{0};
std::atomic<uint32_t> g_dtPublished{0};
char*                 g_dtBuf = nullptr;
size_t                g_dtLen = 0;
}  // namespace
namespace diagtext {
uint32_t Request() { return g_dtRequested.fetch_add(1) + 1; }
uint32_t Pending()
{
  const uint32_t r = g_dtRequested.load();
  return r != g_dtPublished.load() ? r : 0;
}
void Publish(uint32_t n, const char* text, size_t len)
{
  if (!g_dtBuf) g_dtBuf = (char*)heap_caps_malloc(kCapacity, MALLOC_CAP_SPIRAM);
  if (!g_dtBuf) return;
  if (len > kCapacity) len = kCapacity;
  memcpy(g_dtBuf, text, len);
  g_dtLen = len;
  g_dtPublished.store(n);
}
const char* Answer(uint32_t n, size_t* len)
{
  if (g_dtPublished.load() != n) return nullptr;
  *len = g_dtLen;
  return g_dtBuf;
}
}  // namespace diagtext
#endif

WiFiManager wm;
ConfigurationWebServer configServer;
HttpRequestManager http;
OpenSkyAuthTokenHandler authHandler(http);

#if defined(FEATURE_EAM)
EamManager appManager(configServer, authHandler, http, tft);
#elif defined(FEATURE_SPACE)
SpaceManager appManager(configServer, authHandler, http, tft);
#elif defined(FEATURE_SEISMIC)
SeismicManager appManager(configServer, authHandler, http, tft);
#elif defined(FEATURE_BIRDING)
BirdingManager appManager(configServer, authHandler, http, tft);
#elif defined(FEATURE_FISHING)
FishingManager appManager(configServer, authHandler, http, tft);
#elif defined(FEATURE_CLAUDESCOPE)
ClaudescopeManager appManager(configServer, authHandler, http, tft);
#elif defined(FEATURE_SPEED)
SpeedManager appManager(configServer, authHandler, http, tft);
#elif defined(FEATURE_WEATHERSCOPE)
WeatherscopeManager appManager(configServer, authHandler, http, tft);
#else
AircraftManager appManager(configServer, authHandler, http, tft);
// The Aviation radar is the #else fallthrough, so "which edition is this?" has no
// positive symbol -- every other edition has one and the radar is defined by the
// absence of all of them. Anything below that needs a RADAR-ONLY member of
// appManager must therefore key on this marker rather than on a
// !defined(A) && !defined(B) && ... chain, which silently rots: a new edition
// added above would satisfy the chain and only fail at the call site, which is
// exactly how this was found (EamManager has no NeedsReverify).
#define BLIPSCOPE_RADAR_EDITION 1
#endif

// The offset the quiet-hour schedule runs on: explicit `tz-offset`, else the
// nominal zone from longitude (include/LocalOffset.h). ONE function, called by
// both the boot print and the firing decision -- see the note at the call site.
//
// Read from config rather than from `appManager` because that is a different
// class per edition; the LIMITATION that the siblings key their offset
// differently is unchanged and recorded in docs/v11-quiet-hour-plan.md.
static long QuietHourOffsetSec()
{
  const String tz  = configServer.GetStoredString("tz-offset");
  const String lon = configServer.GetStoredString("longitude");
  return localoffset::Resolve(tz.c_str(), lon.toFloat());
}

void setup()
{
  Serial.begin(115200); // non-blocking; the wait for a CDC *host* happens after the splash below

#if ARDUINO_USB_CDC_ON_BOOT
  // Never let a Serial write stall the loop. On native USB-CDC (every SKU but the
  // s3-21, which is on UART0) HWCDC::write BLOCKS when the port is enumerated but
  // nothing is draining it -- a device plugged into a computer with no terminal
  // open. isPlugged() stays true (the SOF watchdog still sees the host), so the
  // driver falls back to a bounded wait of max_consec_timeouts(20) x
  // tx_timeout_ms(100) = UP TO 2 s PER write() CALL. RecordFrameUs' [health]
  // report emits two printfs every 30 s, so the loop froze for ~2-4 s every 30 s:
  // the sweep stopped and touch -- polled once per pass -- went dead, which reads
  // exactly as "slow to open and close cards". Diagnosed 2026-07-31 on the bench
  // s3-128 after its serial capture had been detached for 10 days.
  //
  // 2 ms caps the worst case at ~40 ms (under one frame) while still giving the
  // ring a beat to drain under an attached monitor, so the soak ledger keeps its
  // lines. 0 would be strictly non-blocking but drops bytes the moment the ring
  // fills, and these boards are our measurement instrument.
  // ...and give it room so the bound is rarely reached in the first place. The
  // ring defaults to 256 B, which a boot burst (WiFiManager's DEV-level log)
  // overruns in a few lines -- at a 2 ms timeout the overflow is DROPPED, and it
  // ate a diagnostic line outright, leaving mangled half-merged output in the
  // ledger. 4 KB absorbs the burst, so a host that is draining loses nothing and
  // the timeout only ever bites when nothing is reading at all.
#if ARDUINO_USB_MODE
  // HWCDC (USB-Serial/JTAG) only. Under ARDUINO_USB_MODE=0 (TinyUSB, the
  // missileer-s3-128 USB keyboard build) Serial is USBCDC, whose TX buffer is
  // TinyUSB's compile-time CFG_TUD_CDC_TX_BUFSIZE and cannot be resized here --
  // so a boot burst can still be dropped on that build when nothing is reading.
  Serial.setTxBufferSize(4096);
  // ...and 0, not 2 (docs/weatherscope-stage3.md A9.42, 2026-10-26). 2 ms still
  // cost ~40 ms PER CALL (20 retries x 2 ms) once the ring was full, and a PC
  // with no serial window fills it: HWCDC's `connected` is set by the host's
  // driver, not by an open window, so write() waits on a port nobody reads.
  // Weatherscope's zoom took ~1 s after its ring closed (14-17 calls); the
  // radar's 30 s health tick pays 3-12 calls, unseen by the frame stats it
  // prints. With nobody reading, the wait bought nothing -- the write ended
  // short either way. 0 never waits (the lock and the ring are both tried
  // once). The price, where a reader IS attached: a line can be dropped when
  // the ring is full or another task holds the TX lock at that instant --
  // the 07-31 concern above, now paid only in those two cases. Bench dumps
  // that must arrive whole pace themselves (Weatherscope's 'R').
  Serial.setTxTimeoutMs(0);
#else
  // USBCDC (TinyUSB) has no such stall: with no application open (DTR low)
  // write() returns 0 at once, and with one attached it bounds itself to the
  // timeout. Kept at 2 ms (A9.42).
  Serial.setTxTimeoutMs(2);
#endif
#endif

  // FIRST LINE OUT, before any subsystem can fail and before anything else can
  // scroll it away: what this binary actually is. See BuildIdentity.h -- a
  // wrong-env flash presents as a hardware or upstream fault, and the only cheap
  // defence is that the device says its own name unprompted.
  BuildIdentity::PrintBanner();
  // ...and which half of the flash it woke up in. Same reasoning as the banner: after an
  // OTA the device is the only thing that can tell you the update stuck rather than being
  // rolled back, and it must say so unprompted.
  LogOtaSlot("boot");
  Serial.printf("[boot] reset reason=%s\n", ResetReasonName());
#ifdef BLIPSCOPE_JOIN_DIAG
  // Bench diagnostic build. The cycle counter is the ONLY persistent state it
  // keeps, and it exists because RAM does not survive the reboot-retry: without
  // it every cycle shows ~2 minutes elapsed and a twelve-minute run is
  // indistinguishable from a thirty-second one, which is how this started.
  {
    const uint16_t cyc = joindiag::BumpCycle();
    Serial.printf("[jdiag] ** WIFI JOIN DIAGNOSTIC BUILD -- NEVER SHIPS ** cycle=%u\n", cyc);
  }
#endif
  netwatch::Begin(); // prints the reachability ladder, and why this boot happened
  // Bench overrides, announced together and LOUDLY. Not "is the number
  // different" but "am I looking at a bench build?" -- a capture read weeks
  // later must not mistake a permanently-dim panel or a minutes-long reboot cap
  // for a shipping defect.
  //
  // `if constexpr` rather than a runtime test: the discarded branch is never
  // instantiated, so these strings cannot reach a shipping image by an
  // optimiser's choice. The release check greps the ELF to confirm it.
  if constexpr (nightoverride::FORCE_NIGHT || REBOOT_CAP_IS_OVERRIDE) {
    Serial.printf("[bench] ** BENCH OVERRIDE **  force-night=%d  reboot-cap=%lus\n",
                  (int)nightoverride::FORCE_NIGHT,
                  (unsigned long)REBOOT_MIN_INTERVAL_S);
  }
  // The quiet-hour schedule, printed at boot for the same reason the ladder is:
  // otherwise it is invisible until 03:00 local, and "it did not fire" cannot be
  // told from "it is configured for a different hour" without waiting a day to
  // find out. Prints the offset it resolved, because a wrong tz-offset is the
  // single most likely way for this to fire at the wrong time.
  {
    const String tz = configServer.GetStoredString("tz-offset");
    const long   tzSec = QuietHourOffsetSec();
    const uint32_t nowEpoch = (uint32_t)time(nullptr);
    Serial.printf("[quiet] armed: reboot at local %02d:00%s, tz-offset=%s (%+ld s)"
                  " -- local hour now %d%s\n",
                  quiet::QUIET_HOUR,
                  // LOUD, because a capture read weeks later must not mistake a
                  // bench hour for the shipping one.
                  quiet::QUIET_HOUR_IS_OVERRIDE ? "  ** BENCH OVERRIDE **" : "",
                  // THREE STATES, NOT TWO. "unset" means the key is ABSENT,
                  // which is how "auto" is represented and is what makes the
                  // longitude fallback run. "empty" means the key is PRESENT and
                  // blank -- it resolves identically, so it is invisible in
                  // behaviour, and it is a state the config page must never
                  // produce. Collapsing them would leave the config fix's whole
                  // invariant unobservable from a capture.
                  tz.isEmpty() ? (configServer.HasStoredKey("tz-offset") ? "empty" : "unset")
                               : tz.c_str(), tzSec,
                  nowEpoch < quiet::CLOCK_SANE_EPOCH ? -1
                      : quiet::LocalHour(nowEpoch, tzSec),
                  nowEpoch < quiet::CLOCK_SANE_EPOCH
                      ? " (clock unsynced -- falling back to the 24 h timer)" : "");
  }

  // Give the Task Watchdog headroom over a single synchronous network call. The OpenSky
  // and adsbdb fetches run TLS handshakes that take the lwIP core lock and don't yield;
  // on the single-core C3 that can keep the watchdog-fed async_tcp service task from
  // running for several seconds. The IDF default TWDT is 5 s -- the same order as a slow
  // handshake -- so a legitimately slow (but still progressing) fetch tripped it and
  // rebooted the board (async_tcp / osky_fetch watchdog abort). 10 s clears the worst-case
  // connect (bounded to 3 s in HttpRequestManager) plus margin, while a genuine hang still
  // reboots. Reconfigure, not init: IDF startup already armed the TWDT.
  esp_task_wdt_config_t wdtConfig = {
    .timeout_ms = 10000,
    .idle_core_mask = (1 << 0), // CPU0 idle task -- matches the single-core Arduino default
    .trigger_panic = true,      // still reboot on a real hang
  };
  esp_task_wdt_reconfigure(&wdtConfig);

  // Board-specific bring-up that has to happen before the display. On SKUs whose panel/touch
  // reset and chip-select hang off an I2C IO expander (the S3-2.1), this drives that expander
  // (and the IMU); on the C3 it's a no-op. See variant::BoardPreInit().
  // BEFORE ANYTHING NETWORKED. mbedTLS frees with whatever allocator is
  // installed at free() time, so this has to be in place before the first TLS
  // context exists or an internal pointer could reach the PSRAM path. Nothing
  // above this line opens a socket.
  tlsalloc::Install();

  variant::BoardPreInit();

  // initialise LGFX + screen. init() returns false if the panel/bus didn't come up (on an
  // RGB panel that means the framebuffer/bus init failed -> nothing scans out).
  const bool panelOk = tft.init();
  tft.invertDisplay(BLIPSCOPE_DISP_INVERT); // per-variant: the GC9A01 boots inverted, the ST7701 doesn't
  // Drive the backlight via PWM (configured in LGFX.h) so it's dimmable.
  //
  // BEFORE FIRST LIGHT, AND THAT IS THE WHOLE POINT. This used to be a hardcoded
  // 255 with a comment saying "full brightness for the boot screen until
  // AircraftManager applies the saved level" -- accurate, and a bright flash in a
  // dark room every time the board reboots. Night dim is DERIVED (configured
  // level / 5, re-evaluated on a 20 s cadence), so nothing persisted what the
  // panel was actually showing and the first splash always painted at full.
  //
  // The quiet-hour reboot makes that a nightly event at 03:00, so the applied
  // level is now carried across the reboot and applied HERE -- above the first
  // DrawSplash, not after it, because a correction that arrives on the next dim
  // pass is a flash that already happened.
  const uint8_t carried = brightcarry::Recall();
  brightlog::Applied(carried ? "first-light/carry" : "first-light/default",
                     carried ? carried : 255);
  tft.setBrightness(carried ? carried : 255);

  // The full-frame backbuffer only fits on boards with PSRAM (480x480x8bpp ~= 230 KB); banded
  // SKUs keep it in internal RAM so a TLS handshake still has contiguous heap. setPsram() must
  // precede createSprite().
  // COLOUR DEPTH. 16bpp (RGB565) on PSRAM boards, 8bpp only where the buffer has
  // to live in internal RAM.
  //
  // 8bpp in LovyanGFX is RGB332: 8 levels of red, 8 of green, FOUR of blue --
  // 256 colours. That is close to the worst possible palette for the one thing
  // this display does most, which is show a photograph of an aeroplane against
  // sky: blues posterize, gradients band, and everything takes on a cast. The
  // depth was chosen when the backbuffer had to fit the C3's internal heap; on a
  // PSRAM board it was buying nothing and costing the picture.
  //
  // Measured on the bench s3-128 (src/probe/BlitProbe.cpp) before changing it:
  //   panel push       23.242 -> 25.728 ms   (+11%, NOT +100% -- the SPI wire is
  //                                           already RGB565, so 8bpp was paying
  //                                           a per-frame conversion instead)
  //   240x240 blit      5.369 ->  6.228 ms   (+16%; 16bpp is a straight copy)
  //   full card frame  29.838 -> 33.214 ms   (+11%, against a 60 ms budget)
  //   PSRAM            +115,200 B total      (1.4% of free)
  //   internal heap    unchanged, tlsOk unchanged
  if constexpr (!variant::BANDED_RENDER) {
    backbuffer.setPsram(true);
    backbuffer.setColorDepth(16);
  } else {
    backbuffer.setColorDepth(8);
  }
#if defined(BLIPSCOPE_PUSH_TASK)
  // M3: both backbuffers, 64-byte aligned in PSRAM, plus the push task; allocated here, once, before Wi-Fi.
  void* spriteBuf = pushtask::CreateBuffers();
#else
  void* spriteBuf = backbuffer.createSprite(SCREEN_SIZE, BAND_H);
#endif
  // Published only once it exists; the handler checks for nullptr, so a
  // failed allocation is a 503 rather than a crash.
  if (spriteBuf) framebuf::Register(&backbuffer);
#if defined(M3_BENCH)
  Serial.println("[m3-bench] glass test frame over the radar (docs/m3-push-task.md, M3-2)");
#endif

  // FIRST PIXELS EVER DRAWN. Everything above this point is panel/bus bring-up, so this
  // is the earliest the wordmark can physically appear -- ~100-150 ms after reset, and
  // ahead of the up-to-3 s USB-CDC wait that used to run before the display came up at
  // all (that wait is now below, spent behind the splash instead of a dark screen).
  // Same wordmark in every mode and on every SKU: it is the brand, not a data-source
  // credit. It STAYS on screen from here until the app's first frame -- the boot
  // messages below render as status lines underneath it. See SplashScreen.h.
  DrawSplash(tft, backbuffer);

  // Now wait (up to 3 s) for a USB CDC host to open the port, so a bench capture still
  // catches the boot log below. On a wall wart no host ever appears and this runs in
  // full -- which is exactly why it must come after the splash, not before it.
  while (!Serial && millis() < 3000) { delay(10); }

  Serial.printf("[disp] tft.init=%d %dx%d  backbuffer %s; psram_free=%u heap_free=%u\n",
                panelOk, (int)tft.width(), (int)tft.height(), spriteBuf ? "ok" : "ALLOC FAILED",
                (unsigned)ESP.getFreePsram(), (unsigned)ESP.getFreeHeap());

#ifdef BLIPSCOPE_BRINGUP_DIAG
  // Bring-up only (behind a per-env flag): cycle full-screen colours straight to the panel so we
  // can SEE whether direct draws reach the scanned framebuffer. If the screen flashes
  // red/green/blue/white, the RGB path works and the problem is elsewhere; if it stays black,
  // our pixels aren't reaching the framebuffer the bus scans out. Remove once the panel is up.
  {
    const uint32_t colors[] = { 0xFF0000, 0x00FF00, 0x0000FF, 0xFFFFFF, 0x000000 };
    const char* names[]     = { "RED", "GREEN", "BLUE", "WHITE", "BLACK" };
    for (int pass = 0; pass < 3; ++pass) {
      for (int c = 0; c < 5; ++c) {
        tft.fillScreen(lgfx::color888((colors[c] >> 16) & 0xFF, (colors[c] >> 8) & 0xFF, colors[c] & 0xFF));
        board::DisplayFlush(tft); // push the fill out of cache so the RGB DMA scans it
        Serial.printf("[diag] fillScreen %-5s  tft.init=%d %dx%d psram_free=%u\n",
                      names[c], panelOk, (int)tft.width(), (int)tft.height(), (unsigned)ESP.getFreePsram());
        Serial.flush();
        delay(700);
      }
    }
  }
#endif

  // NOTE: the "Connecting to Wi-Fi..." status is NOT painted here -- it now sits after the
  // touch-to-forget window, immediately before the join actually starts. It used to be here,
  // which was fine while the boot window sampled silently for 1.2 s and this line stayed up
  // underneath it. Once that window grew an on-screen prompt, this paint was overwritten a
  // few milliseconds later: a green flash between the wordmark and TOUCH & HOLD, and for the
  // three seconds that followed the device claimed to be connecting while it was doing
  // nothing of the kind. Status lines say what is happening now, or they are decoration.

#if defined(BLIPSCOPE_PANEL_SPD2010)
  // Critical ordering for the 1.46B: the Wi-Fi radio must NOT come up in the first seconds after
  // power-on or its bring-up glitches the QSPI panel and it never recovers (writes stop landing)
  // until a cold reboot -- a power/clock stabilisation issue, bisected on hardware (WiFi at ~7 s
  // still fails; ~10 s is reliable). Hold the boot screen here so the rails settle before the radio
  // inrush; the radar renders normally afterwards.
  //
  // This hold gets its own status rather than inheriting the connect line it used to sit under:
  // nine seconds is far too long to spend claiming to connect before the join has been asked for.
  DrawSplash(tft, backbuffer, "Starting up...");
  delay(9000);
#endif

  // Log every WiFi radio event so we can see exactly where a join fails.
  // These fire on the WiFi event task even while autoConnect() blocks below.
  WiFi.onEvent([](arduino_event_id_t event, arduino_event_info_t info) {
    switch (event) {
      case ARDUINO_EVENT_WIFI_STA_START:
        // Re-apply the hostname the instant the STA interface starts -- before the DHCP
        // request goes out -- so it's sent as DHCP option 12 and the router registers it.
        // That's what lets Angry IP Scanner (and the router's device list) resolve the
        // name; WiFiManager sets it too, but its mode-cycling applies it after DHCP, too
        // late. This mirrors MiniSpeedCam's working mode->setHostname->begin ordering.
        WiFi.setHostname(DeviceIdentity::Name().c_str());
        // Full TX power (chip default) for best range. Both setters require a started STA,
        // which is exactly what this event signals.
        WiFi.setTxPower(WIFI_POWER_19_5dBm);
        Serial.printf("[WiFi] STA started; hostname=%s, TX 19.5dBm\n", DeviceIdentity::Name().c_str());
        break;
      case ARDUINO_EVENT_WIFI_STA_CONNECTED:
        Serial.printf("[WiFi] Associated with \"%s\", waiting for IP...\n", WiFi.SSID().c_str());
#ifdef BLIPSCOPE_JOIN_DIAG
        joindiag::RecordAssociated(WiFi.SSID().c_str()); // POD write only
#endif
        break;
      case ARDUINO_EVENT_WIFI_STA_GOT_IP:
        Serial.printf("[WiFi] CONNECTED  IP=%s  RSSI=%d dBm\n",
                      WiFi.localIP().toString().c_str(), WiFi.RSSI());
#ifdef BLIPSCOPE_JOIN_DIAG
        joindiag::RecordGotIp(); // POD write only
#endif
        break;
      case ARDUINO_EVENT_WIFI_STA_DISCONNECTED: {
        const auto reason = (wifi_err_reason_t)info.wifi_sta_disconnected.reason;
        Serial.printf("[WiFi] DISCONNECTED  reason=%d (%s)\n",
                      reason, WiFi.disconnectReasonName(reason));
        // RECORD ONLY -- one byte, no SPI, no allocation. The loop task turns
        // this into a sentence on the glass after a failed portal provisioning;
        // see JoinFailure.h for why the screen and not the setup page.
        joinfail::RecordReason((uint8_t)reason);
#ifdef BLIPSCOPE_JOIN_DIAG
        // RECORD ONLY. Rendering from here would put a blocking SPI write on the
        // WiFi event task and perturb the timing this exists to measure.
        joindiag::RecordDisconnect((uint8_t)reason,
                                   WiFi.disconnectReasonName(reason), millis());
        // THE SSID MUST BE CAPTURED ON FAILURE, NOT ON ASSOCIATION. It was only
        // recorded in STA_CONNECTED, which never fires when auth fails -- so the
        // screen read "SSID (not set)" on exactly the boards this instrument
        // exists for.
        //
        // AND NOT FROM WiFi.SSID() EITHER: that reports the ASSOCIATED AP, so it
        // is empty for precisely the failures being diagnosed. Tried it, watched
        // it still print "(none)" against four real AUTH_FAILs.
        //
        // The disconnect EVENT carries the SSID it was attempting, which is the
        // authoritative source and is already in hand here.
        {
          const auto& d = info.wifi_sta_disconnected;
          char attempted[33];
          const size_t n = d.ssid_len < 32 ? d.ssid_len : 32;
          memcpy(attempted, d.ssid, n);
          attempted[n] = 0;
          joindiag::RecordTargetSsid(attempted);
        }
#endif
        if (reason == WIFI_REASON_NO_AP_FOUND)
          Serial.println("       SSID not found: check spelling/range. The ESP32-C3 is 2.4GHz-only and cannot see 5GHz networks.");
        else if (reason == WIFI_REASON_AUTH_FAIL || reason == WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT ||
                 reason == WIFI_REASON_AUTH_EXPIRE)
          Serial.println("       Auth/key mismatch: most likely a wrong password.");
        else if (reason == WIFI_REASON_HANDSHAKE_TIMEOUT)
          Serial.println("       Handshake frames lost (not a key mismatch): RF/power instability or weak signal. Try lower TX power / better supply.");
        break;
      }
      default:
        break;
    }
  });

  WiFiManagerHelpers::ConfigureWiFiManager(wm, tft, backbuffer);

  // Touch-to-forget, checked BEFORE any join attempt so it works even when the
  // saved credentials would hang the boot. Costs 3 s on an ordinary boot; see
  // BootTouchToForget for why the window is that shape (the touch has to ARRIVE
  // after tft.init(), because tft.init() erases one that was already there).
  //
  // STILL A PRESS-AND-HOLD, AND SCHEDULED FOR REMOVAL. The CST816D may report
  // no change interrupt under a static contact, which is what COM119 is
  // currently measuring; this gesture is on the wrong side of that. It stays
  // ONLY because it is presently the sole recovery path when touch works but
  // the network is unreachable, and deleting it before the five-power-cycle
  // failsafe is proven on hardware would leave that gap open. Removal is gated
  // on that evidence, not on the replacement merely being written.
  if (WiFiManagerHelpers::BootTouchToForget(tft, backbuffer)) {
    factoryreset::Perform(factoryreset::Tier::Wifi, [&wm]() { wm.resetSettings(); });
    DrawSplash(tft, backbuffer, "Wi-Fi cleared", "Restarting into setup...");
    delay(1200);
    ESP.restart();
  }

  // NOW say we are connecting, because from the next line on we are. Composed through the
  // backbuffer so it renders on the SPD2010 (which can't take direct per-glyph writes); a
  // no-op-different path on every other SKU. See BootScreen.h. Status renders BENEATH the
  // wordmark, which stays exactly where it was first painted -- so this reads as a line
  // changing under a fixed brand mark, not as the splash being replaced by a different
  // screen. DrawSplash flushes for RGB panels itself.
  DrawSplash(tft, backbuffer, "Connecting to Wi-Fi...");

  // Aim straight at the last known-good AP before doing this the slow way. On a
  // mesh SSID that turns a multi-scan, multi-minute join into a ~1-2 s one; it
  // silently falls through to autoConnect() whenever it can't (first boot, moved
  // device, that node gone). See WiFiManagerHelpers for the full rationale.
  bool connected = WiFiManagerHelpers::TryFastJoin();
  if (!connected) {
    connected = wm.autoConnect(WiFiManagerHelpers::WiFiManagerName().c_str());
    // The portal is over: stop the setup screen's watcher BEFORE anything else draws.
    WiFiManagerHelpers::StopSetupWatcher();
    Serial.printf("[WiFi] autoConnect() returned %s\n",
                  connected ? "true (connected)" : "false (portal timed out / not connected)");
    if (!connected) {
      // The portal timed out with nobody using it (see setConfigPortalTimeout).
      // Reboot rather than run on without a network: that retries the SAVED
      // credentials from the top, which is the whole point -- it is what lets a
      // unit that came up before its router heal itself a few minutes later
      // instead of parking in setup mode until a human intervenes.
      //
      // COUNT IT FIRST. The counter is what stretches the portal on later boots
      // for a device that has MOVED (PortalTimeoutPolicy.h), and it has to be
      // written before the restart it is counting -- a count kept in RAM would
      // reset on exactly the event it exists to measure.
      WiFiManagerHelpers::RecordJoinOutcome(false);
      Serial.println("[WiFi] no network and nobody at the portal -- restarting to retry saved credentials");

      // SAY WHY, BUT ONLY TO SOMEONE WHO IS STANDING THERE.
      //
      // The generic "No Wi-Fi / Retrying..." is the same nine words for a typo,
      // an out-of-range network and a failing antenna -- which is how a customer
      // typing the wrong password produced a two-day hardware investigation.
      // The reason was diagnosed correctly within seconds and printed only to
      // serial. See JoinFailure.h.
      //
      // GATED ON PortalProvisioned(), and that is the load-bearing part. It is
      // true only when a human has just typed a password into the portal THIS
      // boot. On the far commoner path -- a power cut, the router still coming
      // up, nobody in the room -- the saved credentials are almost certainly
      // fine, and flashing "WRONG PASSWORD" at an empty room would be both
      // false and alarming. Same reason code, opposite meaning, decided by who
      // asked.
      if (WiFiManagerHelpers::PortalProvisioned()) {
        const joinfail::Advice a = joinfail::AdviceFor(joinfail::LastCause());
        Serial.printf("[WiFi] portal provisioning FAILED: reason=%u -> \"%s\"\n",
                      (unsigned)joinfail::LastReason(), a.l0);
        // PERSIST BEFORE SHOWING. The splash below lasts 8 s and is then replaced
        // by the setup screen -- a toast timed to land while the owner is still
        // looking at their phone. Persisting it puts the same answer on the setup
        // screen they come back to, so the diagnosis outlives the moment nobody
        // was watching. Cleared by a successful join, never by a timer.
        WiFiManagerHelpers::PersistJoinFailure(joinfail::LastReason());
        DrawSplash(tft, backbuffer, a.l0, a.l1, a.l2);
        delay(8000);   // still shown, for whoever IS looking
      } else {
        DrawSplash(tft, backbuffer, "No Wi-Fi", "Retrying...");
        delay(1500);
      }
      ESP.restart();
    }
  }

  // The portal FAILING already restarted, just above. The portal SUCCEEDING has
  // to as well, and that asymmetry is the bug: WiFiManager's captive portal owns
  // port 80, does not release it before ConfigurationWebServer::Initialise() calls
  // server.begin(), and the bind fails with lwIP ERR_USE. AsyncWebServer cannot
  // report that to its caller, so the device runs perfectly while refusing every
  // connection to its own config page until someone power-cycles it.
  //
  // That is the FIRST-RUN path for every customer -- a factory-fresh unit has no
  // saved credentials, so it always arrives here. See PortalProvisioned().
  if (WiFiManagerHelpers::PortalProvisioned()) {
    Serial.println("[WiFi] provisioned via the portal -- restarting so the config server can bind :80");
    DrawSplash(tft, backbuffer, "Wi-Fi saved", "Starting up...");
    delay(1200);
    ESP.restart();
  }

  // Disable Wi-Fi modem-sleep. By default the radio sleeps between beacons and
  // wakes each DTIM to listen, pulsing the supply current at the beacon rate.
  // On this board that periodic load makes the decoupling caps near the ESP32-C3
  // sing -- a faint, steady buzz. Keeping the radio always-on flattens the draw
  // and silences it. The device is USB/mains powered, so the extra ~20-30 mA is
  // a non-issue, and latency/throughput actually improve.
  if (connected) {
    // A JOIN CLEARS THE PORTAL LADDER, unconditionally and from any depth. Without
    // this a board that struggled once would keep the stretched portal forever and
    // never return to the fast router-reboot self-heal. Note it is deliberately
    // NOT folded into RememberFastAp(): that one refuses a weak link, and a weak
    // join is still a join as far as "this device is in the right house" goes.
    WiFiManagerHelpers::RecordJoinOutcome(true);

    // Remember this AP for the next boot's fast join -- but only if the link was
    // solid (a weak connect clears the hint instead; see RememberFastAp).
    WiFiManagerHelpers::RememberFastAp();

    WiFi.setSleep(WIFI_PS_NONE);

    // Show how to reach the config page so the user doesn't have to remember the device's
    // name later: it lives at http://<name>.local (mDNS), with the IP as a fallback for
    // networks where mDNS doesn't resolve. Held a few seconds before OTA/app startup.
    // Composed through the backbuffer so it renders on the SPD2010 (see BootScreen.h); the
    // host line is also available any time on the radar's Stats screen.
    const String host = DeviceIdentity::Name() + ".local";
    const String ip   = WiFi.localIP().toString();
    DrawSplash(tft, backbuffer, "- CONNECTED -", host.c_str(), ip.c_str());
    delay(4000);
  }

  // start NTP in UTC; the on-screen clock applies the configured offset, and the
  // solar auto-dim works directly in UTC
  configTime(0, 0, "pool.ntp.org");

  // self-update from the latest GitHub release before normal startup; reboots
  // into the new firmware if one is newer than this build.
  //
  // THIS IS NOW THE ONLY PATH THAT TOUCHES THE NETWORK FOR AN UPDATE. The heap is
  // clean here and nowhere else: measured 31,732 B largest block at boot against
  // 1,140 B at 48 h uptime, versus a 16,717 B TLS handshake. The loop paths below
  // arm a reboot and land here instead of failing on a fragmented heap.
  //
  // The flag is consumed BEFORE the check, so a crash inside it cannot re-arm.
  if (ConsumeDeferredCheckFlag())
    Serial.println("[ota] this boot was armed by a deferred update check");
#if defined(PHOTO_MODE)
  // PHOTO MODE HAS NO OTA (env:missileer-photo). The photo firmware must not be
  // replaced mid-shoot by whatever release is current, and it is never published, so
  // there is nothing for it to update into. Back to shipping is a USB flash.
  Serial.println("[photo-mode] OTA off: update check skipped");
#else
  MaybeUpdateFirmware(tft, backbuffer, http);
#endif

  // begin background server for configuration
  configServer.Initialise();

  // One-shot config migrations, BEFORE anything reads settings. Ordering is
  // load-bearing: run this after Initialise() and the app spends its first
  // session on the pre-migration values, which for #238 is exactly the session a
  // customer would be looking at while wondering why the OTA changed nothing.
  configmigration::Apply();

#if defined(WX_LOOK_AB)
  // Step C (docs/wx-device.md, ruled 2026-10-29), bench image only: once at boot, the copy floor. Measured here, and
  // reprinted with every 30 s [health] report, because serial writes never block (#485): a boot line printed before
  // the trace opens can be dropped.
  appManager.MeasureCopyFloor();
#endif

  // A private ntfy topic, generated once on a factory-fresh device (14.1).
  // AFTER Wi-Fi, because esp_random() wants the RF subsystem up for full
  // entropy, and BEFORE the app reads settings, so the first session already
  // has it. Only ever writes a key that has never been written -- an owner
  // who saved the form with the box cleared made a decision.
  ntfytopic::Ensure();

  // initialise the active app (radar or EAM monitor)
  appManager.Initialise();

  // Claim the reserved TLS handshake block (heap fix 4).
  //
  // TAKEN HERE, AT THE END OF setup(), AND THE PLACEMENT IS THE WHOLE TRICK.
  // Earlier would be a bigger, more contiguous heap to carve from -- and would
  // also mean competing with WiFi/TLS/display bring-up, which are the largest
  // allocations this device ever makes. Starving boot to protect a handshake
  // would trade an intermittent blank card for a device that does not come up.
  //
  // By this line every one-time allocation has been made and the heap is as
  // whole as it will ever be again. Everything after this point is the churn
  // that docs/heap-fragmentation-2026-08-17.md measured eroding ~10 KB of
  // contiguous headroom in 12 idle minutes -- so this is the last moment the
  // block is cheap.
  //
  // A failure here is not fatal and not logged as an error: the device simply
  // runs as it does today, and the health line's ball=0 says so.
  heaphealth::ReserveHandshakeBallast();
  Serial.printf("[heap] handshake ballast %s (%u B); free8=%u\n",
                heaphealth::BallastHeld() ? "reserved" : "UNAVAILABLE",
                (unsigned)heaphealth::TLS_HANDSHAKE_BYTES,
                (unsigned)heaphealth::FreeInternal8Bit());

#ifdef SOAK_TEST
  // Arm the human-scale gesture script; the normal bring-up above (WiFi, NTP,
  // config server, real cloud fetching) is exactly what the soak exercises.
  SoakHarness::Setup(appManager);
#endif
}

void loop()
{
#if !defined(FEATURE_EAM) && !defined(FEATURE_SPACE) && !defined(FEATURE_SEISMIC) && !defined(FEATURE_BIRDING) && !defined(FEATURE_FISHING) && !defined(FEATURE_CLAUDESCOPE) && !defined(FEATURE_SPEED) && !defined(FEATURE_WEATHERSCOPE)
  // Frame-time instrumentation (radar builds): the whole pass -- update, draw,
  // flush -- is one sample; AircraftManager logs avg/p95 + heap every 30 s and
  // shouts when a budget breaks. u32 micros() wrap-around subtracts correctly.
  const uint32_t frameStartUs = micros();
  // THE FRAME PROFILE BEGIN
#ifdef FRAME_PROFILE
  frameprofile::Start(frameStartUs);
#endif
  // THE FRAME PROFILE END
#endif

  // Perform a requested reset and reboot into the setup portal -- from the
  // config web page, or from the on-screen Stats reset menu (radar builds),
  // which is the path that still works when the device is off the network.
  //
  // BOTH SOURCES ARE CONSUMED, then the larger tier wins. Short-circuiting on
  // the first would leave the other's request latched to fire on the next pass,
  // which after a reboot means a second, unrequested reset.
  factoryreset::Tier tier = configServer.ConsumeResetTier();
#if !defined(FEATURE_EAM) && !defined(FEATURE_SPACE) && !defined(FEATURE_SEISMIC) && !defined(FEATURE_BIRDING) && !defined(FEATURE_FISHING) && !defined(FEATURE_CLAUDESCOPE) && !defined(FEATURE_SPEED) && !defined(FEATURE_WEATHERSCOPE)
  tier = factoryreset::Larger(tier, appManager.ConsumeResetTier());
#endif
  if (tier != factoryreset::Tier::None) {
    factoryreset::Perform(tier, [&wm]() { wm.resetSettings(); });
#if defined(BLIPSCOPE_PUSH_TASK)
    pushtask::Quiesce();   // M3: this splash writes the panel directly; no push may be in flight
#endif
    DrawSplash(tft, backbuffer,
               tier == factoryreset::Tier::Factory ? "Factory reset" : "Wi-Fi cleared",
               "Restarting into setup...");
    delay(1000); // let the HTTP response flush, and let the user read the screen
    ESP.restart();
  }

  // REACHABILITY WATCHDOG. The block below watches ASSOCIATION; this one watches
  // whether traffic actually moves, and they are not the same failure. On
  // 2026-09-03 COM16 spent 9 min 37 s failing every single request with
  // "Host is unreachable" while WiFi.status() returned WL_CONNECTED throughout --
  // so the supervisor below never armed, and the board sat dark until the daily
  // reboot. Both are kept, because neither subsumes the other: this one cannot
  // see a device that never associates, and that one cannot see a device that
  // associates and reaches nothing.
  netwatch::Tick();

  // RUNTIME WIFI WATCHDOG. Losing the network after boot recovered nowhere: the
  // IDF's auto-reconnect retries forever, setup() has already returned, and the
  // loop never looked at WiFi.status() -- so a password changed while the device
  // was running left a customer staring at a dead feed with no route forward and
  // no indication anything was wrong beyond "no data".
  //
  // Rebooting converts that into the boot-time case, which now self-heals (portal
  // timeout -> restart -> retry saved credentials) or presents the setup portal.
  //
  // 10 minutes, deliberately not less: a router reboot is 1-3 min and a router
  // firmware update can be ~5, and rebooting a perfectly healthy device over a
  // blip would be a worse bug than the one being fixed. At a 15 s poll cadence
  // this is ~40 consecutive missed polls -- not ambiguous. Only armed once we
  // have actually been connected, so it can never fight the boot path.
  {
    constexpr unsigned long WIFI_DOWN_REBOOT_MS = 10UL * 60UL * 1000UL;
    static unsigned long wifiDownSinceMs = 0;
    static bool everConnected = false;
    if (WiFi.status() == WL_CONNECTED) {
      everConnected = true;
      wifiDownSinceMs = 0;
    } else if (everConnected) {
      if (wifiDownSinceMs == 0) {
        wifiDownSinceMs = millis();
        Serial.println("[WiFi] link lost; watchdog armed (reboot in 10 min if it stays down)");
      } else if (millis() - wifiDownSinceMs >= WIFI_DOWN_REBOOT_MS) {
        Serial.println("[WiFi] down 10 min -- restarting to re-run the join/portal path");
#if defined(BLIPSCOPE_PUSH_TASK)
        pushtask::Quiesce();   // M3: this splash writes the panel directly; no push may be in flight
#endif
        DrawSplash(tft, backbuffer, "Wi-Fi lost", "Restarting...");
        delay(1500);
        ESP.restart();
      }
    }
  }

  // Re-check for firmware updates once a day for always-on devices -- by REBOOTING
  // INTO the check rather than running it here.
  //
  // Running it here is what Run 1 proved does not work. At 48 h uptime the version
  // check never reached the wire: mbedTLS asked for 16,717 contiguous bytes, the
  // largest free block was 1,140, and the whole update path ended at
  // `[ota] version check failed: HTTP -1`. An always-on device never leaves that
  // state on its own, so the daily timer was firing into a heap that could not
  // serve it -- for the life of the unit.
  //
  // The stamp is taken BEFORE the deferral so a refusal (see the reboot cap) costs
  // one attempt per day rather than one per loop iteration.
  // AT A LOCAL QUIET HOUR, not 24 h after boot. The uptime timer drifted: a board
  // that booted at 14:00 rebooted at 14:00 every day thereafter, in front of the
  // customer, for ~60 s of black screen. See include/QuietHourPolicy.h for why
  // the two classic local-time hazards -- an hour that happens twice, an hour
  // that never happens -- both arrive here as a `tz-offset` edit rather than as
  // a clock event, and how each is answered.
  //
  // The policy PROPOSES; DeferRebootWithCause DISPOSES. The reboot cap and the
  // unsynced-clock refusal stay in that one place, shared with the reachability
  // watchdog's rung 3, so there is one implementation of each rather than two.
  // The cap itself is include/RebootCap.h: 24 h for an unscheduled reboot, and
  // the quiet policy's own 20 h for this scheduled one (#377).
  //
  // NOT IN PHOTO MODE: it reboots the board to run an update check that the photo
  // build skips (above), and on an EAM build the quiet hour is 03:00 UTC -- an
  // evening in the Americas, which is when a shoot happens.
#if !defined(PHOTO_MODE)
  {
    static quiet::State qs;
    // The offset is read from config rather than from the app manager, because
    // `appManager` is a different class per edition and an accessor would mean
    // touching all eight. KNOWN LIMITATION, stated rather than hidden: the radar
    // stores its offset under "tz-offset", while the sibling editions use their
    // own keys (fi-tz-offset, cl-tz-offset, sc-tz-offset...). On those builds
    // this reads empty and the quiet hour lands at 03:00 UTC instead of 03:00
    // local -- still better than the uptime drift it replaces, but not the
    // intended behaviour.
    //
    // POST-LAUNCH BACKLOG, not an oversight: all 50 launch units are the radar
    // edition, so no customer can meet this and it is bench-only. It becomes
    // real the moment one non-radar edition ships -- a shipping decision, not a
    // date. See "Post-launch backlog" in docs/v11-quiet-hour-plan.md.
    // THE SAME RESOLVED VALUE THE BOOT PRINT REPORTS. Deliberately one helper
    // rather than two call sites that agree today: an instrument that reports a
    // different number from the one the decision uses is worse than no
    // instrument, because it reports correctly while the behaviour is wrong.
    const quiet::Decision d =
        quiet::Step(qs, (uint32_t)time(nullptr), QuietHourOffsetSec(), millis());
    if (d != quiet::Decision::None) {
      Serial.printf("[quiet] %s -> deferring update check to reboot\n",
                    quiet::DecisionName(d));
      // scheduled=true: the quiet hour is held to its own 20 h minimum, not the 24 h cap (#377).
      DeferUpdateCheckToReboot(ESP.getMaxAllocHeap(), /*scheduled=*/true); // does not return if it arms
    }
  }
#endif

  // Apply settings saved via the web UI without rebooting. Done here, on the
  // loop task, so all AircraftManager state changes stay on a single task
  // rather than racing the async web-server callback.
  if (configServer.ConsumeConfigChanged())
    appManager.Initialise();

#if !defined(FEATURE_EAM) && !defined(FEATURE_SPACE) && !defined(FEATURE_SEISMIC) && !defined(FEATURE_BIRDING) && !defined(FEATURE_FISHING) && !defined(FEATURE_CLAUDESCOPE) && !defined(FEATURE_SPEED) && !defined(FEATURE_WEATHERSCOPE)
  // Somebody opened the Collection page. Flush a dirty logbook HERE, on the loop
  // task that owns it, so the next read is current -- the page is served from
  // NVS and would otherwise lag the live book by up to the persist debounce.
  // Dirty-only and rate-limited inside MaybePersistForFetch(); this is just the
  // task hand-off.
  if (configServer.ConsumeLogbookFlushRequest())
    appManager.FlushLogbookForFetch();
#endif

  // Publish the credential state across the task boundary, so the config page
  // renders the same bit the radar's banner draws from. A plain store on the
  // loop task, read on async_tcp -- the page must never reach into appManager.
  // Radar-only: the sibling editions authenticate to their own backends (or to
  // nothing) and have no equivalent state, so they do not carry the member.
#ifdef BLIPSCOPE_RADAR_EDITION
  configServer.SetNeedsReverify(appManager.NeedsReverify());
#endif

#ifdef SOAK_TEST
  // burst scheduling + the 60 s stats line + the 24 h gate verdict
  SoakHarness::Tick(appManager);
#endif

  appManager.Update();

  // draw cycle: render the frame one horizontal band at a time into the half-height
  // backbuffer, each shifted into place by a BandCanvas, then pushed to its screen
  // rows. The scene is drawn once per band; the app advances per-frame state (animation
  // tick, trail sampling) only on the first pass so the bands stay in sync.
#if !defined(FEATURE_EAM) && !defined(FEATURE_SPACE) && !defined(FEATURE_SEISMIC) && !defined(FEATURE_BIRDING) && !defined(FEATURE_FISHING) && !defined(FEATURE_CLAUDESCOPE) && !defined(FEATURE_SPEED) && !defined(FEATURE_WEATHERSCOPE)
  const bool drawScan = appManager.SweepEnabled() && appManager.IsRadarView()
                        && !appManager.NightClockActive()  // no beam under the night clock face
                        && !appManager.SweepSuppressed();  // and none over data too old to be live

  // The sweep angle is owned by AircraftManager (advanced in Update()), so the
  // drawn beam matches the blip paint-and-fade crossing test exactly. Sampled
  // once here so both render bands derive an identical wedge (no seam).
  const float sweep = appManager.CurrentSweepAngle();
#endif

#if defined(BLIPSCOPE_PUSH_TASK)
  // M3: the buffer this frame draws into. It waits only while that buffer's previous push is still going.
  // THE FRAME PROFILE BEGIN
#ifdef FRAME_PROFILE
  frameprofile::Mark(frameprofile::B_UPD);
#endif
  // THE FRAME PROFILE END
  LGFX_Sprite& drawBuf = pushtask::TakeForDrawing();
  // THE FRAME PROFILE BEGIN
#ifdef FRAME_PROFILE
  frameprofile::Mark(frameprofile::B_WAIT);
#endif
  // THE FRAME PROFILE END
#endif
  for (int bandY = 0; bandY < SCREEN_SIZE; bandY += BAND_H) {
#if defined(BLIPSCOPE_PUSH_TASK)
    BandCanvas canvas(drawBuf, bandY);
#else
    BandCanvas canvas(backbuffer, bandY);
#endif
    const bool firstPass = (bandY == 0);

    framebuf::SetPhase(framebuf::kPhaseClearing);  // /diag/fb copies only between Drawn and this (A9.26)
#if defined(FEATURE_CLOUD_FEED) && !defined(FEATURE_EAM) && !defined(FEATURE_SPACE) && !defined(FEATURE_SEISMIC) && !defined(FEATURE_BIRDING) && !defined(FEATURE_FISHING) && !defined(FEATURE_CLAUDESCOPE) && !defined(FEATURE_SPEED) && !defined(FEATURE_WEATHERSCOPE)
    // Weather under the planes (docs/wx-device.md, WD5): on the radar scope, with a crop WxPolicy allows, the cached
    // layer replaces the clear, then the sweep and the planes draw over it. Everywhere else: cleared as before.
#if defined(WX_LOOK_AB)
    // Step C (docs/wx-device.md, ruled 2026-10-29): the clear-or-push call, timed, on the bench image only. The #else
    // branch is the shipping code, unchanged.
    {
      const uint32_t clearT0 = micros();
      const bool pushed = appManager.DrawWeatherUnder(canvas);
      if (!pushed) canvas.fillScreen(lgfx::color888(0, 0, 0));
      appManager.RecordClearUs(micros() - clearT0, pushed);
    }
#else
    if (!appManager.DrawWeatherUnder(canvas))
    canvas.fillScreen(lgfx::color888(0, 0, 0));
#endif
#else
    canvas.fillScreen(lgfx::color888(0, 0, 0));
#endif
    framebuf::SetPhase(framebuf::kPhaseCleared);   // diag only (A9.22)
    // THE FRAME PROFILE BEGIN
#ifdef FRAME_PROFILE
    frameprofile::Mark(frameprofile::B_UNDER);
#endif
    // THE FRAME PROFILE END
#if defined(M3_BENCH)
    m3bench::MaybeGrabMidDraw(canvas.sprite());   // M3-1b's plant: the draw buffer, cleared and not yet drawn
#endif

#if !defined(FEATURE_EAM) && !defined(FEATURE_SPACE) && !defined(FEATURE_SEISMIC) && !defined(FEATURE_BIRDING) && !defined(FEATURE_FISHING) && !defined(FEATURE_CLAUDESCOPE) && !defined(FEATURE_SPEED) && !defined(FEATURE_WEATHERSCOPE)
    if (drawScan) {
#if defined(WX_LOOK_AB)
#if !defined(FEATURE_CLOUD_FEED)
#error "WX_LOOK_AB is a cloud-feed bench flag"
#endif
      // W4' (docs/wx-device.md, WD6'): the sweep call alone, timed whichever path runs. Bench image only -- the
      // #else branch is the shipping code, unchanged, so the shipping image builds byte-identical.
      const uint32_t sweepT0 = micros();
      const bool sweepBlended =
          appManager.DrawSweepOverWeather(canvas, SCREEN_SIZE_DIV_2 - 1, SCREEN_SIZE_DIV_2 - 1, SCREEN_SIZE_DIV_2, sweep);
      if (!sweepBlended)
        DrawRadarSweep(canvas, SCREEN_SIZE_DIV_2 - 1, SCREEN_SIZE_DIV_2 - 1, SCREEN_SIZE_DIV_2, sweep);
      appManager.RecordSweepUs(micros() - sweepT0, sweepBlended);
#else
#if defined(FEATURE_CLOUD_FEED)
      // Over a shown weather layer the wedge blends (WD6, looks 3/4); otherwise today's opaque wedge, unchanged.
      if (!appManager.DrawSweepOverWeather(canvas, SCREEN_SIZE_DIV_2 - 1, SCREEN_SIZE_DIV_2 - 1, SCREEN_SIZE_DIV_2, sweep))
#endif
        DrawRadarSweep(canvas, SCREEN_SIZE_DIV_2 - 1, SCREEN_SIZE_DIV_2 - 1, SCREEN_SIZE_DIV_2, sweep);
#endif
    }
#endif

    // THE FRAME PROFILE BEGIN
#ifdef FRAME_PROFILE
    frameprofile::Mark(frameprofile::B_SWEEP);
#endif
    // THE FRAME PROFILE END
    appManager.Draw(canvas, firstPass);
    // THE FRAME PROFILE BEGIN
#ifdef FRAME_PROFILE
    frameprofile::Mark(frameprofile::B_DRAW);
#endif
    // THE FRAME PROFILE END
#if defined(M3_BENCH)
    m3bench::DrawTestFrame(canvas);   // M3-2: the glass test frame, over the radar
#endif
    framebuf::SetPhase(framebuf::kPhaseDrawn);     // diag only (A9.22)
#ifdef FEATURE_WEATHERSCOPE
    // Diag only (A9.22): what was pushed, the phase it was pushed at, and how long the push held the loop task.
    const uint8_t pushPhase = framebuf::Phase();
    const uint32_t pushT0 = micros();
#endif
#if defined(BLIPSCOPE_PUSH_TASK)
    // M3: frame N goes to the push task, and the loop moves on to frame N+1. The Pushed phase and
    // FrameDone are set by the push task when the send completes (docs/m3-push-task.md, section 5).
    pushtask::HandOver();
    // THE FRAME PROFILE BEGIN
#ifdef FRAME_PROFILE
    frameprofile::Mark(frameprofile::B_HAND);
#endif
    // THE FRAME PROFILE END
#else
    backbuffer.pushSprite(0, bandY);
    framebuf::SetPhase(framebuf::kPhasePushed);    // diag only (A9.22)
#endif
#ifdef FEATURE_WEATHERSCOPE
    appManager.OnPushed(framebuf::Tag(), pushPhase, micros() - pushT0);
#endif
  }
  // RGB SKUs draw into a cached PSRAM framebuffer; write it back so the panel DMA sees the
  // new frame. No-op on SPI SKUs (the pushSprite above already hit the panel directly).
  board::DisplayFlush(tft);
  // THE FRAME PROFILE BEGIN
#ifdef FRAME_PROFILE
  frameprofile::Mark(frameprofile::B_FLUSH);
#endif
  // THE FRAME PROFILE END

  // AFTER the flush, so a reader that samples the counter either side of its
  // copy is comparing against completed frames rather than against a frame
  // still being assembled.
#if !defined(BLIPSCOPE_PUSH_TASK)
  framebuf::FrameDone();
  // THE FRAME BENCH BEGIN
#ifdef FRAME_BENCH
  framebench::Present(micros());   // M3-1, image A: the push runs in the loop, so the present is here
#endif
  // THE FRAME BENCH END
#endif
  // M3: FrameDone runs in the push task, at the send's completion. RecordFrameUs below therefore no
  // longer contains the push: `fr` drops by up to the push's 25.8 ms whether or not the glass is faster.
  // The gate reads map-client's p2p, which is anchored on FrameDone (docs/m3-push-task.md, section 5).

#if !defined(FEATURE_EAM) && !defined(FEATURE_SPACE) && !defined(FEATURE_SEISMIC) && !defined(FEATURE_BIRDING) && !defined(FEATURE_FISHING) && !defined(FEATURE_CLAUDESCOPE) && !defined(FEATURE_SPEED) && !defined(FEATURE_WEATHERSCOPE)
  // THE FRAME BENCH BEGIN
#ifdef FRAME_BENCH
  if (framebench::DelayOn()) delayMicroseconds(framebench::PLANTED_DELAY_US);   // I-6, the loop's +10 ms
#endif
  // THE FRAME BENCH END
  appManager.RecordFrameUs(micros() - frameStartUs);

#ifdef FEATURE_CLOUD_FEED
  // The fleet config raised the firmware floor past this build: run the normal
  // OTA check now rather than waiting out the daily timer. Same code path as
  // the daily check; a same-or-older published release is simply a no-op.
  // Same reboot-then-fetch route as the daily timer, and for the same reason: this
  // fires from a cloud-config poll on a device that has been up for days, which is
  // exactly the heap the direct call cannot survive.
  if (appManager.ConsumeOtaCheckRequest())
    DeferUpdateCheckToReboot(ESP.getMaxAllocHeap(), /*scheduled=*/false); // the floor keeps the full cap
#endif
#endif
}

