#pragma once

#include <Arduino.h>
#include <vector>

#include "ConfigurationWebServer.h"
#include "OpenSkyAuthTokenHandler.h"
#include "HttpRequestManager.h"
#include "LGFX.h"
#include "BandCanvas.h"
#include "SpeedTheme.h"
#include "SpeedFeedClient.h"

// FEATURE_SPEED top-level controller -- the Speedscope edition: a desk speed-radar console that ties
// into a MiniSpeedCam (minispeedcam.com) over the LAN, reading its keyless local /api/state (health +
// live proximity) and /api/events (recent vehicle passes) endpoints. The seventh sibling to
// AircraftManager / EamManager / SpaceManager / SeismicManager / BirdingManager / FishingManager,
// selected at compile time in main.cpp (same Initialise / Update / Draw surface, driven by the loop
// task, same shared infra).
//
// The UI is a HYBRID (like Birding/Fishing): a dwell-timed auto-rotation that skips empty screens AND
// swipe-to-navigate, plus a tap-to-inspect detail-card overlay. There is no aircraft PPI sweep (the
// camera is a fixed point, not moving contacts) -- main.cpp gates the sweep out of the FEATURE_SPEED
// build, so IsRadarView()/CurrentSweepAngle() are intentionally absent.
class SpeedManager
{
public:
    SpeedManager(ConfigurationWebServer& config, OpenSkyAuthTokenHandler& auth,
                 HttpRequestManager& httpManager, LGFX& tftGfx)
        : configServer(config), authHandler(auth), http(httpManager), tft(tftGfx),
          feed(httpManager)
    {
    }
    ~SpeedManager() = default;

    void Initialise();
    void Update();
    void Draw(BandCanvas& backbuffer, bool firstPass);

private:
    // Screens. Order here is the canonical rotation order (Splash self-drops once data lands).
    enum class Screen : uint8_t { Last, Live, List, Stats, Device, Clock, Splash, COUNT };

    ConfigurationWebServer& configServer;
    OpenSkyAuthTokenHandler& authHandler;   // reserved (parity with the other apps); unused here
    HttpRequestManager& http;
    LGFX& tft;
    SpeedFeedClient feed;

    // ---- config-derived state (set in Initialise) ----
    speed::Palette palette = speed::PaletteDefault();
    String camHost;                        // sc-host: MiniSpeedCam mDNS name / IP / URL
    String backendBaseUrl;                 // sc-base-url: optional proxy/aggregator (empty = direct)
    int    speedLimit = 0;                 // sc-limit: posted limit, device units (0 = unset)
    int    alertSpeed = 0;                 // sc-alert-speed: ntfy speeder threshold, device units
    long   tzOffsetSec = 0;                // local clock offset for Clock/day boundaries
    bool   hasLatLon = false;
    double deviceLat = 0.0, deviceLon = 0.0;
    std::vector<Screen> enabledOrder;      // per-view toggles, in display order
    uint8_t configuredBrightness = 255;
    bool autoDim = true;

    // ---- host resolution (mDNS name -> IP) ----
    String feedOrigin;                     // origin currently handed to the feed client
    String resolvedIpOrigin;               // cached "http://<ip>" from the last good mDNS query
    String resolvedName;                    // the mDNS label resolvedIpOrigin belongs to
    unsigned long lastResolveMs = 0;

    // ---- navigation / selection ----
    Screen current = Screen::Splash;       // cold-start: greet with the splash / setup prompt
    unsigned long lastAdvanceMs = 0;
    unsigned long lastInteractionMs = 0;
    bool inDetail = false;
    Screen detailFor = Screen::Last;

    // ---- brightness / night-dim ----
    uint8_t currentBrightness = 255;
    bool nightDim = false;
    unsigned long lastBrightnessCheck = 0;

    // ---- ntfy alerts (shared ntfy-topic key + POST pattern) ----
    String ntfyTopic;
    bool alertSpeeder = false;             // a new pass at/over the speeder threshold
    bool alertRecord = false;              // a new fastest-of-the-day
    bool alertOffline = false;             // the camera stopped responding
    // edge state, seeded at first data so the backlog never fires.
    bool alertSeeded = false;
    bool epochSeeded = false;              // speeder baseline seeded once NTP + events are both up
    bool recordSeeded = false;             // day-record baseline seeded once NTP + events are both up
    long newestSeenEpoch = 0;              // newest event epoch already considered
    int  recordTop = 0;                    // fastest speed seen "today" so far
    long recordDayIndex = 0;               // local day index recordTop belongs to
    bool wasOnline = false;
    bool everOnline = false;               // latched once the camera has ever answered (gates "back online")
    unsigned long lastNotifyMs = 0;

    // ---- touch / gestures ----
    bool wasTouched = false;
    int touchStartX = 0, touchStartY = 0;
    int touchLastX = 0, touchLastY = 0;

    // host resolution
    void MaybeResolveOrigin(bool force);

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

    // domain helpers
    bool DeviceOnline() const;
    bool UnitKph() const;
    const char* UnitLabel() const { return UnitKph() ? "km/h" : "mph"; }
    void ComputeToday(int& count, int& top, int& over, double& avg) const;
    long LocalDayIndex(long nowUtc) const { return (nowUtc + tzOffsetSec) / 86400; }

    // screens (defined in SpeedScreens.cpp)
    void DrawLast(BandCanvas& c);
    void DrawLive(BandCanvas& c);
    void DrawList(BandCanvas& c);
    void DrawStats(BandCanvas& c);
    void DrawDevice(BandCanvas& c);
    void DrawClock(BandCanvas& c);
    void DrawSplash(BandCanvas& c);
    void DrawDetailCard(BandCanvas& c);
    void DrawScreenDots(BandCanvas& c, const std::vector<Screen>& rot) const;

    // helpers
    void DrawScreen(BandCanvas& c, Screen s);
    void CenterText(BandCanvas& c, const String& s, int y, uint32_t color);
    static String FormatClock(long epoch, long tzOffsetSec);
    static String FormatAgo(long epochSecs, int fallbackAgeSec);
};
