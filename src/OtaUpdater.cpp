#include "OtaUpdater.h"
#include "LGFX.h"
#include "Layout.h"
#include "OtaCerts.h"
#include "variants/Variant.h"

#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <time.h>

namespace {

// "releases/latest/download/<asset>" always resolves to the newest published
// (non-draft, non-prerelease) release's asset, redirecting to the CDN.
//
// All SKUs are built and released together from one commit, so a single shared
// version.txt (an integer) gates everyone. Each SKU then downloads ITS OWN binary,
// named by its variant slug -- one SKU must never flash another's image. CI
// publishes firmware-<slug>.bin per SKU (see RELEASING.md).
const char* VERSION_URL = "https://github.com/Valar-Systems/valar-scopes/releases/latest/download/version.txt";

// FEATURE_EAM and the radar app can share a board (and thus a variant::SLUG) while shipping
// as separate products, so they ride separate OTA channels: FW_OTA_PREFIX is empty for the
// radar build and "eam-" for the EAM build, keeping firmware-s3-146.bin and
// firmware-eam-s3-146.bin distinct. A device only ever fetches its own channel's binary.
#ifndef FW_OTA_PREFIX
#define FW_OTA_PREFIX ""
#endif

String FirmwareUrl()
{
    return String("https://github.com/Valar-Systems/valar-scopes/releases/latest/download/firmware-")
           + FW_OTA_PREFIX + variant::SLUG + ".bin";
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

void drawStatus(LGFX& tft, const String& msg)
{
    tft.fillScreen(lgfx::color888(0, 0, 0));
    tft.setTextColor(lgfx::color888(0, 255, 0));
    tft.drawCentreString(msg, SCREEN_SIZE_DIV_2, SCREEN_SIZE_DIV_2 - 12);
}

} // namespace

void MaybeUpdateFirmware(LGFX& tft)
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
    drawStatus(tft, "Updating firmware...");

    WiFiClientSecure updClient;
    updClient.setCACert(OTA_ROOT_CAS);

    httpUpdate.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
    httpUpdate.rebootOnUpdate(true);
    httpUpdate.onProgress([&tft](int current, int total) {
        const int pct = total > 0 ? (int)((current * 100L) / total) : 0;
        const int barW = SCREEN_SIZE - 100;        // 140 on a 240 screen
        const int barX = (SCREEN_SIZE - barW) / 2;  // centred on any panel
        const int barY = SCREEN_SIZE_DIV_2 + 20;
        tft.fillRect(barX, barY, barW, 14, lgfx::color888(0, 40, 0));
        tft.fillRect(barX + 2, barY + 2, ((barW - 4) * pct) / 100, 10, lgfx::color888(0, 255, 0));
    });

    const String firmwareUrl = FirmwareUrl();
    Serial.printf("[ota] downloading %s\n", firmwareUrl.c_str());
    const t_httpUpdate_return ret = httpUpdate.update(updClient, firmwareUrl);
    if (ret == HTTP_UPDATE_FAILED) {
        Serial.printf("[ota] update failed (%d): %s\n",
                      httpUpdate.getLastError(), httpUpdate.getLastErrorString().c_str());
        drawStatus(tft, "Update failed");
        delay(2000);
    }
    // HTTP_UPDATE_OK reboots automatically (rebootOnUpdate(true))
}
