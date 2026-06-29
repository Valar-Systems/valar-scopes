#pragma once

#include <Arduino.h>
#include <vector>
#include <set>

#include "ConfigurationWebServer.h"
#include "OpenSkyAuthTokenHandler.h"
#include "HttpRequestManager.h"
#include "LGFX.h"
#include "BandCanvas.h"
#include "EamFeedClient.h"
#include "EamLogbook.h"
#include "EamTheme.h"

// FEATURE_EAM top-level controller -- the EAM (Emergency Action Message) monitor app.
//
// Sibling to AircraftManager, selected at compile time in main.cpp: same lifecycle
// (Initialise / Update / Draw) driven by the loop task, and the same shared infra
// (ConfigurationWebServer, HttpRequestManager's single TLS client, OpenSky OAuth), but it
// renders HFGCS EAM screens instead of the radar and pulls from the valar-eam-feed backend.
//
// Stage 3 builds the seven screens + the rotation/touch/brightness shell. The logbook and the
// ntfy alerts arrive in Stage 4; the full config page in Stage 5.
class EamManager
{
public:
    EamManager(ConfigurationWebServer& config, OpenSkyAuthTokenHandler& auth,
               HttpRequestManager& httpManager, LGFX& tftGfx)
        : configServer(config), authHandler(auth), http(httpManager), tft(tftGfx),
          feed(httpManager, auth)
    {
    }
    ~EamManager() = default;

    void Initialise();
    void Update();
    void Draw(BandCanvas& backbuffer, bool firstPass);

private:
    // The seven screens. Order here is the default rotation order; the user can enable/disable
    // and reorder in config (Stage 5). Clock is the idle screen and is always available.
    enum class Screen : uint8_t { Ticker, Tempo, Codewords, Abncp, Propagation, Icbm, Clock, COUNT };

    ConfigurationWebServer& configServer;
    OpenSkyAuthTokenHandler& authHandler;
    HttpRequestManager& http;
    LGFX& tft;
    EamFeedClient feed;

    // ---- config-derived state (set in Initialise) ----
    String backendBaseUrl;
    eam::Palette palette = eam::PaletteGreen();
    bool colonBlink = false;                 // Zulu clock colon blink (default steady)
    std::vector<Screen> enabledOrder;        // enabled screens in display order
    bool hasLatLon = false;
    double deviceLat = 0.0, deviceLon = 0.0;
    uint8_t configuredBrightness = 255;
    bool autoDim = true;

    // ---- rotation / navigation ----
    Screen current = Screen::Clock;
    unsigned long lastAdvanceMs = 0;         // last auto-rotate tick
    unsigned long lastInteractionMs = 0;     // last touch; pauses auto-rotate briefly

    // ---- animation / per-frame state (advanced on firstPass) ----
    unsigned long newPulseUntilMs = 0;       // "NEW" pulse window after a fresh EAM
    int tickerScroll = 0;                    // vertical marquee offset for an overflowing ticker
    unsigned long lastScrollMs = 0;
    int ambientIndex = 0;                    // rotating ambient stat on the clock
    unsigned long lastAmbientMs = 0;

    // ---- brightness / night-dim ----
    uint8_t currentBrightness = 255;
    bool nightDim = false;
    unsigned long lastBrightnessCheck = 0;

    // ---- logbook + codeword "new to this device" ----
    EamLogbook logbook;               // persistent, bounded: seen EAM ids + codewords
    std::set<String> newCodewords;    // codewords first seen this session (drives the NEW marker)

    // ---- ntfy alerts ----
    String ntfyTopic;                 // ntfy.sh topic; empty disables all alerts
    bool alertNew = true;             // (a) new EAM
    bool alertTempo = true;           // (b) tempo crossing into elevated/high
    bool alertAbncp = true;           // (c) ABNCP not-airborne -> airborne
    int lastTempoRank = -1;           // 0 normal / 1 elevated / 2 high; -1 = no reading yet
    bool lastAbncpAirborne = false;
    bool abncpSeen = false;           // a valid ABNCP reading has arrived (so the first isn't a "transition")
    unsigned long lastNotifyMs = 0;   // throttle ntfy POSTs

    // ---- touch / gestures ----
    bool wasTouched = false;
    int touchStartX = 0, touchStartY = 0;
    int touchLastX = 0, touchLastY = 0;
    enum class Swipe { Up, Down, Left, Right };

    // rotation helpers
    std::vector<Screen> BuildRotation() const;   // enabled screens that currently have data
    bool HasData(Screen s) const;
    void AdvanceRotation(int dir);               // +1 next, -1 prev (manual)
    void AutoRotate();                           // dwell-timed auto advance, skipping empty

    // input + chrome
    void HandleTouch();
    void DrawScreenDots(BandCanvas& c, const std::vector<Screen>& rot) const;

    // screens
    void DrawTicker(BandCanvas& c, bool firstPass);
    void DrawTempo(BandCanvas& c);
    void DrawCodewords(BandCanvas& c);
    void DrawAbncp(BandCanvas& c);
    void DrawPropagation(BandCanvas& c);
    void DrawIcbm(BandCanvas& c);
    void DrawClock(BandCanvas& c);

    // brightness
    void UpdateBrightness();
    float GlowFactor() const { return nightDim ? 0.5f : 1.0f; }

    // logbook + alerts
    void UpdateLogbook();                 // note seen EAMs/codewords (loop task)
    void CheckAlerts(bool newEamArrived); // evaluate the three ntfy triggers
    void SendNtfy(const String& title, const String& body, const String& tags, int priority);

    // small helpers
    static std::vector<String> SplitList(const String& s, bool lower);
    static String TimeAgo(long epoch);                 // "4m ago" from a UTC epoch, "" if 0/unsynced
    static String FormatCountdown(long secondsLeft);   // "T-01:23:45" / "T-2d 03:14"
    void CenterText(BandCanvas& c, const String& s, int y, uint32_t color);
};
