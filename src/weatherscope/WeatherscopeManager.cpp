#include "WeatherscopeManager.h"

#include <WiFi.h>
#include <time.h>

#include "DeviceIdentity.h"
#include "Layout.h"
#include "SolarDim.h"
#include "WxCrop.h"

namespace {

constexpr unsigned long SPLASH_MS = 4000;  // the splash, once per boot, before the status face

const uint32_t kFg     = lgfx::color888(230, 237, 234);
const uint32_t kDim    = lgfx::color888(130, 150, 160);
const uint32_t kAccent = lgfx::color888(90, 200, 255);
const uint32_t kWarn   = lgfx::color888(255, 176, 40);

}  // namespace

void WeatherscopeManager::Initialise()
{
    // Called again after every config save (main.cpp): the splash belongs to boot only.
    if (!splashStarted) {
        splashStarted = true;
        splashStartMs = millis();
        LogFeeds();
    }

    const String latStr = configServer.GetStoredString("latitude");
    const String lonStr = configServer.GetStoredString("longitude");
    hasLatLon = latStr.length() && lonStr.length();
    deviceLat = latStr.toDouble();
    deviceLon = lonStr.toDouble();

    const String br = configServer.GetStoredString("brightness");
    configuredBrightness = br.isEmpty() ? 255 : (uint8_t)constrain(br.toInt(), 10, 255);
    const String ad = configServer.GetStoredString("autodim");
    autoDim = ad.isEmpty() || ad == "true";

    currentBrightness = configuredBrightness;
    tft.setBrightness(currentBrightness);
    lastBrightnessCheck = 0;

    // Flags only: the location itself is never logged.
    Serial.printf("[wx] init; location=%s autodim=%d brightness=%u\n",
                  hasLatLon ? "set" : "unset", (int)autoDim, (unsigned)configuredBrightness);
}

void WeatherscopeManager::Update()
{
    UpdateBrightness();
}

void WeatherscopeManager::Draw(BandCanvas& backbuffer, bool /*firstPass*/)
{
    if (!splashDone && millis() - splashStartMs < SPLASH_MS) {
        DrawSplash(backbuffer);
        return;
    }
    splashDone = true;
    DrawStatus(backbuffer);
}

// One line on the serial port at boot, proving the three feeds answer through the factory. It
// prints sizes, counts and times only: no coordinate from the crop's header.
void WeatherscopeManager::LogFeeds() const
{
    const wx::CropBytes crop = feeds.radar.Latest();
    wxcrop::Header h;
    const wxcrop::Status s = wxcrop::Validate(crop.data, crop.size, &h);
    const wx::Observation o = feeds.obs.Latest();
    const wx::AlertList a = feeds.alerts.Active();
    Serial.printf("[wx] feeds: crop %u B %s %ux%u frame=%lu | obs valid=%d t=%ld present=%u | alerts=%u%s%s\n",
                  (unsigned)crop.size, wxcrop::StatusName(s), (unsigned)h.rows, (unsigned)h.cols,
                  (unsigned long)h.frameEpoch, (int)o.valid, (long)o.timeUtc, (unsigned)o.presentCount,
                  (unsigned)a.count, a.count ? " first=" : "", a.count ? a.items[0].event : "");
}

// ----------------------------------------------------------------------------- brightness

void WeatherscopeManager::UpdateBrightness()
{
    if (lastBrightnessCheck != 0 && millis() - lastBrightnessCheck < 20000) return;
    lastBrightnessCheck = millis();

    bool night = false;
    if (autoDim && hasLatLon) {
        const time_t utc = time(nullptr);
        if (utc > 1600000000)
            night = SunElevationDeg(deviceLat, deviceLon, utc) < -0.833f;
    }
    nightDim = night;

    uint8_t target = configuredBrightness;
    if (night) {
        target = configuredBrightness / 5;
        if (target < 10) target = 10;
    }
    if (target != currentBrightness) {
        currentBrightness = target;
        tft.setBrightness(target);
    }
}

// ----------------------------------------------------------------------------- screens

void WeatherscopeManager::DrawSplash(BandCanvas& c)
{
    c.setTextSize(3);
    CenterText(c, "Weatherscope", SCREEN_SIZE_DIV_2 - 24, kFg);
    c.setTextSize(1);
    CenterText(c, "weather radar", SCREEN_SIZE_DIV_2 + 12, kDim);
}

// Step 3.2's only working face: who the device is, whether it is online, where its settings are,
// and the time it believes it is. No weather is drawn until 3.3.
void WeatherscopeManager::DrawStatus(BandCanvas& c)
{
    c.setTextSize(2);
    CenterText(c, "Weatherscope", SCREEN_SIZE / 5, kFg);

    const bool online = WiFi.status() == WL_CONNECTED;
    CenterText(c, online ? "Wi-Fi connected" : "No Wi-Fi", SCREEN_SIZE * 34 / 100, online ? kAccent : kWarn);
    c.setTextSize(1);
    if (online) CenterText(c, String(WiFi.RSSI()) + " dBm", SCREEN_SIZE * 34 / 100 + 20, kDim);

    const time_t utc = time(nullptr);
    if (utc < 1600000000) {
        c.setTextSize(2);
        CenterText(c, "syncing time", SCREEN_SIZE_DIV_2 + 4, kDim);
    } else {
        struct tm t;
        gmtime_r(&utc, &t);
        char hms[12];
        snprintf(hms, sizeof(hms), "%02d:%02d:%02d", t.tm_hour, t.tm_min, t.tm_sec);
        c.setTextSize(3);
        CenterText(c, hms, SCREEN_SIZE_DIV_2 + 4, kFg);
        c.setTextSize(1);
        CenterText(c, "UTC", SCREEN_SIZE_DIV_2 + 32, kDim);
    }

    String url = "http://" + DeviceIdentity::Name() + ".local";
    url.toLowerCase();
    c.setTextSize(1);
    CenterText(c, url, SCREEN_SIZE * 76 / 100, kDim);
}

void WeatherscopeManager::CenterText(BandCanvas& c, const String& s, int y, uint32_t color)
{
    c.setTextColor(color);
    c.drawString(s, SCREEN_SIZE_DIV_2 - c.textWidth(s) / 2, y);
}
