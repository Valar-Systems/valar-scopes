#include "OtaUpdater.h"
#include "LGFX.h"
#include "Layout.h"
#include "variants/Variant.h"

#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>

namespace {

// "releases/latest/download/<asset>" always resolves to the newest published
// (non-draft, non-prerelease) release's asset, redirecting to the CDN.
//
// All SKUs are built and released together from one commit, so a single shared
// version.txt (an integer) gates everyone. Each SKU then downloads ITS OWN binary,
// named by its variant slug -- a C3 must never flash an S3 image and vice-versa. CI
// publishes firmware-<slug>.bin per SKU (see RELEASING.md). For migrating devices that
// shipped before per-SKU naming, the release also keeps a legacy firmware.bin alias.
const char* VERSION_URL = "https://github.com/Valar-Systems/Blipscope/releases/latest/download/version.txt";

// FEATURE_EAM and the radar app can share a board (and thus a variant::SLUG) while shipping
// as separate products, so they ride separate OTA channels: FW_OTA_PREFIX is empty for the
// radar build and "eam-" for the EAM build, keeping firmware-c3-128.bin and
// firmware-eam-c3-128.bin distinct. A device only ever fetches its own channel's binary.
#ifndef FW_OTA_PREFIX
#define FW_OTA_PREFIX ""
#endif

String FirmwareUrl()
{
    return String("https://github.com/Valar-Systems/Blipscope/releases/latest/download/firmware-")
           + FW_OTA_PREFIX + variant::SLUG + ".bin";
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
    // 1. read the latest published version. setInsecure() skips certificate
    // validation -- acceptable here, matching the rest of the device's HTTPS.
    WiFiClientSecure verClient;
    verClient.setInsecure();

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
    updClient.setInsecure();

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
