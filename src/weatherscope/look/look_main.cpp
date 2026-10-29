// Weatherscope's light-look bench select (docs/weatherscope-stage3.md A12.27, A12.30, A12.32): BENCH ONLY, never
// merged. A standalone viewer, like the stage-1 harness (src/weatherscope/bench/): LovyanGFX only, no Wi-Fi, no config
// page, no OTA, and it never opens NVS. It shows the host's exact pictures (tools/weatherscope/screens/look_frames.py)
// in three looks -- dark (today), light (a), light (b) -- for Daniel to choose between on the glass.
//
// THE DATA (LookData.S, .incbin from build-look/, which look_frames.py writes): 20 role-index frames of 240 x 240, one
// byte a pixel naming the role that drew it, and a 66-entry RGB888 palette per look. A frame is drawn by looking each
// byte up in the look's table, straight into the sprite's buffer -- so a pixel is exactly the host's colour.
//
//   F0 Bend dry · F1 Bend rain · F2-F16 the Bend loop · F17 Dallas approaching · F18/F19 a patch of each code 1-15,
//   terrain on and off
//
// ON THE GLASS: swipe left / right = the next / previous look (its name shows for 1 s on its own ground first); swipe
// up / down = the next / previous scene (Bend, Dallas, the patches with terrain on, with terrain off); on Bend, a tap
// on the dry screen plays the loop (70 ms a frame, as the edition's) and ends on the rain screen, and a tap there goes
// back to dry.
//
// SERIAL (the bench's run, the same handlers): T tap, S / B next / previous look, U / D next / previous scene, R redraw
// and report. Every drawn screen prints "[look] look=<l> frame=<n> sha=<sha256 of the sprite's 115,200 B> expand_us=";
// a loop prints nothing while it plays, then its 15 records, paced.
#include <Arduino.h>
#include <esp_heap_caps.h>
#include <esp_ota_ops.h>
#include <esp_timer.h>
#include <mbedtls/sha256.h>
#include <stdarg.h>

#include "LGFX.h"
#include "variants/Variant.h"

extern const uint8_t look_frames_start[] asm("look_frames_start");
extern const uint8_t look_frames_end[] asm("look_frames_end");
extern const uint8_t look_pal_start[] asm("look_pal_start");
extern const uint8_t look_pal_end[] asm("look_pal_end");

namespace {

constexpr int kSize = variant::SCREEN_SIZE;
static_assert(kSize == 240, "the frames are 240 x 240 (look_frames.py)");
constexpr int kPixels = kSize * kSize;
constexpr int kFrames = 20, kLooks = 3, kRoles = 66;
constexpr int kRoleBg = 54, kRoleLabel = 58;   // look_frames.py's ROLES: the ground and the ink of a look's title
constexpr int kLoopFirst = 2, kLoopN = 15;
constexpr int kDry = 0, kRain = 1;
constexpr int kSceneFrame[] = {kDry, 17, 18, 19};   // Bend (dry first), Dallas, patches terrain on, patches terrain off
constexpr int kScenes = sizeof kSceneFrame / sizeof kSceneFrame[0];
constexpr int64_t kLoopPeriodUs = 70000;   // 14.29 fps, the edition's loop (A8.20)
const char* const kLookName[kLooks] = {"dark", "a", "b"};
const char* const kLookTitle[kLooks] = {"DARK (TODAY)", "LIGHT (A)", "LIGHT (B)"};

LGFX tft;
LGFX_Sprite canvas(&tft);
bool ready = false;
uint16_t lut[kLooks][kRoles];   // RGB565, byte-swapped as a 16-bit sprite holds it (high byte first in memory)
int look = 0, scene = 0, frame = kDry;

struct LoopRec {
    uint8_t sha[32];
    int64_t startUs;
    uint32_t expandUs;
};
LoopRec recs[kLoopN];

// ---------------------------------------------------------------------------------------------- serial, paced
void Line(const char* fmt, ...)
{
    char buf[200];
    va_list ap;
    va_start(ap, fmt);
    const int n = vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    if (n <= 0) return;
    const size_t len = n < (int)sizeof buf ? (size_t)n : sizeof buf - 1;
    const uint32_t t0 = millis();   // wait (at most 20 ms) for room, so a host that is reading loses nothing
    while (Serial.availableForWrite() < (int)len && millis() - t0 < 20) vTaskDelay(1);
    Serial.write((const uint8_t*)buf, len);
}

void Hex(const uint8_t* s, char* out)
{
    static const char d[] = "0123456789abcdef";
    for (int i = 0; i < 32; ++i) {
        out[2 * i] = d[s[i] >> 4];
        out[2 * i + 1] = d[s[i] & 15];
    }
    out[64] = 0;
}

// ---------------------------------------------------------------------------------------------- drawing
uint32_t Expand(int f)
{
    const int64_t t0 = esp_timer_get_time();
    uint16_t* buf = (uint16_t*)canvas.getBuffer();
    const uint8_t* idx = look_frames_start + (size_t)f * kPixels;
    const uint16_t* l = lut[look];
    for (int i = 0; i < kPixels; ++i) buf[i] = l[idx[i]];
    return (uint32_t)(esp_timer_get_time() - t0);
}

void Sha(uint8_t out[32])
{
    mbedtls_sha256((const unsigned char*)canvas.getBuffer(), (size_t)kPixels * 2, out, 0);
}

void Show(int f)
{
    frame = f;
    const uint32_t us = Expand(f);
    canvas.pushSprite(0, 0);
    uint8_t sha[32];
    char hex[65];
    Sha(sha);
    Hex(sha, hex);
    Line("[look] look=%s frame=%d sha=%s expand_us=%lu\n", kLookName[look], f, hex, (unsigned long)us);
}

uint16_t Native(int k)   // a role's colour as LovyanGFX's drawing calls take it (native RGB565, not the buffer's order)
{
    const uint8_t* p = look_pal_start + (look * kRoles + k) * 3;
    return lgfx::color565(p[0], p[1], p[2]);
}

void Title()
{
    canvas.fillSprite(Native(kRoleBg));
    canvas.setTextDatum(middle_center);
    canvas.setTextSize(2);
    canvas.setTextColor(Native(kRoleLabel));
    canvas.drawString(kLookTitle[look], kSize / 2, kSize / 2);
    canvas.pushSprite(0, 0);
    Line("[look] title look=%s\n", kLookName[look]);
    delay(1000);
}

void PlayLoop()
{
    const int64_t t0 = esp_timer_get_time() + 1000;
    for (int k = 0; k < kLoopN; ++k) {
        const int64_t due = t0 + (int64_t)k * kLoopPeriodUs;
        while (esp_timer_get_time() < due - 2000) vTaskDelay(1);
        while (esp_timer_get_time() < due) {
        }
        LoopRec& r = recs[k];
        r.startUs = esp_timer_get_time();
        r.expandUs = Expand(kLoopFirst + k);
        canvas.pushSprite(0, 0);
        Sha(r.sha);
    }
    const int64_t end = t0 + (int64_t)kLoopN * kLoopPeriodUs;
    while (esp_timer_get_time() < end) vTaskDelay(1);
    Show(kRain);
    for (int k = 0; k < kLoopN; ++k) {   // printed only now: printing while it plays could pace it (A8.19)
        char hex[65];
        Hex(recs[k].sha, hex);
        Line("[look] lrec look=%s i=%d frame=%d sha=%s start_us=%lld expand_us=%lu\n", kLookName[look], k,
             kLoopFirst + k, hex, (long long)recs[k].startUs, (unsigned long)recs[k].expandUs);
    }
}

// ---------------------------------------------------------------------------------------------- the handlers
void NextLook(int dir)
{
    look = (look + dir + kLooks) % kLooks;
    Title();
    Show(frame);
}

void NextScene(int dir)
{
    scene = (scene + dir + kScenes) % kScenes;
    Show(kSceneFrame[scene]);
}

void Tap()
{
    if (scene != 0) {
        Show(frame);
        return;
    }
    if (frame == kDry) PlayLoop();
    else Show(kDry);
}

// ---------------------------------------------------------------------------------------------- touch
struct {
    bool down = false;
    int32_t x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    int idle = 0;
} touch;
uint32_t lastPollMs = 0, lastActivityMs = 0, lastNoSleepMs = 0;

void Classify(int dx, int dy)
{
    const int ax = abs(dx), ay = abs(dy);
    if (ax >= 40 && ax > ay) NextLook(dx < 0 ? +1 : -1);
    else if (ay >= 40 && ay > ax) NextScene(dy < 0 ? +1 : -1);
    else if (ax < 15 && ay < 15) Tap();
}

void PollTouch()
{
    int32_t x, y;
    if (tft.getTouch(&x, &y)) {
        if (!touch.down) {
            touch.down = true;
            touch.x0 = x;
            touch.y0 = y;
        }
        touch.x1 = x;
        touch.y1 = y;
        touch.idle = 0;
        lastActivityMs = millis();
    } else if (touch.down && ++touch.idle >= 2) {   // two idle polls: the finger is up
        touch.down = false;
        Classify(touch.x1 - touch.x0, touch.y1 - touch.y0);
    }
}

// The CST816's DisAutoSleep (0xFE), re-armed when a silent chip reset has cleared it -- TouchWatchdog's
// MaintainNoSleep, done here while the screen is idle (A12.27).
void MaintainNoSleep()
{
    const auto cur = lgfx::i2c::readRegister8(BLIPSCOPE_TOUCH_I2C_PORT, BLIPSCOPE_TOUCH_I2C_ADDR, 0xFE,
                                              BLIPSCOPE_TOUCH_FREQ);
    if (cur.has_value() && cur.value() != 0) return;
    lgfx::i2c::writeRegister8(BLIPSCOPE_TOUCH_I2C_PORT, BLIPSCOPE_TOUCH_I2C_ADDR, 0xFE, 0x01, 0, BLIPSCOPE_TOUCH_FREQ);
}

}  // namespace

void setup()
{
    Serial.begin(115200);
#if ARDUINO_USB_CDC_ON_BOOT
    Serial.setTxBufferSize(4096);
    Serial.setTxTimeoutMs(0);   // never block the screen on a PC that is not reading; Line() paces when one is
#endif
    delay(300);
    Line("\n[build] env=%s  Weatherscope light-look bench select (A12.27-A12.32)\n",
#ifdef BUILD_ENV
         BUILD_ENV
#else
         "weatherscope-look"
#endif
    );
    // the slot that booted, read after every write (A12.33, A12.34 item 3)
    const esp_partition_t* running = esp_ota_get_running_partition();
    Line("[ota-slot] boot running=%s @0x%06lx\n", running ? running->label : "?",
         running ? (unsigned long)running->address : 0UL);
    variant::BoardPreInit();
    const bool panelOk = tft.init();
    tft.setRotation(0);
    tft.invertDisplay(BLIPSCOPE_DISP_INVERT);
    tft.setBrightness(255);   // the edition's default
    tft.fillScreen(TFT_BLACK);
    const size_t psramBefore = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    canvas.setPsram(true);
    canvas.setColorDepth(16);
    const bool spriteOk = canvas.createSprite(kSize, kSize) != nullptr;
    const size_t frameBytes = (size_t)(look_frames_end - look_frames_start);
    const size_t palBytes = (size_t)(look_pal_end - look_pal_start);
    const bool dataOk = frameBytes == (size_t)kFrames * kPixels && palBytes == (size_t)kLooks * kRoles * 3;
    for (int l = 0; l < kLooks; ++l)
        for (int k = 0; k < kRoles; ++k) {
            const uint8_t* p = look_pal_start + (l * kRoles + k) * 3;
            const uint16_t v = lgfx::color565(p[0], p[1], p[2]);
            lut[l][k] = (uint16_t)((v << 8) | (v >> 8));
        }
    ready = panelOk && spriteOk && dataOk;
    Line("[look] ready=%d frames=%u looks=%d roles=%d frame_bytes=%u pal_bytes=%u psram_sprite=%u tft.init=%d\n",
         (int)ready, (unsigned)(frameBytes / kPixels), kLooks, kRoles, (unsigned)frameBytes, (unsigned)palBytes,
         (unsigned)(psramBefore - heap_caps_get_free_size(MALLOC_CAP_SPIRAM)), (int)panelOk);
    if (!ready) {
        tft.setTextColor(TFT_WHITE);
        tft.drawString("look data missing", 40, 110);
        return;
    }
    Show(kDry);
    lastActivityMs = millis();
}

void loop()
{
    if (!ready) {
        delay(100);
        return;
    }
    while (Serial.available()) {
        const int ch = Serial.read();
        if (ch == 'T') Tap();
        else if (ch == 'S') NextLook(+1);
        else if (ch == 'B') NextLook(-1);
        else if (ch == 'U') NextScene(+1);
        else if (ch == 'D') NextScene(-1);
        else if (ch == 'R') Show(frame);
        lastActivityMs = millis();
    }
    const uint32_t now = millis();
    if (now - lastPollMs >= 20) {
        lastPollMs = now;
        PollTouch();
    }
    if (!touch.down && now - lastActivityMs > 2000 && now - lastNoSleepMs > 10000) {
        lastNoSleepMs = now;
        MaintainNoSleep();
    }
    delay(1);
}
