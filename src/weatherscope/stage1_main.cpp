// Weatherscope Stage 1 (docs/weatherscope-stage1.md): a bench harness that draws ONE stored MRMS frame.
//
// Env weatherscope-s3-128 builds ONLY src/weatherscope/ (build_src_filter = -<*> +<weatherscope/>), and every other
// env drops src/weatherscope/ through [filters] editions_off. No Wi-Fi, no config page, no OTA, no network:
// the frame is test/fixtures/wx/kdfw-60mi.wxc, embedded with board_build.embed_files (so no generated
// .inc can go stale), the 60 mi KDFW crop of the 2024-05-21 22:00Z MRMS composite.
//
// On boot it decodes the crop (include/WxCrop.h) into a code grid in PSRAM, works out each screen row's
// and column's cell (src/weatherscope/WxRender.h), paints a 240 x 240 PSRAM sprite in the RULED palette
// (src/weatherscope/WxPalette.h, Stage 2 step 2.1), pushes it once, and reports its measurements -- WITHOUT the
// dump (see below).
//
// STAGE 2, STEP 2.1 (docs/weatherscope-stage2.md): the palette is shown for Daniel to look at before it is final.
// After boot the screen alternates every 10 s between the frame and a palette card (rain, snow, both code-1 dim
// levels, no coverage, both missing options, dry). The boot report adds WX,sprite_sha256: the sha256 of the
// frame's 115,200 sprite bytes, computed AFTER the timed path, which test/fixtures/wx/palette_ref.py derives
// independently on the host (check C2).
//
// Serial, for a single-open capture (test/fixtures/wx/capture_stage1.py, never a reattaching recorder):
//   WX,<key>,<value>            one measurement per line (predictions B3-B8)
//   WXR,<row>,<crc32>,<base64>  one screen row: the code every pixel was painted from, 240 bytes, 255 = off
//                               the disc; crc32 is zlib's, over those 240 raw bytes
//   WX,fb_sha256,<hex>          the device's own sha256 of all 57,600 bytes (prediction B9)
//   WX,end
//
// THE DUMP IS A MEASURING TOOL, AND IT USED TO LOSE ROWS (2026-10-20, rows 197-200 of one capture). The
// 2 ms TX timeout below -- right for a log nobody is reading -- drops a write whenever the host stalls. So
// the dump is printed only ON REQUEST, with BLOCKING writes (up to 2 s), and every row carries its number
// and checksum so the host can name what is missing rather than miss it. Any byte asks for a report with
// the blocking dump; 'D' asks for the same report with the old drop-on-stall writes, which exists only
// for the control that proves the tags see a loss (docs/weatherscope-stage1.md, addendum 3).
#include <Arduino.h>
#include <esp_heap_caps.h>
#include <mbedtls/base64.h>
#include <mbedtls/sha256.h>

#include "LGFX.h"
#include "WxCrop.h"
#include "WxPalette.h"
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

// The RULED palette (src/weatherscope/WxPalette.h). The frame is rain only -- the wire carries no snow field
// yet -- with code 1 dimmed to 85 %. Stage 1's provisional palette (dark violet for missing) is retired; it is
// recorded in docs/weatherscope-stage1.md. B9 still compares codes, not colours.
constexpr int kFrameDim = wxpalette::kDim85;
constexpr uint32_t kAlternateMs = 10000;   // frame <-> palette card, so the look needs no serial input

struct Report {
    const char* status = "not run";
    size_t cropBytes = 0;
    uint32_t decodeUs = 0, indexUs = 0, renderUs = 0, pushUs = 0;
    size_t psramBefore = 0, psramAfter = 0, internalBefore = 0, internalAfter = 0;
    bool sprite = false;
    char spriteSha[65] = "";   // the frame's sprite bytes, hashed after the timed path (C2)
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
            const wxpalette::Rgb p = wxpalette::Code(run == wxrender::kOffDisc ? 0 : (run & 15), false, kFrameDim);
            canvas.writeFastHLine(x0, y, x - x0, canvas.color565(p.r, p.g, p.b));
            x0 = x;
            run = c;
        }
    }
    canvas.endWrite();
}

// The palette card, for the look. Not measured and not graded: Daniel's eyes are the result of this step.
void Swatch(int x, int y, int w, int h, wxpalette::Rgb c) { canvas.fillRect(x, y, w, h, canvas.color565(c.r, c.g, c.b)); }

void HatchSwatch(int x, int y, int w, int h)
{
    const uint16_t line = canvas.color565(wxpalette::kMissingHatch.r, wxpalette::kMissingHatch.g, wxpalette::kMissingHatch.b);
    const uint16_t bg = canvas.color565(wxpalette::kDry.r, wxpalette::kDry.g, wxpalette::kDry.b);
    for (int yy = y; yy < y + h; ++yy)
        for (int xx = x; xx < x + w; ++xx) canvas.drawPixel(xx, yy, (xx + yy) % wxpalette::kHatchPeriod == 0 ? line : bg);
}

void Label(int x, int y, const char* text)
{
    canvas.setCursor(x, y);
    canvas.print(text);
}

void DrawCard()
{
    canvas.fillSprite(TFT_BLACK);
    canvas.setTextColor(TFT_WHITE);
    canvas.setTextSize(1);
    const int w = 12, x0 = 120 - 13 * w / 2;
    Label(x0, 34, "RAIN  15 ........ 75+ dBZ");
    for (int c = 1; c <= 13; ++c) Swatch(x0 + (c - 1) * w, 44, w - 1, 20, wxpalette::Rain((uint8_t)c, kFrameDim));
    Label(x0, 72, "15 dBZ: 85%");
    Swatch(x0 + 70, 69, 18, 12, wxpalette::Rain(1, wxpalette::kDim85));
    Label(x0 + 96, 72, "70%");
    Swatch(x0 + 118, 69, 18, 12, wxpalette::Rain(1, wxpalette::kDim70));
    Label(x0, 92, "SNOW  15 ........ 75+ dBZ");
    for (int c = 1; c <= 13; ++c) Swatch(x0 + (c - 1) * w, 102, w - 1, 20, wxpalette::Snow((uint8_t)c, kFrameDim));
    const char* names[4] = {"NOCOV", "MIS-A", "MIS-B", "DRY"};
    for (int i = 0; i < 4; ++i) {
        const int x = 40 + i * 41;
        Label(x, 134, names[i]);
        if (i == 0) Swatch(x, 144, 32, 22, wxpalette::kNoCoverage);
        if (i == 1) Swatch(x, 144, 32, 22, wxpalette::kMissingDim);
        if (i == 2) HatchSwatch(x, 144, 32, 22);
        if (i == 3) canvas.drawRect(x, 144, 32, 22, canvas.color565(128, 128, 128));   // dry is black: outlined
    }
    Label(52, 176, "MISSING: A dim, B hatched");
}

void ShowFrame() { Render(); canvas.pushSprite(&tft, 0, 0); }
void ShowCard() { DrawCard(); canvas.pushSprite(&tft, 0, 0); }

void SpriteSha(char out[65])
{
    uint8_t d[32];
    mbedtls_sha256((const unsigned char*)canvas.getBuffer(), (size_t)wxrender::kSize * wxrender::kSize * 2, d, 0);
    for (int i = 0; i < 32; ++i) snprintf(out + 2 * i, 3, "%02x", d[i]);
}

// zlib's CRC-32 (reflected 0xEDB88320, init and final ~0), bit by bit: no table, so no static RAM.
uint32_t Crc32(const uint8_t* p, size_t n)
{
    uint32_t c = 0xFFFFFFFFu;
    while (n--) {
        c ^= *p++;
        for (int k = 0; k < 8; ++k) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    return ~c;
}

void SetTxTimeoutMs(uint32_t ms)
{
#if ARDUINO_USB_CDC_ON_BOOT
    Serial.setTxTimeoutMs(ms);
#else
    (void)ms;
#endif
}

// withDump = false at boot (nobody may be listening, and a blocking write would stall the board).
// lossy = true only for the control: the old 2 ms drop-on-stall writes during the dump.
void PrintReport(bool withDump, bool lossy)
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
    Serial.printf("WX,sprite_sha256,%s\n", rep.spriteSha[0] ? rep.spriteSha : "none");
    if (!grid || !withDump) {
        if (grid) Serial.println("WX,dump,on request (any byte: blocking; D: the drop-on-stall control)");
        Serial.println("WX,end");
        return;
    }

    // The dump: the code every pixel was painted from (the same CodeAt the renderer used), one tagged
    // row per line, streamed so nothing the size of the screen is allocated.
    SetTxTimeoutMs(lossy ? 2 : 2000);
    Serial.printf("WX,dump,mode=%s,rows=%d\n", lossy ? "drop-on-stall" : "blocking", wxrender::kSize);
    mbedtls_sha256_context sha;
    mbedtls_sha256_init(&sha);
    mbedtls_sha256_starts(&sha, 0);
    uint8_t rowCodes[wxrender::kSize];
    unsigned char b64[wxrender::kSize * 4 / 3 + 4];
    for (int y = 0; y < wxrender::kSize; ++y) {
        for (int x = 0; x < wxrender::kSize; ++x) rowCodes[x] = wxrender::CodeAt(hdr, grid, view, x, y);
        mbedtls_sha256_update(&sha, rowCodes, sizeof rowCodes);
        size_t n = 0;   // 240 bytes is a multiple of 3, so each row encodes on its own with no padding
        mbedtls_base64_encode(b64, sizeof b64, &n, rowCodes, sizeof rowCodes);
        Serial.printf("WXR,%d,%08lx,", y, (unsigned long)Crc32(rowCodes, sizeof rowCodes));
        Serial.write(b64, n);
        Serial.println();
    }
    uint8_t digest[32];
    mbedtls_sha256_finish(&sha, digest);
    mbedtls_sha256_free(&sha);
    Serial.print("WX,fb_sha256,");
    for (uint8_t b : digest) Serial.printf("%02x", b);
    Serial.println();
    Serial.println("WX,end");
    SetTxTimeoutMs(2);
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
    if (grid && rep.sprite) SpriteSha(rep.spriteSha);   // after the timed path and the memory readings
    PrintReport(false, false);   // measurements only: nobody may be listening yet
}

void loop()
{
    if (Serial.available()) {
        bool lossy = false;   // 'D' anywhere in what arrived: the drop-on-stall control
        while (Serial.available()) lossy |= Serial.read() == 'D';
        PrintReport(true, lossy);
    }
    static uint32_t lastSwitch = millis();
    static bool card = false;
    if (grid && rep.sprite && millis() - lastSwitch >= kAlternateMs) {
        lastSwitch = millis();
        card = !card;
        if (card) ShowCard(); else ShowFrame();
    }
    delay(20);
}
