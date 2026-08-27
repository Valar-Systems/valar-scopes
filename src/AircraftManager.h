#pragma once

#include <map>
#include <vector>

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include "HttpRequestManager.h"
#include "models/TrackedAircraft.h"
#include "ConfigurationWebServer.h"
#include "OpenSkyAuthTokenHandler.h"
#include "AircraftInfoFields.h"
#include "SpecialAircraft.h"
#include "Logbook.h"
#include "FactoryReset.h"
#include "MqttPublisher.h"
#include "LGFX.h"
#include "BandCanvas.h"
#include "FollowTrack.h" // Follow Mode track buffer (header-only; radar path only)
#include "FollowState.h" // Follow Mode state machine + copy (spec 5 and 6)
#include "FollowGeometry.h" // Follow Mode local-face geometry (spec 10)
#include "FollowArc.h" // Follow Mode arc-face arithmetic (spec 8)
#include "FollowLog.h" // Follow Mode post-flight record (spec 11)
#include "CloudFeed.h" // no-op unless FEATURE_CLOUD_FEED

class AircraftManager
{
private:
    double lat = 0.0;
    double lon = 0.0;

    /// Whether a location was ever CONFIGURED, which is not the same question as
    /// whether `lat`/`lon` are zero.
    ///
    /// 0.0, 0.0 is a real place (the Gulf of Guinea), so "the parsed value is 0"
    /// cannot mean "unset" -- and `String("").toDouble()` is 0.0, so the two
    /// states are indistinguishable after parsing. The only honest source is the
    /// stored STRING being empty, which is what Initialise() reads.
    ///
    /// Same class of mistake as inferring "no credentials" from an uninitialised
    /// struct: do not read an unset value out of a parsed default.
    bool hasLocation = false;

    /// Full-screen "tell the customer what to do" state, drawn in place of the
    /// radar when no location has been set.
    void DrawNoLocation(BandCanvas& backbuffer) const;
    double radLat = 0.2; // latitude half-span of the scan box, in degrees
    double radLon = 0.2; // longitude half-span of the scan box, in degrees
    double rangeRadiusDisplay = 0.0; // outer ring distance in the user's unit, for range labels
    String rangeUnit = "km";
    std::map<String, TrackedAircraft> trackedAircraft;

    // Window-up rotation: the compass bearing drawn at the TOP of the screen
    // (config "radar-up"; 0 = classic north-up). Set to the bearing the user
    // faces so the radar picture matches the view out their window -- a blip on
    // the upper-left of the screen is upper-left out the window, no mental
    // rotation at the "look up!" moment. rotCos/rotSin cache the rotation
    // applied in ProjectCoordinateToScreen (identity at 0), so every consumer
    // of the projection -- blips, trails, tap hit-testing, the sweep's
    // paint-crossing test -- rotates together for free.
    int radarUpDeg = 0;
    float rotCos = 1.0f;
    float rotSin = 0.0f;

    bool displayInfoText = true;
    bool displayTriangles = true;
    bool displayAirports = true;  // fixed airport markers from the baked table (Airports.h)
    // Minimum airport size class to draw from the cloud /api/v1/blipscope/airports overlay:
    // All (zoom rule only), MedLarge (hide small strips), LargeOnly.
    enum class AirportsMin : uint8_t { All, MedLarge, LargeOnly };
    AirportsMin airportsMin = AirportsMin::All;
    bool displayTrails = true;
    bool displayAltColor = true;  // color aircraft markers by altitude band
    bool displayHighlight = true; // ring the nearest/highest/fastest contacts
    bool displaySweep = true;     // draw the rotating PPI sweep beam (the "scanline" config)
    bool displayFade = true;      // paint-and-fade blips: latch position + fade per sweep pass

    // Radar sweep beam angle (radians), advanced once per frame in Update() so the
    // drawn beam (main.cpp reads CurrentSweepAngle()) and the blip-paint crossing
    // test stay in lockstep. prevSweepAngle is last frame's value, used to detect
    // which contacts the beam swept past this frame.
    float sweepAngle = 0.0f;
    float prevSweepAngle = 0.0f;
    // ~5 s per revolution = a terminal ATC radar (ASR, ~12 RPM); also the fade
    // window for a painted blip. angle = TWO_PI * millis() / SWEEP_PERIOD_MS.
    static constexpr unsigned long SWEEP_PERIOD_MS = 5000;

    // Screen navigation. Top-level screens cycle via horizontal swipe; the
    // detail card overlays whichever screen you're on.
    //
    // FOLLOW IS ONE SLOT WITH SEVERAL FACES, NOT SEVERAL SCREENS (spec C6). The
    // local face, the arc face, the globe and the post-flight card are all this
    // one entry; which of them draws is decided by regime and state (§7.1), and
    // is never something the customer picks.
    //
    // It is HIDDEN ENTIRELY when no aircraft is being followed (§13.3), the way
    // the other editions skip empty feeds -- so the cycle below walks visible
    // screens rather than counting a fixed number. A collection customer who
    // never uses this must not inherit a dead screen, and a fixed `% 4` is
    // exactly how they would.
    enum class Screen { Radar, List, Stats, Follow };
    static constexpr int SCREEN_COUNT = 4;
    Screen screen = Screen::Radar;

    // Stats-screen "Reset" row -- the entry point to the reset menu below. Its
    // drawn bounds are recorded each frame rather than computed twice, because
    // the Stats layout is dynamic (rows are dropped as they run out of room
    // above the clock) -- a hardcoded hit box would drift out of alignment on
    // the smaller panels exactly where the rows get squeezed.
    int  resetRowY0 = -1, resetRowY1 = -1;   // the Stats "Reset" row, as drawn
    /// Largest reset the on-screen menu has asked for, not yet performed.
    /// Consumed by main.cpp on the loop task -- see FactoryReset.h.
    uint8_t resetTierRequested = 0;

    // ---------------------------------------------------------------------
    // THE HISTORY, KEPT BECAUSE THE NEW DESIGN HAS TO ANSWER IT.
    //
    // This control was two taps in a 6 s window. A bench board lost its network
    // to two taps 633 ms apart (#165) -- a cloth dragged over the panel. The fix
    // was to make it a HOLD, on the reasoning that a cloth cannot produce two
    // seconds of sustained contact on one row: the accident excluded by physics
    // rather than by heuristic. That reasoning was sound and is why the hold
    // stood.
    //
    // It is superseded by a fact about the hardware, not by a change of mind.
    // The CST816D may report no change interrupt under a static contact, so the
    // panel can fail to see the very gesture the design depends on -- and a hold
    // that is not seen is indistinguishable, to the customer, from a device that
    // has stopped responding.
    //
    // SO #165 HAS TO BE ANSWERED AGAIN, DIFFERENTLY. A tap opens a MENU, which
    // is not destructive; the cloth now reaches a screen listing two options and
    // a large Cancel. Reaching a wipe from there takes a tap on a specific small
    // target and then a second tap on another specific small target, with the
    // biggest target on both screens being the one that backs out. The accident
    // is excluded by requiring aim, twice -- not by requiring duration.
    //
    // THE RESET MENU -- DISCRETE TAPS, AND NEVER A PRESS-AND-HOLD.
    //
    // This row used to be "[ Hold to reset Wi-Fi ]" and the gesture was a 2 s
    // hold with a countdown. It is gone, and not for tidiness: the CST816D may
    // report NO change interrupt under a static contact, so a held finger can
    // register as nothing at all. A destructive control whose gesture the panel
    // may silently fail to see is a control that appears broken to the customer
    // who needs it most -- the one whose device has already gone wrong.
    //
    // COM119 is measuring that behaviour now (docs/bench-runbook-device-game.md
    // station 1). The measurement decides what a HOLD may be used for; it does
    // not need to land before a reset can stop depending on one, because a tap
    // is already known to work -- every other control on the device is one.
    //
    // Two taps to act, never one: choose a tier, then confirm it. Cancel is the
    // larger target and sits nearer the bottom of the screen, so the cheap
    // mistake (cancelling) is the easy one to make and the expensive mistake
    // (confirming) takes deliberate aim.
    enum class ResetMenu : uint8_t { Closed = 0, Choosing, ConfirmWifi, ConfirmFactory };
    ResetMenu resetMenu = ResetMenu::Closed;

    // Hit boxes, recorded by the draw that produced them. -1 = not drawn this
    // frame and therefore not tappable -- the pixels and the hit test agree by
    // construction, which is the same rule the Stats reset row already followed.
    int resetOptWifiY0 = -1, resetOptWifiY1 = -1;
    int resetOptFactoryY0 = -1, resetOptFactoryY1 = -1;
    int resetConfirmY0 = -1, resetConfirmY1 = -1;
    int resetCancelY0 = -1, resetCancelY1 = -1;

    void DrawResetMenu(BandCanvas& backbuffer);
    /// Returns true when the tap was consumed by the menu.
    bool HandleResetMenuTap(int tx, int ty);
    void CloseResetMenu();

    bool inDetail = false;     // detail card shown over the current screen
    String selectedIcao = "";  // aircraft shown in the detail card
    String pinnedIcao = "";    // aircraft kept highlighted ("tracked") on the radar
    int detailPage = 0;        // 0 = photo card, 1 = full-data card
    int listScroll = 0;        // first visible row in the list view

    // touch/gesture state: a release is classified as a tap or a 4-way swipe
    bool wasTouched = false;
    int touchStartX = 0, touchStartY = 0;
    int touchLastX = 0, touchLastY = 0;
    unsigned long touchPressMs = 0; // press-edge time, for the [touch] trace's hold duration
    enum class Swipe { Up, Down, Left, Right };

    // Radar tap disambiguation: repeated taps at ~the same spot cycle through the
    // contacts stacked under the finger (dense areas pile several blips + overlapping
    // labels into a couple of mm), so a buried contact is always reachable. See HandleTap.
    int lastTapX = -1000, lastTapY = -1000;
    int tapCycleIndex = 0;

    // Card-close refractory: taps that would OPEN a card (radar/list hit-test) are
    // swallowed briefly after a card closes. A slow tap on the card can be delivered
    // twice (a mid-hold read glitch splits one physical tap into release+press+release),
    // which closed the card and instantly reopened whatever sat under the finger --
    // the same contact, a stacked neighbour, or a list row. Guarding the reopen side
    // kills the class no matter what the touch controller does. Bench report 2026-07-10.
    unsigned long tapSuppressUntilMs = 0;

    // Last frame getTouch() actually read a touch. HandleTouch uses it to briefly pause
    // background enrichment after a touch so the enrichment task's TLS doesn't hold the
    // I2C bus (which touch is serialized against) while the user is interacting.
    unsigned long lastTouchActivityMs = 0;

    // Decoded aircraft photo for the detail view. The sprite is created once and
    // reused; photoIcao/photoReady track which aircraft it currently holds.
    LGFX_Sprite photoSprite;
    String photoIcao = "";
    bool photoReady = false;
    // Whether the photo lookup for photoIcao has finished (image decoded, OR confirmed that
    // adsbdb has no photo, OR the fetch/decode failed). Lets the detail card say "No photo
    // available" only once we actually know, vs "Loading photo..." while it's still resolving.
    bool photoResolved = false;

    // Parallel to AIRCRAFT_INFO_FIELDS: which info lines the user has enabled.
    // Populated in Initialise(), which main.cpp re-runs on every web-config save
    // (ConsumeConfigChanged) -- settings apply live, no reboot.
    std::vector<bool> infoFieldEnabled;

    // True when at least one enabled field needs the adsbdb lookup; lets us skip
    // all enrichment network traffic when the user shows none of those fields.
    bool metadataNeeded = false;
    unsigned long lastMetadataLookup = 0;

    // Watchlist: aircraft whose callsign/icao/registration/type starts with one
    // of these (lowercased) prefixes is flagged on screen and triggers an ntfy
    // flyover alert. Empty watchlist disables all of it.
    std::vector<String> watchlist;
    String ntfyTopic = "";

    // ---- Follow Mode (docs/follow-mode-consolidated.md) ---------------------
    // STAGE 1: the track buffer and its draw cost. The state machine, the faces
    // and the alerts are not built -- §19 puts the draw-cost measurement first
    // because everything else is contingent on it.
    //
    // `followTarget` gates the whole feature. Empty means no allocation, no
    // draw, no behaviour change for anyone who did not ask (§15).
    String followTarget = "";      // lowercased tail / callsign / hex prefix
    bool   followDrawTrack = true; // "follow-track"; §15 marks the default conditional on §18.1
    // 15's alert toggles. follow-lost defaults OFF and the asymmetry IS the
    // argument: a missed lost-alert costs mild worry, an unwanted one costs
    // panic. The screen always shows the state; the phone only if asked.
    bool   followAlertUp   = true;   // "follow-up"
    bool   followAlertDown = true;   // "follow-down"
    bool   followAlertLost = false;  // "follow-lost"
    follow::Track followTrack;

    // The states and the words (spec 5, 6). A pure module -- this only holds the
    // instance; all the reasoning lives in include/FollowState.h so it can be
    // host-tested, which spec 17 names as the prerequisite for the privacy test.
    follow::Machine     followMachine;
    follow::HomeContext followHome;
    // The flight's four numbers, accumulated live and frozen on landing (11).
    follow::FlightStats followStats;
    // The post-flight card's store. ONE write per flight, own NVS namespace,
    // and the only part of Follow that survives a power cycle -- because it is
    // the only part that cannot become untrue while the device is off (11).
    follow::Log         followLog;
    bool                followLogLoaded = false;

    // The number this build exists to produce. Measured around the track draw
    // ALONE, not the whole frame: the frame figure already exists and cannot
    // answer "what did the track cost", which is the question in §18.1.
    uint32_t followDrawUs = 0;      // last frame
    uint32_t followDrawMaxUs = 0;   // worst since the last health report
    uint32_t followDrawSumUs = 0;   // for a mean over the report interval
    uint32_t followDrawFrames = 0;
    size_t   followDrawSegments = 0; // segments actually drawn last frame

    // ---- the local face (§10) ----------------------------------------------
    // The home field's CODE, from the airport data already on the device. §10 is
    // explicit that the local face adds no dataset and that the marker carries a
    // code and never a name: the device has coordinates and identifiers, not
    // names, and inventing one would mean a new table and a flash cost for
    // nothing Follow needs. Empty when no field is close enough to claim.
    String followHomeCode = "";
    // The elevation half of C5 is NOT delivered (the CC0 corpus has AltitudeFeet
    // for 34,128 fields; no running code writes it to KV). Kept separate from the
    // position half so an absent elevation costs AGL and nothing else -- see the
    // note on follow::HomeContext.
    bool   followHomeCodeResolved = false;

    // §13.3: Follow auto-surfaces ONLY on a state transition -- takeoff, landing
    // -- for a dwell, then returns to wherever the owner was. It never takes the
    // screen otherwise, and a swipe during the dwell cancels it: the customer
    // moving is a decision, and a screen that snaps back after one is a screen
    // that feels broken.
    follow::State followLastState = follow::State::Idle;
    Screen        followAutoReturnTo = Screen::Radar;
    unsigned long followAutoUntilMs = 0;
    static constexpr unsigned long FOLLOW_AUTO_DWELL_MS = 20000;

#ifdef FOLLOW_BENCH
    // The bench image self-enables a synthetic target once per boot so the face
    // has something to draw. It is ARMED ONCE: after the first Initialise an
    // empty follow field means the owner CLEARED it, which is what makes the
    // 4.3 disable path reachable on the bench image at all.
    bool followBenchArmed = true;
    // 6's absence copy is the emotional core of the feature and can only be
    // judged by eye. It cannot be reached on a bench without cutting the
    // network, so the state can be FORCED for display -- the drawing is what
    // needs looking at; the transitions are graded in the host suite.
    bool          followForce = false;
    follow::State followForced = follow::State::SignalLost;
    void PollBenchSerial();
#endif
    unsigned long lastNotifyCheck = 0;

    // Special-aircraft detection. Every class is derived offline from the live
    // feed (ICAO address / emitter category / callsign), so it works on any data
    // source and even with all adsbdb enrichment disabled.
    bool showMilitary = true;     // ring + "MIL" tag military contacts on radar/list
    bool alertMilitary = false;   // also raise an ntfy flyover alert for them
    bool alertEmergency = false;  // ntfy alert when a contact squawks 7500/7600/7700 (config "emg-alert")
    bool showHelicopters = false; // ring + "HELI" tag rotorcraft
    bool showSpecial = false;     // ring + "SPC" tag distinctive callsigns

    // Visual alert layer for military / emergency-squawk contacts: a colour-coded
    // pulsing ring at the screen edge while one is in range, optionally led by a
    // brief full-screen flash burst when the contact first appears. The primary
    // attention channel on SKUs without a speaker. Flash implies the ring after
    // the burst; the burst is edge-triggered per contact (never a sustained
    // strobe) and duty-limited below the 3 flashes/sec photosensitivity guideline.
    enum class VisualAlertMode : uint8_t { Off, Ring, Flash };
    VisualAlertMode milVisual = VisualAlertMode::Off;  // config "mil-visual"
    VisualAlertMode emgVisual = VisualAlertMode::Ring; // config "emg-visual"
    bool visualNightOverride = false;                  // config "visual-night": alerts punch through night dim
    // Refreshed once per frame in UpdateVisualAlerts() (loop task): the active
    // ring colour (0 = none; emergency red beats military orange) and the flash
    // burst window. visualAlertActive feeds the UpdateBrightness night override.
    uint32_t visualRingColor = 0;
    bool visualAlertActive = false;
    unsigned long flashBurstUntilMs = 0;
    uint32_t flashBurstColor = 0;
    // Manual dismiss: tapping the ring (or anywhere during a flash burst) latches
    // this, suppressing the alert until every alerting contact has left the screen,
    // then it re-arms for the next genuinely new one. Fallback for the case where a
    // contact lingers -- the on-screen gate in UpdateVisualAlerts is the main cure.
    bool visualAlertDismissed = false;

    // "Look up!" overhead alert: flag a contact passing within overheadKm of the
    // centre so you can physically glance up and spot it.
    bool showOverhead = false;    // pulsing "LOOK UP" ring on the radar
    bool alertOverhead = false;   // also raise an ntfy alert
    double overheadKm = 3.0;      // how close to the centre counts as "overhead"

    // Spotting logbook ("lifelist"): persistent tally of unique types/airlines/
    // countries seen. Opt-in, because it forces an adsbdb lookup on every contact
    // (to learn the type/airline) and writes to flash. Country + contact odometer
    // work from the OpenSky feed alone.
    Logbook logbook;
    bool logbookEnabled = false;

    // Home Assistant / MQTT. The publisher runs on its own task; we just hand it a
    // retained JSON "summary" (count, nearest, overhead/military flags) every few
    // seconds, plus the HA discovery configs on (re)connect so sensors auto-create.
    MqttPublisher mqtt;
    bool mqttEnabled = false;
    bool mqttDiscovery = true;
    String mqttBase = "blipscope";
    unsigned long lastMqttState = 0;

    unsigned long fetchInterval = 0;
    unsigned long lastFetch = 999999;

    // True once the first feed fetch has populated trackedAircraft. Lets the new-contact
    // buzzer chirp (HAS_AUDIO boards) stay silent through the initial bulk population and
    // only sound for genuinely new arrivals afterwards.
    bool initialSyncDone = false;

    // TODAY stats: contacts since local midnight, peak simultaneous airborne
    // count, and an hourly histogram (drives the Stats sparkline + "busiest
    // hour"). RAM-only by design -- no flash wear; resets at local midnight and
    // on reboot (devices run continuously; the weekly preventive reboot lands
    // at night when the counters are near-empty anyway). NTP-gated: without a
    // clock nothing is attributed.
    uint32_t todayContacts = 0;
    uint16_t todayPeak = 0;
    uint16_t todayHourCounts[24] = {0};
    uint32_t statsDayLocal = 0; // local epoch-day the counters describe

    // "Aircraft of the day": the single most notable catch since local midnight,
    // shown as a Stats block. RAM-only, resets with the TODAY counters. Priority
    // (highest score wins): emergency > new lifelist type > military > new
    // airline > highest-flying, with altitude as the within-class tiebreak.
    int aotdScore = 0;
    String aotdCallsign, aotdLabel, aotdReason;

    // Alert-tone sequencer (HAS_AUDIO boards; config "tones", default on). The board
    // chirp primitive is a single <=80 ms burst, so distinct per-class tones are built
    // as chirp PATTERNS (count x on/gap) stepped non-blockingly from Update(). A
    // pattern in progress is never interrupted -- the first signal is the loudest.
    // Patterns: new contact 1x40, watchlist 2x40, military 2x70, overhead 3x40,
    // emergency 4x80.
    bool tonesEnabled = true;
    uint8_t toneRemaining = 0;
    uint16_t toneOnMs = 0;
    uint16_t toneGapMs = 0;
    unsigned long nextToneAtMs = 0;
    void PlayTone(uint8_t count, uint16_t onMs, uint16_t gapMs);
    void UpdateTones(); // step the pattern; pumped every Update()

    // Latest IMU-derived tilt (HAS_IMU boards), refreshed in Update() and shown on the Stats
    // screen. pitch = nose up/down, roll = bank left/right, in degrees; imuValid gates display.
    float imuPitch = 0.0f;
    float imuRoll = 0.0f;
    bool imuValid = false;
    unsigned long lastImuReadMs = 0;

    // Data source. Default is the OpenSky cloud API; the user can instead point
    // Blipscope at their own ADS-B receiver's dump1090-fa/readsb "aircraft.json"
    // HTTP endpoint, which has no rate limit and updates ~1 Hz. The two sources
    // are mutually exclusive (config selector), so only one feed is ever polled.
    bool useLocalSource = false;
    String localUrl = ""; // normalised aircraft.json URL, empty unless local

    // Where a LOCAL-receiver device gets detail-card enrichment (config
    // "local-details"). Only consulted when useLocalSource.
    //
    // Cloud is the default because the adsbdb-direct path is the worse deal on
    // every axis that matters here: it needs TWO external hosts for one card
    // (api.adsbdb.com for metadata + a separate callsign call, then
    // airport-data.com behind a redirect for the thumbnail), it has no shared
    // cache, no failover, and no access to the curated photo library -- so the
    // users running their own receiver got the weakest cards. Routing details
    // through the proxy REPLACES that connection rather than adding to it: one
    // host, one round trip for metadata+route+photo pointer, and the same
    // keep-alive client the rest of the firmware already uses.
    enum class LocalDetails : uint8_t {
        Cloud,   // via the Blipscope Cloud proxy (default)
        Off,     // contact nothing; the card shows only what the receiver reports
        // An "Adsbdb" member used to sit here -- straight to the public API, no
        // photos. Removed when adsbdb left the stack; stored values are migrated
        // to Cloud by ConfigMigration rev 4 so no device boots holding an enum
        // value that no longer exists.
    };
    LocalDetails localDetails = LocalDetails::Cloud;

    // True when detail lookups should go through the proxy: either a full cloud
    // device, or a local-receiver device that opted into cloud details AND has
    // somewhere to send them. The URL/key check is what makes an unconfigured
    // device fall back instead of firing requests at an empty host.
    bool UseCloudEnrich() const {
#ifdef FEATURE_CLOUD_FEED
        if (useCloudSource) return true;
        return useLocalSource && localDetails == LocalDetails::Cloud
               && !cloudUrl.isEmpty() && !cloudKey.isEmpty();
#else
        return false;
#endif
    }

#ifdef FEATURE_CLOUD_FEED
    // Blipscope Cloud (the proxy/ Worker): the DEFAULT source on cloud builds.
    // One host, keep-alive TLS, tiny payloads; the proxy handles the upstream
    // relationship (adsb.lol, ODbL 1.0 -- credited on the config page).
    bool useCloudSource = false;
    String cloudUrl = "";  // normalised base URL: NVS "cloud-url" else CLOUD_FEED_BASE
    String cloudKey = "";  // X-Blip-Key: NVS "cloud-key" else CLOUD_FEED_KEY (never logged)
    double rangeKmCfg = 100.0; // configured radar radius in km, the /api/v1/blipscope/blips r param

    // Fleet tunables from /api/v1/blipscope/config (server-resolved for this X-Blip-Model),
    // fetched on boot + daily and applied live -- no reboot. cloudCfg's defaults
    // serve until the first fetch lands.
    CloudFeed::Config cloudCfg;
    unsigned long lastCloudCfgFetch = 0; // millis() of the last request (0 = never)
    bool cloudCfgEverApplied = false;
    bool otaCheckRequested = false; // set when config minFw > FW_VERSION; main.cpp consumes

    // Airport overlay long tail from /api/v1/blipscope/airports (server-capped, priority-
    // sorted). While non-empty it supersedes the baked include/Airports.h
    // table in DrawAirports; empty (boot, fetch failed, non-cloud) falls back
    // to the baked majors. Fetched once the location is known, then daily.
    std::vector<CloudFeed::CloudAirport> cloudAirports;
    unsigned long lastCloudAirportsFetch = 0; // millis() of the last request (0 = never)
    // How long to wait before the next airports request. 0 = use the default
    // rule (5 min while the overlay is empty, 24 h once it has landed). Set to
    // a real backoff only by a FAILED fetch. This replaced a trick where the
    // failure path rewound lastCloudAirportsFetch to fake a 15 min due time --
    // which silently stopped working the moment the due interval was no longer
    // a fixed 24 h, and would have turned a persistent failure into a retry
    // every loop pass.
    unsigned long cloudAirportsRetryMs = 0;

    // Public spotting leaderboard (opt-in, off by default). Submits the logbook
    // tallies hourly through the proxy; the parsed standing feeds a Stats block.
    bool lbEnabled = false;
    String lbName;
    unsigned long lastLeaderboardSubmit = 0; // millis() of the last submit (0 = never)
    bool lbHaveStanding = false;             // a submit has returned a rank at least once
    int lbRank = 0, lbSeasonRank = 0, lbTotal = 0;
    long lbPoints = 0, lbSeasonPoints = 0;
    String lbRarestType;                     // this device's rarest logged type (fleet-wide)
    int lbRarestPct = 0;                     // % of opted-in devices that also have it
    // A due submit outranks new enrichment until it is away. Without this the
    // hourly request loses every race for the depth-1 enrich queue on a dense
    // sky and never lands at all -- silently, since nothing errors.
    bool lbSubmitPending = false;            // due, waiting for the enrich slot
    unsigned long lbSubmitDueMs = 0;         // millis() it became due
    unsigned long lbWorstSubmitWaitMs = 0;   // worst due->away wait (the soak gate reads this)
    bool lbStarvedReported = false;          // GATE BROKEN printed once per pending episode
    uint8_t lbConsecutiveFails = 0;          // identical-failure run; 0 whenever one succeeds
    unsigned long lbRetryBackoffMs = 0;      // 0 = healthy hourly schedule, else the backoff gap



    // Rank-up toast: a transient celebratory banner drawn over any screen for a few
    // seconds after a submit whose overall rank climbed. Armed only once a standing
    // already exists (the first-ever rank never toasts).
    unsigned long rankToastUntilMs = 0;      // millis() the toast expires (0 = none)
    int rankToastRank = 0;                    // new (improved) overall rank to show
    int rankToastDelta = 0;                   // positions gained (positive)

    // Recent enrichments by hex, surviving aircraft eviction so re-taps are
    // instant even when a contact flapped out of range and back.
    CloudFeed::EnrichCache enrichCache;
#endif

    // ---- MEASUREMENT (a [perf] line every 60 s) ------------------------------
    // Answering one question before any capacity work is built: at a dense
    // location, is the delay CONTENTION (the shared depth-1 client busy with
    // enrichment while polls slip) or UPSTREAM DROUGHT (the feed itself dry --
    // adsb.lol positions run ~90 percent 429 with stretches to 43 min)? The two
    // demand fixes in different workstreams and look identical on the glass.
    //
    // The split is decidable on-device and needs no log join. Data age is
    // (millis() - lastGoodDataMs) + dataLagAtMergeMs: the first term is time WE
    // did not poll, the second is age the snapshot already carried when it
    // arrived. Contention inflates the first. A dry feed inflates the second.
    // X-Cache/X-Upstream then say which of the two the proxy blames.
    struct PerfWindow {
        uint32_t polls = 0;              // completed position fetches
        uint32_t enrichReqs = 0;         // enrichment requests issued
        unsigned long fetchBusyMs = 0;   // wall time inside position fetches
        unsigned long enrichBusyMs = 0;  // wall time inside enrichment requests
        unsigned long parseMs = 0;       // of fetchBusyMs, time consuming the body
        uint64_t bodyBytes = 0;          // position payload bytes
        uint32_t acReceived = 0;         // aircraft in the payload, BEFORE the MAX_AIRCRAFT cut
        uint32_t acKept = 0;             // aircraft actually tracked
        uint32_t cacheHit = 0, cacheStale = 0, cacheMiss = 0;
        unsigned long lagSumMs = 0;      // sum of dataLagAtMergeMs -- upstream staleness
        unsigned long lagMaxMs = 0;
        unsigned long gapMaxMs = 0;      // longest observed gap between good merges
        uint32_t episodes = 0;           // times the picture crossed into Aging
    };
    PerfWindow perf;
    unsigned long lastPerfReportMs = 0;
    bool perfInEpisode = false;          // currently past AGING_MS (edge-detect for episodes)
    void ReportPerf();

    // Claim confirmation. A claim is the whole reward for tapping, so it is
    // acknowledged rather than left to be inferred from a badge disappearing.
    //
    // A QUEUE, not a single slot. Repeated taps at one spot cycle through stacked
    // contacts (see HandleTap), so three claims can land inside a couple of
    // seconds; overwriting would show one pill and silently swallow the other two,
    // which is the exact failure the confirmation exists to prevent. Small and
    // fixed: claims are rare, and a queue that can grow is a leak on a device that
    // runs for months.
    static constexpr size_t CLAIM_TOAST_QUEUE = 4;
    static constexpr unsigned long CLAIM_TOAST_MS = 2200;
    String claimToastText[CLAIM_TOAST_QUEUE];
    size_t claimToastCount = 0;             // entries waiting, including the one showing
    unsigned long claimToastUntilMs = 0;    // millis() the front entry expires (0 = idle)
    void PushClaimToast(const String& text);
    void UpdateClaimToast();
    void DrawClaimToast(BandCanvas& backbuffer) const;
    // Claim everything a tapped aircraft is carrying (type, airline, country,
    // route airports) and queue the confirmation. Safe to call every frame the
    // card is open: it no-ops once the type is claimed.
    void ClaimTappedAircraft(TrackedAircraft& tracked);

    // Staleness bookkeeping (all sources): when the last good feed merge landed
    // (device clock), and how old the server said that snapshot already was --
    // cloud mode's SWR-served stale tiles keep their original t, so the lag is
    // part of the honest total data age. Non-cloud sources leave the lag at 0.
    unsigned long lastGoodDataMs = 0;   // millis() at merge; 0 = no data yet
    unsigned long dataLagAtMergeMs = 0; // (device epoch - snapshot t) * 1000 at merge

#ifdef FEATURE_CLOUD_FEED
    // CREDENTIAL REJECTION -- a cloud 401/403 that has persisted, which is the one
    // failure the device cannot wait out. Everything else in the stale ladder
    // recovers on its own; this recovers only when a human re-verifies the board,
    // so it has to say so rather than presenting as a long outage.
    //
    // Kept in RAM ONLY, deliberately. A persisted latch that was wrong would
    // survive a reboot and need a manual clear, and the failure mode of "the
    // device insists it needs re-verifying when it does not" is worse than
    // re-detecting after a restart -- the rejection reappears within minutes if
    // it is real. See DEBOUNCE in the .cpp for why both thresholds exist.
    uint8_t cloudAuthFailStreak = 0;      // consecutive 401/403 cloud fetches
    unsigned long cloudAuthFirstFailMs = 0; // millis() of the streak's first failure
    bool cloudAuthLatched = false;        // both thresholds crossed: surface it
#endif

    // Background OpenSky states fetch. The HTTPS GET + JSON decode used to run
    // inline on the loop and stall it for a second or two each cycle; since touch
    // is only polled once per loop, a tap on a plane during that stall was missed.
    // A dedicated task now does the GET + parse, and the loop only merges the parsed
    // result into trackedAircraft (a fast map operation), so it stays responsive
    // throughout the refresh. The task shares the loop's single HTTP client (the C3
    // hasn't the heap for a second TLS context); HttpRequestManager's own mutex
    // serializes the two, and the !fetchInFlight gate keeps the loop from blocking
    // on it during a fetch. All trackedAircraft mutation still happens on the loop
    // task; the fetch task only ever produces a parsed vector of its own.
    TaskHandle_t  fetchTaskHandle = nullptr;   // non-null once the task is running
    QueueHandle_t fetchRequestQueue = nullptr; // loop -> task: FetchRequest*
    QueueHandle_t fetchResultQueue = nullptr;  // task -> loop: FetchResult*
    bool fetchInFlight = false;                // loop-task-only: a request is outstanding

    // Background enrichment task (adsbdb metadata/route + aircraft photo download).
    // The detail-card and radar-metadata lookups used to run as blocking HTTPS GETs
    // on the loop; under low heap a slow third-party host stalled the single core
    // long enough to starve the watchdog-fed async_tcp service task into a reboot.
    // They now run here, off-loop, exactly like the OpenSky fetch above: the task
    // does the blocking GET + parse and never holds a pointer into trackedAircraft;
    // the loop applies the parsed result (and decodes the photo into its sprite, so
    // the sprite stays single-task). One enrichment is outstanding at a time
    // (enrichInFlight), shared between the detail path and the radar enrichment.
    TaskHandle_t  enrichTaskHandle = nullptr;
    QueueHandle_t enrichRequestQueue = nullptr; // loop -> task: EnrichRequest*
    QueueHandle_t enrichResultQueue = nullptr;  // task -> loop: EnrichResult*
    bool enrichInFlight = false;                // loop-task-only: a request is outstanding

    // Frame-time + heap instrumentation (serial health line every 30 s; loud
    // warning when a budget is broken). Samples are whole loop() passes in
    // tenths of a millisecond, recorded by main.cpp on radar builds.
    static constexpr size_t FRAME_SAMPLES = 128;
    uint16_t frameSampleBuf[FRAME_SAMPLES] = {0}; // 0.1 ms units
    size_t frameSampleCount = 0;                  // ring write index (wraps)
    // Worst single loop pass since the last health report (0.1 ms units). The
    // avg/p95 above are computed only over the last FRAME_SAMPLES frames (~a few
    // seconds), so a transient multi-second stall EARLIER in the 30 s window is
    // overwritten before the report -- exactly the overnight-slowdown signature.
    // This max spans the whole interval, so any stall is caught and logged.
    uint16_t frameMaxTenths = 0;
    unsigned long lastHealthReportMs = 0;

    // ---- issue #245: the enrichment-starvation watch ------------------------
    //
    // The state this tracks is SILENT and PERMANENT without it. When the internal
    // heap fragments past a handshake-sized block the ballast cannot be re-taken,
    // both CanHandshake() gate sites refuse forever, and every aircraft card
    // loses its type, operator and photo. Positions are ungated, so the radar
    // keeps drawing a full sky at a healthy frame rate and nothing looks wrong.
    // Observed on COM119 2026-08-24: ball=0/48, tlsOk=0, enrichReqs=0, and the
    // owner's report was "the images stopped loading", not "my device is broken".
    // How long a board must be CONTINUOUSLY starved before we try to recover.
    //
    // Not zero, and the reason is that `starved` is legitimately true for a
    // moment during normal operation: while a handshake is in flight the ballast
    // has been released and the session holds the block, so both halves of the
    // test can be true at once. The health tick is 30 s, so 2 minutes means four
    // consecutive starved observations -- comfortably past any real handshake,
    // and short enough that an owner barely registers the gap.
    static constexpr unsigned long STARVE_RECOVERY_AFTER_MS = 120000UL;

    // The reminder cadence once starved. The edge is always logged; this stops
    // the reminder becoming the noise that the old ball=0 line already was.
    static constexpr unsigned long STARVE_LOG_INTERVAL_MS = 300000UL;

    // Consecutive health ticks on which the allocator refused a handshake
    // block. The watch keys off a RUN rather than one sample: a healthy board
    // dips for a tick or two (measured on BOTH soak boards), and an alarm
    // that fired on those would be switched off within a week.
    uint8_t       starveRun = 0;
    unsigned long starvedSinceMs = 0;      // 0 = not starved; else when it began
    unsigned long lastStarveLogMs = 0;     // rate-limits the reminder line
    unsigned long lastStarveRecoveryMs = 0;// last recovery attempt
    uint32_t      starveRecoveries = 0;    // attempts made this starvation episode
    uint32_t      starveEpisodes = 0;      // lifetime, for the health line
    uint32_t budgetBreaches = 0;                  // BUDGET BROKEN lines emitted (soak gate: must stay 0)

    // backlight + clock
    uint8_t configuredBrightness = 255; // day/base level from the slider
    uint8_t currentBrightness = 255;    // currently applied level (avoids redundant writes)
    bool autoDim = true;                // dim at night based on solar elevation
    bool nightClockEnabled = false;     // config "night-clock": show a big 7-seg clock when it's night and the sky is empty
    bool nightNow = false;              // solar night, cached by UpdateBrightness (20 s cadence)
    long utcOffsetSec = 0;              // local time = UTC + this, for the clock
    unsigned long lastBrightnessCheck = 0;

    ConfigurationWebServer& configServer;
    OpenSkyAuthTokenHandler& authHandler;
    HttpRequestManager& http;
    LGFX& tft;

    void DrawRadarCircles(BandCanvas& backbuffer) const;
    // Fixed airport markers (baked Airports.h table culled to the scan box),
    // drawn under the aircraft layer so blips always win the ink.
    void DrawAirports(BandCanvas& backbuffer) const;
    std::pair<int, int> ProjectCoordinateToScreen(float predLat, float predLon) const;

    // Step the sweep beam one frame and, while paint-and-fade is active, paint
    // every contact the beam crossed this frame (latch position + reset fade).
    void AdvanceSweep();
    // Paint-and-fade needs both the beam (so there's something to paint under) and
    // the fade toggle. Off -> blips glide live at full brightness.
    bool PaintAndFadeActive() const { return displaySweep && displayFade; }
    // Radar blip position/brightness honoring paint-and-fade when it's active,
    // else the live dead-reckoned position at full brightness. Hit-testing uses the
    // position form too, so taps land on the blip as drawn, not where it really is.
    std::pair<float, float> RadarBlipPosition(const TrackedAircraft& tracked) const;
    float RadarBlipBrightness(const TrackedAircraft& tracked) const;
    void DrawAircraftInfo(BandCanvas& backbuffer, int x, int y, const TrackedAircraft& tracked, float brightness = 1.0f) const;
    // Screen box of the info label as DrawAircraftInfo lays it out (below-right of the marker
    // at x,y). Returns false when no label is drawn. Used by the tap hit-test so a tap on the
    // label -- the part the eye reads as "the aircraft" -- selects the contact, not just the dot.
    bool AircraftLabelBox(const TrackedAircraft& tracked, int x, int y, int& bx, int& by, int& bw, int& bh) const;
    void DrawAircraftTriangle(BandCanvas& backbuffer, int x, int y, const TrackedAircraft& tracked, uint32_t color) const;
    // Generic dim aircraft glyph drawn in the detail card's photo slot when adsbdb has no photo,
    // so a photo-less card reads as designed rather than broken. Varied by emitter category.
    void DrawAircraftSilhouette(BandCanvas& backbuffer, int cx, int cy, const TrackedAircraft& tracked) const;
    void DrawAircraftTrail(BandCanvas& backbuffer, const TrackedAircraft& tracked, int headX, int headY, float brightness = 1.0f) const;
    // Follow Mode stage 1. DrawFollowTrack is const-except-for-instrumentation,
    // so it is not const: it writes the timing counters that are its whole point.
    void DrawFollowTrack(BandCanvas& backbuffer);
    void DrawFollowHud(BandCanvas& backbuffer) const;
    bool MatchesFollow(const TrackedAircraft& tracked) const;
    void UpdateFollowTrack();
    // The local face (§10). One screen slot, several faces (C6) -- DrawFollow is
    // the router, and the local face is the only one built.
    void DrawFollow(BandCanvas& backbuffer);
    void DrawFollowLocalFace(BandCanvas& backbuffer);
    /// The view the local face draws: home at the centre, rings auto-fitted to
    /// the track's extent. Pure arithmetic lives in include/FollowGeometry.h.
    follow::LocalView BuildLocalView() const;
    std::pair<int, int> ProjectLocal(float pLat, float pLon, const follow::LocalView& v) const;
    /// The followed contact, if it is in the table this pass. Null is the normal
    /// case at pattern altitude, not an error -- see FollowTrack.h.
    const TrackedAircraft* FollowedAircraft() const;
    /// Empty follow field -> the screen does not exist (§13.3).
    bool FollowScreenVisible() const { return !followTarget.isEmpty(); }
    /// Next/previous VISIBLE screen. dir is +1 or -1.
    void AdvanceScreen(int dir);
    /// Everything that happens on a follow STATE CHANGE: freeze the finished
    /// flight (11), start a new one, and surface the screen for a dwell (13.3).
    void HandleFollowTransition();
    /// The ONE sanctioned outbound use of the follow target (17 / C3).
    bool SendFollowAlert(follow::State was, follow::State now);
    void DrawFollowPostFlightCard(BandCanvas& backbuffer);
    /// Ellipsise `t` to the chord of the round panel at row `yTop`.
    /// A round screen has no edge to clip against -- glyphs just run off the
    /// curve, and text clipped by the bezel looks identical to a bug.
    String FitToDisc(BandCanvas& backbuffer, const String& t, int yTop, int lineH) const;
    /// 7.1: LANDED until the next takeoff shows the card, not the live face.
    bool ShowPostFlightCard() const;
    /// The nearest airport code to home, from the data already on the device.
    void ResolveHomeField();
    void DrawEmergencyAlert(BandCanvas& backbuffer, int x, int y, const TrackedAircraft& tracked) const;
    void DrawDetailCard(BandCanvas& backbuffer, const TrackedAircraft& tracked);

    void DrawRadar(BandCanvas& backbuffer, bool firstPass);
    void DrawList(BandCanvas& backbuffer);
    void DrawStats(BandCanvas& backbuffer);
    void DrawScreenIndicator(BandCanvas& backbuffer) const;
    void DrawClock(BandCanvas& backbuffer) const;
    void DrawNightClock(BandCanvas& backbuffer) const; // big 7-seg face replacing an empty night radar
    std::vector<String> SortedAircraftByDistance();

    void UpdateBrightness(); // apply solar day/night dimming (throttled)

    void StartFetchTask();                      // spawn the OpenSky fetch task once
    static void FetchTaskTrampoline(void* arg); // FreeRTOS entry -> RunFetchTask()
    void RunFetchTask();                        // blocking GET + JSON decode, off-loop
    void RequestFetch();                        // loop: snapshot params + token, signal task
    void ConsumeFetchResult();                  // loop: merge a ready result, non-blocking

    // The feed cadence currently in force. Cloud mode runs the config-driven
    // active/idle/night state machine; other sources return fetchInterval.
    unsigned long CurrentPollIntervalMs() const;

    // Data-staleness indicator: true once the last good picture is older than
    // staleFactor x the current poll interval (cloud counts server-side cache
    // age too). Drawn as a small tag on the radar so a quietly-failing feed is
    // visible instead of a frozen-looking sky.
    bool IsDataStale() const;
    void DrawStaleIndicator(BandCanvas& backbuffer) const;

public:
    // Escalation ladder for stale data. The failure this exists to prevent is a
    // FROZEN SKY THAT LOOKS LIVE: nothing evicts aircraft when fetches stop (the
    // eviction pass runs inside the successful-merge path), so without this the
    // scope keeps drawing a plausible picture indefinitely -- planes drifting,
    // then motionless, under a small amber tag that is easy to miss. An empty
    // radar reads as "something is wrong"; a still one reads as "working".
    //
    // Age is (millis() - lastGoodDataMs) + the snapshot's own lag, which is
    // source-independent: an unplugged local receiver, a dead OpenSky token and a
    // failing upstream all land here identically, which is the point.
    enum class StaleStage : uint8_t {
        Live,   // fresh, or nothing merged yet (that's "starting up", not stale)
        Stale,  // past staleFactor x interval -- quiet amber tag, as before
        Aging,  // the picture is old enough that a glance must not read as live
        NoData, // past the dead-reckoning cap: stop presenting a plausible picture
    };

    // Age of the newest data we have merged, including the lag the snapshot
    // already carried. 0 before the first merge.
    unsigned long DataAgeMs() const;
    StaleStage CurrentStaleStage() const;

    // The sweep is the strongest "this is live" affordance on the scope, so it
    // stops once the data is Aging. A sweeping radar over stale contacts is
    // exactly the lie this ladder is closing. Cheap, too: suppressing it SAVES
    // the per-frame sweep render rather than adding work.
    bool SweepSuppressed() const { return CurrentStaleStage() >= StaleStage::Aging; }

#ifdef FEATURE_CLOUD_FEED
    // True once a cloud 401/403 has persisted past BOTH debounce thresholds: the
    // board's key is being refused and only a re-verify fixes it.
    //
    // Read by the config web server as well as the radar, so the page and the
    // screen cannot disagree about whether this board needs attention -- they are
    // the same bit, not two evaluations of the same idea.
    bool NeedsReverify() const { return cloudAuthLatched; }
#else
    // Non-cloud builds (OpenSky BYO, local receiver) have no credential the
    // server can refuse, so this is structurally false rather than merely unset.
    bool NeedsReverify() const { return false; }
#endif

private:

#ifdef FEATURE_CLOUD_FEED
    // Both return TRUE only when the request actually reached the fetch queue.
    // The caller MUST NOT commit its "last fetched" timestamp until it does --
    // stamping first turns a dropped request into a 24 h outage of that feed,
    // because the retry condition is then already satisfied-away. See #129.
    bool RequestCloudConfig();                  // loop: queue a /api/v1/blipscope/config fetch on the fetch task
    bool RequestCloudAirports();                // loop: queue a /api/v1/blipscope/airports fetch on the fetch task
    bool QueueLeaderboardSubmit();              // loop: queue an hourly /api/v1/blipscope/leaderboard POST
    void PersistLeaderboardStanding();          // mirror the standing to NVS for the config page
    void RequestCloudEnrich(const String& icao24, const String& callsign,
                            float acLat, float acLon); // loop: queue a /api/v1/blipscope/enrich lookup
    // Apply one enrichment payload to a tracked aircraft (shared by the network
    // result and the LRU-cache hit paths); notes the logbook like adsbdb did.
    void ApplyEnrichment(TrackedAircraft& tracked, const CloudFeed::Enrichment& e);
    // Background-enrichment gate for the current cloud enrich level: Full = any
    // aircraft, Watchlist = only watchlist-prefix matches on hex/callsign (the
    // fields available pre-enrichment), Off = none.
    bool CloudShouldBackgroundEnrich(const TrackedAircraft& tracked) const;
#endif

    void StartEnrichTask();                      // spawn the enrichment task once
    static void EnrichTaskTrampoline(void* arg); // FreeRTOS entry -> RunEnrichTask()
    void RunEnrichTask();                        // blocking adsbdb GET / photo download, off-loop
    bool EnrichDeferredForSubmit() const;    // true while a due submit owns the next enrich slot
    void RequestPhoto(const String& icao24, const String& url, const String& authKey = ""); // loop: queue a photo download (authKey set in cloud mode)
    void ConsumeEnrichResults();                 // loop: apply a ready result, non-blocking

    void HandleTouch();             // poll the touch panel, classify tap vs swipe
    // Gesture classification for one touch sample (press/drag/release -> tap or
    // 4-way swipe). Split from the hardware poll so a harness (the SOAK_TEST
    // gesture script) can drive the exact production pipeline with synthetic
    // samples rather than a parallel copy of the classifier.
    void ProcessTouchSample(bool touched, int32_t tx, int32_t ty);
    void HandleTap(int tx, int ty); // route a tap to selection / dismissal
    void HandleSwipe(Swipe swipe);  // route a swipe to navigation / pin
    void ExitDetail();              // leave the detail card and free its ~15 KB photo sprite
    // Backfill the logbook from contacts already on screen when it is switched on
    // mid-session. EDGE-ONLY (NoteContact is an odometer); see the definition.
    void SeedLogbookFromTracked();

    // Resolve the selected aircraft's metadata, route, then photo. Each step is
    // handed to the enrichment task and applied when it returns, so the card fills
    // in progressively without ever blocking the loop. Runs for the inspected
    // aircraft even when the radar's enrichment fields are disabled.
    void ProcessDetailLookups();

    // Queue type/operator/registration lookups for tracked aircraft via adsbdb.com,
    // one at a time and throttled, so enrichment never blocks the render loop.
    void ProcessMetadataLookups();

    // Watchlist match, classified so the alert tone can distinguish WHICH kind
    // of entry fired: Specific = this exact aircraft (callsign/hex/registration
    // prefix), Category = a whole aircraft type (type-designator prefix). A
    // specific-identity match outranks a category one.
    enum class WatchClass : uint8_t { None, Specific, Category };
    WatchClass ClassifyWatchlist(const TrackedAircraft& tracked) const;
    bool MatchesWatchlist(const TrackedAircraft& tracked) const
    {
        return ClassifyWatchlist(tracked) != WatchClass::None;
    }
    bool IsOverhead(const TrackedAircraft& tracked) const; // within overheadKm of the centre
    // Score a freshly-enriched contact for "aircraft of the day" and adopt it if
    // it beats the day's current holder.
    void ConsiderAircraftOfDay(const TrackedAircraft& tracked, bool newType, bool newOperator);
    void ProcessAlerts();                                  // ntfy: flyover + overhead, throttled
    // The Send* builders queue the POST onto the enrichment task (the loop must
    // never block on ntfy.sh -- it used to cost taps). They return false when the
    // depth-1 queue is busy, so the caller leaves the aircraft un-marked and the
    // alert retries on a later tick instead of being lost.
    bool QueueNtfyPost(const String& title, const String& tags, const String& body);
    bool SendFlyoverNotification(const TrackedAircraft& tracked, bool military = false);
    bool SendOverheadNotification(const TrackedAircraft& tracked);
    bool SendEmergencyNotification(const TrackedAircraft& tracked);
    void DrawOverheadAlert(BandCanvas& backbuffer, int x, int y) const;

    // Visual alert layer: scan the picture for alerting classes + fire flash
    // burst edges (loop task, once per frame), and draw the resulting overlay
    // (edge ring pulse / full-screen flash) on top of whatever screen is active.
    void UpdateVisualAlerts();
    void DrawVisualAlert(BandCanvas& backbuffer) const;
    bool TapDismissesAlert(int tx, int ty) const; // tap landed on an active ring / flash burst
    void DismissVisualAlert();                     // latch the dismiss + clear the current overlay
    void DrawRankToast(BandCanvas& backbuffer) const; // transient "RANK UP" banner (cloud feed only)

    void PublishMqttState();     // retained JSON summary of the current picture
    void PublishMqttDiscovery(); // Home Assistant MQTT discovery configs (retained)
    void PublishMqttEvents();    // fire one-shot HA events on watchlist/emergency/mil/overhead hits

    // Toggle-aware special classification + its display colour, shared by the
    // radar, list, and detail views. Honors the per-class show toggles and the
    // priority order in SpecialAircraft::Class.
    SpecialAircraft::Class SpecialClassOf(const TrackedAircraft& tracked) const;
    static uint32_t SpecialColor(SpecialAircraft::Class c);

public:
    AircraftManager(ConfigurationWebServer& config, OpenSkyAuthTokenHandler& auth, HttpRequestManager& httpManager, LGFX& tftGfx)
        : configServer(config), authHandler(auth), http(httpManager), tft(tftGfx)
    {
    }
    ~AircraftManager() = default;

    void Initialise();

    /// Flush a dirty logbook because the Collection page was fetched. Called on
    /// the loop task; rate-limited and dirty-only inside the logbook.
    void FlushLogbookForFetch() { if (logbookEnabled) logbook.MaybePersistForFetch(); }
    void Update();
    void Draw(BandCanvas& backbuffer, bool firstPass);
    bool IsRadarView() const { return screen == Screen::Radar && !inDetail; }
    // Is the detail card up? Distinct from !IsRadarView(), which is also true on
    // the List and Stats screens -- a caller that needs "a card is covering the
    // view" cannot get it from IsRadarView() without conflating the two. Read-only,
    // so it adds no mutation surface; the soak harness uses it to dwell on a card
    // and then dismiss it rather than leaving it open until the next burst.
    bool DetailCardOpen() const { return inDetail; }

    // On-screen "Reset WiFi", consumed by main.cpp. This is the PRIMARY recovery
    // path for a device with wrong credentials: it still boots, renders and takes
    // touch -- only the feed is dead -- so the UI is fully available exactly when
    // the config web page is not (that page needs the network the device has lost).
    // Lives on the Stats screen, never the radar face, and needs a second
    // confirming tap.
    factoryreset::Tier ConsumeResetTier()
    {
        const factoryreset::Tier t = (factoryreset::Tier)resetTierRequested;
        resetTierRequested = 0;
        return t;
    }
    // Night clock mode: at solar night with an empty sky (and NTP synced), the radar
    // screen shows a big seven-segment clock instead of a dead scope. main.cpp also
    // consults this to suppress the sweep beam under the clock face.
    bool NightClockActive() const;
    // Current sweep beam angle in radians; main.cpp draws the beam from this so it
    // matches the paint-and-fade crossing test exactly (single source of truth).
    float CurrentSweepAngle() const { return sweepAngle; }
    // Cached "scanline" config (reloaded on ConsumeConfigChanged); main.cpp reads
    // this instead of hitting NVS every frame from the loop task.
    bool SweepEnabled() const { return displaySweep; }

    // Frame instrumentation: main.cpp reports each loop() pass; every 30 s this
    // logs avg/p95 frame time + free heap/largest block and warns loudly when
    // the budgets (p95 <= 60 ms with sweep -- recalibrated 2026-07-10 from the
    // s3-128 soak's measured 51-53 sustained; largest block >= 20 KB) are broken.
    void RecordFrameUs(uint32_t frameUs);
    /**
     * Top of the DESTRUCTIVE chrome the soak harness must never tap.
     *
     * The 2026-08-04 run ended itself at 65 h: a scripted double-tap landed in the
     * [ Reset WiFi ] row on the Stats screen, armed it, confirmed it 633 ms later,
     * and wiped the credentials (#164). ~68 double-taps happen in a multi-day run
     * and each has a small chance of landing there, so this was not bad luck --
     * any long enough soak ends itself, and the failure presents as a spontaneous
     * reset, which is how it was first reported.
     *
     * Returned from the DRAWN bounds rather than as a constant, for the same
     * reason the hit box is: the reserved rows move with the panel and the layout,
     * and a hardcoded "avoid the bottom 40 px" would drift silently -- back into
     * killing soaks -- the moment the Stats screen changes. -1 before the Stats
     * screen has been drawn once, which the caller treats as "no exclusion yet".
     */
    int DestructiveRowTopY() const { return resetRowY0; }

    uint32_t BudgetBreachCount() const { return budgetBreaches; } // soak-gate criterion
    uint32_t AllocFailureCount() const;  // heap alloc-failure hook count (outcome-based soak criterion)
    uint32_t FetchHardFailCount() const; // fetches failing with statusCode <= 0 (TLS/DNS/connect/timeout class)

#ifdef FEATURE_CLOUD_FEED
    // True once after /api/v1/blipscope/config reported minFw newer than this build; main.cpp
    // consumes it to run the normal OTA check immediately instead of waiting for
    // the daily timer.
    bool ConsumeOtaCheckRequest() {
        const bool req = otaCheckRequested;
        otaCheckRequested = false;
        return req;
    }
#endif

};