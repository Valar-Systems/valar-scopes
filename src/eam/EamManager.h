#pragma once

#include <Arduino.h>
#include <vector>
#include <set>
#include <map>

#include "ConfigurationWebServer.h"
#include "OpenSkyAuthTokenHandler.h"
#include "HttpRequestManager.h"
#include "NtfyAlerter.h"
#include "LGFX.h"
#include "BandCanvas.h"
#include "EamFeedClient.h"
#include "EamLogbook.h"
#include "EamTheme.h"

#if defined(FEATURE_EAM_GAME)
#include "../game/Derive.h"
#include "../game/DrillMachine.h"
#include "../game/KeyTurn.h"
#include "../game/GameClient.h"
#include "KeyTouchSampler.h"
#endif

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
#if defined(FEATURE_EAM_GAME)
          , gameClient(httpManager)
#endif
    {
    }
    ~EamManager() = default;

    void Initialise();
    void Update();
    void Draw(BandCanvas& backbuffer, bool firstPass);

private:
    // The screens. Order here is the default rotation order; the user can enable/disable and
    // reorder in config. Clock is the idle screen and is always available; Reference is a static
    // help card (also always available). Activity / MilAir appear only when their feed has data.
    enum class Screen : uint8_t {
        Ticker, Tempo, Activity, Codewords, Abncp, MilAir, Propagation, Icbm, Reference, Clock, COUNT
    };

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
    bool alertSpace = true;           // (d) HF blackout / geomagnetic storm onset
    int lastTempoRank = -1;           // 0 normal / 1 elevated / 2 high; -1 = no reading yet
    bool lastAbncpAirborne = false;
    bool abncpSeen = false;           // a valid ABNCP reading has arrived (so the first isn't a "transition")
    bool lastHfDegraded = false;      // last space-weather HF-degraded flag (edge detect)
    int lastGScaleRank = 0;           // last geomagnetic-storm rank 0..5 (G-scale; edge detect)
    bool spaceSeen = false;           // a valid space-weather reading has arrived
    NtfyAlerter ntfy;                 // deferring ntfy sender (see NtfyAlerter.h)

    // ---- touch / gestures ----
    bool wasTouched = false;
    int touchStartX = 0, touchStartY = 0;
    // FEATURE_USB_OPEN: a long press (held >= LONG_PRESS_MS, moved < 40 px) opens
    // the shown message on the computer; it fires once, while still held, and the
    // release that follows is neither a tap nor a swipe.
    static constexpr unsigned long LONG_PRESS_MS = 1000;
    unsigned long touchDownMs = 0;
    bool longPressFired = false;
    unsigned long usbToastUntilMs = 0;
    String usbToast;
    String ShownMessageId() const;   // the ticker's message id, or "" when none is shown
    void OpenOnComputer();
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
    void DrawActivity(BandCanvas& c);
    void DrawCodewords(BandCanvas& c);
    void DrawAbncp(BandCanvas& c);
    void DrawMilAir(BandCanvas& c);
    void DrawPropagation(BandCanvas& c);
    void DrawIcbm(BandCanvas& c);
    void DrawReference(BandCanvas& c);
    void DrawClock(BandCanvas& c);

    // brightness
    void UpdateBrightness();
    float GlowFactor() const { return nightDim ? 0.5f : 1.0f; }

    // logbook + alerts
    // Note seen EAMs/codewords (loop task). Appends to `fresh` every EAM that was NEW to
    // the logbook on this call (newest first, as the feed orders them).
    void UpdateLogbook(std::vector<eam::Msg>* fresh = nullptr);
    void CheckAlerts(bool newEamArrived); // evaluate the three ntfy triggers
    void SendNtfy(const String& title, const String& body, const String& tags, int priority);

    // small helpers
    static std::vector<String> SplitList(const String& s, bool lower);
    static String TimeAgo(long epoch);                 // "4m ago" from a UTC epoch, "" if 0/unsynced
    static String FormatCountdown(long secondsLeft);   // "T-01:23:45" / "T-2d 03:14"
    void CenterText(BandCanvas& c, const String& s, int y, uint32_t color);

#if defined(FEATURE_EAM_GAME)
    // ---- the REACT drill (src/game/) ----
    // One drill at a time, owned here and driven by the loop task. DrillMachine decides,
    // DrawDrill draws, and every decision around them (offer, clock gate, T, targets)
    // is src/game/DrillPolicy.h -- pure and host-tested. Rulings: Fable, 2026-09-23.
    game::DrillMachine drill;
    game::KeyTurnGesture keyTurn;
    bool drillScreen = false;         // the drill face is up (a mode, never a rotation screen)
    uint64_t offeredAtUs = 0;         // monotonic instant of the offer (auto-decode clock)
    uint64_t endedAtUs = 0;           // when Complete/Aborted was first seen (60 s dwell)
    String drillMsgId;                // the message this drill is working
    game::Derivation drillDerivation; // its derivation, as offered
    // Messages whose offer was WITHDRAWN at the ack cutoff: they stay in the ticker with
    // their class shown (Fable, 2026-09-23). Bounded; oldest dropped.
    std::map<String, game::MsgClass> withdrawnClass;
    static constexpr size_t WITHDRAWN_KEEP = 20;
    bool drillPressed = false;        // tap tracking on the drill face
    int drillPressX = 0, drillPressY = 0;
    int drillLastX = 0, drillLastY = 0;
    unsigned long drillPressMs = 0;
    String drillHeardAt;              // the message's heard_at, to re-derive after a refetch
    bool executeSent = false;         // this drill's measured key turn went to the game client
    uint32_t staleEpoch = 0;          // the epoch a commit was refused under (409 stale_config)

    // Touch at the panel's report rate during the key window (Armed/Window).
    KeyTouchSampler keyTouch;
    uint64_t lastKeySampleUs = 0;     // samples reach the drill in time order, never backwards
    bool benchTouched = false;        // KEYTOUCH_BENCH: the last sample, for the ordinary handler
    int32_t benchX = 0, benchY = 0;

    // The drill's votes (src/game/GameClient.h). Inert without GAME_API_BASE.
    game::GameClient gameClient;

    void UpdateDrill(const std::vector<eam::Msg>& fresh, bool reconnected);
    void DrainKeySamples(uint64_t upToUs);
    void UpdateVotes(uint64_t now);
    void EndDrill();
    void HandleDrillTouch(bool touched, int x, int y, uint64_t tUs);
    void OnDrillTap(int x, int y);
    game::Config DrillConfig() const;
    static uint64_t NowUs();
    static int64_t UtcMsNow();
    bool ClockFreshNow() const;
#endif
};
