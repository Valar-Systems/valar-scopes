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
#include "MqttPublisher.h"
#include "LGFX.h"
#include "BandCanvas.h"
#include "CloudFeed.h" // no-op unless FEATURE_CLOUD_FEED

class AircraftManager
{
private:
    // Networking-off bisection builds (-DBISECT_TEST): compile-time kill switch for
    // every path that would reach the network (task spawns, fetch/enrich requests,
    // ntfy alerts), so the render/touch pipeline runs unmodified while no WiFi, no
    // background task, and no socket exists. See src/BisectHarness.h.
#ifdef BISECT_TEST
    static constexpr bool kNoNet = true;
#else
    static constexpr bool kNoNet = false;
#endif

    double lat = 0.0;
    double lon = 0.0;
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
    // Minimum airport size class to draw from the cloud /v1/airports overlay:
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

    // Screen navigation. Three top-level screens cycle via horizontal swipe; the
    // detail card overlays whichever screen you're on.
    enum class Screen { Radar, List, Stats };
    Screen screen = Screen::Radar;
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

#ifdef FEATURE_CLOUD_FEED
    // Blipscope Cloud (the proxy/ Worker): the DEFAULT source on cloud builds.
    // One host, keep-alive TLS, tiny payloads; the proxy handles the upstream
    // relationship (adsb.lol, ODbL 1.0 -- credited on the config page).
    bool useCloudSource = false;
    String cloudUrl = "";  // normalised base URL: NVS "cloud-url" else CLOUD_FEED_BASE
    String cloudKey = "";  // X-Blip-Key: NVS "cloud-key" else CLOUD_FEED_KEY (never logged)
    double rangeKmCfg = 100.0; // configured radar radius in km, the /v1/blips r param

    // Fleet tunables from /v1/config (server-resolved for this X-Blip-Model),
    // fetched on boot + daily and applied live -- no reboot. cloudCfg's defaults
    // serve until the first fetch lands.
    CloudFeed::Config cloudCfg;
    unsigned long lastCloudCfgFetch = 0; // millis() of the last request (0 = never)
    bool cloudCfgEverApplied = false;
    bool otaCheckRequested = false; // set when config minFw > FW_VERSION; main.cpp consumes

    // Airport overlay long tail from /v1/airports (server-capped, priority-
    // sorted). While non-empty it supersedes the baked include/Airports.h
    // table in DrawAirports; empty (boot, fetch failed, non-cloud) falls back
    // to the baked majors. Fetched once the location is known, then daily.
    std::vector<CloudFeed::CloudAirport> cloudAirports;
    unsigned long lastCloudAirportsFetch = 0; // millis() of the last request (0 = never)

    // Public spotting leaderboard (opt-in, off by default). Submits the logbook
    // tallies hourly through the proxy; the parsed standing feeds a Stats block.
    bool lbEnabled = false;
    String lbName;
    unsigned long lastLeaderboardSubmit = 0; // millis() of the last submit (0 = never)
    bool lbHaveStanding = false;             // a submit has returned a rank at least once
    int lbRank = 0, lbSeasonRank = 0, lbTotal = 0;
    long lbPoints = 0, lbSeasonPoints = 0;

    // Recent enrichments by hex, surviving aircraft eviction so re-taps are
    // instant even when a contact flapped out of range and back.
    CloudFeed::EnrichCache enrichCache;
#endif

    // Staleness bookkeeping (all sources): when the last good feed merge landed
    // (device clock), and how old the server said that snapshot already was --
    // cloud mode's SWR-served stale tiles keep their original t, so the lag is
    // part of the honest total data age. Non-cloud sources leave the lag at 0.
    unsigned long lastGoodDataMs = 0;   // millis() at merge; 0 = no data yet
    unsigned long dataLagAtMergeMs = 0; // (device epoch - snapshot t) * 1000 at merge

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
    unsigned long lastHealthReportMs = 0;
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

#ifdef FEATURE_CLOUD_FEED
    void RequestCloudConfig();                  // loop: queue a /v1/config fetch on the fetch task
    void RequestCloudAirports();                // loop: queue a /v1/airports fetch on the fetch task
    bool QueueLeaderboardSubmit();              // loop: queue an hourly /v1/leaderboard POST
    void RequestCloudEnrich(const String& icao24, const String& callsign,
                            float acLat, float acLon); // loop: queue a /v1/enrich lookup
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
    void RequestMetadata(const String& icao24);                      // loop: queue a metadata lookup
    void RequestRoute(const String& icao24, const String& callsign); // loop: queue a route lookup
    void RequestPhoto(const String& icao24, const String& url, const String& authKey = ""); // loop: queue a photo download (authKey set in cloud mode)
    void ConsumeEnrichResults();                 // loop: apply a ready result, non-blocking

    void HandleTouch();             // poll the touch panel, classify tap vs swipe
    // Gesture classification for one touch sample (press/drag/release -> tap or
    // 4-way swipe). Split from the hardware poll so the bisection storm can drive
    // the exact production pipeline with synthetic samples.
    void ProcessTouchSample(bool touched, int32_t tx, int32_t ty);
    void HandleTap(int tx, int ty); // route a tap to selection / dismissal
    void HandleSwipe(Swipe swipe);  // route a swipe to navigation / pin
    void ExitDetail();              // leave the detail card and free its ~15 KB photo sprite

    // Resolve the selected aircraft's metadata, route, then photo. Each step is
    // handed to the enrichment task and applied when it returns, so the card fills
    // in progressively without ever blocking the loop. Runs for the inspected
    // aircraft even when the radar's enrichment fields are disabled.
    void ProcessDetailLookups();

    // Queue type/operator/registration lookups for tracked aircraft via adsbdb.com,
    // one at a time and throttled, so enrichment never blocks the render loop.
    void ProcessMetadataLookups();

    bool MatchesWatchlist(const TrackedAircraft& tracked) const;
    bool IsOverhead(const TrackedAircraft& tracked) const; // within overheadKm of the centre
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

    void PublishMqttState();     // retained JSON summary of the current picture
    void PublishMqttDiscovery(); // Home Assistant MQTT discovery configs (retained)

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
    void Update();
    void Draw(BandCanvas& backbuffer, bool firstPass);
    bool IsRadarView() const { return screen == Screen::Radar && !inDetail; }
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
    uint32_t BudgetBreachCount() const { return budgetBreaches; } // soak-gate criterion
    uint32_t AllocFailureCount() const;  // heap alloc-failure hook count (outcome-based soak criterion)
    uint32_t FetchHardFailCount() const; // fetches failing with statusCode <= 0 (TLS/DNS/connect/timeout class)

#ifdef FEATURE_CLOUD_FEED
    // True once after /v1/config reported minFw newer than this build; main.cpp
    // consumes it to run the normal OTA check immediately instead of waiting for
    // the daily timer.
    bool ConsumeOtaCheckRequest() {
        const bool req = otaCheckRequested;
        otaCheckRequested = false;
        return req;
    }
#endif

#ifdef BISECT_TEST
    // Bisection-harness hooks (src/BisectHarness.cpp). The harness owns the fleet
    // ground truth and the gesture storm; these let it drive the production paths
    // without adding mutation surfaces: injection goes through the real fetch-result
    // queue and merge, and aimed taps use the real screen projection.
    void BisectApplyTestConfig();                          // deterministic bench config (overrides NVS)
    bool BisectInjectFleet(std::vector<Aircraft>&& fleet); // false = result queue busy (e.g. card open); drop the frame
    bool BisectCardOpen() const { return inDetail; }
    std::pair<int, int> BisectProject(float la, float lo) const { return ProjectCoordinateToScreen(la, lo); }
#endif
};