#pragma once

#include <Arduino.h>

#include "ConfigurationWebServer.h"
#include "OpenSkyAuthTokenHandler.h"
#include "HttpRequestManager.h"
#include "LGFX.h"
#include "BandCanvas.h"

// FEATURE_REMOTE top-level controller -- the Remote edition: a desk control for Valar window and
// curtain openers (Glasscalibur, the Ropener) on the LAN. Selected at compile time in main.cpp, with
// the same Initialise / Update / Draw surface as every other edition, driven by the loop task.
//
// SCAFFOLD (docs/remote-edition.md section 3). This is the edition's shell and nothing more: a splash
// that says no devices are added yet and where to add them, brightness with the shared SolarDim
// night dim, and a touch poll that feeds TouchWatchdog exactly as the radar does: on the s3-128, wedge
// detection and the recovery ladder (today the radar is its only other caller). NOT a no-sleep re-arm:
// MaintainNoSleep is compiled out of every env (none defines CST816_DISABLE_AUTOSLEEP). The chip's
// DisAutoSleep=1 comes from its factory config, which INCOMING-INSPECTION.md gates per batch. Touch does
// nothing else yet; devices, control and the safety policy arrive with the feature work. There is no
// PPI sweep: main.cpp gates it out of this build like the other editions.
class RemoteManager
{
public:
    RemoteManager(ConfigurationWebServer& config, OpenSkyAuthTokenHandler& auth,
                  HttpRequestManager& httpManager, LGFX& tftGfx)
        : configServer(config), authHandler(auth), http(httpManager), tft(tftGfx)
    {
    }
    ~RemoteManager() = default;

    void Initialise();
    void Update();
    void Draw(BandCanvas& backbuffer, bool firstPass);

    // main.cpp asks before arming the quiet hour's update reboot (src/remote/UpdateHold.h): true while
    // a device this remote controls is moving or was just commanded. The scaffold controls nothing.
    bool HoldsUpdate() const { return false; }

private:
    ConfigurationWebServer& configServer;
    OpenSkyAuthTokenHandler& authHandler;  // reserved (parity with the other apps); unused here
    HttpRequestManager& http;
    LGFX& tft;

    // ---- display (config-derived, set in Initialise) ----
    uint8_t configuredBrightness = 255;
    bool autoDim = true;
    bool hasLatLon = false;
    double deviceLat = 0.0, deviceLon = 0.0;

    uint8_t currentBrightness = 255;
    bool nightDim = false;
    unsigned long lastBrightnessCheck = 0;

    void UpdateBrightness();
    void PollTouch();
    float GlowFactor() const { return nightDim ? 0.5f : 1.0f; }

    void DrawSplash(BandCanvas& c);
    void CenterText(BandCanvas& c, const String& s, int y, uint32_t color);
};
