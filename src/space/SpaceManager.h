#pragma once

#include <Arduino.h>
#include <vector>

#include "ConfigurationWebServer.h"
#include "OpenSkyAuthTokenHandler.h"
#include "HttpRequestManager.h"
#include "LGFX.h"
#include "BandCanvas.h"
#include "SpaceTheme.h"

// FEATURE_SPACE top-level controller -- the Spacescope app: a desk window onto live space data
// (ISS, rocket launches, space weather, deep-space probes) built from the same boards + shared
// infra as the radar and the EAM monitor.
//
// Third sibling to AircraftManager / EamManager, selected at compile time in main.cpp: same
// lifecycle (Initialise / Update / Draw) driven by the loop task, and the same shared infra
// (ConfigurationWebServer, HttpRequestManager's single TLS client, OpenSky OAuth). It renders
// space screens instead of the radar; from Stage 2 it pulls from free public space APIs via a
// SpaceFeedClient (mirroring EamFeedClient).
//
// Stage 1 (this file) is the skeleton: the FEATURE_SPACE gate, the config form, the OTA channel,
// and the rotation / touch / brightness shell with a splash + a Zulu (UTC) clock. Each later stage
// is purely additive: a feed in SpaceFeedClient + a Screen enum entry + a DrawX() + a HasData()
// case; nothing else in here changes.
class SpaceManager
{
public:
    SpaceManager(ConfigurationWebServer& config, OpenSkyAuthTokenHandler& auth,
                 HttpRequestManager& httpManager, LGFX& tftGfx)
        : configServer(config), authHandler(auth), http(httpManager), tft(tftGfx)
    {
    }
    ~SpaceManager() = default;

    void Initialise();
    void Update();
    void Draw(BandCanvas& backbuffer, bool firstPass);

private:
    // The screens. Order here is the default rotation order; the user can enable/disable and
    // reorder via the "space-screens" CSV config key. Clock is the idle screen and is always
    // available, so BuildRotation() is never empty. Later stages add ISS / Launch / Kp / DSN / ...
    enum class Screen : uint8_t {
        Splash, Clock, COUNT
    };

    ConfigurationWebServer& configServer;
    OpenSkyAuthTokenHandler& authHandler;   // reserved for Stage-2 OpenSky-direct feeds (ABNCP-style)
    HttpRequestManager& http;
    LGFX& tft;

    // ---- config-derived state (set in Initialise) ----
    space::Palette palette = space::PaletteDefault();
    String backendBaseUrl;                   // optional Valar space-feed backend; empty = direct APIs
    std::vector<Screen> enabledOrder;        // enabled screens in display order
    bool hasLatLon = false;
    double deviceLat = 0.0, deviceLon = 0.0;
    uint8_t configuredBrightness = 255;
    bool autoDim = true;

    // ---- rotation / navigation ----
    Screen current = Screen::Clock;
    unsigned long lastAdvanceMs = 0;         // last auto-rotate tick
    unsigned long lastInteractionMs = 0;     // last touch; pauses auto-rotate briefly

    // ---- brightness / night-dim ----
    uint8_t currentBrightness = 255;
    bool nightDim = false;
    unsigned long lastBrightnessCheck = 0;

    // ---- touch / gestures ----
    bool wasTouched = false;
    int touchStartX = 0, touchStartY = 0;
    int touchLastX = 0, touchLastY = 0;

    // rotation helpers
    std::vector<Screen> BuildRotation() const;   // enabled screens that currently have data
    bool HasData(Screen s) const;
    void AdvanceRotation(int dir);               // +1 next, -1 prev (manual)
    void AutoRotate();                           // dwell-timed auto advance, skipping empty

    // input + chrome
    void HandleTouch();
    void DrawScreenDots(BandCanvas& c, const std::vector<Screen>& rot) const;

    // screens
    void DrawSplash(BandCanvas& c);
    void DrawClock(BandCanvas& c);

    // brightness
    void UpdateBrightness();
    float GlowFactor() const { return nightDim ? 0.5f : 1.0f; }

    // small helpers
    static std::vector<String> SplitList(const String& s, bool lower);
    void CenterText(BandCanvas& c, const String& s, int y, uint32_t color);
};
