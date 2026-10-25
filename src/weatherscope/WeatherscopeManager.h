#pragma once

#include <Arduino.h>

#include "ConfigurationWebServer.h"
#include "OpenSkyAuthTokenHandler.h"
#include "HttpRequestManager.h"
#include "LGFX.h"
#include "BandCanvas.h"
#include "LongPressPolicy.h"
#include "RadarZoom.h"
#include "WxFeedFactory.h"

// The screen layer's types, used here only through pointers: their headers (and the town table) are included by
// WeatherscopeManager.cpp alone.
namespace wxscreens { struct Cycle; struct ScreenIn; struct ScreenOut; }
namespace wxscen { struct Scenario; }
namespace wxrender { struct View; }
namespace wxcrop { struct Header; }

// FEATURE_WEATHERSCOPE top-level controller: the Weatherscope edition, a desk weather radar. It is a
// sibling of AircraftManager and the other editions' managers, selected at compile time in main.cpp,
// with the same Initialise / Update / Draw surface, driven by the loop task on the same shared infra.
//
// Stage 3 step 3.5a (docs/weatherscope-stage3.md, Addendum 9): the map screens of the reviewer's layout, from stored
// data. In stored mode the device cycles six scenarios (dry, approaching, here now, a SYNTHETIC warning, stale, and a
// SYNTHETIC clear), 30 s each. Every screen is composed by wxscreens::Compose (WxRaster.h draws every pixel; no
// LovyanGFX text or circle routine draws a map screen, A9.3) ONCE per scenario and zoom, into a cached sprite.
//   - a vertical swipe zooms (up = in, down = out) on the {15, 30, 60} mi ladder; a long press toggles 60 <-> 15
//     (LongPressPolicy.h, unchanged); 10 min without a touch returns to 60 mi (A9.3);
//   - a tap plays the past hour's 15 frames once, at the current zoom (Q19), with the loop's band;
//   - a left swipe opens the legend (screen 7), and a tap returns.
// Stored mode, its cycle, its synthetic crops and the bench's serial gesture bytes (T, S, U, D, H) must be
// unreachable on a customer unit before any Weatherscope ships (A8.19, A8.20, Q21). The feeds are reached only
// through wx::MakeFeeds(), never by name (D1). No aircraft PPI sweep: main.cpp gates the sweep out of this build.
//
// Memory: this object is a global, so it holds only small state and pointers; the frame path (the cached screen
// sprite, the code grid, the screen's input and output, the view, the town ranking, the loop's records) lives in
// PSRAM, allocated once at the first Initialise.
class WeatherscopeManager
{
public:
    WeatherscopeManager(ConfigurationWebServer& config, OpenSkyAuthTokenHandler& /*auth*/,
                        HttpRequestManager& httpManager, LGFX& tftGfx)
        : configServer(config), http(httpManager), tft(tftGfx), feeds(wx::MakeFeeds())
    {
    }

    void Initialise();
    void Update();
    void Draw(BandCanvas& backbuffer, bool firstPass);

    static constexpr int kLoopFrames = 15;

    // One loop frame's record, kept in RAM while the loop plays and printed after it ends (A8.19 item 4).
    struct LoopRecord {
        uint32_t epoch;
        uint8_t sha[32];
        int64_t startUs;
        uint32_t drawUs;
        uint32_t hashUs;
    };

private:
    enum class Mode : uint8_t { Splash, Status, Scenario, Loop, Legend };

    ConfigurationWebServer& configServer;
    HttpRequestManager& http;
    LGFX& tft;
    wx::Feeds feeds;

    Mode mode = Mode::Splash;
    bool splashStarted = false;     // latched at the first Initialise (boot), never re-armed
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

    // the frame path, in PSRAM (made once)
    bool buffersReady = false;
    LGFX_Sprite* screenSprite = nullptr;       // the current scenario's whole screen, composed once per scenario + zoom
    uint8_t* grid = nullptr;                   // decoded codes
    size_t gridCells = 0;
    wxscen::Scenario* scenario = nullptr;
    wxscreens::ScreenIn* screenIn = nullptr;
    wxscreens::ScreenOut* screenOut = nullptr;
    wxrender::View* view = nullptr;
    int* townOrder = nullptr;                  // RankOrder over the towns' populations, worked out once
    wxscreens::Cycle* cycle = nullptr;
    LoopRecord* records = nullptr;

    // the loop's frames and the scenario picks (A8.10's rule, applied on the device)
    wx::CropBytes history[kLoopFrames];
    size_t nHistory = 0;
    int pickApproaching = -1, pickDry = -1;
    char pickApproachingName[7] = "none", pickDryName[7] = "none";   // "212400": the frames' UTC clocks
    size_t psramUsed = 0;   // the frame path's PSRAM, measured once in MakeBuffers

    // zoom (A9.3): the {15, 30, 60} ladder, the long press, and the idle return
    radarzoom::Ladder zoomLadder;
    int zoomIdx = 0;
    uint32_t lastTouchMs = 0;
    longpress::State longPress;

    // the loop
    int loopIndex = 0;
    int64_t loopT0Us = 0;
    bool loopFrameDue = false;

    // touch
    bool wasTouched = false;
    int touchStartX = 0, touchStartY = 0, touchLastX = 0, touchLastY = 0;

    void UpdateBrightness();
    void LogFeeds() const;
    void MakeBuffers();
    void PickFrames();
    bool DecodeCrop(const wx::CropBytes& crop, wxcrop::Header* h);
    void BuildScenario();
    int ZoomMiles() const;
    void ApplyZoom(int idx, const char* why);
    void StartLoop();
    void WaitForLoopFrame();
    void FinishLoop();
    void OpenLegend();
    void CloseLegend();
    void HandleTouch();
    void HandleSerialGestures();
    void ClassifyStroke(int dx, int dy);
    void EndHoldGrace(const longpress::GraceEnd& e);
    void OnTap();
    void OnSwipeLeft();
    void OnSwipeVertical(int dir);
    void OnHoldFired();

    void DrawSplash(BandCanvas& c);
    void DrawStatus(BandCanvas& c);
    void DrawScenario(BandCanvas& c);
    void DrawLoopFrame(BandCanvas& c);
    void DrawLegend(BandCanvas& c);
    void CenterText(BandCanvas& c, const String& s, int y, uint32_t color);
};
