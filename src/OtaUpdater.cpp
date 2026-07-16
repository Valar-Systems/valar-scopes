#include "OtaUpdater.h"
#include "LGFX.h"
#include "Layout.h"
#include "OtaCerts.h"
#include "BootScreen.h"
#include "Board.h"
#include "variants/Variant.h"

#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <Preferences.h>
#include <time.h>

namespace {

// NVS namespace for the one-shot OTA memory report (see TakeOtaMemReport).
// It has to outlive the update itself: the attempt is recorded by the OLD
// firmware moments before the download, and reported by the NEW firmware --
// a successful OTA reboots straight into the new image, so nothing after
// httpUpdate.update() ever runs to record the happy path in RAM.
constexpr const char* OTA_MEM_NS = "ota-mem";

// Pre-arm the record BEFORE the download begins, as "incomplete". Whatever
// happens next leaves a truthful record: success reboots (the new firmware
// finalises it), failure rewrites it in this same boot, and a WDT/power-loss
// reboot mid-flash leaves "incomplete" standing -- which is precisely the
// verdict that case deserves and the only trace it would otherwise leave.
void NoteOtaAttempt(int fwTo, uint32_t preLargest)
{
    Preferences p;
    if (!p.begin(OTA_MEM_NS, false))
        return; // telemetry must never be a reason an update doesn't happen
    p.putInt("from", FW_VERSION);
    p.putInt("to", fwTo);
    p.putUInt("pre", preLargest);
    p.putUInt("post", 0); // 0 = not measured yet; filled at report time
    p.putString("res", "incomplete");
    p.end();
}

// Failure is recorded in the same boot, so post is the heap right after the
// attempt -- the interesting number when asking why an update didn't fit.
void NoteOtaFailed(int err)
{
    Preferences p;
    if (!p.begin(OTA_MEM_NS, false))
        return;
    p.putUInt("post", ESP.getMaxAllocHeap());
    p.putString("res", String("fail-") + err);
    p.end();
}

// "releases/latest/download/<asset>" always resolves to the newest published
// (non-draft, non-prerelease) release's asset, redirecting to the CDN.
//
// All SKUs are built and released together from one commit, so a single shared
// version.txt (an integer) gates everyone. Each SKU then downloads ITS OWN binary,
// named by its variant slug -- one SKU must never flash another's image. CI
// publishes firmware-<slug>.bin per SKU (see RELEASING.md).
//
// Bench builds may pin OTA to a specific (pre-)release via -DOTA_RELEASE_BASE
// (e.g. .../releases/download/<tag>): pre-releases never resolve through
// /latest, so a pre-gate test release is invisible to the fleet while a bench
// unit pointed at its tag exercises the full production OTA path.
#ifndef OTA_RELEASE_BASE
#define OTA_RELEASE_BASE "https://github.com/Valar-Systems/valar-scopes/releases/latest/download"
#endif
const char* VERSION_URL = OTA_RELEASE_BASE "/version.txt";

// FEATURE_EAM and the radar app can share a board (and thus a variant::SLUG) while shipping
// as separate products, so they ride separate OTA channels: FW_OTA_PREFIX is empty for the
// radar build and "eam-" for the EAM build, keeping firmware-s3-146.bin and
// firmware-eam-s3-146.bin distinct. A device only ever fetches its own channel's binary.
#ifndef FW_OTA_PREFIX
#define FW_OTA_PREFIX ""
#endif

String FirmwareUrl()
{
    return String(OTA_RELEASE_BASE "/firmware-") + FW_OTA_PREFIX + variant::SLUG + ".bin";
}

// X.509 validation needs a roughly-correct clock (cert validity periods), but the
// boot-time update check runs right after configTime() starts NTP in the background.
// Wait briefly for the first sync; give up if it never lands (no internet -- the
// version fetch would fail anyway) and let the daily re-check try again.
bool WaitForClock()
{
    constexpr time_t BUILD_ERA = 1750000000; // mid-2025; NTP-synced time is always past this
    for (int i = 0; i < 40; ++i) {           // up to ~10 s, usually syncs in 1-2 s
        if (time(nullptr) > BUILD_ERA)
            return true;
        delay(250);
    }
    return false;
}

void drawStatus(LGFX& tft, LGFX_Sprite& fb, const String& msg)
{
    // Compose full-frame through the backbuffer (SPD2010 drops direct partial
    // writes) and flush for the RGB panel -- the same path every boot screen uses.
    DrawCenteredScreen(tft, fb, lgfx::color888(0, 0, 0), lgfx::color888(0, 255, 0), msg.c_str());
    board::DisplayFlush(tft);
}

// Draw the "Updating firmware..." title plus a progress bar at `pct` (0-100),
// composed full-frame so it shows on every panel (see drawStatus / BootScreen.h).
void drawProgress(LGFX& tft, LGFX_Sprite& fb, int pct)
{
#if defined(BLIPSCOPE_PANEL_SPD2010)
    auto& g = fb;   // compose into the sprite, push even-aligned below
#else
    auto& g = tft;  // direct to the panel (flushed after)
#endif
    g.fillScreen(lgfx::color888(0, 0, 0));
    g.setTextColor(lgfx::color888(0, 255, 0));
    g.drawCenterString("Updating firmware...", SCREEN_SIZE_DIV_2, SCREEN_SIZE_DIV_2 - 24);

    const int barW = SCREEN_SIZE - 100;         // 140 on a 240 screen
    const int barX = (SCREEN_SIZE - barW) / 2;   // centred on any panel
    const int barY = SCREEN_SIZE_DIV_2 + 20;
    g.drawRect(barX, barY, barW, 14, lgfx::color888(0, 120, 0));
    g.fillRect(barX + 2, barY + 2, ((barW - 4) * pct) / 100, 10, lgfx::color888(0, 255, 0));

#if defined(BLIPSCOPE_PANEL_SPD2010)
    fb.pushSprite(0, 0);
#endif
    board::DisplayFlush(tft); // RGB panels: make the paint visible (no-op on SPI SKUs)
}

} // namespace

void MaybeUpdateFirmware(LGFX& tft, LGFX_Sprite& fb)
{
    // 1. read the latest published version. Unlike the feeds, OTA validates the
    // TLS chain against pinned roots (see OtaCerts.h): a spoofed binary is
    // persistent code execution, not just bad data on a screen.
    if (!WaitForClock()) {
        Serial.println("[ota] clock not synced; skipping update check");
        return;
    }

    WiFiClientSecure verClient;
    verClient.setCACert(OTA_ROOT_CAS);

    HTTPClient http;
    http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS); // GitHub redirects to its CDN
    http.setConnectTimeout(8000);
    http.setTimeout(8000);

    if (!http.begin(verClient, VERSION_URL)) {
        Serial.println("[ota] version check: begin failed");
        return;
    }

    const int code = http.GET();
    if (code != HTTP_CODE_OK) {
        Serial.printf("[ota] version check: HTTP %d\n", code);
        http.end();
        return;
    }

    const int latest = http.getString().toInt();
    http.end();

    Serial.printf("[ota] channel=%s%s current=%d latest=%d\n", FW_OTA_PREFIX, variant::SLUG, FW_VERSION, latest);
    if (latest <= FW_VERSION)
        return; // already up to date

    // 2. download + flash the new image, showing a progress bar on the screen
    Serial.println("[ota] newer firmware available -- updating");
    // Memory evidence through the whole download+flash window (gate item: a
    // production-shaped OTA must complete at the steady-state heap operating
    // point, and the ledger wants numbers, not just pass/fail). Serial carries
    // the fine-grained ladder for a bench unit; NVS carries the summary home
    // from the field (see TakeOtaMemReport) -- the same number in both, so the
    // fleet's report can be read against a bench capture directly.
    const uint32_t preLargest = ESP.getMaxAllocHeap();
    Serial.printf("[ota-mem] pre-update free=%u largest=%u\n",
                  ESP.getFreeHeap(), preLargest);
    NoteOtaAttempt(latest, preLargest);
    drawProgress(tft, fb, 0);

    WiFiClientSecure updClient;
    updClient.setCACert(OTA_ROOT_CAS);

    httpUpdate.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
    httpUpdate.rebootOnUpdate(true);
    // Repaint only on a whole-percent change: each paint is a full-frame push +
    // panel flush, and the callback fires per network chunk.
    static int lastPct = -1;
    lastPct = -1;
    httpUpdate.onProgress([&tft, &fb](int current, int total) {
        const int pct = total > 0 ? (int)((current * 100L) / total) : 0;
        if (pct != lastPct) {
            lastPct = pct;
            drawProgress(tft, fb, pct);
            // Heap sampled every 5% across download + OTA-partition writes:
            // the memory evidence the gate's OTA-at-steady-state leg exists for.
            if (pct % 5 == 0)
                Serial.printf("[ota-mem] pct=%d free=%u largest=%u\n",
                              pct, ESP.getFreeHeap(), ESP.getMaxAllocHeap());
        }
    });

    const String firmwareUrl = FirmwareUrl();
    Serial.printf("[ota] downloading %s\n", firmwareUrl.c_str());
    const t_httpUpdate_return ret = httpUpdate.update(updClient, firmwareUrl);
    if (ret == HTTP_UPDATE_FAILED) {
        Serial.printf("[ota] update failed (%d): %s\n",
                      httpUpdate.getLastError(), httpUpdate.getLastErrorString().c_str());
        NoteOtaFailed(httpUpdate.getLastError());
        drawStatus(tft, fb, "Update failed");
        delay(2000);
    }
    // HTTP_UPDATE_OK reboots automatically (rebootOnUpdate(true))
}

String TakeOtaMemReport()
{
    Preferences p;
    if (!p.begin(OTA_MEM_NS, false))
        return "";
    if (!p.isKey("res")) {
        p.end();
        return ""; // no OTA attempted since the last report: send nothing
    }

    const int      from = p.getInt("from", 0);
    const int      to   = p.getInt("to", 0);
    const uint32_t pre  = p.getUInt("pre", 0);
    uint32_t       post = p.getUInt("post", 0);
    String         res  = p.getString("res", "");

    // Finalise the happy path. The old firmware could not: a successful update
    // reboots inside httpUpdate.update(), so the pre-armed "incomplete" record
    // is the last thing it wrote. If that record names the version now running,
    // the update plainly worked -- and anything else that rebooted us (watchdog,
    // power loss) leaves "incomplete" standing, correctly.
    if (res == "incomplete" && to == FW_VERSION)
        res = "ok";
    if (post == 0)
        post = ESP.getMaxAllocHeap(); // ok/incomplete: this check-in is the "after"

    p.clear(); // fire-once: cleared whether or not the request that carries it lands
    p.end();

    return String(from) + "," + String(to) + "," + String(pre) + "," + String(post) + "," + res;
}
