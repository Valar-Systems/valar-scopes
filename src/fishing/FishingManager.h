#pragma once

#include <Arduino.h>
#include <vector>
#include <utility>

#include "ConfigurationWebServer.h"
#include "OpenSkyAuthTokenHandler.h"
#include "HttpRequestManager.h"
#include "NtfyAlerter.h"
#include "LGFX.h"
#include "BandCanvas.h"
#include "FishingTheme.h"
#include "FishingFeedClient.h"
#include "FishingLogbook.h"

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
    enum class Screen : uint8_t { Tide, Flow, Temp, Solunar, Weather, Moon, CatchLog, Clock, Splash, COUNT };

    ConfigurationWebServer& configServer;
    OpenSkyAuthTokenHandler& authHandler;   // reserved (parity with the other apps); unused here
    HttpRequestManager& http;
    LGFX& tft;
    FishingFeedClient feed;
    FishingLogbook logbook;                 // persistent catch log (fi-log NVS namespace)

    // ---- config-derived state (set in Initialise) ----
    fishing::Palette palette = fishing::PaletteDefault();
    String backendBaseUrl;
    uint8_t waterType = FishingFeedClient::BOTH;
    bool hasLatLon = false;
    double deviceLat = 0.0, deviceLon = 0.0;
    long tzOffsetSec = 0;                 // local-clock offset for the Clock/day boundaries
    String usgsSite, noaaStation, buoyId; // primary ids (for on-screen station labels)
    bool imperial = true;                 // fi-units: ft/degF/mph/inHg vs m/degC/(km/h)/hPa
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
    bool alertBaro = false;               // barometer falling fast (front moving in)
    bool alertTide = false;               // a hi/lo tide is turning soon
    bool chimeOnAlert = false;            // also chirp the speaker on an alert (HAS_AUDIO only)
    // edge state, seeded at first data so the backlog never fires.
    bool alertSeeded = false;
    int  lastFlowSide = 0;                // -1 below threshold / +1 above / 0 unknown
    int  lastTempSide = 0;                // -1 out of band / +1 in band / 0 unknown
    long lastSolunarAlertEpoch = 0;       // start epoch of the last-alerted major window
    bool baroAlerted = false;             // a fast-fall episode already alerted (reset when it eases)
    long lastAlertedTideEpoch = 0;        // tide event already alerted for
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
    void RecomputeSolunar(bool force);
    void CheckAlerts();
    void SendNtfy(const String& title, const String& body, const String& tags, int priority);

    // water-temp resolution -- returns a value in the CONFIGURED display units (F/C). Priority:
    // CO-OPS station gauge > NDBC buoy > Open-Meteo Marine SST > USGS river (fresh).
    bool BestWaterTemp(float& outVal, const char*& outSource) const;
    // Waves resolution in display units (ft/m). NDBC buoy (real, with period) > Open-Meteo Marine.
    bool BestWaves(float& waveDisp, float& periodS, const char*& source) const;
    // A solunar major/minor window is active right now (drives the catch-log "during window" tag).
    bool ActiveNow(long nowUtc) const;

    // Barometer trend derived from the weather feed's 24h sea-level pressure history (rate hPa/h from
    // the sample nearest ~6h-ago vs now, requiring >=1h span).
    struct BaroTrend { bool valid = false; float rateHpaPerH = 0; float nowHpa = 0; };
    BaroTrend ComputeBaro() const;

    // unit-aware display helpers (imperial flag from config). NOAA + Open-Meteo are already fetched in
    // the chosen units for temp/wind; these convert the always-SI marine data + always-hPa pressure.
    const char* TempUnit()  const { return imperial ? "F" : "C"; }
    const char* WindUnit()  const { return imperial ? "mph" : "km/h"; }
    const char* WaveUnit()  const { return imperial ? "ft" : "m"; }
    const char* PressUnit() const { return imperial ? "inHg" : "hPa"; }
    float SeaTempDisp(float degC) const { return imperial ? degC * 1.8f + 32.0f : degC; }
    float WaveDisp(float m)       const { return imperial ? m * 3.28084f : m; }
    float PressDisp(float hpa)    const { return imperial ? hpa * 0.02953f : hpa; }

    // screens (defined in FishingScreens.cpp)
    void DrawTide(BandCanvas& c);
    void DrawFlow(BandCanvas& c);
    void DrawTemp(BandCanvas& c);
    void DrawSolunar(BandCanvas& c);
    void DrawWeather(BandCanvas& c);
    void DrawMoon(BandCanvas& c);
    void DrawCatchLog(BandCanvas& c);
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
