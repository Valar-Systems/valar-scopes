// Weatherscope Stage 1 (docs/weatherscope-stage1.md): a bench harness that draws ONE stored MRMS frame.
//
// Env weatherscope-s3-128 builds ONLY src/weather/ (build_src_filter = -<*> +<weather/>), and every other
// env drops src/weather/ through [filters] editions_off. No Wi-Fi, no config page, no OTA, no network:
// the frame is test/fixtures/wx/kdfw-60mi.wxc, embedded with board_build.embed_files (so no generated
// .inc can go stale), the 60 mi KDFW crop of the 2024-05-21 22:00Z MRMS composite.
//
// On boot it decodes the crop (include/WxCrop.h) into a code grid in PSRAM, works out each screen row's
// and column's cell (src/weather/WxRender.h), paints a 240 x 240 PSRAM sprite in the PROVISIONAL palette
// (not ruled), pushes it once, and reports. Send any byte over serial to get the report again.
//
// Serial, tagged for a single-open capture (scripts/bench-trace.ps1, never a reattaching recorder):
//   WX,<key>,<value>      one measurement per line (predictions B3-B8)
//   WXFB,<base64>         the code every pixel was painted from, 57,600 bytes row-major, 255 = off disc
//   WX,fb_sha256,<hex>    the device's own sha256 of those bytes (prediction B9)
//   WX,end
#include <Arduino.h>
#include <esp_heap_caps.h>
#include <mbedtls/base64.h>
#include <mbedtls/sha256.h>

#include "LGFX.h"
#include "WxCrop.h"
#include "WxRender.h"
#include "variants/Variant.h"

extern const uint8_t kdfw_crop_start[] asm("_binary_test_fixtures_wx_kdfw_60mi_wxc_start");
extern const uint8_t kdfw_crop_end[] asm("_binary_test_fixtures_wx_kdfw_60mi_wxc_end");

namespace {

LGFX tft;
LGFX_Sprite canvas(&tft);
wxrender::View view;
wxcrop::Header hdr;
uint8_t* grid = nullptr;   // decoded codes, rows x cols, in PSRAM

// PROVISIONAL palette (NOT ruled): NWS reflectivity steps for codes 1-13, black for dry, grey for no
// coverage, dark violet for missing. Same values as render_ref.py's PNG. B9 compares codes, not colours.
const uint8_t kPalette[16][3] = {
    {0, 0, 0},       {3, 0, 244},   {2, 253, 2},   {1, 197, 1},   {0, 142, 0},     {253, 248, 2},
    {229, 188, 0},   {253, 149, 0}, {253, 0, 0},   {212, 0, 0},   {188, 0, 0},     {248, 0, 253},
    {152, 84, 198},  {253, 253, 253}, {60, 60, 68}, {110, 0, 110}};

struct Report {
    const char* status = "not run";
    size_t cropBytes = 0;
    uint32_t decodeUs = 0, indexUs = 0, renderUs = 0, pushUs = 0;
    size_t psramBefore = 0, psramAfter = 0, internalBefore = 0, internalAfter = 0;
    bool sprite = false;
} rep;

void Render()
{
    canvas.startWrite();
    for (int y = 0; y < wxrender::kSize; ++y) {
        int x0 = 0;
        uint8_t run = wxrender::CodeAt(hdr, grid, view, 0, y);
        for (int x = 1; x <= wxrender::kSize; ++x) {
            const uint8_t c = x < wxrender::kSize ? wxrender::CodeAt(hdr, grid, view, x, y) : 0xFE;
            if (c == run) continue;
            const uint8_t* p = kPalette[run == wxrender::kOffDisc ? 0 : (run & 15)];
            canvas.writeFastHLine(x0, y, x - x0, canvas.color565(p[0], p[1], p[2]));
            x0 = x;
            run = c;
        }
    }
    canvas.endWrite();
}

void PrintReport()
{
    Serial.printf("WX,build,env=%s\n",
#ifdef BUILD_ENV
                  BUILD_ENV
#else
                  "weatherscope"
#endif
    );
    Serial.printf("WX,crop,kdfw-60mi.wxc,bytes=%u,status=%s,rows=%u,cols=%u,frame=%lu\n", (unsigned)rep.cropBytes,
                  rep.status, (unsigned)hdr.rows, (unsigned)hdr.cols, (unsigned long)hdr.frameEpoch);
    Serial.printf("WX,sprite,%s\n", rep.sprite ? "psram" : "FAILED");
    Serial.printf("WX,psram_used,%u\n", (unsigned)(rep.psramBefore - rep.psramAfter));
    Serial.printf("WX,internal_heap_delta,%d\n", (int)rep.internalBefore - (int)rep.internalAfter);
    Serial.printf("WX,decode_us,%lu\n", (unsigned long)rep.decodeUs);
    Serial.printf("WX,index_us,%lu\n", (unsigned long)rep.indexUs);
    Serial.printf("WX,render_us,%lu\n", (unsigned long)rep.renderUs);
    Serial.printf("WX,push_us,%lu\n", (unsigned long)rep.pushUs);
    if (!grid) { Serial.println("WX,end"); return; }

    // The dump: the code every pixel was painted from (the same CodeAt the renderer used), streamed in
    // rows so nothing the size of the screen is allocated.
    mbedtls_sha256_context sha;
    mbedtls_sha256_init(&sha);
    mbedtls_sha256_starts(&sha, 0);
    Serial.print("WXFB,");
    uint8_t rowCodes[wxrender::kSize];
    unsigned char b64[wxrender::kSize * 4 / 3 + 4];
    for (int y = 0; y < wxrender::kSize; ++y) {
        for (int x = 0; x < wxrender::kSize; ++x) rowCodes[x] = wxrender::CodeAt(hdr, grid, view, x, y);
        mbedtls_sha256_update(&sha, rowCodes, sizeof rowCodes);
        size_t n = 0;   // 240 bytes is a multiple of 3, so each row encodes on its own with no padding
        mbedtls_base64_encode(b64, sizeof b64, &n, rowCodes, sizeof rowCodes);
        Serial.write(b64, n);
    }
    Serial.println();
    uint8_t digest[32];
    mbedtls_sha256_finish(&sha, digest);
    mbedtls_sha256_free(&sha);
    Serial.print("WX,fb_sha256,");
    for (uint8_t b : digest) Serial.printf("%02x", b);
    Serial.println();
    Serial.println("WX,end");
}

}  // namespace

void setup()
{
    Serial.begin(115200);
#if ARDUINO_USB_CDC_ON_BOOT
    Serial.setTxBufferSize(4096);   // the dump is ~77 KB; see animtest_main.cpp on USB-CDC back-pressure
    Serial.setTxTimeoutMs(2);
#endif
    delay(300);
    Serial.println();
    Serial.printf("[build] env=%s  Weatherscope Stage 1: one stored MRMS frame\n",
#ifdef BUILD_ENV
                  BUILD_ENV
#else
                  "weatherscope"
#endif
    );
    Serial.println("[build] confirm this matches the image you intended BEFORE diagnosing any symptom");

    variant::BoardPreInit();
    const bool panelOk = tft.init();
    tft.setRotation(0);
    tft.invertDisplay(BLIPSCOPE_DISP_INVERT);   // the GC9A01 boots inverted (animtest_main.cpp, 2026-08-06)
    tft.setBrightness(255);
    tft.fillScreen(TFT_BLACK);
    Serial.printf("[disp] tft.init=%d %dx%d invert=%d\n", panelOk ? 1 : 0, (int)tft.width(), (int)tft.height(),
                  (int)BLIPSCOPE_DISP_INVERT);

    rep.psramBefore = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    rep.internalBefore = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    rep.cropBytes = (size_t)(kdfw_crop_end - kdfw_crop_start);

    canvas.setPsram(true);
    canvas.setColorDepth(16);
    rep.sprite = canvas.createSprite(wxrender::kSize, wxrender::kSize) != nullptr;

    wxcrop::Status s = wxcrop::ParseHeader(kdfw_crop_start, rep.cropBytes, &hdr);
    if (s == wxcrop::Status::Ok) {
        grid = (uint8_t*)heap_caps_malloc(hdr.Cells(), MALLOC_CAP_SPIRAM);
        if (!grid) s = wxcrop::Status::BufferTooSmall;
    }
    if (s == wxcrop::Status::Ok) {
        uint32_t t = micros();
        s = wxcrop::DecodeInto(kdfw_crop_start, rep.cropBytes, grid, hdr.Cells(), &hdr);
        rep.decodeUs = micros() - t;
    }
    rep.status = wxcrop::StatusName(s);
    if (s != wxcrop::Status::Ok) {
        heap_caps_free(grid);
        grid = nullptr;
    }

    if (grid && rep.sprite) {
        uint32_t t = micros();
        wxrender::BuildView(hdr, &view);
        rep.indexUs = micros() - t;
        t = micros();
        Render();
        rep.renderUs = micros() - t;
        t = micros();
        canvas.pushSprite(&tft, 0, 0);
        rep.pushUs = micros() - t;
    }
    rep.psramAfter = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    rep.internalAfter = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    PrintReport();
}

void loop()
{
    if (Serial.available()) {
        while (Serial.available()) Serial.read();
        PrintReport();
    }
    delay(20);
}
