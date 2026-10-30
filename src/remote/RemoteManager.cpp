#include "RemoteManager.h"
#include "TouchPoll.h"
#include "TouchWatchdog.h"
#include "SolarDim.h"
#include "DeviceIdentity.h"

#include <time.h>

#include "Layout.h"

namespace {

// Scaffold palette (RGB888). The feature work replaces this with the edition's own theme.
constexpr uint32_t FG     = 0xE6EDEA;
constexpr uint32_t DIM    = 0x6B7F72;
constexpr uint32_t ACCENT = 0xA3E635;

uint32_t Scale(uint32_t rgb, float f)
{
    const uint32_t r = (uint32_t)(((rgb >> 16) & 0xFF) * f);
    const uint32_t g = (uint32_t)(((rgb >> 8) & 0xFF) * f);
    const uint32_t b = (uint32_t)((rgb & 0xFF) * f);
    return (r << 16) | (g << 8) | b;
}

}  // namespace

void RemoteManager::Initialise()
{
    auto boolCfg = [&](const char* k, bool def) {
        const String v = configServer.GetStoredString(k);
        return v.isEmpty() ? def : (v == "true");
    };

    const String br = configServer.GetStoredString("brightness");
    configuredBrightness = br.isEmpty() ? 255 : (uint8_t)constrain(br.toInt(), 10, 255);
    autoDim = boolCfg("autodim", true);

    const String latStr = configServer.GetStoredString("latitude");
    const String lonStr = configServer.GetStoredString("longitude");
    hasLatLon = latStr.length() && lonStr.length();
    deviceLat = latStr.toDouble();
    deviceLon = lonStr.toDouble();

    currentBrightness = configuredBrightness;
    tft.setBrightness(currentBrightness);
    lastBrightnessCheck = 0;

    // Flags only: a location is a record of where somebody lives (CLAUDE.md, telemetry and logging).
    Serial.printf("[remote] init; devices=0 autodim=%d location=%s\n",
                  autoDim ? 1 : 0, hasLatLon ? "set" : "unset");
}

void RemoteManager::Update()
{
    UpdateBrightness();
    PollTouch();
}

void RemoteManager::Draw(BandCanvas& backbuffer, bool /*firstPass*/)
{
    DrawSplash(backbuffer);
}

// ----------------------------------------------------------------------------- input

void RemoteManager::PollTouch()
{
    // The shared poll (TouchPoll.h), ungated on every S3, and the CST816 supervisor fed exactly as the
    // radar feeds it (AircraftManager's touch poll): inert unless variant::TOUCH_WATCHDOG, which is the
    // s3-128. Nothing acts on a touch yet.
    int32_t tx = 0, ty = 0;
    const TouchPoll poll = ReadTouch(tft, http, tx, ty);
    if (poll == TouchPoll::Skipped) return;
    const bool touched = (poll == TouchPoll::Touched);
    if constexpr (variant::TOUCH_WATCHDOG)
        TouchWatchdog::OnPoll(tft, touched, true); // no bus serialization on these boards
}

// ----------------------------------------------------------------------------- brightness

void RemoteManager::UpdateBrightness()
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

void RemoteManager::DrawSplash(BandCanvas& c)
{
    const float gf = GlowFactor();
    const uint32_t fg     = Scale(FG, gf);
    const uint32_t dim    = Scale(DIM, gf);
    const uint32_t accent = Scale(ACCENT, gf);

    c.setTextSize(4);
    CenterText(c, "Remote", SCREEN_SIZE_DIV_2 - 44, fg);
    c.setTextSize(1);
    CenterText(c, "windows & curtains", SCREEN_SIZE_DIV_2 - 6, dim);
    CenterText(c, "no devices added yet", SCREEN_SIZE_DIV_2 + 30, accent);

    CenterText(c, "add them at", SCREEN_SIZE - 52, dim);
    String host = "http://" + DeviceIdentity::Name() + ".local";
    host.toLowerCase();
    CenterText(c, host, SCREEN_SIZE - 32, dim);
}

void RemoteManager::CenterText(BandCanvas& c, const String& s, int y, uint32_t color)
{
    c.setTextColor(color);
    c.drawString(s, SCREEN_SIZE_DIV_2 - c.textWidth(s) / 2, y);
}
