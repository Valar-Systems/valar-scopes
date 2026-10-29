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
namespace wxscreens { struct Cycle; struct ScreenIn; struct ScreenOut; struct Layer; }
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
    // DIAG ONLY (A9.22): main.cpp hands over every push -- the backbuffer's tag, the phase it was pushed at, and how
    // long the push held the loop task. Logged run-length on the scene; held during a loop, printed after it.
    void OnPushed(uint32_t tag, uint8_t phase, uint32_t pushUs);

    static constexpr int kLoopFrames = 15;

    // One loop frame's record, kept in RAM while the loop plays and printed after it ends (A8.19 item 4).
    struct LoopRecord {
        uint32_t epoch;
        uint8_t sha[32];
        int64_t startUs;
        uint32_t drawUs;
        uint32_t hashUs;
        uint32_t decodeUs;     // A9.22's profile: the decode, then Compose's ten phases
        uint32_t phaseUs[11];   // wxscreens::Profile::kPhases (A9.23 adds the replay)
        uint32_t qReads;        // BUILD Q (A12.25): the probe's reads in this frame
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
    wxscreens::Layer* layer = nullptr;         // the loop's frame-invariant layer (A9.23), built at the tap
    LoopRecord* records = nullptr;

    // BUILD Q ONLY (A12.25): the probe's 240 x 240 buffer, and a grid for timing the Bend crops' decode ('Q')
    uint8_t* qProbeBuf = nullptr;
    uint8_t* qBendGrid = nullptr;
    size_t qBendCells = 0;
    void MakeQBuffers();
    void RunQDecode();

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
    int loopRebuilds = 0;   // layer rebuilds inside a loop's frames (printed after it)
    int64_t loopT0Us = 0;
    bool loopFrameDue = false;

    // diag (A9.22): the backbuffer's tag, the push log, the full-frame copy's timing
    uint32_t drawCount = 0;
    struct PushRun {
        uint16_t scene = 0;          // the tag's top half: mode, scene, radius
        uint16_t first = 0, last = 0; // draw counts
        uint32_t n = 0;
        uint8_t phases = 0;          // bit p = a push seen at phase p
        uint32_t pushMin = 0, pushMax = 0;
    };
    PushRun pushRun;
    static constexpr int kHeldRuns = 24;
    PushRun heldRuns[kHeldRuns];
    int nHeldRuns = 0, droppedRuns = 0;
    uint32_t copyN = 0, copyMin = 0, copyMax = 0;
    uint64_t copySum = 0;
    bool ringShown = false;

    // THE FIRE (A9.34): a fired hold stores its target; the zoom's rebuild runs at the start of the NEXT Update, so the
    // fire's own frame shows the ring complete at the old radius first. -1 = none pending.
    int pendingZoom = -1;

    // The instrument (A9.34): every BuildScenario timed by phase, its prints through Out(), and the last 160 frames.
    struct BuildRecord {
        uint8_t scene = 0;
        uint16_t fromMi = 0, toMi = 0;
        char why[8] = "";
        uint32_t decodeUs = 0, scenUs = 0, fromUs = 0, composeUs = 0, shaUs = 0, printUs = 0, totalUs = 0;
        uint16_t prints = 0, shortWrites = 0;
        uint32_t frameUs = 0;         // A9.42: the push-to-push interval of the frame it ran in (0 until pushed)
    };
    static constexpr int kBuildRecords = 8;
    BuildRecord builds[kBuildRecords];
    int nBuilds = 0;
    const char* buildWhy = "cycle";   // who asked for the next BuildScenario: the cycle, a zoom's reason
    int buildFromMi = 0;
    bool timingOut = false;           // Out() is inside a timed BuildScenario
    int64_t outUs = 0;
    uint16_t outN = 0, outShort = 0;
    struct FrameRecord {
        uint32_t startUs = 0;        // the push's start, esp_timer's low 32 bits
        uint16_t permille = 0;       // the ring as drawn, 0 when not drawn
        uint8_t flags = 0;           // kHeld | kFired | kRebuilt
        uint8_t scene = 0;
        uint16_t radiusMi = 0;
    };
    static constexpr uint8_t kHeld = 1, kFired = 2, kRebuilt = 4;
    static constexpr int kFrameLog = 160;   // 5.9 s at ~37 ms: 16 baseline frames + a 1.5 s hold + 2 s to the R
    FrameRecord frameLog[kFrameLog];
    uint32_t frameSeq = 0;            // frames logged since the last 'R'
    FrameRecord curFrame;             // filled while the frame is drawn, logged at its push
    bool benchPress = false;          // 'P' holds a synthetic touch at the centre until 'L' (stored mode only)
    size_t Out(const char* fmt, ...);
    void PrintRecords();

    // A9.42 (item 2, diag only): every Out() write, with the USB path it met, and the frame push it ran before.
    struct WriteRecord {
        uint32_t tUs = 0, elapsedUs = 0;
        uint16_t asked = 0, written = 0, freeBefore = 0;
        uint8_t plugged = 0, connected = 0;   // 2 = not an HWCDC build
    };
    static constexpr int kWriteLog = 64;
    WriteRecord writeLog[kWriteLog];
    uint32_t writeSeq = 0;
    uint32_t lastPushStartUs = 0;
    char* diagBuf = nullptr;          // /diag/wx's text, rendered on the loop task (DiagText.h)
public:
    class RecordSink {   // public so the .cpp's sinks can derive from it
    public:
        virtual ~RecordSink() {}
        virtual void Line(const char* s, size_t n) = 0;
    };

private:
    void RenderRecords(RecordSink& out);
    void ServiceDiagText();

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
    void DrawHoldRing(BandCanvas& c);
    void PrintPushRun(const PushRun& r) const;
    void PrintCopyStats();
    void CenterText(BandCanvas& c, const String& s, int y, uint32_t color);
};
