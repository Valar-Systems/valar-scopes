/**
 * BlitProbe -- what does a full-bleed 240x240 detail card actually cost per frame?
 *
 * WHY THIS EXISTS. The full-bleed round card (option C) was scoped with one
 * number reasoned rather than measured: the per-frame cost of pushing a 240x240
 * 8bpp sprite out of PSRAM instead of a 150x100 one. The estimate was "+2-6 ms,
 * maybe 10-20%", which is precisely the range where the design either ships as
 * scoped or has to fall back to a cached composite / a lens band. Spending 2.5
 * days of implementation on top of a guess is the wrong order.
 *
 * THE THING THE ESTIMATE MISSED, and the reason this probe measures terms rather
 * than a single before/after: the detail card is OPAQUE. DrawDetailCard opens with
 *
 *     backbuffer.fillScreen(black)                 // AircraftManager.cpp:4890
 *
 * so a card-open frame never draws the radar, the sweep, the 40 blips or the 41
 * airport symbols -- and a full-bleed photo COVERS that fillScreen completely,
 * making it dead work. So the delta is not "+42,600 px of blit". It is
 *
 *     delta = blit(240x240) - blit(150x100) - fillScreen(240x240)
 *
 * and the third term is 57,600 px being REMOVED. The sign of that sum is not
 * obvious from reading the code, which is the whole point.
 *
 * WHAT IT MEASURES. Each primitive in isolation, N times, reported as avg/p95/max
 * (p95 because that is the budget the [health] line polices, and a mean would
 * hide exactly the stutter an owner notices):
 *
 *   1. pushSprite 150x100 -> backbuffer      (today's photo blit)
 *   2. pushSprite 240x240 -> backbuffer      (full-bleed photo blit)
 *   3. fillScreen on the backbuffer          (the term full-bleed DELETES)
 *   4. drawCircle, the card's frame ring     (unchanged either way)
 *   5. the text block                        (unchanged either way)
 *   6. backbuffer -> panel push over SPI     (unchanged either way; the floor)
 *
 * then the two whole card frames assembled from them, so the composed number is
 * checked against the sum rather than assumed equal to it.
 *
 * IT ALSO ANSWERS THE SLEEPER COST. A 240x240 source JPEG is ~4x the pixels of
 * today's 150x100, so the compressed body held in a String during fetch grows
 * from the measured 2.5-4.3 KB to something like 10-20 KB -- in INTERNAL heap,
 * competing with the TLS handshake that is fetching it. Part 3 holds a 240^2
 * sprite AND a payload-sized block live at once and trial-allocates a real
 * handshake against what is left, which is the only form of that question worth
 * asking (see src/HeapHealth.h on why a largest-free-block number cannot answer
 * it on this board).
 *
 * CAVEAT, stated because the probe cannot fix it: these loops are cache-warm and
 * uncontended. A real frame runs with WiFi, the fetch task and TLS competing for
 * bus and cache. The card frame fillScreens the whole backbuffer every pass so
 * the PSRAM working set is warm there too, but treat these as the floor and
 * confirm against a real [health] p95 if the design proceeds.
 *
 * Build/flash (COM118 IS THE SOAK BOARD -- do not touch it):
 *     pio run -e probe-s3-128-blit -t upload --upload-port COM119
 */

// Guarded like TouchProbe.cpp/HeapProbe.cpp: src/probe/ is inside the default
// `+<*>` source filter, so without this every product build links a second
// setup()/loop().
#ifdef PROBE_SKETCH

#include <Arduino.h>
#include <algorithm>
#include <esp_heap_caps.h>

#include "LGFX.h"
#include "Layout.h"

namespace {

constexpr int kIters = 200; // per primitive; 200 x ~5 ms worst case = a few seconds

// Today's photo slot -- keep in sync with PHOTO_W/PHOTO_H in AircraftManager.cpp
// and with PHOTO_W/PHOTO_H in proxy/scripts/ingest-photos.ts.
constexpr int kPhotoW = 150;
constexpr int kPhotoH = 100;

// The full-bleed candidate: the whole round panel.
constexpr int kFullW = SCREEN_SIZE;
constexpr int kFullH = SCREEN_SIZE;

// The exact contiguous block an mbedTLS handshake was measured to need
// (HeapHealth.h -- the size the OTA check failed on, twice, 24 h apart).
constexpr size_t kTlsBytes = 16717;
constexpr uint32_t kTlsCaps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;

// A 240x240 q82 JPEG is roughly 4x the pixels of the 150x100 the library ships
// today (measured range 2,520-4,265 B), so this is the payload the fetch path
// would be holding in a String while the handshake above is trying to allocate.
constexpr size_t kPayloadBytes = 20480;

LGFX tft;
LGFX_Sprite backbuffer(&tft);
LGFX_Sprite photoSmall(&tft);
LGFX_Sprite photoFull(&tft);

uint32_t samples[kIters];

struct Stat { float avg, p95, max; };

Stat summarize()
{
    uint32_t sorted[kIters];
    memcpy(sorted, samples, sizeof(samples));
    std::sort(sorted, sorted + kIters);
    uint64_t sum = 0;
    for (int i = 0; i < kIters; ++i) sum += sorted[i];
    return {
        (float)sum / kIters / 1000.0f,
        sorted[(int)((kIters - 1) * 0.95f)] / 1000.0f,
        sorted[kIters - 1] / 1000.0f,
    };
}

void report(const char* label, const Stat& s)
{
    Serial.printf("[blit] %-34s avg=%6.3fms  p95=%6.3fms  max=%6.3fms\n",
                  label, s.avg, s.p95, s.max);
}

// Run `body` kIters times, timing each pass.
template <typename F>
Stat measure(F&& body)
{
    body(); // one warm-up pass, not sampled
    for (int i = 0; i < kIters; ++i) {
        const uint32_t t0 = micros();
        body();
        samples[i] = micros() - t0;
    }
    return summarize();
}

// Fill a sprite with a gradient. Blit cost is content-independent (pushSprite is
// a fixed-size copy), so this is only here to make sure the buffers are real,
// committed pages rather than untouched allocations.
void paint(LGFX_Sprite& s, int w, int h)
{
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            s.drawPixel(x, y, lgfx::color888((x * 255) / w, (y * 255) / h, 128));
}

// The card's text block, as DrawDetailCard draws it: a title plus the telemetry
// lines. Identical work in both layouts, so it cancels out of the delta -- it is
// measured anyway so the assembled frame can be checked against the sum.
void drawCardText(LGFX_Sprite& b)
{
    constexpr int cx = SCREEN_SIZE_DIV_2;
    auto centered = [&](const char* str, int yy, int size) {
        b.setTextSize(size);
        const int x = cx - (int)b.textWidth(str) / 2;
        b.drawString(str, x, yy);
    };
    b.setTextColor(lgfx::color888(0, 255, 0));
    centered("SKW6042", 140, 2);
    centered("Embraer E175", 165, 1);
    centered("SkyWest Airlines", 178, 1);
    centered("N204SY", 191, 1);
    centered("12,450 ft   288 kt", 204, 1);
    centered("14.2 km   HDG 291", 217, 1);
}

void heapLine(const char* when)
{
    void* trial = heap_caps_malloc(kTlsBytes, kTlsCaps);
    const bool tlsOk = trial != nullptr;
    if (trial) heap_caps_free(trial);
    Serial.printf("[blit] heap %-22s psram_free=%-8u free8=%-7u largest=%-7u tlsOk=%d\n",
                  when,
                  (unsigned)ESP.getFreePsram(),
                  (unsigned)heap_caps_get_free_size(kTlsCaps),
                  (unsigned)heap_caps_get_largest_free_block(kTlsCaps),
                  tlsOk ? 1 : 0);
}

} // namespace

void setup()
{
    Serial.begin(115200);
    while (!Serial && millis() < 3000) { delay(10); }
    delay(300);

    Serial.println("\n\n=== BlitProbe: full-bleed 240x240 detail card, per-frame cost ===");
    Serial.printf("[blit] build: SCREEN_SIZE=%d BAND_H=%d BANDED_RENDER=%d iters=%d\n",
                  SCREEN_SIZE, BAND_H, (int)variant::BANDED_RENDER, kIters);

    const bool panelOk = tft.init();
    tft.setBrightness(255);

    // Reproduce main.cpp's memory configuration EXACTLY -- a blit measured against
    // a differently-placed backbuffer measures nothing about the product.
    if constexpr (!variant::BANDED_RENDER)
        backbuffer.setPsram(true);
    backbuffer.setColorDepth(8);
    void* bb = backbuffer.createSprite(SCREEN_SIZE, BAND_H);
    Serial.printf("[blit] tft.init=%d %dx%d  backbuffer=%s\n",
                  panelOk, (int)tft.width(), (int)tft.height(), bb ? "ok" : "ALLOC FAILED");
    if (!bb) { Serial.println("[blit] FATAL: no backbuffer"); return; }

    heapLine("baseline");

    // ---- allocate both photo sprites the way AircraftManager does -------------
    if constexpr (!variant::BANDED_RENDER) photoSmall.setPsram(true);
    photoSmall.setColorDepth(8);
    const bool smallOk = photoSmall.createSprite(kPhotoW, kPhotoH) != nullptr;
    paint(photoSmall, kPhotoW, kPhotoH);
    Serial.printf("[blit] photoSmall %dx%d 8bpp = %d B  %s\n",
                  kPhotoW, kPhotoH, kPhotoW * kPhotoH, smallOk ? "ok" : "ALLOC FAILED");
    heapLine("with 150x100 sprite");

    if constexpr (!variant::BANDED_RENDER) photoFull.setPsram(true);
    photoFull.setColorDepth(8);
    const bool fullOk = photoFull.createSprite(kFullW, kFullH) != nullptr;
    paint(photoFull, kFullW, kFullH);
    Serial.printf("[blit] photoFull  %dx%d 8bpp = %d B  %s\n",
                  kFullW, kFullH, kFullW * kFullH, fullOk ? "ok" : "ALLOC FAILED");
    heapLine("with BOTH sprites");

    if (!smallOk || !fullOk) { Serial.println("[blit] FATAL: sprite alloc failed"); return; }

    // ---- part 1: the primitives ----------------------------------------------
    Serial.println("\n[blit] --- primitives -------------------------------------------");
    constexpr int cx = SCREEN_SIZE_DIV_2;

    report("1. blit 150x100 (today)", measure([&] {
        photoSmall.pushSprite(&backbuffer, cx - kPhotoW / 2, 30);
    }));
    report("2. blit 240x240 (full-bleed)", measure([&] {
        photoFull.pushSprite(&backbuffer, 0, 0);
    }));
    report("3. fillScreen 240x240 (DELETED)", measure([&] {
        backbuffer.fillScreen(lgfx::color888(0, 0, 0));
    }));
    report("4. drawCircle frame ring", measure([&] {
        backbuffer.drawCircle(cx - 1, cx - 1, SCREEN_SIZE_DIV_2 - 1, lgfx::color888(0, 200, 0));
    }));
    report("5. card text block", measure([&] {
        drawCardText(backbuffer);
    }));
    report("6. backbuffer -> panel (SPI)", measure([&] {
        backbuffer.pushSprite(0, 0);
    }));

    // ---- part 2: the two whole card frames -----------------------------------
    // Composed, not summed: if these disagree with the sum of the primitives, the
    // primitives are missing an interaction and the composed number is the real one.
    Serial.println("\n[blit] --- whole card frames, composed --------------------------");

    report("TODAY   fill+ring+150x100+text", measure([&] {
        backbuffer.fillScreen(lgfx::color888(0, 0, 0));
        backbuffer.drawCircle(cx - 1, cx - 1, SCREEN_SIZE_DIV_2 - 1, lgfx::color888(0, 200, 0));
        photoSmall.pushSprite(&backbuffer, cx - kPhotoW / 2, 30);
        drawCardText(backbuffer);
    }));
    report("FULLBLEED 240x240+ring+text", measure([&] {
        // No fillScreen: the photo covers every pixel of the backbuffer.
        photoFull.pushSprite(&backbuffer, 0, 0);
        backbuffer.drawCircle(cx - 1, cx - 1, SCREEN_SIZE_DIV_2 - 1, lgfx::color888(0, 200, 0));
        drawCardText(backbuffer);
    }));
    report("TODAY   + panel push", measure([&] {
        backbuffer.fillScreen(lgfx::color888(0, 0, 0));
        backbuffer.drawCircle(cx - 1, cx - 1, SCREEN_SIZE_DIV_2 - 1, lgfx::color888(0, 200, 0));
        photoSmall.pushSprite(&backbuffer, cx - kPhotoW / 2, 30);
        drawCardText(backbuffer);
        backbuffer.pushSprite(0, 0);
    }));
    report("FULLBLEED + panel push", measure([&] {
        photoFull.pushSprite(&backbuffer, 0, 0);
        backbuffer.drawCircle(cx - 1, cx - 1, SCREEN_SIZE_DIV_2 - 1, lgfx::color888(0, 200, 0));
        drawCardText(backbuffer);
        backbuffer.pushSprite(0, 0);
    }));
    // The honest worst case: if the full-bleed layout ends up keeping a fillScreen
    // (e.g. a letterboxed photo that does not cover the corners of the square), the
    // saving evaporates and this is what it costs instead.
    report("FULLBLEED + fill (no saving)", measure([&] {
        backbuffer.fillScreen(lgfx::color888(0, 0, 0));
        photoFull.pushSprite(&backbuffer, 0, 0);
        backbuffer.drawCircle(cx - 1, cx - 1, SCREEN_SIZE_DIV_2 - 1, lgfx::color888(0, 200, 0));
        drawCardText(backbuffer);
        backbuffer.pushSprite(0, 0);
    }));

    // ---- part 2b: the per-tap churn C1 removes -------------------------------
    // Until 2026-08-09 ExitDetail() freed the photo sprite on every card close and
    // the enrich path re-created it on every open, on the C3's reasoning that
    // holding it starved the TLS handshake -- which is false on a PSRAM board (the
    // heap lines above show the sprite never touches internal heap at all). This is
    // what that cost per tap, and what it WOULD have cost at full-bleed.
    Serial.println("\n[blit] --- per-tap sprite churn (what C1 deletes) ---------------");
    {
        LGFX_Sprite churn(&tft);
        report("open+close 150x100 (was: per tap)", measure([&] {
            if constexpr (!variant::BANDED_RENDER) churn.setPsram(true);
            churn.setColorDepth(8);
            churn.createSprite(kPhotoW, kPhotoH);
            churn.fillScreen(lgfx::color888(0, 0, 0));
            churn.deleteSprite();
        }));
        report("open+close 240x240 (would have)", measure([&] {
            if constexpr (!variant::BANDED_RENDER) churn.setPsram(true);
            churn.setColorDepth(8);
            churn.createSprite(kFullW, kFullH);
            churn.fillScreen(lgfx::color888(0, 0, 0));
            churn.deleteSprite();
        }));
    }

    // ---- part 3: the payload / TLS question ----------------------------------
    // READ THE CAVEAT BEFORE CITING ANY OF THIS. This probe links no WiFi stack, so
    // its internal heap is nearly virgin (free8 ~330 KB, largest ~270 KB) where a
    // running device sits at free8 ~85 KB and largest 31,732 -- the plateau from
    // issue #163. A tlsOk=1 here therefore proves NOTHING about a device that is
    // actually fetching, and the real question (does a ~20 KB compressed 240^2 body
    // held in a String starve the handshake fetching it?) needs a CanHandshake()
    // trial on the live enrich path. Kept only because the DELTA between the rows
    // below is still informative about relative pressure.
    Serial.println("\n[blit] --- payload vs TLS handshake ----------------------------");
    Serial.printf("[blit] holding a %u B payload (a 240x240 q82 body) in INTERNAL heap\n",
                  (unsigned)kPayloadBytes);
    void* payload = heap_caps_malloc(kPayloadBytes, kTlsCaps);
    if (!payload) {
        Serial.println("[blit] payload alloc FAILED outright -- that is the answer");
    } else {
        memset(payload, 0xAB, kPayloadBytes); // commit it
        heapLine("sprite + payload live");
        heap_caps_free(payload);
    }
    heapLine("payload freed");

    // For contrast: what today's payload costs in the same state.
    void* small = heap_caps_malloc(4300, kTlsCaps); // top of the measured 2,520-4,265 range
    if (small) { memset(small, 0xAB, 4300); heapLine("sprite + 4.3 KB payload"); heap_caps_free(small); }

    // ---- part 4: 8bpp vs 16bpp -----------------------------------------------
    // WHY THIS MATTERS MORE THAN ANYTHING ABOVE. 8bpp in LovyanGFX is RGB332:
    // 8 levels of red, 8 of green, FOUR of blue. 256 colours total. A photograph
    // of an aeroplane against sky is close to the worst case for that palette --
    // skies band, blues posterize, and everything picks up a cast. The depth was
    // chosen when the backbuffer had to live in the C3's internal heap; on a
    // PSRAM board it is buying nothing.
    //
    // Both buffers have to move together: pushing a 16bpp photo into an 8bpp
    // backbuffer just quantizes it straight back down.
    //
    // The panel push is the interesting number. At 8bpp it is 23.2 ms, and the
    // SPI clock (40 MHz) says 115,200 bytes of RGB565 takes 23.04 ms -- so the
    // transfer is ALREADY 16-bit on the wire and the 8bpp buffer is being
    // converted on the fly. If that holds, 16bpp costs PSRAM and blit bandwidth
    // but nothing at all on the panel push, which is 78-86% of the frame.
    Serial.println("\n[blit] --- 16bpp (RGB565) vs 8bpp (RGB332) -----------------");
    {
        LGFX_Sprite bb16(&tft), photo16(&tft);
        bb16.setPsram(true);    bb16.setColorDepth(16);
        photo16.setPsram(true); photo16.setColorDepth(16);
        const bool ok16 = bb16.createSprite(SCREEN_SIZE, BAND_H) != nullptr &&
                          photo16.createSprite(kFullW, kFullH) != nullptr;
        Serial.printf("[blit] 16bpp buffers %s (backbuffer %d B + photo %d B)\n",
                      ok16 ? "ok" : "ALLOC FAILED",
                      SCREEN_SIZE * BAND_H * 2, kFullW * kFullH * 2);
        if (ok16) {
            paint(photo16, kFullW, kFullH);
            heapLine("both 16bpp buffers live");

            report("16bpp fillScreen 240x240", measure([&] {
                bb16.fillScreen(lgfx::color888(0, 0, 0));
            }));
            report("16bpp blit 240x240", measure([&] {
                photo16.pushSprite(&bb16, 0, 0);
            }));
            report("16bpp backbuffer -> panel", measure([&] {
                bb16.pushSprite(0, 0);
            }));
            report("16bpp FULLBLEED + panel push", measure([&] {
                photo16.pushSprite(&bb16, 0, 0);
                bb16.drawCircle(cx - 1, cx - 1, SCREEN_SIZE_DIV_2 - 1, lgfx::color888(0, 200, 0));
                drawCardText(bb16);
                bb16.pushSprite(0, 0);
            }));
            // The radar screen is the frame that actually sets the budget (p95
            // ~40 ms at 8bpp with 40 contacts), and it is DRAW-bound rather than
            // blit-bound, so it is the one at risk from a deeper buffer. Stand in
            // for it with a comparable pile of primitives at both depths.
            auto radarish = [&](LGFX_Sprite& b) {
                b.fillScreen(lgfx::color888(0, 0, 0));
                for (int r = 30; r <= 110; r += 20)
                    b.drawCircle(cx, cx, r, lgfx::color888(0, 90, 0));
                for (int i = 0; i < 40; ++i) {
                    const int x = 20 + (i * 37) % 200, y = 20 + (i * 61) % 200;
                    b.fillTriangle(x, y - 4, x - 3, y + 4, x + 3, y + 4, lgfx::color888(0, 255, 0));
                    b.drawLine(x, y, x - 6, y + 6, lgfx::color888(0, 120, 0));
                }
                for (int i = 0; i < 41; ++i)
                    b.fillRect(15 + (i * 29) % 210, 15 + (i * 47) % 210, 3, 3, lgfx::color888(0, 160, 0));
            };
            report(" 8bpp radar-ish + panel push", measure([&] {
                radarish(backbuffer); backbuffer.pushSprite(0, 0);
            }));
            report("16bpp radar-ish + panel push", measure([&] {
                radarish(bb16); bb16.pushSprite(0, 0);
            }));
        }
    }

    Serial.println("\n[blit] === done. The verdict is FULLBLEED vs TODAY on the two");
    Serial.println("[blit] === '+ panel push' rows -- that pair is the real frame.");
}

void loop() { delay(1000); }

#endif // PROBE_SKETCH
