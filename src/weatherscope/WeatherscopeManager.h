#pragma once

#include <Arduino.h>

#include "ConfigurationWebServer.h"
#include "OpenSkyAuthTokenHandler.h"
#include "HttpRequestManager.h"
#include "LGFX.h"
#include "BandCanvas.h"
#include "WxFeedFactory.h"

// FEATURE_WEATHERSCOPE top-level controller: the Weatherscope edition, a desk weather radar. It is a
// sibling of AircraftManager and the other editions' managers, selected at compile time in main.cpp,
// with the same Initialise / Update / Draw surface, driven by the loop task on the same shared infra.
//
// Stage 3 step 3.2 (docs/weatherscope-stage3.md, Addendum 6) is the scaffolding only: a splash, then
// a status face (the device name, Wi-Fi state, the config-page URL and the UTC time). No weather
// screen is drawn until 3.3. The feeds are reached only through wx::MakeFeeds(), never by name (D1).
// There is no aircraft PPI sweep, so main.cpp gates the sweep out of this build.
class WeatherscopeManager
{
public:
    WeatherscopeManager(ConfigurationWebServer& config, OpenSkyAuthTokenHandler& /*auth*/,
                        HttpRequestManager& /*httpManager*/, LGFX& tftGfx)
        : configServer(config), tft(tftGfx), feeds(wx::MakeFeeds())
    {
    }

    void Initialise();
    void Update();
    void Draw(BandCanvas& backbuffer, bool firstPass);

private:
    ConfigurationWebServer& configServer;
    LGFX& tft;
    wx::Feeds feeds;

    bool splashStarted = false;     // latched at the first Initialise (boot), never re-armed
    bool splashDone = false;        // latched when the splash ends, so a millis() wrap can't replay it
    unsigned long splashStartMs = 0;

    // config-derived (set in Initialise; re-read on every config save)
    bool hasLatLon = false;
    double deviceLat = 0.0, deviceLon = 0.0;
    uint8_t configuredBrightness = 255;
    bool autoDim = true;

    // brightness / night-dim
    uint8_t currentBrightness = 255;
    bool nightDim = false;
    unsigned long lastBrightnessCheck = 0;

    void UpdateBrightness();
    void LogFeeds() const;

    void DrawSplash(BandCanvas& c);
    void DrawStatus(BandCanvas& c);
    void CenterText(BandCanvas& c, const String& s, int y, uint32_t color);
};
