#pragma once

#include <Arduino.h>
#include <vector>
#include <utility>

#include "ConfigurationWebServer.h"
#include "OpenSkyAuthTokenHandler.h"
#include "HttpRequestManager.h"
#include "LGFX.h"
#include "BandCanvas.h"
#include "FishingTheme.h"
#include "FishingFeedClient.h"

// FEATURE_FISHING top-level controller -- the Fishing edition (product name "Reelscope"): a fishing-
// conditions console for the round AMOLED, covering BOTH freshwater (USGS) and saltwater (NOAA CO-OPS
// + NDBC). The sixth sibling to AircraftManager / EamManager / SpaceManager / SeismicManager /
// BirdingManager, selected at compile time in main.cpp (same Initialise / Update / Draw surface,
// driven by the loop task, same shared infra).
//
// The UI is a HYBRID (like Birding): a dwell-timed auto-rotation that skips empty/disabled dials AND
// swipe-to-navigate, plus a tap-to-inspect detail-card overlay on any dial. There is no aircraft PPI
// sweep (fishing data is a handful of fixed stations, not moving contacts) -- main.cpp gates the sweep
// out of the FEATURE_FISHING build, so IsRadarView()/CurrentSweepAngle() are intentionally absent.
class FishingManager
{
public:
    FishingManager(ConfigurationWebServer& config, OpenSkyAuthTokenHandler& auth,
                   HttpRequestManager& httpManager, LGFX& tftGfx)
        : configServer(config), authHandler(auth), http(httpManager), tft(tftGfx),
          feed(httpManager)
    {
    }
    ~FishingManager() = default;

    void Initialise();
    void Update();
    void Draw(BandCanvas& backbuffer, bool firstPass);

private:
    // Dial screens. Order here is the canonical rotation order (Splash self-drops once data lands).
    enum class Screen : uint8_t { Tide, Flow, Temp, Solunar, Weather, Moon, Clock, Splash, COUNT };

    ConfigurationWebServer& configServer;
    OpenSkyAuthTokenHandler& authHandler;   // reserved (parity with the other apps); unused here
    HttpRequestManager& http;
    LGFX& tft;
    FishingFeedClient feed;

    // ---- config-derived state (set in Initialise) ----
    fishing::Palette palette = fishing::PaletteDefault();
    String backendBaseUrl;
    uint8_t waterType = FishingFeedClient::BOTH;
    bool hasLatLon = false;
    double deviceLat = 0.0, deviceLon = 0.0;
    long tzOffsetSec = 0;                 // local-clock offset for the Clock/day boundaries
    String usgsSite, noaaStation, buoyId; // primary ids (for on-screen station labels)
    float flowAlertCfs = NAN;             // fi-flow-cfs threshold
    float tempLoF = NAN, tempHiF = NAN;   // active-feeding water-temp band
    std::vector<Screen> enabledOrder;     // per-view toggles, in display order
    uint8_t configuredBrightness = 255;
    bool autoDim = true;

    // ---- computed on-device ----
    fishing::SolunarDay today;
    unsigned long lastSolunarCalc = 0;

    // ---- navigation / selection ----
    Screen current = Screen::Splash;      // cold-start: greet with the splash / setup prompt
    unsigned long lastAdvanceMs = 0;
    unsigned long lastInteractionMs = 0;
    bool inDetail = false;                // detail card shown over the current dial
    Screen detailFor = Screen::Tide;

    // ---- brightness / night-dim ----
    uint8_t currentBrightness = 255;
    bool nightDim = false;
    unsigned long lastBrightnessCheck = 0;

    // ---- ntfy alerts (shared ntfy-topic key + POST pattern) ----
    String ntfyTopic;
    bool alertFlow = false;               // river crossed the CFS threshold
    bool alertTemp = false;               // water temp entered the active-feeding band
    bool alertSolunar = false;            // a major bite window is starting
    // edge state, seeded at first data so the backlog never fires.
    bool alertSeeded = false;
    int  lastFlowSide = 0;                // -1 below threshold / +1 above / 0 unknown
    int  lastTempSide = 0;                // -1 out of band / +1 in band / 0 unknown
    long lastSolunarAlertEpoch = 0;       // start epoch of the last-alerted major window
    unsigned long lastNotifyMs = 0;

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
    void RecomputeSolunar(bool force);
    void CheckAlerts();
    void SendNtfy(const String& title, const String& body, const String& tags, int priority);

    // water-temp resolution (fresh USGS / salt CO-OPS / buoy fallback)
    bool BestWaterTempF(float& outF, const char*& outSource) const;

    // screens (defined in FishingScreens.cpp)
    void DrawTide(BandCanvas& c);
    void DrawFlow(BandCanvas& c);
    void DrawTemp(BandCanvas& c);
    void DrawSolunar(BandCanvas& c);
    void DrawWeather(BandCanvas& c);
    void DrawMoon(BandCanvas& c);
    void DrawClock(BandCanvas& c);
    void DrawSplash(BandCanvas& c);
    void DrawDetailCard(BandCanvas& c);
    void DrawScreenDots(BandCanvas& c, const std::vector<Screen>& rot) const;

    // helpers
    void DrawScreen(BandCanvas& c, Screen s);
    void CenterText(BandCanvas& c, const String& s, int y, uint32_t color);
    static String FormatClock(long epoch, long tzOffsetSec);
    static String FormatCountdown(long secsUntil);
    static String FormatAgo(long epochSecs);
};
