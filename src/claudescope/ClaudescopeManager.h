#pragma once

#include <Arduino.h>
#include <vector>

#include "ConfigurationWebServer.h"
#include "OpenSkyAuthTokenHandler.h"
#include "HttpRequestManager.h"
#include "NtfyAlerter.h"
#include "LGFX.h"
#include "BandCanvas.h"
#include "ClaudescopeTheme.h"
#include "ClaudescopeFeedClient.h"

// FEATURE_CLAUDESCOPE top-level controller -- the Claudescope edition: a desk gauge for your live
// Claude usage limits (session + weekly), fed by the on-LAN claudescope-sidecar (which holds the
// OAuth token and republishes the undocumented usage window state as normalized JSON). The seventh
// sibling to AircraftManager / EamManager / SpaceManager / SeismicManager / BirdingManager /
// FishingManager, selected at compile time in main.cpp (same Initialise / Update / Draw surface,
// driven by the loop task, same shared infra).
//
// The UI is a dwell-timed auto-rotation that skips empty screens AND swipe-to-navigate (like Space),
// plus a tap-to-inspect detail-card overlay on the data screens (like Fishing). There is no aircraft
// PPI sweep -- main.cpp gates the sweep out of the FEATURE_CLAUDESCOPE build, so IsRadarView()/
// CurrentSweepAngle() are intentionally absent.
class ClaudescopeManager
{
public:
    ClaudescopeManager(ConfigurationWebServer& config, OpenSkyAuthTokenHandler& auth,
                       HttpRequestManager& httpManager, LGFX& tftGfx)
        : configServer(config), authHandler(auth), http(httpManager), tft(tftGfx),
          feed(httpManager)
    {
    }
    ~ClaudescopeManager() = default;

    void Initialise();
    void Update();
    void Draw(BandCanvas& backbuffer, bool firstPass);

private:
    // Screens. Order here is the canonical rotation order (Splash self-drops once data lands).
    enum class Screen : uint8_t { Session, Weekly, Clock, Splash, COUNT };

    ConfigurationWebServer& configServer;
    OpenSkyAuthTokenHandler& authHandler;   // reserved (parity with the other apps); unused here
    HttpRequestManager& http;
    LGFX& tft;
    ClaudescopeFeedClient feed;

    // ---- config-derived state (set in Initialise) ----
    claudescope::Palette palette = claudescope::PaletteDefault();
    String backendBaseUrl;              // the sidecar's address ("cl-base-url")
    long   tzOffsetSec = 0;             // local-clock offset for the Clock screen
    bool   hasLatLon = false;
    double deviceLat = 0.0, deviceLon = 0.0;
    std::vector<Screen> enabledOrder;   // in display order (all data screens are always enabled)
    uint8_t configuredBrightness = 255;
    bool autoDim = true;

    // ---- navigation / selection ----
    Screen current = Screen::Splash;    // cold-start: greet with the splash / setup prompt
    unsigned long lastAdvanceMs = 0;
    unsigned long lastInteractionMs = 0;
    bool inDetail = false;              // detail card shown over the current screen
    Screen detailFor = Screen::Session;

    // ---- brightness / night-dim ----
    uint8_t currentBrightness = 255;
    bool nightDim = false;
    unsigned long lastBrightnessCheck = 0;

    // ---- ntfy alerts (shared ntfy-topic key + POST pattern) ----
    String ntfyTopic;
    bool  alertSession = false;         // session limit crossed the threshold
    bool  alertWeek = false;            // weekly limit crossed the threshold
    float sessionPctThresh = 80.0f;     // "cl-session-pct"
    float weekPctThresh = 80.0f;        // "cl-week-pct"
    // edge state, seeded at first data so the backlog never fires.
    bool alertSeeded = false;
    int  lastSessionSide = 0;           // -1 below threshold / +1 at-or-above / 0 unknown
    int  lastWeekSide = 0;
    NtfyAlerter ntfy;                 // deferring ntfy sender (see NtfyAlerter.h)

    // ---- touch / gestures ----
    bool wasTouched = false;
    int touchStartX = 0, touchStartY = 0;
    int touchLastX = 0, touchLastY = 0;

    // rotation
    std::vector<Screen> BuildRotation() const;
    bool HasData(Screen s) const;
    void AdvanceRotation(int dir);
    void AutoRotate();

    // input
    void HandleTouch();
    void HandleTap(int tx, int ty);
    void ExitDetail() { inDetail = false; }

    // brightness / alerts
    void UpdateBrightness();
    float GlowFactor() const { return nightDim ? 0.5f : 1.0f; }
    void CheckAlerts();
    void SendNtfy(const String& title, const String& body, const String& tags, int priority);

    // screens (defined in ClaudescopeScreens.cpp)
    void DrawScreen(BandCanvas& c, Screen s);
    void DrawSession(BandCanvas& c);
    void DrawWeekly(BandCanvas& c);
    void DrawClock(BandCanvas& c);
    void DrawSplash(BandCanvas& c);
    void DrawDetailCard(BandCanvas& c);
    void DrawScreenDots(BandCanvas& c, const std::vector<Screen>& rot) const;

    // helpers
    void CenterText(BandCanvas& c, const String& s, int y, uint32_t color);
    void DrawRingGauge(BandCanvas& c, int cx, int cy, int rInner, int rOuter, float pct, uint32_t fill);
    static String FormatCountdown(long secsUntil);   // "2h06m" until a reset
    static String FormatResetClock(long epoch, long tzOffsetSec); // "Tue 18:00" local
};
