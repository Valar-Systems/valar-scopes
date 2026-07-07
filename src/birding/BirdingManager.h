#pragma once

#include <Arduino.h>
#include <vector>
#include <utility>
#include <set>

#include "ConfigurationWebServer.h"
#include "OpenSkyAuthTokenHandler.h"
#include "HttpRequestManager.h"
#include "NtfyAlerter.h"
#include "LGFX.h"
#include "BandCanvas.h"
#include "BirdingTheme.h"
#include "BirdingFeedClient.h"

// FEATURE_BIRDING top-level controller -- the Birding edition: notable bird sightings near you, live
// from eBird. The fifth sibling to AircraftManager / EamManager / SpaceManager / SeismicManager,
// selected at compile time in main.cpp (same Initialise / Update / Draw surface, driven by the loop
// task, same shared infra).
//
// The UI is a HYBRID of the two existing patterns: a dwell-timed auto-rotation that skips empty feeds
// AND swipe-to-navigate (from the Space/EAM shell), plus a tap-to-inspect detail-card overlay on the
// radar / notable screens (from the Seismic/Aviation radar). The data layer mirrors SpaceFeedClient.
// IsRadarView()/CurrentSweepAngle() are intentionally absent -- main.cpp gates the aircraft PPI sweep
// out of the FEATURE_BIRDING build; the sightings radar draws static range rings.
class BirdingManager
{
public:
    BirdingManager(ConfigurationWebServer& config, OpenSkyAuthTokenHandler& auth,
                   HttpRequestManager& httpManager, LGFX& tftGfx)
        : configServer(config), authHandler(auth), http(httpManager), tft(tftGfx),
          feed(httpManager)
    {
    }
    ~BirdingManager() = default;

    void Initialise();
    void Update();
    void Draw(BandCanvas& backbuffer, bool firstPass);

private:
    enum class Screen : uint8_t { Radar, Notable, BigDay, Hotspot, Targets, Splash, Clock, COUNT };

    ConfigurationWebServer& configServer;
    OpenSkyAuthTokenHandler& authHandler;   // reserved (parity with the other apps); unused here
    HttpRequestManager& http;
    LGFX& tft;
    BirdingFeedClient feed;

    // ---- config-derived state (set in Initialise) ----
    birding::Palette palette = birding::PaletteDefault();
    bool hasKey = false;          // an eBird token is configured
    bool hasLatLon = false;
    double deviceLat = 0.0, deviceLon = 0.0;
    int radiusKm = 25;
    int backDays = 7;
    std::vector<String> targets;  // lowercased species names / codes to watch
    std::vector<Screen> enabledOrder;
    uint8_t configuredBrightness = 255;
    bool autoDim = true;

    // ---- navigation / selection ----
    Screen current = Screen::Splash; // cold-start: greet with the splash / setup prompt
    unsigned long lastAdvanceMs = 0;
    unsigned long lastInteractionMs = 0;
    bool inDetail = false;
    birding::Sighting selected;
    bool selectedValid = false;
    int notableScroll = 0;

    // ---- brightness / night-dim ----
    uint8_t currentBrightness = 255;
    bool nightDim = false;
    unsigned long lastBrightnessCheck = 0;

    // ---- ntfy alerts (shared ntfy-topic key + POST pattern) ----
    String ntfyTopic;
    bool alertNotable = true;
    bool alertTarget = true;
    // seen-keys (speciesCode) seeded per feed on its first successful fetch so the
    // backlog never fires; then new arrivals alert. Per-feed flags because the
    // staggered fetches land at different times (see CheckAlerts).
    std::set<String> seenNotable;
    std::set<String> seenTarget;
    bool notableSeeded = false;
    bool recentSeeded = false;
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
    void ExitDetail() { inDetail = false; selectedValid = false; }

    // brightness / alerts
    void UpdateBrightness();
    float GlowFactor() const { return nightDim ? 0.5f : 1.0f; }
    void CheckAlerts();
    void SendNtfy(const String& title, const String& body, const String& tags, int priority);
    bool MatchesTarget(const birding::Sighting& s) const;

    // screens (defined in BirdingScreens.cpp)
    void DrawRadar(BandCanvas& c);
    void DrawNotable(BandCanvas& c);
    void DrawBigDay(BandCanvas& c);
    void DrawHotspot(BandCanvas& c);
    void DrawTargets(BandCanvas& c);
    void DrawSplash(BandCanvas& c);
    void DrawClock(BandCanvas& c);
    void DrawDetailCard(BandCanvas& c);
    void DrawScreenDots(BandCanvas& c, const std::vector<Screen>& rot) const;

    // helpers
    std::pair<int, int> ProjectSightingToScreen(const birding::Sighting& s) const;
    int HitTestSighting(int tx, int ty, const std::vector<birding::Sighting>& shown) const;
    int DistinctSpecies(const std::vector<birding::Sighting>& v) const;
    static String ShortDate(const String& obsDt); // "YYYY-MM-DD HH:MM" -> "Jun 30 06:14"
    static std::vector<String> SplitList(const String& s, bool lower);
    void CenterText(BandCanvas& c, const String& s, int y, uint32_t color);
};
