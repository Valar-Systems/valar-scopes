#include "AircraftManager.h"
#include "FollowLabel.h"
#include "DiscGeometry.h"
#include "DisplayUnits.h"
#include "RouteLabel.h"
#include "ConfigMigration.h"
#include "TlsAllocator.h"

#include <algorithm>
#include <cmath>
#include <time.h>
#include <utility>

#include <WiFi.h> // WiFi.localIP() for the device address on the Stats screen

#include "SpecialAircraft.h"
#include "IcaoCountry.h"      // origin country from the ICAO address, for feeds that omit it
#include "DeviceIdentity.h"
#include "Layout.h"
#include "Board.h"
#include "OtaUpdater.h" // FW_VERSION, compared against the cloud config's minFw gate
#include "TouchWatchdog.h" // CST816 supervisor; inert unless variant::TOUCH_WATCHDOG
#ifdef SOAK_TEST
#include "SoakHarness.h" // sparse human-scale gesture script (soak builds only)
#endif

// The detail card's photo is FULL BLEED: the sprite is the whole round panel, so
// it is the panel's size on every SKU rather than a fixed 150x100 slot.
//
// The server decides which artifact a device receives -- the enrich response
// carries the photo PATH (`p`), so the Worker picks the square variant for
// firmware new enough to draw it and the legacy 150x100 for everything else.
// That gate is not cosmetic: our drawJpg call site passes no scale, so maxWidth/
// maxHeight CLIP rather than shrink, and a square blob reaching old firmware
// would render as its own top-left corner.
//
constexpr int PHOTO_W = SCREEN_SIZE;
constexpr int PHOTO_H = SCREEN_SIZE;

// Where the two surviving lines sit on the full-bleed page. Kept next to each
// other because the scrim's ramp is solved against them: the band reaches full
// strength at 0.80 of height (y=192 on a 240 panel) and the first glyph must
// land below that, or the contrast measurement in #209 does not describe the
// pixels the text is actually on.
constexpr int FULLBLEED_TITLE_Y = SCREEN_SIZE - 46; // callsign, size 2
constexpr int FULLBLEED_HINT_Y  = SCREEN_SIZE - 22; // "tap: details", size 1

// The longest of two captions that survives the round bezel at FULLBLEED_HINT_Y.
//
// THE ROW THAT DECIDES IS THE LOWEST ONE, NOT THE FIRST. A disc narrows across
// the height of a glyph, so a string can begin comfortably inside the circle and
// lose its first and last characters where it ends. Measured on the s3-128:
// "representative photo" is 120 px at size 1; the chord at the caption's top row
// (y=218) is 138 px and at its bottom row (y=226) only 112 px. It fit where it
// started and was clipped where it finished -- which is why it read as a font or
// centring problem rather than a geometry one.
//
// MEASURED, NOT SHORTENED FOR EVERYBODY. The 412 and 480 panels have room for the
// full phrase; picking the short string globally would spend their geometry on the
// 240's problem. Same rule as everywhere else here -- no hardcoded 240, ask
// Layout.h (see the multi-SKU section of CLAUDE.md).
//
// Only the caption is chosen this way. Text is NOT truncated to fit: a caption
// clipped by software looks identical to one clipped by the bezel, and the whole
// point is to stop producing that.
static const char* CaptionForDisc(BandCanvas& canvas, const char* preferred, const char* fallback)
{
    // Match the ring the card actually draws -- centre (cx-1, cx-1), radius
    // SCREEN_SIZE_DIV_2-1 -- rather than the panel square. Clearing the panel is
    // not the test; clearing the circle someone is looking through is.
    const int r  = SCREEN_SIZE_DIV_2 - 1;
    const int dy = (FULLBLEED_HINT_Y + canvas.fontHeight() - 1) - (SCREEN_SIZE_DIV_2 - 1);
    if (dy >= r)
        return fallback; // the row is off the disc entirely; nothing fits
    const int half   = (int)sqrtf((float)(r * r - dy * dy));
    const int usable = 2 * half - 4; // a couple of px so glyphs don't sit on the ring
    return ((int)canvas.textWidth(preferred) <= usable) ? preferred : fallback;
}

// Display-unit conversions. The feeds are normalised to OpenSky's SI internally
// (metres, m/s); aviation/US convention shows altitude in feet and ground speed
// in knots, so convert at each on-screen/notification site that shows telemetry.
constexpr float METRES_TO_FEET = 3.28084f;
constexpr float MS_TO_KNOTS    = 1.94384f;

// There is deliberately NO numeric heap floor here any more. ENRICH_TLS_HEAP_FLOOR
// (16000) used to gate every enrichment HTTPS lookup by comparing it against
// ESP.getMaxAllocHeap(); issue #163 and src/probe/HeapProbe.cpp proved on hardware
// that the comparison could not work -- getMaxAllocHeap() is a max ACROSS REGIONS
// and latches onto reserves nothing allocates from, so over a 54 h soak it took
// five distinct values in 6,466 samples and the gate never fired once, including
// at the two moments an allocation genuinely failed.
//
// The replacement is not a better constant, because no constant answers the
// question: ask the allocator. heaphealth::CanHandshake() trial-allocates the real
// size and believes the result. If you came here looking for the heap gate, that
// is it -- see src/HeapHealth.h.

#include <esp_heap_caps.h>
#include <esp_timer.h> // Follow track draw-cost timing (§18.1)

// Follow track draw budget (§4.5). A build flag rather than a bare constant
// because the FIRST thing anyone will want after seeing one number is the shape
// of the curve -- 64 / 128 / 256 / 512 -- and re-editing a header between runs is
// how a measurement session turns into a bisect. -DFOLLOW_DRAW_SEGMENT_CAP=N.
#ifndef FOLLOW_DRAW_SEGMENT_CAP
#define FOLLOW_DRAW_SEGMENT_CAP 256
#endif

// The local face's usable disc (§10). Derived from SCREEN_SIZE, never a literal
// -- this draws on a 240 round, a 412 round and a 480 square, and the inset is
// what keeps the outer ring's label off the bezel on all three.
static constexpr int FOLLOW_FACE_RADIUS_PX = SCREEN_SIZE_DIV_2 - SCREEN_SIZE / 10;

// The follow palette, in one place, because the two faces MUST agree about
// which colour means which kind of absence. §6's whole argument is that an
// expected absence must not look like a fault; that argument is worth nothing
// if the local face says amber and the arc face says red for the same state.
static const uint32_t FOLLOW_GREEN   = lgfx::color888(0, 220, 0);
static const uint32_t FOLLOW_AMBER   = lgfx::color888(255, 176, 0);
static const uint32_t FOLLOW_RED     = lgfx::color888(255, 60, 60);
static const uint32_t FOLLOW_DIM     = lgfx::color888(110, 110, 110);
static const uint32_t FOLLOW_TRACK   = lgfx::color888(0, 96, 160);
static const uint32_t FOLLOW_HOME    = lgfx::color888(200, 200, 200);
// The unflown arc and the dead bezel: present, never competing with data.
static const uint32_t FOLLOW_NEUTRAL = lgfx::color888(52, 52, 52);

// Scale a colour toward black. §8 asks for the bearing wedge at "~40% opacity"
// and there is no alpha in this draw path -- the backbuffer is opaque and the
// wedge sits on the bezel, so scaling the colour is the same picture.
static uint32_t FollowFade(uint32_t c, float f)
{
    return lgfx::color888((uint8_t)(((c >> 16) & 0xFF) * f),
                          (uint8_t)(((c >> 8)  & 0xFF) * f),
                          (uint8_t)(( c        & 0xFF) * f));
}

// State -> colour. ONE mapping, and the single parameter is the one place the
// two faces are allowed to differ.
//
// §8 IS EMPHATIC ABOUT APPROACH_LOST: "Not the warning colour. Use the active
// accent... Getting this one wrong alarms someone watching a family member
// land. It is the single most important state in the feature." So on the arc
// face it is green.
//
// §10 says nothing about the local face's colour for it, and the local face has
// already been eyeballed on the bench with amber. Changing it here would be an
// unreviewed change to a composition that was signed off, so it keeps amber
// until the absence-copy session judges the two side by side. That session is
// where this belongs: it is a question about how a colour READS, and no
// argument in a comment settles it.
static uint32_t FollowStateColour(follow::State st, bool benignApproach)
{
    if (st == follow::State::ApproachLost)
        return benignApproach ? FOLLOW_GREEN : FOLLOW_AMBER;
    if (st == follow::State::SignalLost)             return FOLLOW_RED;
    if (follow::Machine::IsExpectedAbsence(st))      return FOLLOW_AMBER;
    if (st == follow::State::Ground ||
        st == follow::State::Landed)                 return FOLLOW_HOME;
    return FOLLOW_GREEN;
}

#include "HeapHealth.h"
#include "StarvationPolicy.h"

namespace {
    // Outcome-based soak criteria (2026-07-10): count what actually fails instead of
    // policing a fixed heap floor (7.7 KB largest is the measured operating level at
    // 25 aircraft, and feed TLS re-handshakes demonstrably succeed there).
    uint32_t allocFailures = 0;
    uint32_t fetchHardFailures = 0; // statusCode <= 0: TLS/DNS/connect/timeout class
    void OnAllocFailed(size_t size, uint32_t caps, const char* fn)
    {
        // A heap gate asking "would this fit?" is not a failure, and must not be
        // logged as one -- otherwise the gate WORKING is indistinguishable from
        // the failure it just prevented, and the soak's outcome counters fill up
        // with its own successes. Task-scoped, so a genuine failure on another
        // task inside the same few microseconds still counts (see HeapHealth.h).
        if (heaphealth::InTrial())
            return;
        allocFailures++;
        Serial.printf("[health] ALLOC FAILED: %u B (caps 0x%x) in %s (#%lu)\n",
                      (unsigned)size, (unsigned)caps, fn ? fn : "?", (unsigned long)allocFailures);
    }
}

// aircraft list-view layout, shared by the renderer and the row hit-test. Everything is
// derived from SCREEN_SIZE so the list stays centred on any SKU: the columns are laid out
// inside a fixed-width block that DrawList centres horizontally, and the row count fills the
// panel down to the clock. (The old absolute 40/120/162 columns + 9 rows were 240-only and
// clustered the whole list into the top-left quadrant of the 412 panel.)
constexpr int LIST_ROW_TOP = 40;
constexpr int LIST_ROW_H = 18;
constexpr int LIST_BOTTOM_RESERVE = 38; // leaves the clock row (SCREEN_SIZE-30) clear; yields 9 rows on the 240 C3
constexpr int LIST_ROWS = (SCREEN_SIZE - LIST_ROW_TOP - LIST_BOTTOM_RESERVE) / LIST_ROW_H;
// Row columns, relative to the centred block's left edge (block-relative, not absolute):
constexpr int LIST_BLOCK_W  = 160; // nominal block width used to centre the columns (== the 240 C3's 40..200 span)
constexpr int LIST_COL_CS   = 0;   // callsign (<= 8 chars = 48 px at size 1)
constexpr int LIST_COL_TYPE = 52;  // type (4 chars)
constexpr int LIST_COL_DIST = 80;  // distance from centre (<= 5 chars, e.g. "123km")
constexpr int LIST_COL_ALT  = 118; // altitude

#include <ArduinoJson.h>

#include "Airports.h"     // baked major-airport table for the radar overlay
#include "GlobeProjection.h"  // the orthographic globe, shared with src/anim (§7.2)
#include "SevenSegment.h" // shared 7-seg renderer, for the night clock face

// Cap on aircraft retained per fetch, applied to BOTH sources: keep only the nearest this-many to
// the configured location so a busy feed (a local dump1090/readsb receiver, or a wide OpenSky box)
// can't flood RAM, the tracked map, or the per-frame render.
static constexpr size_t MAX_AIRCRAFT = 60;

// Handoff payloads for the background OpenSky fetch task. Both are passed between
// tasks by pointer through a queue, transferring ownership: the receiver frees it.
// The same task also serves the cloud feed and its /api/v1/blipscope/config fetch (kind), so all
// blocking cloud I/O shares the one TLS client + keep-alive connection.
enum class FetchKind : uint8_t { Feed, CloudConfig, Airports };

struct FetchRequest {
    FetchKind kind = FetchKind::Feed;
    double lat, lon, radLat, radLon; // scan box, snapshotted on the loop task
    String token;                    // bearer token (empty = anonymous request)
    bool   local = false;            // true: poll the local receiver instead of OpenSky
    String url;                      // local aircraft.json URL (only when local)
#ifdef FEATURE_CLOUD_FEED
    bool   cloud = false;            // true: poll the Blipscope Cloud proxy
    String cloudBase;                // normalised proxy base URL (cloud + CloudConfig)
    String cloudKey;                 // X-Blip-Key value (cloud + CloudConfig)
    double rangeKm = 100.0;          // /api/v1/blipscope/blips r param
    String otaMem;                   // X-Blip-OTA-Mem value, "" when nothing to report.
                                     // Read (and cleared) on the LOOP task at request
                                     // build time -- TakeOtaMemReport touches NVS, and
                                     // this task owns that, like every other snapshot here.
    String usage;                    // X-Blip-Usage value, "" unless a report is due.
                                     // Same rule and the same reason as otaMem: taken on
                                     // the LOOP task at request build time, because the
                                     // take writes NVS and commits the delta.
#endif
};
struct FetchResult {
    FetchKind kind = FetchKind::Feed;
    bool ok = false;                 // false on network / parse failure
    bool authFailed = false;         // OpenSky said 401/403: the token needs a refetch
    std::vector<Aircraft> aircraft;  // parsed state vectors (empty unless ok)
#ifdef FEATURE_CLOUD_FEED
    bool cloudAuthRejected = false;  // the PROXY said 401/403: this board's key is refused
    long dataEpoch = 0;              // cloud blips snapshot time t (0 = not cloud)
    CloudFeed::Config config;        // CloudConfig kind only
    std::vector<CloudFeed::CloudAirport> airports; // Airports kind only
#endif
    // Measurement carried back to the loop task, which owns the counters (this
    // struct crosses a queue; the fetch task must not touch loop state).
    // receivedCount is taken BEFORE the MAX_AIRCRAFT cut -- the gap between it and
    // aircraft.size() is exactly the work spent parsing contacts that get thrown
    // away, which is the case for Worker-side truncation if it is large.
    size_t receivedCount = 0;
    size_t bodyBytes = 0;
    unsigned long parseMs = 0;
    unsigned long requestMs = 0;
    String cacheState;   // X-Cache from the proxy
    String upstream;     // X-Upstream from the proxy
};

// Handoff payloads for the background enrichment task (adsbdb metadata/route +
// aircraft photo, the cloud /api/v1/blipscope/enrich join, and the ntfy alert POSTs -- anything
// blocking that must never run on the loop). Like the fetch payloads above,
// ownership transfers by pointer through a depth-1 queue: the receiver frees it.
// The task fills a result the loop applies; it never touches trackedAircraft,
// the photo sprite, or the logbook.
// Metadata and Route were the two adsbdb lookups; both are gone. CloudEnrich
// replaces them with one pre-joined request to our own proxy.
enum class EnrichKind : uint8_t { Photo, CloudEnrich, Ntfy, Leaderboard };

struct EnrichRequest {
    EnrichKind kind;
    String icao24;    // aircraft the result applies to (Metadata/Route/Photo/CloudEnrich)
    String callsign;  // Route + CloudEnrich (the route half of the join)
    String url;       // Photo: image URL; Ntfy: the topic URL
    // CloudEnrich: proxy endpoint + auth, and the aircraft's live position for
    // the route plausibility check (callsigns get reused across legs).
    String cloudBase, cloudKey;
    float  acLat = 0.0f, acLon = 0.0f;
    bool   hasPos = false;
    // Ntfy: the alert content, POSTed off-loop with the usual ntfy headers.
    String ntfyTitle, ntfyTags, ntfyBody;
    // Leaderboard: the JSON submission body, POSTed to cloudBase + /api/v1/blipscope/leaderboard.
    String lbBody;
};

struct EnrichResult {
    EnrichKind kind = EnrichKind::Photo;
    String icao24;
    bool definitive = false; // a final HTTP answer arrived; false = transient failure (retry)
    // How long the loop should hold off a retry after a non-definitive result.
    // adsbdb outages back off 30 s; a cloud proxy still warming its caches
    // answers in seconds, so its retries come quicker.
    unsigned long retryCooldownMs = 30000;
    // MEASUREMENT: wall time this request held the shared HTTP client. Summed on
    // the loop task into the [perf] window's enrich share.
    unsigned long busyMs = 0;

    // Metadata / CloudEnrich
    String typeCode, typeName, operatorName, registration, photoUrl;
    // Route (CloudEnrich carries the route in the same fields)
    String routeCallsign, routeOrigin, routeDest;
    // CloudEnrich stock-photo join: the proxy's relative /api/v1/blipscope/photo path (made
    // absolute in ApplyEnrichment) and whether it's a generic type shot.
    String photoPath;
    bool photoRepresentative = false;
    // Photo: the raw JPEG body, decoded on the loop so the sprite stays single-task
    bool photoFetched = false;
    String photoBytes;

    // Leaderboard: this device's standing, parsed from the submit response, for
    // the Stats-screen rank block. lbOk gates whether the loop adopts them.
    bool lbOk = false;
    int lbRank = 0, lbSeasonRank = 0, lbTotal = 0;
    long lbPoints = 0, lbSeasonPoints = 0;
    String lbResolvedName;
    String lbRarestType;
    int lbRarestPct = 0;
};

namespace {

// The international emergency squawk codes. These always trigger the alert
// styling regardless of display settings.
bool isEmergencySquawk(const String& squawk)
{
    return squawk == "7500" || squawk == "7600" || squawk == "7700";
}

// Marker color by barometric altitude (meters), low to high. Deliberately
// avoids red, which is reserved for the emergency alert.
uint32_t altitudeColor(float altMeters)
{
    if (altMeters < 1000.0f) return lgfx::color888(0, 255, 0);     // green   - low
    if (altMeters < 3000.0f) return lgfx::color888(170, 255, 0);   // lime
    if (altMeters < 6000.0f) return lgfx::color888(255, 255, 0);   // yellow
    if (altMeters < 9000.0f) return lgfx::color888(0, 255, 255);   // cyan
    return lgfx::color888(255, 255, 255);                          // white   - high
}

// Scale a packed RGB888 colour's brightness by f (0..1). Used to fade a blip as
// its radar return ages between sweep passes.
uint32_t scaleColor(uint32_t rgb, float f)
{
    if (f >= 1.0f) return rgb;
    if (f < 0.0f) f = 0.0f;
    const uint8_t r = (rgb >> 16) & 0xFF, g = (rgb >> 8) & 0xFF, b = rgb & 0xFF;
    return lgfx::color888((uint8_t)(r * f), (uint8_t)(g * f), (uint8_t)(b * f));
}

// True when the sun is below the horizon at (lat, lon) for the given UTC time.
// Uses the NOAA solar-position equations and evaluates the sun's elevation
// directly, which sidesteps the UTC day-wrap pitfalls of comparing against
// sunrise/sunset clock times.
bool isNightNow(double latDeg, double lonDeg, time_t nowUtc)
{
    struct tm t;
    gmtime_r(&nowUtc, &t);

    const double gamma = 2.0 * PI / 365.0 * (t.tm_yday + (t.tm_hour - 12) / 24.0);
    const double eqTime = 229.18 * (0.000075 + 0.001868 * cos(gamma) - 0.032077 * sin(gamma)
                          - 0.014615 * cos(2 * gamma) - 0.040849 * sin(2 * gamma));
    const double decl = 0.006918 - 0.399912 * cos(gamma) + 0.070257 * sin(gamma)
                        - 0.006758 * cos(2 * gamma) + 0.000907 * sin(2 * gamma)
                        - 0.002697 * cos(3 * gamma) + 0.00148 * sin(3 * gamma);

    const double nowMin = t.tm_hour * 60.0 + t.tm_min + t.tm_sec / 60.0;
    const double trueSolarMin = nowMin + eqTime + 4.0 * lonDeg; // longitude east-positive
    const double hourAngle = radians(trueSolarMin / 4.0 - 180.0);
    const double latRad = radians(latDeg);
    const double elevation = asin(sin(latRad) * sin(decl) + cos(latRad) * cos(decl) * cos(hourAngle));

    return degrees(elevation) < -0.833; // standard sunrise/sunset refraction angle
}

// Turn whatever the user typed for their receiver into a usable aircraft.json URL.
// Accepts a bare host/IP ("192.168.1.50"), a base URL, or a full .json URL: we add
// a default scheme and the conventional dump1090/readsb path only when they're
// missing, so someone who pasted an exact URL is left untouched.
String normalizeLocalUrl(String url)
{
    url.trim();
    if (url.isEmpty())
        return url;

    if (url.indexOf("://") < 0)
        url = "http://" + url;

    String lower = url;
    lower.toLowerCase();
    if (lower.indexOf(".json") < 0) {
        while (url.endsWith("/"))
            url.remove(url.length() - 1);
        url += "/data/aircraft.json";
    }
    return url;
}

// --- Enrichment producers ---------------------------------------------------
// Each runs on the enrichment task: one blocking GET + parse, returning a
// heap-allocated result for the loop to apply. They reference nothing the loop
// owns -- only the shared, mutex-guarded HTTP client -- so an aircraft leaving
// range (erased on the loop) can never be written under them. Always non-null,
// so the loop's in-flight gate is always cleared by the matching result.

// fetchAircraftMetadata() and fetchRoute() USED TO LIVE HERE.
//
// They called api.adsbdb.com directly from the device for aircraft metadata and
// routes. Both are deleted: adsbdb is out of the stack entirely, on both halves
// (the Worker's fallback went first; this is the firmware half).
//
// WHY THE DEVICE STOPPED TALKING TO THEM AT ALL. We have no written permission to
// use adsbdb commercially. These call sites never appeared in the measured
// request volume because they originate from CUSTOMER HOME IPs, so the exposure
// was understated by exactly the traffic we could not see.
//
// WHAT REPLACED THEM: fetchCloudEnrich() below, one GET to our own Worker that
// pre-joins metadata + route + photo pointer. BYO is about POSITIONS -- a local
// receiver, one-second updates, a radar that survives an internet outage -- and
// it was never about enrichment. So a local-receiver or OpenSky device keeps its
// own position source and gets its cards from us. During an internet outage the
// cards degrade while the radar keeps working, which is already exactly how
// photos behave.
//
// See docs/follow-mode-consolidated.md §10 for the licensing reasoning.

EnrichResult* fetchPhoto(HttpRequestManager& http, const String& url, const String& authKey)
{
    EnrichResult* res = new EnrichResult();

    // The BYO adsbdb thumbnail host is public; the cloud /api/v1/blipscope/photo route needs
    // the device key. authKey is "" for BYO, the proxy key in cloud mode.
    //
    // USE THE FULL HEADER SET, not just X-Blip-Key. This hand-rolled the one
    // header and was correct for exactly as long as every device shared one key.
    // With per-device keys it broke: the proxy only tries its per-device auth path
    // when a request carries X-Blip-Device, so a photo request without it fell
    // back to the shared BLIP_KEYS list, did not find the minted key, and got a
    // 401 -- whose 30-byte JSON body then reached the JPEG decoder:
    //     [photo] a8dc3a: decode FAILED (bytes=30 head=7b22)   ("{\"" = JSON)
    // Feed and enrichment were unaffected (they already use CloudFeed::Headers),
    // so a provisioned board looked entirely healthy and simply never showed a
    // single photo.
    // The full header set exists only on cloud builds -- this function is also
    // compiled into the plain radar envs for the BYO adsbdb thumbnail, where
    // CloudFeed is not included at all. authKey is always "" on those, so the
    // branch is dead there; it still has to COMPILE.
    std::vector<std::pair<String, String>> headers;
    if (!authKey.isEmpty()) {
#ifdef FEATURE_CLOUD_FEED
        headers = CloudFeed::Headers(authKey);
#else
        headers.push_back({ "X-Blip-Key", authKey });
#endif
    }

    HttpResult result = http.Get(url, {}, headers);
    if (!result.success) {
        Serial.printf("[photo] fetch failed: %s\n", result.errorMessage.c_str());
        return res; // photoFetched stays false
    }
    // A JSON body here is an error envelope, not an image. Say so plainly rather
    // than letting it reach drawJpg and surface as a decode failure.
    if (result.response.length() > 0 && result.response[0] == '{') {
        Serial.printf("[photo] rejected: proxy returned JSON, not an image: %s\n",
                      result.response.substring(0, 96).c_str());
        return res; // photoFetched stays false
    }

    res->photoFetched = true;
    res->photoBytes = std::move(result.response); // decoded on the loop into the shared sprite
    return res;
}

#ifdef FEATURE_CLOUD_FEED
// One GET to the proxy replaces the old two adsbdb lookups: /api/v1/blipscope/enrich pre-joins
// registration/type/operator AND the route. Streams the ~200 B body straight
// into the document like the feed fetch does.
EnrichResult* fetchCloudEnrich(HttpRequestManager& http, const EnrichRequest& req)
{
    EnrichResult* res = new EnrichResult();

    std::vector<std::pair<String, String>> params;
    if (!req.callsign.isEmpty()) params.push_back({ "cs", req.callsign });
    if (req.hasPos) {
        params.push_back({ "lat", String(req.acLat, 4) });
        params.push_back({ "lon", String(req.acLon, 4) });
    }

    JsonDocument doc;
    // Headers(key) deliberately, NOT Headers(key, otaMem): the OTA memory report
    // rides the cloud feed/config fetches only. A local-receiver device on
    // details=cloud makes neither of those, and heap telemetry has no business
    // riding a detail tap -- do not add otaMem here.
    const HttpResult result = http.GetJson(
        CloudFeed::EnrichUrl(req.cloudBase, req.icao24), doc, params,
        CloudFeed::Headers(req.cloudKey));

    if (!result.success || result.statusCode < 200 || result.statusCode >= 300) {
        // Network failure, 429, or the proxy's fast 503s: transient, retry soon
        // (the proxy warms its caches in the background after a cold miss).
        Serial.printf("[cloud] enrich %s: HTTP %d %s\n", req.icao24.c_str(),
                      result.statusCode, result.errorMessage.c_str());
        res->retryCooldownMs = 5000;
        return res;
    }

    CloudFeed::Enrichment e;
    if (!CloudFeed::ParseEnrich(doc, e)) {
        Serial.printf("[cloud] enrich %s: schema mismatch\n", req.icao24.c_str());
        res->retryCooldownMs = 60000; // wrong schema won't fix itself quickly
        return res;
    }

    // An all-empty 200 is ambiguous: unknown aircraft, or the proxy still
    // warming. Non-definitive with a short cooldown lets the loop retry a
    // bounded number of times (TrackedAircraft::enrichAttempts) before
    // accepting "unknown".
    res->definitive = e.AnyField();
    res->retryCooldownMs = 4000;
    res->registration = e.registration;
    res->typeCode     = e.typeCode;
    res->typeName     = e.typeName;
    res->operatorName = e.operatorName;
    res->routeOrigin  = e.routeOrigin;
    res->routeDest    = e.routeDest;
    res->photoPath          = e.photoPath;
    res->photoRepresentative = e.photoRepresentative;
    res->routeCallsign = req.callsign;
    return res;
}
#endif // FEATURE_CLOUD_FEED

// ntfy alert POST, moved off the loop task: a slow ntfy.sh (or a TLS handshake
// after an idle period) used to stall the loop for seconds, eating taps. The
// result carries nothing to apply -- success/failure is logged here and the
// caller's un-marked notified flag retries a failed alert on a later tick.
EnrichResult* postNtfy(HttpRequestManager& http, const EnrichRequest& req)
{
    EnrichResult* res = new EnrichResult();
    const HttpResult result = http.Post(
        req.url, req.ntfyBody,
        { { "Title", req.ntfyTitle }, { "Tags", req.ntfyTags } });
    res->definitive = result.success;
    Serial.printf("[ntfy] %s -> %s\n", req.ntfyTitle.c_str(),
                  result.success ? "sent" : result.errorMessage.c_str());
    return res;
}

#ifdef FEATURE_CLOUD_FEED
// Leaderboard submission POST, off the loop like ntfy: send the logbook tallies
// as JSON, parse back this device's rank/points for the Stats block. Failure is
// non-fatal (the block just keeps the last standing until the next hour).
EnrichResult* postLeaderboard(HttpRequestManager& http, const EnrichRequest& req)
{
    EnrichResult* res = new EnrichResult();
    res->kind = EnrichKind::Leaderboard;
    auto headers = CloudFeed::Headers(req.cloudKey);
    headers.push_back({ "Content-Type", "application/json" });
    const HttpResult result = http.Post(req.url, req.lbBody, headers);
    if (!result.success || result.statusCode < 200 || result.statusCode >= 300) {
        Serial.printf("[leaderboard] submit failed: HTTP %d %s\n",
                      result.statusCode, result.errorMessage.c_str());
        return res;
    }
    JsonDocument doc;
    if (deserializeJson(doc, result.response) == DeserializationError::Ok && doc["ok"].as<bool>()) {
        res->lbOk = true;
        res->lbRank = doc["rank"].as<int>();
        res->lbPoints = doc["points"].as<long>();
        res->lbSeasonRank = doc["seasonRank"].as<int>();
        res->lbSeasonPoints = doc["seasonPoints"].as<long>();
        res->lbTotal = doc["total"].as<int>();
        res->lbResolvedName = doc["name"].as<String>();
        res->lbRarestType = doc["rarestType"].as<String>();
        res->lbRarestPct = doc["rarestPct"].as<int>();
        Serial.printf("[leaderboard] rank #%d/%d, %ld pts\n", res->lbRank, res->lbTotal, res->lbPoints);
    }
    res->definitive = true;
    return res;
}
#endif

// Hand a request to the enrichment task. Frees it (and reports failure) if the
// depth-1 queue is somehow full -- which can't happen while the loop only ever
// queues one request at a time behind the enrichInFlight gate.
bool enqueueEnrich(QueueHandle_t queue, EnrichRequest* req)
{
    if (xQueueSend(queue, &req, 0) == pdTRUE)
        return true;
    delete req;
    return false;
}

} // namespace

void AircraftManager::Initialise()
{
    // get centre point + radius
    const String latStr = configServer.GetStoredString("latitude");
    const String lonStr = configServer.GetStoredString("longitude");
    lat = latStr.toDouble();
    lon = lonStr.toDouble();
    // Read from the STRINGS, before parsing collapses "unset" into 0.0 -- see the
    // note on hasLocation in the header. A factory-fresh or factory-reset device
    // lands here with both empty, and the radar has nothing to centre on.
    hasLocation = latStr.length() > 0 && lonStr.length() > 0;
    // Follow's home field is resolved once and cached (see ResolveHomeField).
    // Initialise re-runs on every config save, and a save is the only way the
    // location can move -- so this is exactly where the cache must be dropped.
    // Left stale, a customer who moved the device would keep the old field's
    // code under the marker the whole local face is built around.
    followHomeCodeResolved = false;
    // Said out loud, because "the screen is asking me to set a location" is a
    // support conversation and this is the line that answers it in one look.
    if (!hasLocation)
        Serial.println("[config] no location set -- the radar screen will prompt for it");

    // "radius" is stored as a real-world distance (km or mi). Convert it into
    // separate latitude/longitude degree spans: 1 deg latitude is ~111 km
    // everywhere, but 1 deg longitude is ~111 km * cos(latitude), so the box
    // must be wider in degrees near the equator and narrower near the poles to
    // stay square on the ground.
    // Default to 100 when unset/empty, matching the config form's default (it shows "100" only when
    // the key is absent -- a stored empty string would otherwise read as 0). Without this, a device
    // with a location but no saved radius scans a ~111 m box (the MIN_DEGREES floor) and shows no
    // aircraft until the radius is explicitly changed.
    const String radiusStr = configServer.GetStoredString("radius");
    const double distance = radiusStr.isEmpty() ? 100.0 : radiusStr.toDouble();
    // Normalise once, here, and pass the unit around -- never re-read and
    // re-branch at a render site. units::Normalise falls back to the default for
    // anything unrecognised, so a value written by a NEWER firmware cannot be
    // treated as a unit this build does not know.
    const String cfgUnit = units::Normalise(configServer.GetStoredString("radius-unit"));
    const double distanceKm = units::ToKm(distance, cfgUnit);

    constexpr double KM_PER_DEGREE = 111.0;
    constexpr double MAX_DEGREES = 2.0; // keep the OpenSky box within rate-limit area

    double cosLat = std::cos(radians(lat));
    if (cosLat < 0.01) cosLat = 0.01; // guard against div-by-zero near the poles

    constexpr double MIN_DEGREES = 0.001; // ~111 m floor; keeps the projection from dividing by zero

    radLat = std::min(std::max(distanceKm / KM_PER_DEGREE, MIN_DEGREES), MAX_DEGREES);
    radLon = std::min(std::max(distanceKm / (KM_PER_DEGREE * cosLat), MIN_DEGREES), MAX_DEGREES);

    // outer range-ring distance in the user's unit (derived from the clamped
    // radLat so the labels match what's actually drawn), for the ring labels
    rangeRadiusDisplay = radLat * KM_PER_DEGREE;
    rangeRadiusDisplay = units::FromKm(rangeRadiusDisplay, cfgUnit);
    rangeUnit = cfgUnit;

    // configuration
    const String renderText = configServer.GetStoredString("infotext");
    const String renderTris = configServer.GetStoredString("triangle");
    const String renderAirports = configServer.GetStoredString("airports");
    if (!renderAirports.isEmpty()) displayAirports = renderAirports == "true";
    // Minimum airport size to draw (cloud /api/v1/blipscope/airports overlay only; the baked
    // majors are all major-class so the filter is a no-op there). Default: all.
    const String airportsMinStr = configServer.GetStoredString("airports-min");
    airportsMin = airportsMinStr == "large" ? AirportsMin::LargeOnly
                : airportsMinStr == "med"   ? AirportsMin::MedLarge
                                            : AirportsMin::All;
    const String renderTrail = configServer.GetStoredString("trail");
    const String renderAltColor = configServer.GetStoredString("altcolor");
    const String renderHighlight = configServer.GetStoredString("highlight");
    if (!renderText.isEmpty()) displayInfoText = renderText == "true" ? true : false;
    if (!renderTris.isEmpty()) displayTriangles = renderTris == "true" ? true : false;
    if (!renderTrail.isEmpty()) displayTrails = renderTrail == "true" ? true : false;
    if (!renderAltColor.isEmpty()) displayAltColor = renderAltColor == "true" ? true : false;
    if (!renderHighlight.isEmpty()) displayHighlight = renderHighlight == "true" ? true : false;

    // sweep beam: unset (or "true") = on, matching the radar's drawScan gate in main.cpp
    const String renderSweep = configServer.GetStoredString("scanline");
    displaySweep = renderSweep.isEmpty() || renderSweep == "true";

    // paint-and-fade blips: unset (or "true") = on. Only takes effect with the sweep on.
    const String renderFade = configServer.GetStoredString("fade");
    displayFade = renderFade.isEmpty() || renderFade == "true";

    // which individual info lines to show. An unset key (device never saved, or
    // an older save predating this field) falls back to the field's default.
    infoFieldEnabled.resize(AIRCRAFT_INFO_FIELD_COUNT);
    metadataNeeded = false;
    for (size_t i = 0; i < AIRCRAFT_INFO_FIELD_COUNT; ++i) {
        const String stored = configServer.GetStoredString(AIRCRAFT_INFO_FIELDS[i].key);
        infoFieldEnabled[i] = stored.isEmpty() ? AIRCRAFT_INFO_FIELDS[i].defaultOn
                                               : (stored == "true");
        // only spend network calls on adsbdb when an enrichment field is shown
        if (infoFieldEnabled[i] && AIRCRAFT_INFO_FIELDS[i].needsLookup)
            metadataNeeded = true;
    }

    // watchlist: split on commas/newlines/semicolons into lowercased prefixes
    watchlist.clear();
    const String wl = configServer.GetStoredString("watchlist");
    int tokenStart = 0;
    for (int i = 0; i <= (int)wl.length(); ++i) {
        const char c = (i < (int)wl.length()) ? wl[i] : ',';
        if (c == ',' || c == ';' || c == '\n' || c == '\r') {
            String token = wl.substring(tokenStart, i);
            token.trim();
            token.toLowerCase();
            if (!token.isEmpty()) watchlist.push_back(token);
            tokenStart = i + 1;
        }
    }
    ntfyTopic = configServer.GetStoredString("ntfy-topic");
    ntfyTopic.trim();

    // a watchlist needs registration/type, so make sure metadata gets fetched
    if (!watchlist.empty()) metadataNeeded = true;

    // ---- Follow Mode stage 1 (docs/follow-mode-consolidated.md §4, §14) ------
    //
    // ONE field gates the feature. Empty means the track is never allocated, the
    // draw is never entered and nothing changes for anyone who did not ask --
    // which is §15's argument for the default, restated as code so a later reader
    // does not have to trust a table.
    //
    // Initialise() runs on EVERY config save, so this is written to be safe to
    // re-enter: Enable() is idempotent, and after a failure it latches degraded
    // and short-circuits. The retry that §4.3 forbids is a retry in a LOOP; a
    // person pressing Save is not a loop, and clearing the field re-arms it.
    {
        String want = configServer.GetStoredString("follow");
        want.trim();
        want.toLowerCase();
        const String followTrackStr = configServer.GetStoredString("follow-track");
        followDrawTrack = followTrackStr.isEmpty() ? true : (followTrackStr == "true");

        // 15: these defaults are frozen the first time anybody saves the form,
        // and setting a location IS a whole-form save -- so they are the defaults
        // for everyone who ever owns this. They match the config page exactly;
        // two copies of a default disagreeing is how a box renders ticked on a
        // device that behaves as though it is not.
        const String upStr   = configServer.GetStoredString("follow-up");
        const String downStr = configServer.GetStoredString("follow-down");
        const String lostStr = configServer.GetStoredString("follow-lost");
        followAlertUp   = upStr.isEmpty()   ? true  : (upStr   == "true");
        followAlertDown = downStr.isEmpty() ? true  : (downStr == "true");
        followAlertLost = lostStr.isEmpty() ? false : (lostStr == "true");

        if (want != followTarget) {
            // Identity changed -- including to or from empty.
            // OWNERSHIP: the buffer belongs to whatever is being followed, and
            // that is now TWO possible owners. This site knew only the
            // configured one, so clearing the config field freed 12 KB out from
            // under a live SESSION follow -- the face kept drawing while its
            // track buffer was returned. 4.3 says "freed only when follow is
            // disabled", and a session follow running is not follow disabled.
            //
            // Deliberately NOT symmetric: this only ever frees. Whether a SWIPE
            // should ALLOCATE 12 KB is a design question, not an oversight --
            // see the issue -- so nothing here enables on a session target.
            if (want.isEmpty() && followSessionTarget.isEmpty()) {
                followTrack.Disable();   // the ONLY free site (§4.3)
            } else if (!followTarget.isEmpty()) {
                // Still following, but somebody else: keep the allocation, drop
                // the path. This is exactly the ResetFlight/Disable split.
                followTrack.ResetFlight();
                // AND DROP THE CARD. A previous aeroplane's flight is not this
                // aeroplane's history, and a souvenir attributed to the wrong
                // aircraft is the one way the post-flight card can be wrong --
                // which is the entire reason it is allowed to persist at all.
                followLog.Clear();
            }
            followStats.Reset();
            followLastState = follow::State::Idle;
            followTarget = want;
            if (!followTarget.isEmpty() && followDrawTrack) {
                if (!followTrack.Enable()) {
                    // Degraded: notification-only. Stage 1 has no notifications
                    // yet, so today this simply means no track -- but the state is
                    // real and the HUD says so rather than looking like a bug.
                    Serial.println("[follow] notification-only: no track buffer");
                }
            }
        } else if (!followTarget.isEmpty() && followDrawTrack && !followTrack.Active()) {
            // The toggle came back on for the same aircraft.
            followTrack.Enable();
        } else if (!followDrawTrack && followTrack.Active()) {
            followTrack.Disable();
        }
        // Read once per boot. Initialise() re-runs on every config save, so this
        // is guarded rather than unconditional -- re-reading NVS would be
        // harmless but would also overwrite an in-RAM record written since, and
        // "harmless today" is how a re-read becomes a data-loss bug later.
        if (!followLogLoaded) {
            followLogLoaded = true;
            followLog.Load();
        }
#ifdef FOLLOW_BENCH
        // SELF-ENABLE. The config page has no "follow" field yet -- §19 orders the
        // config surface AFTER this measurement, so it is deliberately not built.
        // Without this the bench image is unusable: there is no way to set the key
        // the feature gates on, so the track never allocates and the build cannot
        // produce the one number it exists to produce.
        //
        // Found by flashing it and reading the board back, not by reading the code.
        // The banner said env=follow-bench-s3-128 and the board came up in its
        // Wi-Fi setup AP with no stored config at all -- which is exactly the state
        // a fresh bench board is in, and exactly the state this build could not
        // work in.
        //
        // A real follow target overrides it, so setting the key changes nothing
        // here.
        //
        // ARMED ONCE PER BOOT, and that is a bench finding rather than a tidy-up.
        // The self-enable used to fire on EVERY Initialise, and Initialise re-runs
        // on every config save -- so clearing the follow field and saving fell
        // straight back to the synthetic target. Correct-looking, and it made the
        // §4.3 DISABLE path (allocation freed, screen hidden entirely) impossible
        // to exercise on the only image that can reach this code.
        //
        // Now: one auto-enable at boot so the face has something to draw, and
        // after that an empty field means the owner CLEARED it and is honoured.
        // A reboot re-arms, which is what a bench image should do.
        if (followBenchArmed) {
            followBenchArmed = false;
            if (followTarget.isEmpty()) {
                followTarget = "bench";
                if (followDrawTrack && !followTrack.Active())
                    followTrack.Enable();
                Serial.println("[follow] FOLLOW_BENCH: self-enabled once for this boot; "
                               "clearing the field now sticks (tests the 4.3 disable path)");
            }
        }
        // ---- bench only: make the §18.1 measurement obtainable ---------------
        //
        // The question this build exists to answer is the draw cost of a FULL
        // 1024-point track. Waiting for one is not a plan: 1024 points at 150 m
        // separation is ~150 km of flown path, i.e. a couple of hours of a real
        // aeroplane near a real bench, and nobody runs that before deciding
        // whether to build the feature. What would happen instead is that the
        // number gets taken off a half-empty buffer and is quietly optimistic --
        // which is the worst outcome, because it decides the product.
        //
        // So the bench env fills the worst case directly, at boot, with no
        // aircraft required. Empty follow field still means no allocation and no
        // fill: this exercises the feature, it does not switch it on.
        // distanceKm, not rangeKmCfg: rangeKmCfg is assigned further down and
        // only inside #ifdef FEATURE_CLOUD_FEED, so reading it here would give a
        // stale 100 km on the first boot and nothing at all on a non-cloud build.
        // distanceKm is the configured radius, already normalised to km above.
        if (!followTarget.isEmpty() && followTrack.Active())
            followTrack.FillSynthetic((float)lat, (float)lon, (float)distanceKm);
#endif
        // THE TARGET IS NOT PRINTED. §17 lists serial output among the places
        // the follow value must never appear, with the Wi-Fi password incident
        // as the precedent -- a tail number tied to a named person is the same
        // class of data, and a serial log is copied into bug reports, pasted
        // into chats and committed to bench-logs/ without anyone rereading it.
        //
        // It WAS printed here, from stage 1 until 2026-08-27, and it was found
        // by scripts/check_follow_privacy.py on the first run rather than by
        // anyone rereading this line. The length is safe to state and is the
        // only part that helps diagnose ("did the field save?").
        //
        // The length is lifted into a local rather than called inside the
        // printf, and that is not style. scripts/check_follow_privacy.py's one
        // absolute rule is that the token never appears in a statement that
        // reaches a sink -- no exceptions, no allow list -- because a rule with
        // "unless it is only the length" in it needs a reader to judge
        // `.length()` against `.substring()` against `.charAt()`, and a rule
        // that needs judgement is one that gets judged wrong.
        const unsigned targetLen = (unsigned)followTarget.length();
        if (targetLen)
            Serial.printf("[follow] target set (%u chars) track=%d active=%d degraded=%d\n",
                          targetLen, (int)followDrawTrack,
                          (int)followTrack.Active(), (int)followTrack.Degraded());
    }

    // military / special-aircraft detection (offline, ICAO-address based; needs no
    // enrichment). Highlighting defaults on; the ntfy alert defaults off.
    const String milShow = configServer.GetStoredString("mil-show");
    showMilitary = milShow.isEmpty() ? true : (milShow == "true");
    const String milAlert = configServer.GetStoredString("mil-alert");
    alertMilitary = milAlert.isEmpty() ? false : (milAlert == "true");
    alertEmergency = configServer.GetStoredString("emg-alert") == "true";
    const String heliShow = configServer.GetStoredString("heli-show");
    showHelicopters = heliShow.isEmpty() ? false : (heliShow == "true");
    const String spcShow = configServer.GetStoredString("spc-show");
    showSpecial = spcShow.isEmpty() ? false : (spcShow == "true");

    // visual alerts (screen-level attention; the primary alert channel on SKUs
    // without a speaker): "off" | "ring" (edge pulse while in range) | "flash"
    // (full-screen burst on first appearance, then the ring). Emergency squawks
    // default to the ring -- they're rare and always worth a glance; military
    // defaults off (near a base, MIL contacts are routine).
    const auto visualMode = [](const String& s, VisualAlertMode fallback) {
        if (s == "off")   return VisualAlertMode::Off;
        if (s == "ring")  return VisualAlertMode::Ring;
        if (s == "flash") return VisualAlertMode::Flash;
        return fallback;
    };
    milVisual = visualMode(configServer.GetStoredString("mil-visual"), VisualAlertMode::Off);
    emgVisual = visualMode(configServer.GetStoredString("emg-visual"), VisualAlertMode::Ring);
    visualNightOverride = configServer.GetStoredString("visual-night") == "true";
    flashBurstUntilMs = 0; // a config reload cancels any in-progress burst

    // alert tones (HAS_AUDIO boards; inert elsewhere). Default on -- it subsumes
    // the original unconditional new-contact chirp.
    const String tonesStr = configServer.GetStoredString("tones");
    tonesEnabled = tonesStr.isEmpty() ? true : (tonesStr == "true");

    // spotting logbook: when on, learn each contact's type/airline (so it needs
    // the adsbdb enrichment) and start the persistent store once.
    //
    // The PREVIOUS state is captured because turning the logbook on mid-session
    // has to backfill from what is already on screen -- see the seed call below.
    const bool logbookWasEnabled = logbookEnabled;
    const String logbookStr = configServer.GetStoredString("logbook");
    // The default lives in ConfigMigration.h, NOT as a literal here -- the config
    // page resolves the same toggle and the two must not be able to disagree
    // about what "unset" means.
    logbookEnabled = configmigration::ResolveToggle(logbookStr.c_str(),
                                                    configmigration::LOGBOOK_DEFAULT_ON);
    if (logbookEnabled) {
        metadataNeeded = true;
        logbook.Begin(); // idempotent: only the first call loads from NVS
    }
    // WHAT THE TOGGLE MEANS, decided and written down because today it silently
    // meant the other thing.
    //
    // It means STOP COLLECTING. It does not, and must not, mean "stop saving
    // what you already collected" -- but that is exactly what it did: the only
    // writer is gated on this same flag, so unchecking the box stranded every
    // change made since the last flush, permanently. With the 10-minute debounce
    // that could be a whole session's spotting, and the customer's own Collection
    // page would then show a book that had gone backwards.
    //
    // So the disable EDGE flushes before logging stops. The customer keeps what
    // they collected; only new collecting ceases, which is what the checkbox
    // says on the page.
    if (logbookWasEnabled && !logbookEnabled) {
        Serial.println("[logbook] disabled -- flushing before logging stops");
        logbook.PersistNow();
    }

#ifdef FEATURE_CLOUD_FEED
    // Public spotting leaderboard (opt-in, off by default). Submits the logbook
    // tallies hourly through the proxy; needs both the logbook (the numbers) and
    // the cloud feed (the transport), so it's inert without them.
    lbEnabled = configServer.GetStoredString("lb-enabled") == "true";
    lbName = configServer.GetStoredString("lb-name");
    lbName.trim();
    if (lbEnabled) {
        logbookEnabled = true; // the leaderboard's numbers ARE the logbook
        metadataNeeded = true;
        logbook.Begin();
        lastLeaderboardSubmit = 0; // submit promptly after (re)initialise
    }
#endif

    // BACKFILL WHEN THE LOGBOOK IS SWITCHED ON MID-SESSION. Checked here, after
    // the leaderboard block, because opting into the leaderboard forces the
    // logbook on too and that edge has to seed as well.
    //
    // Without this the toggle silently did nothing until unfamiliar traffic
    // wandered into range. The logbook is only ever written where a contact is
    // FIRST emplaced, so every aircraft already on screen had been emplaced
    // before the toggle and never revisited that branch -- contacts stayed 0, no
    // type was ever noted, and because ClaimType() rejects a type that was never
    // seen, tapping those aircraft produced no claim AND no toast. The owner taps,
    // nothing happens, and nothing says why: exactly the "did that do anything?"
    // failure the claim confirmation exists to prevent. It hits everyone who
    // enables the logbook while their scope is already populated, which is the
    // only way anyone would ever do it.
    if (logbookEnabled && !logbookWasEnabled)
        SeedLogbookFromTracked();

    // "look up!" overhead alert. The distance is entered in the same unit as the
    // radar radius; store it in km for the centre-distance comparison.
    showOverhead = configServer.GetStoredString("lookup") == "true";
    alertOverhead = configServer.GetStoredString("lookup-alert") == "true";
    double lookupDist = configServer.GetStoredString("lookup-dist").toDouble();
    if (lookupDist <= 0.0) lookupDist = 3.0;
    overheadKm = units::ToKm(lookupDist, cfgUnit);

    // Home Assistant / MQTT publishing (off by default).
    mqttEnabled = configServer.GetStoredString("mqtt") == "true";
    mqttDiscovery = configServer.GetStoredString("mqtt-disco") != "false"; // default on
    mqttBase = configServer.GetStoredString("mqtt-base");
    mqttBase.trim();
    if (mqttBase.isEmpty()) mqttBase = "blipscope";

    MqttPublisher::Config mc;
    mc.enabled = mqttEnabled;
    mc.host = configServer.GetStoredString("mqtt-host");  mc.host.trim();
    const String mqttPort = configServer.GetStoredString("mqtt-port");
    mc.port = mqttPort.isEmpty() ? 1883 : (uint16_t)mqttPort.toInt();
    mc.user = configServer.GetStoredString("mqtt-user");
    mc.pass = configServer.GetStoredString("mqtt-pass");
    mc.statusTopic = mqttBase + "/status";
    mqtt.Begin(mc); // spawns the publisher task once; reconfigures it thereafter
    lastMqttState = millis() - 5000; // publish a first snapshot promptly once connected

    // data source: Blipscope Cloud (the proxy; default on cloud builds), the
    // OpenSky cloud API (BYO credentials -- the user's own ToS relationship), or
    // the user's own ADS-B receiver. URLs are normalised once here so the fetch
    // task gets ready-to-GET endpoints.
    const String dataSource = configServer.GetStoredString("data-source");
    useLocalSource = dataSource == "local";
    localUrl = useLocalSource ? normalizeLocalUrl(configServer.GetStoredString("local-url")) : "";

    // Detail-card source for a local receiver. There is NO default: only an exact,
    // explicitly-saved value selects a source, and anything else (unset, or a value
    // this firmware does not recognise) falls to Off. That is the one fallback that
    // can never surprise a user -- a device does not begin contacting anything
    // because of an upgrade, a downgrade, or a config it could not parse. The config
    // page enforces the choice up front; this is the belt-and-braces half.
    {
        const String det = configServer.GetStoredString("local-details");
        // "adsbdb" is migrated to "cloud" at boot (ConfigMigration rev 4), so by the
        // time this runs the stored value is never "adsbdb". The mapping is NOT
        // repeated here: a stored value must resolve in exactly one place, and a
        // second silent translation would hide a migration that failed to run.
        localDetails = det == "cloud" ? LocalDetails::Cloud : LocalDetails::Off;
    }

#ifdef FEATURE_CLOUD_FEED
    // An unset key defaults to cloud on cloud builds: new devices land on the
    // proxy out of the box; an explicit "opensky"/"local" choice is respected.
    useCloudSource = !useLocalSource && dataSource != "opensky";
    rangeKmCfg = distanceKm;
    cloudUrl = CloudFeed::NormalizeBaseUrl(configServer.GetStoredString("cloud-url"));
    if (cloudUrl.isEmpty())
        cloudUrl = CloudFeed::NormalizeBaseUrl(CLOUD_FEED_BASE);
    // Access key, in priority order: the user-editable override, then the key
    // burned in at assembly, then the compile-time default.
    //
    // "cloud-key-fac" is written once by scripts/provision-device.py and is NEVER
    // shown or writable on the config page, so nothing a customer does in a browser
    // can destroy it. That makes an EMPTY "cloud-key" the repair rather than the
    // injury: clearing the box on the settings page restores the key the device
    // shipped with. Before this, a wiped key was unrecoverable on the device --
    // the page only ever displayed asterisks, so the customer had never seen the
    // value they had just deleted, and every instance became a support round-trip
    // to re-derive it from the fleet secret.
    cloudKey = configServer.GetStoredString("cloud-key");
    cloudKey.trim();
    if (cloudKey.isEmpty()) {
        cloudKey = configServer.GetStoredString("cloud-key-fac");
        cloudKey.trim();
        if (!cloudKey.isEmpty())
            Serial.println("[cloud] no key override; using the factory-provisioned key");
    }
    if (cloudKey.isEmpty())
        cloudKey = CLOUD_FEED_KEY;

    if (useCloudSource) {
        // Cadence is the /api/v1/blipscope/config-driven active/idle/night state machine (see
        // CurrentPollIntervalMs); fetchInterval only seeds the pre-config default.
        fetchInterval = cloudCfg.pollActiveMs;
        lastCloudCfgFetch = 0; // re-fetch the fleet config promptly after any (re)initialise
        // Location / radius / toggle may just have changed: drop the airport
        // long tail and re-fetch (the baked majors serve in the gap).
        lastCloudAirportsFetch = 0;
        cloudAirportsRetryMs = 0; // a config save clears any failure backoff too
        cloudAirports.clear();
        Serial.printf("[source] Blipscope Cloud: %s (active %lu ms; cfg pending)\n",
                      cloudUrl.c_str(), cloudCfg.pollActiveMs);
    } else
#endif
    if (useLocalSource) {
        // A local receiver has no API rate limit and refreshes about once a second;
        // poll at that rate. The GET + parse runs on the background fetch task, so
        // this cadence doesn't stall the render loop. No OpenSky token is needed.
        constexpr unsigned long LOCAL_FETCH_INTERVAL = 1000;
        fetchInterval = LOCAL_FETCH_INTERVAL;
        Serial.printf("[source] local receiver: %s (every %lu ms)\n",
                      localUrl.c_str(), fetchInterval);
    } else {
        // calculate how often we can call OpenSky API before being rate limited
        constexpr int MS_PER_DAY = 24 * 60 * 60 * 1000;
        constexpr int ANONYMOUS_TOKENS_PER_DAY = 400;
        constexpr int AUTHED_TOKENS_PER_DAY = 4000;
        constexpr int TOKEN_BUFFER = 3;
        int dailyRequestBudget = ANONYMOUS_TOKENS_PER_DAY - TOKEN_BUFFER; // non-authed tokens minus buffer

        const String token = authHandler.GetValidToken(configServer.GetStoredString("opensky-id"), configServer.GetStoredString("opensky-secret"));
        if (!token.isEmpty())
            dailyRequestBudget = AUTHED_TOKENS_PER_DAY - TOKEN_BUFFER; // authed tokens minus buffer

        fetchInterval = MS_PER_DAY / dailyRequestBudget;
    }

    // backlight brightness (PWM). Default full; clamp away from 0 so the screen
    // can't be saved completely dark. This is the daytime/base level; auto-dim
    // reduces it at night.
    const String brightnessStr = configServer.GetStoredString("brightness");
    configuredBrightness = brightnessStr.isEmpty()
        ? 255 : (uint8_t)constrain(brightnessStr.toInt(), 10, 255);
    tft.setBrightness(configuredBrightness);
    currentBrightness = configuredBrightness;

    const String autoDimStr = configServer.GetStoredString("autodim");
    autoDim = autoDimStr.isEmpty() ? true : (autoDimStr == "true");

    // night clock: opt-in clock face for an empty night sky
    nightClockEnabled = configServer.GetStoredString("night-clock") == "true";

    // window-up rotation: the compass bearing at the top of the screen
    // (0 / unset = classic north-up). Normalised into [0, 360).
    radarUpDeg = ((configServer.GetStoredString("radar-up").toInt() % 360) + 360) % 360;
    const float rotRad = radians((float)radarUpDeg);
    rotCos = cosf(rotRad);
    rotSin = sinf(rotRad);

    // clock offset: default to the nominal zone from longitude (15 deg/hour)
    const String tzStr = configServer.GetStoredString("tz-offset");
    utcOffsetSec = tzStr.isEmpty() ? (long)lround(lon / 15.0) * 3600
                                   : (long)(tzStr.toFloat() * 3600.0f);

    lastBrightnessCheck = 0; // re-evaluate dimming promptly after a reload

    // Force the next Update() to fetch immediately. On a config reload this means
    // a changed location/radius refreshes the radar right away rather than after
    // a full interval; subtracting the interval (unsigned millis() wraparound)
    // makes the next "now - lastFetch >= fetchInterval" check true at any uptime.
    lastFetch = millis() - fetchInterval;

    // seed the touch clock to now, so enrichment is paused for the first few seconds after
    // boot/reload -- that gives the touch poll the bus first while the radar populates.
    lastTouchActivityMs = millis();

    // spawn the background fetch task (once; survives config reloads -- it picks up
    // new location/credentials from each RequestFetch snapshot)
    StartFetchTask();

    // spawn the background enrichment task (once; same lifetime as the fetch task)
    StartEnrichTask();
}

void AircraftManager::Update()
{
    unsigned long now = millis();

#ifdef FOLLOW_BENCH
    // Every loop, not on the merge path: the state override has to answer a
    // keypress on an idle board with no feed, which is exactly the condition
    // the absence copy is being judged under.
    PollBenchSerial();
#endif

    // advance the radar sweep + paint the contacts it crossed this frame, before
    // any early-return below, so the beam keeps turning even while a card is open
    AdvanceSweep();

    // refresh the military/emergency visual-alert layer (ring colour + flash burst
    // edges) before the brightness pass so its night override applies the same frame
    UpdateVisualAlerts();

    // solar day/night backlight dimming (self-throttled)
    UpdateBrightness();

    // Touch-wedge last rung: the supervisor's re-init ladder has been failing for
    // over 90 s. Reboot -- historically the one recovery a stuck chip always
    // responded to -- but silently: only once the user has been away a while, so
    // the ~10 s boot never interrupts someone actually watching the scope.
    if constexpr (variant::TOUCH_WATCHDOG) {
        constexpr unsigned long REBOOT_IDLE_MS = 10UL * 60UL * 1000UL;
        if (TouchWatchdog::RebootRecommended() && now - lastTouchActivityMs >= REBOOT_IDLE_MS) {
            Serial.println("[touch-wd] wedged past the outage bound and idle: rebooting to recover the controller");
            Serial.flush();
            delay(100);
            ESP.restart();
        }
    }

    // Preventive weekly reboot (2026-07-10 decision): fragmentation insurance beyond
    // the soak horizon. Same "nobody is watching" bar as the wedge reboot above --
    // >= 10 min touch idle, no card open -- plus solar night and a valid clock so the
    // ~10 s boot blank never happens in front of anyone. millis() wraps at ~49.7 d,
    // so the 7-day default always fires well before wrap.
#ifndef PREVENTIVE_REBOOT_DAYS
#define PREVENTIVE_REBOOT_DAYS 7
#endif
    {
        constexpr unsigned long PREVENTIVE_REBOOT_MS = PREVENTIVE_REBOOT_DAYS * 24UL * 3600UL * 1000UL;
        const time_t utcNow = time(nullptr);
        if (now >= PREVENTIVE_REBOOT_MS && !inDetail && utcNow > 1600000000 &&
            now - lastTouchActivityMs >= 10UL * 60UL * 1000UL && isNightNow(lat, lon, utcNow)) {
            Serial.printf("[health] preventive reboot: uptime %lu d, solar night + idle\n", now / 86400000UL);
            Serial.flush();
            delay(100);
            ESP.restart();
        }
    }

    // Optional on-board peripherals (compiled out on SKUs without them). Both run before the
    // inDetail early-return below so the buzzer still finishes its beep and the tilt readout
    // keeps updating while a card is open.
    if constexpr (variant::HAS_AUDIO) {
        board::BuzzerUpdate();
        UpdateTones(); // step any in-progress alert-tone pattern
    }
    if constexpr (variant::HAS_IMU) {
        if (now - lastImuReadMs >= 200) { // ~5 Hz is ample for a tilt readout
            lastImuReadMs = now;
            board::Imu s;
            if (board::ImuRead(s)) {
                constexpr float RAD2DEG = 57.2957795f;
                // The QMI8658 sits with +Z pointing away from the screen, so it reads az = -1g when
                // the board lies flat. Referencing roll against -az makes "flat" read ~0 instead of
                // ~180; pitch's denominator is sign-independent so it already reads 0 at flat.
                imuPitch = atan2f(-s.ax, sqrtf(s.ay * s.ay + s.az * s.az)) * RAD2DEG;
                imuRoll  = atan2f(s.ay, -s.az) * RAD2DEG;
                imuValid = true;
            }
        }
    }

    // flush the logbook to flash if it's accumulated changes (debounced internally)
    if (logbookEnabled)
        logbook.MaybePersist();

    // Retire an expired claim confirmation and start the next queued one.
    UpdateClaimToast();

    // MEASUREMENT: edge-detect entry into an Aging picture (a "stale episode"),
    // then report the window. Stamped with UTC in ReportPerf so an episode can be
    // lined up against the relay's own log for the same minute -- the backstop
    // that catches the case the on-device numbers cannot see, namely NO DEVICE
    // TRAFFIC AT THE RELAY AT ALL. That signature is what would have identified a
    // wrong-image flash in minutes instead of hours.
    {
        const bool aging = CurrentStaleStage() >= StaleStage::Aging;
        if (aging && !perfInEpisode) perf.episodes++;
        perfInEpisode = aging;
    }
    if (now - lastPerfReportMs >= 60000UL) {
        lastPerfReportMs = now;
        ReportPerf();
    }

#ifdef FEATURE_CLOUD_FEED
    // Public leaderboard: submit the logbook tallies hourly (first submit ~30 s
    // after boot so the lifelist has loaded and the clock/feed have settled).
    //
    // ONCE DUE, THE SUBMIT TAKES PRIORITY over new enrichment. It used to merely
    // retry -- QueueLeaderboardSubmit returns false while enrichInFlight, and the
    // hourly retry would "just catch the next gap". Under a dense sky there is no
    // gap: the tracked set is pinned at MAX_AIRCRAFT and churns continuously, so
    // enrichment (queue depth 1, one outstanding at a time) is always in flight
    // and a request issued once an hour loses every race, permanently. Measured
    // at LAX with a 10 mi radius: zero submits landed. The device kept claiming,
    // the board kept showing nothing, and nothing anywhere reported an error --
    // the worst shape a bug can take.
    //
    // So `lbSubmitPending` latches when due and holds new enrichment off until
    // the submit is away (see RequestMetadata/Route/Photo/CloudEnrich). One small
    // request per hour delaying one photo by a second or two is the right trade;
    // the reverse -- a photo stream permanently starving the only report the
    // owner's collection ever makes -- is not.
    if (lbEnabled && useCloudSource && !cloudUrl.isEmpty()) {
        constexpr unsigned long LB_INTERVAL_MS = 60UL * 60UL * 1000UL; // hourly
        constexpr unsigned long LB_FIRST_DELAY_MS = 30UL * 1000UL;
        // lbRetryBackoffMs is 0 in the healthy case, so this is the plain hourly
        // schedule until something actually fails.
        const unsigned long wait = lbRetryBackoffMs > 0 ? lbRetryBackoffMs : LB_INTERVAL_MS;
        const bool due = lastLeaderboardSubmit == 0
            ? now >= LB_FIRST_DELAY_MS
            : now - lastLeaderboardSubmit >= wait;
        if (due && !lbSubmitPending) {
            lbSubmitPending = true;
            lbSubmitDueMs = now; // start the due -> away clock the soak gate reads
            lbStarvedReported = false;
        }
#ifdef LB_SUBMIT_GATE_MS
        // Dense-sky gate (blipscope-s3-128-densesky). Starvation is silent by
        // nature -- the device keeps working, the board just never changes -- so
        // the run has to say so out loud or a 24 h log proves nothing. Once per
        // episode, not per loop: a line every frame is a line nobody reads.
        if (lbSubmitPending && !lbStarvedReported && now - lbSubmitDueMs > LB_SUBMIT_GATE_MS) {
            lbStarvedReported = true;
            Serial.printf("[leaderboard] GATE BROKEN: due %lu ms ago, still not away "
                          "(enrichInFlight=%d)\n", now - lbSubmitDueMs, (int)enrichInFlight);
        }
#endif
        if (lbSubmitPending && QueueLeaderboardSubmit()) {
            const unsigned long waited = now - lbSubmitDueMs;
            if (waited > lbWorstSubmitWaitMs)
                lbWorstSubmitWaitMs = waited;
            lbSubmitPending = false;
            lastLeaderboardSubmit = now;
            Serial.printf("[leaderboard] submit away after %lu ms (worst %lu ms)\n",
                          waited, lbWorstSubmitWaitMs);
        }
    }
#endif

    // Home Assistant / MQTT: (re)publish the discovery configs + a fresh snapshot
    // on each connect, then a retained summary every few seconds. Runs regardless
    // of the detail view so the broker stays current; the publisher task does the
    // actual (non-blocking) socket work.
    if (mqttEnabled) {
        if (mqtt.ConsumeJustConnected()) {
            if (mqttDiscovery) PublishMqttDiscovery();
            PublishMqttState();
            lastMqttState = now;
        } else if (mqtt.Connected() && now - lastMqttState >= 5000) {
            lastMqttState = now;
            PublishMqttState();
        }
        // Fire one-shot events (watchlist / emergency / military / overhead) so
        // HA automations can trigger on the moment of appearance, not just poll
        // the retained binary sensors. Deduped per aircraft, independent of the
        // ntfy toggles above.
        if (mqtt.Connected())
            PublishMqttEvents();
    }

    // apply any completed background enrichment (metadata / route / photo) so the
    // detail card and radar labels pick it up the same frame it arrives. Done before
    // the inDetail early-return below so a card's lookups keep flowing while it's open.
    ConsumeEnrichResults();

    // fill in the selected aircraft's details first, so the detail card from the
    // prior frame's tap stays on screen during each brief lookup
    ProcessDetailLookups();

    // handle taps every loop so the UI stays responsive between fetches
    HandleTouch();

    // A card that nobody closes must not pause the pipeline forever: the inDetail
    // early-return below stops fetch consumption AND scheduling, which is fine for
    // the seconds-to-minutes a human actually reads a card, but a tap-and-walk-away
    // (or a ghost/synthetic tap) would otherwise freeze the picture indefinitely
    // while the card shows silently aging data. Auto-close after 3 idle minutes.
    // Signed comparison, NOT unsigned subtraction: HandleTouch() above just stamped
    // lastTouchActivityMs with a millis() that can be a tick NEWER than this frame's
    // `now`, and (unsigned)(now - newer) underflows to ~2^32 -- which fired this
    // auto-close on the first frame of every press while a card was open (closing
    // cards at press via the idle path, killing the photo-page flip and swipe-to-pin).
    constexpr unsigned long CARD_IDLE_CLOSE_MS = 3UL * 60UL * 1000UL;
    if (inDetail && (long)(now - lastTouchActivityMs) >= (long)CARD_IDLE_CLOSE_MS) {
        Serial.println("[card] idle 3 min; auto-closing detail card");
        ExitDetail();
    }

    // While the detail card is open the radar isn't visible and the user is
    // interacting, so skip the radar's background network work below (metadata
    // enrichment, watchlist/overhead alerts, and the periodic feed fetch). The
    // enrichment and the fetch are non-blocking now (each runs on its own task),
    // but the ntfy alert POST still blocks the loop for up to a few seconds, and the
    // touch panel is only polled once per loop -- so a quick "tap to close" landing
    // during that POST was never sampled, which is what made it take two or three
    // taps. Bailing out here keeps the loop fast while a card is up, so a dismiss tap
    // registers the first time. The card's own lookups still flow via
    // ProcessDetailLookups()/ConsumeEnrichResults() above, and normal fetching
    // resumes the instant the card closes (lastFetch is already stale, so the next
    // Update() refreshes immediately).
    if (inDetail)
        return;

    // Merge a completed background fetch into trackedAircraft. The blocking GET +
    // JSON decode already ran on the fetch task; this is just a fast map merge.
    ConsumeFetchResult();

    // Hold off the radar enrichment + alerts while a feed fetch is in flight. The
    // metadata enrichment is queued to the enrich task now (non-blocking), but gating
    // it here keeps a second adsbdb request from queueing right behind the feed fetch
    // and spacing them keeps peak heap down on this tight board; the ntfy POST in
    // ProcessAlerts still blocks the loop, and RequestFetch's token refresh can still
    // block on the rare ~29-minute renewal.
    if (!fetchInFlight) {
        // enrich a tracked aircraft with metadata (queued, throttled internally)
        ProcessMetadataLookups();

        // alert on watchlisted / military / overhead aircraft (throttled internally)
        ProcessAlerts();

#ifdef FEATURE_CLOUD_FEED
        // Fleet config (/api/v1/blipscope/config): on boot, then daily; a failed fetch retries
        // in 15 min (ConsumeFetchResult shifts the timer). Runs on the shared
        // fetch task ahead of the next feed poll so cadence/enrich-level tunables
        // land before the picture builds up.
        if (useCloudSource && !cloudUrl.isEmpty()) {
            constexpr unsigned long CFG_REFRESH_MS = 24UL * 60UL * 60UL * 1000UL;
            if (lastCloudCfgFetch == 0 || now - lastCloudCfgFetch >= CFG_REFRESH_MS) {
                // STAMP ONLY ON SUCCESS. Committing the timestamp before the
                // request is known to be queued converts a dropped request into
                // a 24 h outage of this feed: the retry condition is already
                // satisfied-away, and nothing logs it. Leaving the timer at its
                // previous value means the next loop pass simply tries again.
                if (RequestCloudConfig()) {
                    lastCloudCfgFetch = now;
                    return; // the fetch task is busy now; the feed poll goes next cycle
                }
            }
        }

        // Airport overlay long tail (/api/v1/blipscope/airports): geography is static, so
        // once after boot and then daily is plenty. Gated on the display toggle
        // (no toggle, no traffic); Initialise() zeroes the timer on every
        // config save, so a location / range / toggle change re-fetches. A
        // failed fetch retries in 15 min (ConsumeFetchResult shifts the timer)
        // while DrawAirports serves the baked majors table.
        if (useCloudSource && displayAirports && !cloudUrl.isEmpty()) {
            constexpr unsigned long APT_REFRESH_MS = 24UL * 60UL * 60UL * 1000UL;
            // BOUND THE FAILURE. Daily is right once the overlay has landed, but
            // while it is EMPTY the same interval means any lost fetch costs a
            // customer 24 h of missing airports -- silently, and degrading to
            // the baked majors so it reads as "the small airports went away"
            // rather than as a fault. Retrying an empty overlay every 5 min
            // makes any cause -- including one we have not identified (#129) --
            // a five-minute blip instead. This is deliberately a bound on the
            // CLASS of failure, not a fix for a specific one.
            constexpr unsigned long APT_EMPTY_RETRY_MS = 5UL * 60UL * 1000UL;
            // A failed fetch sets its own backoff and that wins; otherwise an
            // empty overlay is due in 5 min and a populated one in 24 h.
            const unsigned long due = cloudAirportsRetryMs != 0
                                          ? cloudAirportsRetryMs
                                          : (cloudAirports.empty() ? APT_EMPTY_RETRY_MS : APT_REFRESH_MS);
            if (lastCloudAirportsFetch == 0 || now - lastCloudAirportsFetch >= due) {
                if (RequestCloudAirports()) {
                    lastCloudAirportsFetch = now;
                    return; // same single-fetch-task etiquette as the config fetch
                }
            }
        }
#endif

        // kick off the next fetch when due. Non-blocking: the loop keeps polling
        // touch and drawing while the request runs on the fetch task, so tapping a
        // plane during a refresh is no longer missed. The interval is the cadence
        // machine's current state (config-driven active/idle/night in cloud mode).
        if (now - lastFetch >= CurrentPollIntervalMs()) {
            lastFetch = now;
            RequestFetch();
        }
    }
}

unsigned long AircraftManager::CurrentPollIntervalMs() const
{
#ifdef FEATURE_CLOUD_FEED
    if (useCloudSource) {
        // Active: the user touched the screen within the idle window -- they're
        // present, poll fast (this outranks night: someone watching at 2 AM gets
        // the good cadence). Otherwise night (solar clock) outranks idle: night
        // is 8-12 h of every fleet-day and cadence is the main cost lever.
        if (millis() - lastTouchActivityMs <= cloudCfg.idleAfterMs)
            return cloudCfg.pollActiveMs;
        const time_t utc = time(nullptr);
        if (utc > 1600000000 && isNightNow(lat, lon, utc))
            return cloudCfg.pollNightMs;
        return cloudCfg.pollIdleMs;
    }
#endif
    return fetchInterval;
}

// The amber "stale" tag. ageMs is the age of the PICTURE, not of our last request:
// dataLagAtMergeMs carries how old the server's snapshot already was when we merged
// it (see MergeSnapshot), so the server's tile TTL lands in here directly.
//
// THE THRESHOLD HAS A FLOOR, and the floor is the point of this function.
// staleFactor x interval alone measures the wrong thing -- it ties "is this data
// stale" to how often WE ASK, when the freshness actually available is set by the
// SERVER's tile TTL. Polling twice as fast does not make the data newer; it only
// makes the threshold half as forgiving. That is backwards, and it already bites:
//
//   SKU              active poll   3 x interval   server tile TTL
//   s3-128 (default)      5 s          15 s             8 s        ok, barely
//   s3-146 / s3-21        2 s           6 s             8 s        ALWAYS AMBER
//
// The 1.46" and 2.1" boards demand data fresher than the cloud has ever served, so
// they can sit on amber while someone is watching a perfectly healthy feed. The floor
// fixes that on its own, independently of any TTL change.
//
// It is also what lets the tile TTL rise to 30 s for the 50-board pilot (the adsb.fi
// per-IP budget needs it -- see relay/setup-relay.sh). Worst-case HEALTHY age at a
// 30 s TTL: 30 (tile TTL) + ~1 (relay fetch) + 3 (the Worker's fresh window) + up to
// one poll interval since our own merge ~= 39 s. minStaleMs defaults to 45 s, which
// clears that and is EXACTLY today's idle-tier threshold (3 x 15 s) -- so the floor
// introduces no new number anywhere: idle (45 s) and night (180 s) are unchanged,
// and only the fast tiers stop being stricter than the data can possibly be.
bool AircraftManager::IsDataStale() const
{
    if (lastGoodDataMs == 0)
        return false; // nothing merged yet: that's "starting up", not "stale"
    const unsigned long ageMs = (millis() - lastGoodDataMs) + dataLagAtMergeMs;
    unsigned long thresholdMs = 3UL * CurrentPollIntervalMs();
#ifdef FEATURE_CLOUD_FEED
    if (useCloudSource) {
        thresholdMs = (unsigned long)cloudCfg.staleFactor * CurrentPollIntervalMs();
        // Cloud only. A local dump1090/readsb feed is on the LAN with no tile cache in
        // front of it, so its data really is as fresh as the poll -- flooring that to
        // 45 s would hide a genuinely dead local feed for 45 s in exchange for nothing.
        if (thresholdMs < cloudCfg.minStaleMs) thresholdMs = cloudCfg.minStaleMs;
    }
#endif
    return ageMs > thresholdMs;
}

unsigned long AircraftManager::DataAgeMs() const
{
    if (lastGoodDataMs == 0)
        return 0; // nothing merged yet
    return (millis() - lastGoodDataMs) + dataLagAtMergeMs;
}

AircraftManager::StaleStage AircraftManager::CurrentStaleStage() const
{
    if (lastGoodDataMs == 0)
        return StaleStage::Live; // starting up, not stale

    // Escalate once the picture is old enough that a passing glance must not read
    // it as live. 75 s is deliberately well past a few missed polls (which are
    // routine on every source) but well inside the span where someone would still
    // believe what they are looking at.
    constexpr unsigned long AGING_MS = 75000UL;
    // Derived from the dead-reckoning cap itself, not copied from it: that is the
    // moment the sky freezes in place, so it is exactly the moment the display has
    // to stop implying the picture means anything. Bound to the source of truth so
    // changing one cannot silently desync the other.
    constexpr unsigned long NODATA_MS =
        (unsigned long)(TrackedAircraft::MAX_DR_SECONDS * 1000.0f);
    static_assert(NODATA_MS > AGING_MS, "NoData must escalate after Aging");

    const unsigned long age = DataAgeMs();
    if (age >= NODATA_MS) return StaleStage::NoData;
    if (age >= AGING_MS)  return StaleStage::Aging;
    return IsDataStale() ? StaleStage::Stale : StaleStage::Live;
}

// Render the ladder's banner. No String anywhere: a fixed stack buffer keeps the
// render path allocation-free (this runs every frame while degraded).
void AircraftManager::DrawStaleIndicator(BandCanvas& backbuffer) const
{
    char buf[24];
    uint32_t colour;

#ifdef FEATURE_CLOUD_FEED
    // CREDENTIAL REJECTION OUTRANKS STALENESS, and shares this one banner slot
    // rather than adding a second.
    //
    // The reason is not screen space, it is accuracy. When the key is refused the
    // data IS stale -- as a CONSEQUENCE. Showing "STALE 2h" would be true and
    // useless: it points the owner at their network, their router, their wifi,
    // anywhere except the one thing that fixes it. The most specific true statement
    // available is the one to show.
    if (NeedsReverify()) {
        colour = lgfx::color888(255, 64, 48);
        // Deliberately not "unauthorized", "rejected" or "invalid key": a fleet-wide
        // credential rotation lights this on every board at once, and the owner did
        // nothing wrong. It names the ACTION, not a fault.
        snprintf(buf, sizeof(buf), "NEEDS VERIFY");
        backbuffer.setTextSize(1);
        backbuffer.setTextColor(colour);
        backbuffer.drawString(buf, SCREEN_SIZE_DIV_2 - (int)backbuffer.textWidth(buf) / 2, 14);
        return;
    }
#endif

    const StaleStage stage = CurrentStaleStage();
    if (stage == StaleStage::Live)
        return;

    const unsigned long ageS = DataAgeMs() / 1000UL;

    switch (stage) {
        case StaleStage::Stale:
            // As before: quiet amber, no number. A few missed polls is routine and
            // does not deserve a countdown.
            colour = lgfx::color888(255, 176, 40);
            snprintf(buf, sizeof(buf), "STALE DATA");
            break;
        case StaleStage::Aging:
            // Now it earns a number -- "how long has this been wrong?" is the
            // question a user actually has, and an elapsed age answers it without
            // needing them to have watched the whole time.
            colour = lgfx::color888(255, 176, 40);
            if (ageS < 600UL) snprintf(buf, sizeof(buf), "STALE %lum", ageS / 60UL);
            else              snprintf(buf, sizeof(buf), "STALE %luh", ageS / 3600UL);
            break;
        case StaleStage::NoData:
        default:
            // Red, and no longer describing the aircraft as merely stale: at this
            // point they are not a picture of anything.
            colour = lgfx::color888(255, 64, 48);
            if (ageS < 3600UL) snprintf(buf, sizeof(buf), "NO DATA - %lum", ageS / 60UL);
            else               snprintf(buf, sizeof(buf), "NO DATA - %luh", ageS / 3600UL);
            break;
    }

    backbuffer.setTextSize(1);
    backbuffer.setTextColor(colour);
    backbuffer.drawString(buf, SCREEN_SIZE_DIV_2 - (int)backbuffer.textWidth(buf) / 2, 14);
}

void AircraftManager::RecordFrameUs(uint32_t frameUs)
{
    // Ring of the last N frames in 0.1 ms units (u16 caps at ~6.5 s -- ample).
    const uint32_t tenths = frameUs / 100;
    const uint16_t capped = (uint16_t)std::min(tenths, (uint32_t)UINT16_MAX);
    frameSampleBuf[frameSampleCount % FRAME_SAMPLES] = capped;
    frameSampleCount++;
    // Interval-spanning worst frame (see the member comment): catches a stall
    // anywhere in the 30 s window, not just the last FRAME_SAMPLES frames.
    if (capped > frameMaxTenths) frameMaxTenths = capped;

    const unsigned long now = millis();
    if (now - lastHealthReportMs < 30000)
        return;
    lastHealthReportMs = now;
    const float maxMs = frameMaxTenths / 10.0f;
    frameMaxTenths = 0; // reset for the next interval

    const size_t n = std::min(frameSampleCount, FRAME_SAMPLES);
    if (n == 0)
        return;
    // p95 via a sorted copy -- 128 u16s every 30 s is nothing, even on the C3.
    uint16_t sorted[FRAME_SAMPLES];
    memcpy(sorted, frameSampleBuf, n * sizeof(uint16_t));
    std::sort(sorted, sorted + n);
    uint32_t sum = 0;
    for (size_t i = 0; i < n; ++i) sum += sorted[i];
    const float avgMs = sum / (float)n / 10.0f;
    const float p95Ms = sorted[(size_t)((n - 1) * 0.95f)] / 10.0f;

    const uint32_t heapFree = ESP.getFreeHeap();
    const uint32_t largest = ESP.getMaxAllocHeap();
    // KEPT DELIBERATELY, AND NO LONGER TRUSTED. `largest` is the plateaued figure
    // from #163 -- it is reported so the plateau can be watched in the field rather
    // than lost, and sits next to the two numbers that actually move: `free8` (free
    // 8-bit internal, which tracked continuously where largest did not) and `tlsOk`
    // (a real trial allocation of a handshake-sized block, i.e. the answer to the
    // only question the gates ever wanted). If largest is pinned while tlsOk flips
    // to 0, that is the bug reproducing itself in the field.
    const uint32_t free8 = heaphealth::FreeInternal8Bit();
    const int      tlsOk = heaphealth::CanHandshake() ? 1 : 0;
    // allocFail + hardFail every report (not just soak builds): their trend
    // leading into a LOOP STALL disambiguates the two slowdown hypotheses --
    // climbing allocFail points at heap fragmentation (a big contiguous alloc
    // can't be satisfied), climbing hardFail at TLS/DNS starvation. Cheap: two
    // counter reads.
    // tls=<handshakes>/<reuses>: a fresh https connection needs a large contiguous
    // block, so handshakes -- not sub-KB body parses -- are what actually costs heap
    // here. The ratio is the direct measure of whether detail lookups are staying on
    // one host (reuses climb) or ping-ponging between two (handshakes climb).
    // n=<tracked>: the aircraft count these frame times were measured AT. Without
    // it the health line reports a p95 with no load attached, so every "is this a
    // regression?" question can only be answered by inference -- which is exactly
    // how the BLIPS_LIMIT budget argument stalled. Frame cost is dominated by how
    // many contacts get drawn, so this is the x-axis for the p95 the line already
    // prints. One size_t read; it stays in shipping builds because it is just as
    // useful for "why is this customer's device slow?" as it is on the bench.
    // ap=<airports>: the overlay symbol count, added 2026-08-02 when it was the
    // leading suspect for what drives frame p95. IT IS NOT, and neither half of
    // that 2026-08-02 guess survived: the overlay costs -0.09 ms over 29 paired
    // ticks at ap=60, and `n` -- which this comment used to say was not the
    // lever -- turned out to be the whole story at 0.802 ms per contact
    // (R2=0.985, 5137 samples; see the budget note below). Kept on the line
    // anyway: it is the control that lets a future overlay change be told apart
    // from a contact-count change, which is precisely what was missing in
    // August.
    // Whichever overlay is actually in force: the cloud long tail while it has
    // landed, else the baked majors table DrawAirports falls back to -- so the
    // number always means "symbols available to draw", on every build.
#ifdef FEATURE_CLOUD_FEED
    const unsigned apCount = cloudAirports.empty() ? (unsigned)AIRPORT_COUNT
                                                   : (unsigned)cloudAirports.size();
#else
    const unsigned apCount = (unsigned)AIRPORT_COUNT;
#endif
    // psram_free was ABSENT from this line until Follow stage 1, and §4.4 names
    // it as the primary acceptance signal: the track allocation should drop it by
    // ~12 KB at follow-enable and then hold flat for the whole soak. Without it
    // the only PSRAM evidence was the boot banner -- one sample, which cannot show
    // a trend, and a trend is the entire question. One cheap read.
    const uint32_t psramFree = ESP.getFreePsram();
    // The frame budget is a LINE, not a constant. Declared here rather than at
    // the gate because the health line reports the residual against it and the
    // gate below tests the same model -- one definition, two readers. The
    // measurement, the provenance and the condition on reusing these numbers are
    // all in the budget note further down; read that before changing either.
    constexpr float FRAME_P95_INTERCEPT_MS    = 35.80f;
    constexpr float FRAME_P95_PER_AIRCRAFT_MS = 0.802f;
    const float predictedP95Ms = FRAME_P95_INTERCEPT_MS +
                                 FRAME_P95_PER_AIRCRAFT_MS * (float)trackedAircraft.size();
    // resid=<observed - predicted>, and it is on the line for one specific
    // reason: these constants are fitted on a board that was heap-starved for
    // 84.9 % of its samples, so they WILL need re-fitting once #245 lands.
    // Printing the residual makes that re-fit a DATA question -- collect
    // residuals from a healthy board, read the bias straight off -- instead of a
    // redesign that has to re-derive the shape from nothing. A model with no
    // published error term is one nobody can correct.
    const float residualMs = p95Ms - predictedP95Ms;
    Serial.printf("[health] frame avg=%.1fms p95=%.1fms max=%.1fms  n=%u ap=%u resid=%+.1fms  heap free=%u largest=%u free8=%u psram_free=%u tlsOk=%d rej=%lu ball=%d/%lu  allocFail=%lu hardFail=%lu  tls=%lu/%lu  tlsmem=%lu/%lu/%lu  interval=%lums%s\n",
                  avgMs, p95Ms, maxMs, (unsigned)trackedAircraft.size(), apCount, residualMs,
                  (unsigned)heapFree, (unsigned)largest, (unsigned)free8, (unsigned)psramFree, tlsOk,
                  (unsigned long)heaphealth::TrialRejectionCount(),
                  heaphealth::BallastHeld() ? 1 : 0,
                  (unsigned long)heaphealth::BallastReacquireFailures(),
                  (unsigned long)AllocFailureCount(), (unsigned long)FetchHardFailCount(),
                  (unsigned long)http.TlsHandshakes(), (unsigned long)http.TlsReuses(),
                  // tlsmem=psram/internal/fallback -- issue #245. The middle number
                  // stays non-zero by design (small allocations belong in internal
                  // RAM); the THIRD must stay 0. A rising fallback count means PSRAM
                  // refused and mbedTLS is back on the heap that fragments -- the fix
                  // installed but not working, which otherwise looks identical to it
                  // working.
                  (unsigned long)tlsalloc::PsramAllocs(),
                  (unsigned long)tlsalloc::InternalAllocs(),
                  (unsigned long)tlsalloc::PsramFallbacks(),
                  CurrentPollIntervalMs(), IsDataStale() ? "  DATA STALE" : "");
    // ---- Follow Mode stage 1: the §18.1 measurement -------------------------
    //
    // Its OWN line, and only while following, so the [health] line's shape does
    // not change for the boards not running this experiment: anything parsing it
    // keeps working, and a bench log stays diffable against one from before
    // Follow existed.
    //
    // trk=<size>/<capacity> is the x-axis. A draw cost quoted without the number
    // of points it was measured at is the same mistake as a frame p95 with no
    // contact count -- which is exactly why n= was added to the line above.
    if (!followTarget.isEmpty()) {
        const uint32_t meanUs = followDrawFrames ? (followDrawSumUs / followDrawFrames) : 0;
        Serial.printf("[follow] trk=%u/%u seg=%u cap=%d  draw mean=%.2fms max=%.2fms over %lu frames  "
                      "appends=%lu near-rejects=%lu  active=%d degraded=%d\n",
                      (unsigned)followTrack.Size(), (unsigned)follow::Track::CAPACITY,
                      (unsigned)followDrawSegments, (int)FOLLOW_DRAW_SEGMENT_CAP,
                      meanUs / 1000.0f, followDrawMaxUs / 1000.0f,
                      (unsigned long)followDrawFrames,
                      (unsigned long)followTrack.Appends(),
                      (unsigned long)followTrack.RejectedNear(),
                      (int)followTrack.Active(), (int)followTrack.Degraded());
        // The arc face (§8), on its own line and only once it has drawn. §7.2's
        // globe reference is 0.34 ms for 1,306 vertices; this is the number to
        // put beside it, and reporting it here means the soak logs carry it.
        if (followArcMaxUs) {
            Serial.printf("[follow] arc=%.2fms max=%.2fms strokes=%u route=%s->%s regime=%s\n",
                          followArcUs / 1000.0f, followArcMaxUs / 1000.0f,
                          (unsigned)followArcStrokes,
                          followRouteOrigin.isEmpty() ? "--" : followRouteOrigin.c_str(),
                          followRouteDest.isEmpty() ? "--" : followRouteDest.c_str(),
                          FollowRegime() == follow::Regime::Airline ? "airline" : "local");
            followArcMaxUs = 0;
        }
        // The STATE, on the same line. §19: "the feature is the states, not the
        // picture", and a soak log that carries the draw cost but not which
        // state the machine sat in cannot answer the one question a logged
        // training flight exists to answer (§18.3) -- whether the constants in
        // Tuning put the machine in the right state at the right moment. It is
        // one word and it is the difference between a log and evidence.
        //
        // The TARGET is deliberately NOT printed. §17 lists serial output among
        // the places the follow value must never appear, with the Wi-Fi password
        // incident as the precedent.
        Serial.printf("[follow] state=%s home=%s pos=%d elev=%d\n",
                      follow::Headline(followMachine.Current()),
                      followHomeCode.isEmpty() ? "-" : followHomeCode.c_str(),
                      (int)followHome.positionKnown, (int)followHome.elevationKnown);
        // THE RING SCALE, STATED RATHER THAN INFERRED FROM A PHOTOGRAPH.
        //
        // Read off the bench as 50/100/150 mi against the synthetic path, which
        // was the right answer and could only be checked by measuring a picture.
        // The number that matters later is the one a REAL circuit produces --
        // 1-5 mi rings -- and nobody will photograph that at the right moment.
        //
        // seg_px is the mean on-screen distance between CONSECUTIVE DRAWN points
        // and is the direct answer to "why does the trail look like dots": the
        // decimation is 150 m of FLOWN PATH, so how it reads is purely a function
        // of the current scale. At a 150 mi ring it is kilometres per pixel and
        // the laps separate; at a 2 mi circuit ring the same buffer draws as a
        // connected racetrack.
        {
            const follow::LocalView v = BuildLocalView();
            const size_t nn = followTrack.Size();
            const size_t stride = follow::Track::StrideFor(nn, FOLLOW_DRAW_SEGMENT_CAP);
            float pitchPx = 0.0f;
            if (nn > stride) {
                const float pxPerKm = (v.radiusKm > 0.0f)
                    ? ((float)FOLLOW_FACE_RADIUS_PX / v.radiusKm) : 0.0f;
                float sumKm = 0.0f; size_t pairs = 0;
                for (size_t i = stride; i < nn; i += stride) {
                    const follow::TrackPoint& a = followTrack.At(i - stride);
                    const follow::TrackPoint& b = followTrack.At(i);
                    sumKm += follow::SeparationKm(a.lat, a.lon, b.lat, b.lon);
                    ++pairs;
                }
                if (pairs) pitchPx = (sumKm / (float)pairs) * pxPerKm;
            }
            Serial.printf("[follow] rings=%d x %.2f%s outer=%.1fkm  stride=%u seg_px=%.1f\n",
                          v.rings, (double)v.stepDisplay, rangeUnit.c_str(),
                          (double)v.radiusKm, (unsigned)stride, (double)pitchPx);
        }
        // Reset the window with the report, like frameMaxTenths above, so each
        // line describes its own 30 s rather than all of history.
        followDrawMaxUs = 0;
        followDrawSumUs = 0;
        followDrawFrames = 0;
    }

#ifdef FOLLOW_BENCH
    // ---- SUBTRACTION HARNESS -----------------------------------------------
    //
    // Why this exists. Daniel's bench board reported frame avg 69 ms where two
    // s3-128s soaking beside it reported 46-48 ms, and the track accounts for
    // 4.3 ms of the difference. Something costs ~18 ms and there was no way to
    // ask which thing.
    //
    // WITHIN ONE BOOT, not across flashes. The obvious experiment -- flash the
    // shipping env, read the number, compare -- changes the board, the sky and
    // the aircraft count at the same time as the variable of interest. That is
    // precisely the mistake the frame-budget note above documents at length: a
    // measurement that "proved" 40 aircraft render cheaper than 25 was comparing
    // a full renderer against a gutted one, and it took a month to unpick. So
    // this rotates the render configuration every health interval and labels
    // what was measured. Same board, same boot, same sky, one variable.
    //
    // The overlay is in the rotation deliberately. That same note says the load
    // axis is UNTESTED and names the airport overlay as a live suspect that was
    // never separated -- "ap= is on the health line so the overlay hypothesis can
    // be tested properly". ap=41 symbols are drawn every frame. This is that test.
    //
    // Track OFF here means DRAW-off, not freed: the allocation stays, so this
    // measures draw cost and cannot be confused with an allocation effect.
    {
        static int benchPhase = 0;
        static bool announced = false;
        // ALTERNATE ONE VARIABLE, EVERY TICK. The first version rotated four
        // configurations and was worthless: a freshly booted board's frame cost
        // CLIMBS ~12 ms over its first six minutes (the soak control does the
        // same, 39.2 -> 51.6 ms at constant n=40), so 30 s between arms put more
        // drift between two readings than the effect being measured. Consecutive
        // ticks bracket the drift instead; average the pairs.
        //
        // The overlay is the variable because the frame-budget note says the load
        // axis is UNTESTED and names it as the never-separated suspect. This board
        // carries ap=60 against the soak pair's ap=41, which is a natural
        // experiment worth not wasting. The track stays ON throughout so its cost
        // stays visible on the [follow] line beside every reading.
        // VARIABLE: the per-aircraft info labels.
        //
        // The overlay arm ran first and came back -0.09 ms over 29 paired
        // ticks -- nothing, at ap=60. Reading the three boards config pages
        // then showed what actually differs between this fresh unit and the
        // soak pair: info-callsign, info-speed and info-baroalt are ON here
        // and OFF there. Those draw PER AIRCRAFT, every frame, and the label
        // box walks the same fields a second time for layout -- so at n=30
        // that is ~180 String builds a frame the soak boards never do.
        //
        // This is the shipping default against the legacy one, which makes it
        // a defaults decision rather than a curiosity.
        static const char* const PHASE[2] = {
            "infotext=1",
            "infotext=0",
        };
        if (!announced) {
            Serial.println("[bench] subtraction harness: rotating render config every health tick");
            announced = true;
        }
        Serial.printf("[bench] MEASURED %s  avg=%.1fms p95=%.1fms max=%.1fms  n=%u ap=%u\n",
                      PHASE[benchPhase], avgMs, p95Ms, maxMs,
                      (unsigned)trackedAircraft.size(), apCount);
        benchPhase = (benchPhase + 1) % 2;
        followDrawTrack = true;             // constant: its cost is already known
        displayAirports = true;             // settled: -0.09 ms, leave it on
        displayInfoText = (benchPhase == 0);
        Serial.printf("[bench] NEXT     %s\n", PHASE[benchPhase]);
    }
#endif

    // ---- issue #245: the enrichment-starvation watch ------------------------
    //
    // TWO JOBS, and the first one is why this exists at all: SAY SO. Before this,
    // a board whose ballast was gone reported ball=0/48 on a line that also
    // carried a healthy frame rate and a full sky, and nothing anywhere called
    // that a fault. It is the single number separating "mitigated" from "failed"
    // and it was being printed as trivia.
    {
        // ASK THE ALLOCATOR DIRECTLY, not tlsOk. tlsOk is CanHandshake(),
        // whose ballast arm short-circuits before it ever reaches the
        // allocator -- so while a block is held it reports 1 however bad the
        // heap is, which is exactly what blinded the first version of this
        // watch for the whole 2026-08-24 soak. See StarvationPolicy.h.
        //
        // A refusal here counts toward `rej` like any other gate refusal.
        // Intended rather than tolerated: on a starved board a climbing rej
        // IS the signal, and it costs one malloc/free per health tick.
        const bool canAlloc = heaphealth::CanAllocate(heaphealth::TLS_HANDSHAKE_BYTES);
        starveRun = starvation::NextRun(starveRun, canAlloc);
        const bool starved = starvation::IsStarved(starveRun, heaphealth::BallastHeld());
        const unsigned long nowMs = millis();

        if (!starved) {
            if (starvedSinceMs != 0) {
                Serial.printf("[health] ENRICHMENT RECOVERED after %lus (%u recovery attempt(s)); "
                              "cards will fill again\n",
                              (unsigned long)((nowMs - starvedSinceMs) / 1000), (unsigned)starveRecoveries);
                starvedSinceMs = 0;
                starveRecoveries = 0;
            }
        } else {
            if (starvedSinceMs == 0) {
                starvedSinceMs = nowMs;
                lastStarveLogMs = 0;
                starveRecoveries = 0;
                ++starveEpisodes;
            }
            const unsigned long forS = (nowMs - starvedSinceMs) / 1000;

            // Loud on the edge, then every STARVE_LOG_INTERVAL_MS, so a capture
            // attached an hour later still learns the board is in this state
            // rather than having to infer it from ball=0.
            if (lastStarveLogMs == 0 || nowMs - lastStarveLogMs >= STARVE_LOG_INTERVAL_MS) {
                lastStarveLogMs = nowMs;
                // Reports ball= rather than asserting ball=0. A board CAN be
                // starved while holding its ballast -- that is the finding this
                // watch was rewritten around, and hiding it would relearn it.
                // `largest` is deliberately absent: a max across regions that
                // latches onto reserves (HeapHealth.h), so printing it beside a
                // real verdict would invite reading it as the cause.
                Serial.printf("[health] ENRICHMENT STARVED for %lus (%u consecutive "
                              "refusals, ball=%d, reacq-fail=%lu): the heap cannot serve "
                              "%u B for a handshake. Type, operator and photos will NOT "
                              "fill until this clears. See issue #245.\n",
                              forS, (unsigned)starveRun,
                              heaphealth::BallastHeld() ? 1 : 0,
                              (unsigned long)heaphealth::BallastReacquireFailures(),
                              (unsigned)heaphealth::TLS_HANDSHAKE_BYTES);
            }

            // SECOND JOB: try to get out of it. A live mbedTLS session is holding
            // the largest contiguous allocation on the device -- its 16 KB record
            // buffer -- so tearing the session down frees exactly the block the
            // ballast needs. Re-take it BEFORE releasing the bus, or a background
            // fetch re-opens a session in the gap and takes the block back.
            //
            // Non-blocking on the bus by design: if a request is in flight we do
            // nothing and try again on the next health tick. A recovery that
            // blocked the loop task waiting on a network mutex would trade a
            // cosmetic fault for a frozen screen.
            if (forS * 1000UL >= STARVE_RECOVERY_AFTER_MS &&
                (lastStarveRecoveryMs == 0 || nowMs - lastStarveRecoveryMs >= STARVE_RECOVERY_AFTER_MS)) {
                lastStarveRecoveryMs = nowMs;
                if (http.TryAcquireBus()) {
                    ++starveRecoveries;
                    http.ReleaseTlsLocked();
                    heaphealth::ReserveHandshakeBallast();
                    const bool got = heaphealth::BallastHeld();
                    http.ReleaseBus();
                    Serial.printf("[health] STARVE RECOVERY #%u: dropped the TLS session to free "
                                  "its record buffer -> ballast %s (largest now %u)\n",
                                  (unsigned)starveRecoveries, got ? "RETAKEN" : "still lost",
                                  (unsigned)ESP.getMaxAllocHeap());
                } else {
                    Serial.println("[health] STARVE RECOVERY deferred: a request is in flight");
                }
            }
        }
    }

    // Opportunistic re-take of the handshake ballast (fix 4). Deliberately on the
    // health line's slow cadence rather than in the loop: while a TLS session is
    // live the block is legitimately gone, and hammering the allocator to be told
    // so is the exact waste fix 1 just removed from the gate. A failure here is
    // the normal state, not an error -- it is counted, not logged.
    heaphealth::ReserveHandshakeBallast();

#if defined(SOAK_TEST) || defined(FETCH_TRACE)
    // Fetch-pipeline state for the soak record. Added for the 2026-07-09 stall
    // (fetches silent 22 min, loop healthy, task never dequeued): these fields
    // adjudicate loop-side (inFlight/inDetail gate) vs task-side (taskState with
    // a non-empty reqQ = blocked despite queued work) on the next occurrence.
    // taskState: 0=Running 1=Ready 2=Blocked 3=Suspended 4=Deleted.
    //
    // ALSO REACHABLE VIA FETCH_TRACE, which is SOAK_TEST's instrumentation WITHOUT
    // its gesture harness. SOAK_TEST arms SoakHarness (synthetic taps, :3706), so
    // using it to investigate an IDLE board would inject the very variable under
    // test -- the 2026-08-17 fragmentation was first misattributed to tapping, and
    // the flag that would have "observed" it also taps. FETCH_TRACE changes no
    // behaviour: it only opens the three printfs here, at :1611 and at :1844.
    Serial.printf("[soak-state] inFlight=%d inDetail=%d screen=%d reqQ=%u resQ=%u fetchAge=%lus touchIdle=%lus enrich=%d task=%d allocFail=%lu hardFail=%lu\n",
                  (int)fetchInFlight, (int)inDetail, (int)screen,
                  fetchRequestQueue ? (unsigned)uxQueueMessagesWaiting(fetchRequestQueue) : 0,
                  fetchResultQueue ? (unsigned)uxQueueMessagesWaiting(fetchResultQueue) : 0,
                  (now - lastFetch) / 1000UL,
                  (now - lastTouchActivityMs) / 1000UL,
                  (int)enrichInFlight,
                  fetchTaskHandle ? (int)eTaskGetState(fetchTaskHandle) : -1,
                  (unsigned long)allocFailures, (unsigned long)fetchHardFailures);
#endif

    // Frame budget RECALIBRATED 2026-07-10 (was the Phase-0 bench-acceptance
    // 50 ms): the s3-128 ship-config overnight soak measured p95 = 51-53 ms
    // SUSTAINED under full load (25 aircraft, trails, sweep, in-enclosure),
    // grazing the old line 42x in 10 h with zero functional impact -- so 50
    // flagged the renderer's honest steady state, not a regression. 60 ms =
    // measured envelope + margin, per bench ruling. Heap floor unchanged.
    //
    // RE-EXAMINED 2026-08-02, and the re-examination was WRONG. Retracted and
    // rewritten 2026-08-03. The original note here concluded "aircraft count is
    // not the frame lever -- 40 contacts render cheaper than 25". That was an
    // artefact of the measurement, not a property of the renderer.
    //
    // WHAT HAPPENED. The measurement changed location by POSTing to /save from a
    // script. Every checkbox absent from a POST was written as false (correct for
    // a browser, which always submits the whole form), so that POST silently
    // turned OFF the airport overlay, trails, fade and scanline. Every sample
    // after it measured a STRIPPED renderer. The "40 aircraft are cheaper" result
    // compared a full renderer at n=25 against a gutted one at n=40.
    //
    // Once that is accounted for, the numbers agree and there is no mystery:
    //
    //   configuration                          render features   p95
    //   overnight soak at cefe95d, 11.6 h      all on            46.6-48.0 ms
    //   2026-08-02, first ~5 min               all on            41.4-44.7 ms
    //   2026-08-02, everything after           overlay/trails    27.5-31.1 ms
    //                                          /fade/scanline off
    //
    // The ~18 ms "unexplained gap" between the overnight run and the rest was
    // never a gap between two honest measurements. The two all-on windows agree
    // with each other. The lesson generalises: when two runs on one board in one
    // place disagree, establish that they measured the same thing before
    // theorising about what changed -- that is what finally resolved this.
    //
    // WHAT IS ACTUALLY KNOWN about frame cost: the all-on steady state on this
    // board is ~42-48 ms p95, and the difference between the two all-on windows
    // (~3 ms) is not attributed. The load axis is UNTESTED. Aircraft count,
    // airport-overlay size and trail rendering are all still candidates and were
    // never separated, because the one experiment that looked like it separated
    // them was the artefact above. `ap=` is on the health line so the overlay
    // hypothesis can be tested properly; nothing here should be cited as
    // evidence for or against it.
    //
    // RE-BASELINED 2026-08-26, 60 -> 85 ms. "ALL ON" WAS NEVER ALL ON.
    //
    // Everything above is still true and its conclusion was still wrong, for a
    // reason nobody could see from the numbers: every board those measurements
    // came from had the per-aircraft INFO LABELS TURNED OFF.
    //
    // The 2026-08-02 scripted POST is the cause, and it did more damage than the
    // retraction above records. It wrote `false` for every checkbox absent from
    // the body -- and `info-callsign`, `info-speed` and `info-baroalt` were among
    // them. They ship ON (see AIRCRAFT_INFO_FIELDS). The bench boards have been
    // carrying them OFF ever since, so "the honest all-on envelope is 48.0 ms" is
    // an envelope for a renderer missing its primary readout, and the 60 ms
    // budget derived from it inherits the same gap.
    //
    // MEASURED 2026-08-26, paired A/B on one board, one boot, alternating every
    // 30 s health tick so the warm-up drift brackets out (a fresh board climbs
    // ~12 ms over its first six minutes -- comparing arms 30 s apart without
    // pairing is how the 2026-08-02 result went wrong in the first place):
    //
    //     info labels ON vs OFF, 17 pairs, n=14-18:  +8.52 ms mean, +9.00 median
    //
    // They are drawn per aircraft and AircraftLabelBox walks the same fields a
    // second time for layout, so the cost scales with contact count: ~9 ms at
    // n=16 implies roughly double at the n=30-40 the fleet actually runs at.
    // Stock config therefore sits around 60-68 ms where the stripped bench boards
    // sit at 46-48, which was being read as a hardware difference between boards.
    //
    // Also measured, and worth recording because it closes a question this note
    // left open: the AIRPORT OVERLAY costs -0.09 ms over 29 paired ticks at
    // ap=60. It is not the frame lever. `ap=` can stop being a suspect.
    //
    // THE DEFAULTS KEEP THEIR COST. Callsign, ground speed and barometric
    // altitude are the radar's readout; a scope of unlabelled dots is not this
    // product, so the ~9-18 ms is bought deliberately. What changes is the
    // ASSERTION, because a budget calibrated against a stripped renderer flags
    // every stock unit as a regression -- and a warning that fires on every
    // healthy device is one nobody reads by week two. That is exactly how the
    // ntfy early-return survived as long as it did.
    //
    // 85 ms = a ~68 ms stock envelope + the same ~25 % margin the 60 ms figure
    // used. PROVISIONAL: no stock-config soak exists yet. The number that should
    // replace it is a multi-hour run on a board with the shipping defaults
    // intact, at n=30-40 -- not another reading from a bench board whose config
    // froze in August.
    // BLIPS_LIMIT=40 is unaffected by all of this -- it is bounded by heap, not
    // frame time, and that bound was measured on the cloud build with the feed
    // saturated (see the constant).
    //
    // ======================================================================
    // SUPERSEDED 2026-08-29 BY THE SOAK THIS NOTE ASKED FOR. The budget is not
    // a constant; it is a LINE. Frame cost is dominated by contact count, and
    // that is now measured rather than suspected.
    //
    // THE RUN: COM4, shipping env, stock defaults never touched, 48 h
    // continuous, 5137 health samples, zero reboots, touch watchdog clean.
    //
    //     p95 = 35.80 ms + 0.802 ms * aircraft       R2 = 0.985, n = 5137
    //           35.8 at n=0         67.9 at n=40 (BLIPS_LIMIT -- the worst case
    //                                             the firmware can reach)
    //           residuals: p99 +3.1 ms, max +9.4 ms
    //
    // It confirms the projection above FROM THE OTHER DIRECTION. That note
    // predicted a 60-68 ms stock envelope by extrapolating a paired label A/B
    // taken at n=16; the soak measured 67.9 ms at the cap. Two methods that
    // share no measurement, one number. The 85 above was a good provisional
    // guess and is kept below as an absolute ceiling.
    //
    // The "n does not drive p95" claim that survived in the ap= comment further
    // up is now dead in both places: n is the lever, at 0.802 ms per contact.
    //
    // WHY A LINE AND NOT A BIGGER CONSTANT. A flat gate can only be set for the
    // worst case, so it is blind everywhere else. Measured against these 5137
    // samples:
    //
    //     gate                     false fires        what it catches
    //     flat 60 ms (pre-#264)    2512 (48.9 %)      unusable -- this is the
    //                                                 BUDGET BROKEN noise
    //     flat 85 ms (#264)             0             >17 ms, and only at n=40;
    //                                                 at n=0 a 49 ms regression
    //                                                 passes in silence
    //     line + 10 ms                  0             >10 ms at ANY n
    //
    // PROVENANCE, AND THE CONDITION ON THESE CONSTANTS. Fitted 2026-08-29 on a
    // board STARVED FOR 84.9 % OF SAMPLES (largest block median 7668 B; a
    // handshake needs 16717). Enrichment was consequently near-idle: 97.4 % of
    // samples sat at enrich <= 4 % busy. So THE ENRICHMENT AXIS IS UNMEASURED
    // and these numbers describe a board that was mostly not enriching.
    //
    // That starvation is not a fault on that board -- it is the experiment. The
    // soak ran the plain shipping env, which is the CONTROL ARM of the #245 A/B:
    // -DBLIPSCOPE_TLS_PSRAM lives only in env:blipscope-s3-128-tlspsram, so the
    // shipping image routes mbedTLS to the internal heap on purpose while the
    // comparison runs. `tlsmem=0/0/0` across all 5137 samples is the tell, and
    // it is the boring reading of it: the allocator was compiled out, not
    // installed-and-failing. Which board to re-fit on is therefore already
    // decided -- the treatment arm, or the shipping env once #245's fix lands
    // on it. Do not re-fit on another control-arm board and expect a different
    // answer.
    //
    // RE-FIT AFTER #245, AND THAT RE-FIT IS A REQUIRED STEP OF CLOSING #245 --
    // not a follow-up for somebody to remember. A gate calibrated on a sick
    // board starts crying wolf the day the board gets well, and "the warning
    // that fires on every healthy device is one nobody reads by week two" is
    // the exact failure the 60 ms gate above already demonstrated. The health
    // line prints resid= so that re-fit is a data question, not a redesign.
    // ======================================================================
    constexpr float FRAME_P95_MARGIN_MS  = 10.0f;   // > the p99 (+3.1) and max
                                                    // (+9.4) residuals observed
    constexpr float FRAME_P95_CEILING_MS = 85.0f;   // absolute, model-independent
    constexpr uint32_t LARGEST_BLOCK_BUDGET = 20000;
    if (p95Ms > predictedP95Ms + FRAME_P95_MARGIN_MS) {
        budgetBreaches++;
        Serial.printf("[health] BUDGET BROKEN: frame p95 %.1fms > %.1fms (model %.1f at n=%u + margin %.0f)\n",
                      p95Ms, predictedP95Ms + FRAME_P95_MARGIN_MS, predictedP95Ms,
                      (unsigned)trackedAircraft.size(), FRAME_P95_MARGIN_MS);
    }
    // The ceiling is not redundant with the line: the model is only fitted over
    // n=0-40, so anything that makes a frame expensive WITHOUT adding contacts
    // (a new overlay, a full-screen effect, a regression in the sweep) can ride
    // under the line at low n and still be far too slow to look at.
    if (p95Ms > FRAME_P95_CEILING_MS) {
        budgetBreaches++;
        Serial.printf("[health] BUDGET BROKEN: frame p95 %.1fms > absolute ceiling %.0fms\n",
                      p95Ms, FRAME_P95_CEILING_MS);
    }
    if (largest < LARGEST_BLOCK_BUDGET) {
        budgetBreaches++;
        Serial.printf("[health] BUDGET BROKEN: largest block %u < %u\n",
                      (unsigned)largest, (unsigned)LARGEST_BLOCK_BUDGET);
    }
    // A single loop pass over half a second means the loop was blocked -- the
    // overnight-slowdown symptom (touch polled once per pass, so a stalled loop
    // is exactly why taps and card-close went sluggish). Shout it, greppable,
    // with the concurrent heap state so a fragmentation/starvation cause is
    // visible at the moment it bit.
    constexpr float STALL_MS = 500.0f;
    if (maxMs > STALL_MS) {
        Serial.printf("[health] LOOP STALL: worst pass %.1fms (heap free=%u largest=%u)\n",
                      maxMs, (unsigned)heapFree, (unsigned)largest);
    }

    // Touch supervisor counters (the C3): "wedges=0" is the soak's headline number.
    if constexpr (variant::TOUCH_WATCHDOG) {
        const auto& wd = TouchWatchdog::GetStats();
        Serial.printf("[health] touch-wd wedges=%lu recovered=%lu/%lu (soft=%lu hard=%lu) wakes=%lu probes=%lu/%lu maxOutage=%lums rebootRec=%lu\n",
                      (unsigned long)wd.wedges, (unsigned long)wd.recoveries,
                      (unsigned long)wd.recoverAttempts, (unsigned long)wd.softRecoveries,
                      (unsigned long)wd.hardRecoveries, (unsigned long)wd.wakes,
                      (unsigned long)wd.probesOk, (unsigned long)wd.probesFailed,
                      (unsigned long)wd.maxOutageMs, (unsigned long)wd.rebootsRecommended);
    }
}

uint32_t AircraftManager::AllocFailureCount() const { return allocFailures; }
uint32_t AircraftManager::FetchHardFailCount() const { return fetchHardFailures; }

void AircraftManager::StartFetchTask()
{
    if (fetchTaskHandle != nullptr)
        return; // already running; survive Initialise() being re-run on a config reload

    // Depth 1: only ever one fetch outstanding (gated by fetchInFlight), so a single
    // slot for the request and one for the result is enough.
    if (fetchRequestQueue == nullptr) {
        fetchRequestQueue = xQueueCreate(1, sizeof(FetchRequest*));
        fetchResultQueue  = xQueueCreate(1, sizeof(FetchResult*));
        // Outcome-criteria hook: count heap allocation failures device-wide. Fires in
        // the failing task's context; registered once alongside the queues.
        heap_caps_register_failed_alloc_callback(OnAllocFailed);
    }

    // 12 KB stack: the HTTPS handshake (mbedTLS) is the stack-hungry part, plus the
    // JSON decode. The Arduino loop task ran the same workload in 8 KB, so this has
    // headroom. Priority 1 (same as the loop); it spends almost all its life blocked
    // on the request queue.
    //
    // Pin to core 0 -- the WiFi core. On the dual-core S3 that keeps the TLS/JSON work off
    // core 1, where the Arduino loop drives the RGB panel: an unpinned task would otherwise
    // float onto the draw core and add to the frame-timing jitter. (Harmless on the single-core
    // C3, where core 0 is the only core.)
    xTaskCreatePinnedToCore(FetchTaskTrampoline, "osky_fetch", 12288, this, 1, &fetchTaskHandle, 0);
}

void AircraftManager::FetchTaskTrampoline(void* arg)
{
    static_cast<AircraftManager*>(arg)->RunFetchTask();
}

void AircraftManager::RunFetchTask()
{
    for (;;) {
        // block until the loop requests a fetch
        FetchRequest* req = nullptr;
        if (xQueueReceive(fetchRequestQueue, &req, portMAX_DELAY) != pdTRUE || req == nullptr)
            continue;

#if defined(SOAK_TEST) || defined(FETCH_TRACE)
        // Soak diagnostics: bracket every request so a silently-stuck task is
        // localizable from the log (took-the-request vs finished-the-request).
        const unsigned long soakReqStartMs = millis();
        Serial.printf("[fetch] task: req kind=%d @%lu\n", (int)req->kind, soakReqStartMs);
#endif

        FetchResult* res = new FetchResult();
        res->kind = req->kind;

#ifdef FEATURE_CLOUD_FEED
        // Fleet-config fetch: a one-off GET on this same task (same TLS client,
        // same keep-alive connection as the feed), handed back like a feed result.
        if (req->kind == FetchKind::CloudConfig) {
            JsonDocument cfgDoc;
            const HttpResult r = http.GetJson(CloudFeed::ConfigUrl(req->cloudBase), cfgDoc,
                                              std::vector<std::pair<String, String>>{},
                                              CloudFeed::Headers(req->cloudKey, req->otaMem, req->usage));
            if (r.success && r.statusCode >= 200 && r.statusCode < 300 &&
                CloudFeed::ParseConfig(cfgDoc, res->config)) {
                res->ok = true;
            } else {
                Serial.printf("[cloud] config fetch failed: HTTP %d %s\n",
                              r.statusCode, r.errorMessage.c_str());
            }
            if (!r.success && r.statusCode <= 0)
                fetchHardFailures++;
            delete req;
            if (xQueueSend(fetchResultQueue, &res, 0) != pdTRUE)
                delete res;
            continue;
        }

        // Airport-overlay fetch (/api/v1/blipscope/airports): same one-off GET pattern as the
        // config fetch above. The reply is server-capped (<= 60 rows, ~2 KB),
        // so the streaming decode stays trivial on the C3-class heap.
        if (req->kind == FetchKind::Airports) {
            JsonDocument aptDoc;
            const HttpResult r = http.GetJson(
                CloudFeed::AirportsUrl(req->cloudBase), aptDoc,
                { { "lat", String(req->lat, 4) },
                  { "lon", String(req->lon, 4) },
                  { "r", String((int)lround(req->rangeKm)) } },
                CloudFeed::Headers(req->cloudKey));
            if (r.success && r.statusCode >= 200 && r.statusCode < 300 &&
                CloudFeed::ParseAirports(aptDoc, res->airports)) {
                res->ok = true;
            } else {
                Serial.printf("[cloud] airports fetch failed: HTTP %d %s\n",
                              r.statusCode, r.errorMessage.c_str());
            }
            if (!r.success && r.statusCode <= 0)
                fetchHardFailures++;
            delete req;
            if (xQueueSend(fetchResultQueue, &res, 0) != pdTRUE)
                delete res;
            continue;
        }
#endif

        // GET + decode the feed straight from the socket (GetJson streams when possible
        // so the raw body and the parsed document aren't both held at once -- that peak
        // is what starved the heap). The user's local receiver (no params/auth), the
        // Blipscope Cloud proxy (tiny pre-clipped payload), or the OpenSky API bounded
        // to the scan box; all share the one HTTP client.
        JsonDocument doc;
        HttpResult result;
        if (req->local) {
            // Pull only the fields ParseLocalAircraft reads, so a large aircraft.json (many
            // aircraft, ~20 fields each) never fully materializes in the parsed document. The
            // [0] template applies to every element of the "aircraft" array; the top-level
            // "now" and everything else is dropped.
            JsonDocument filter;
            for (const char* k : { "hex", "flight", "lat", "lon", "alt_baro", "alt_geom",
                                   "gs", "track", "true_heading", "mag_heading", "baro_rate",
                                   "geom_rate", "category", "squawk", "type", "seen_pos" })
                filter["aircraft"][0][k] = true;
            result = http.GetJson(req->url, doc, filter);
        }
#ifdef FEATURE_CLOUD_FEED
        else if (req->cloud) {
            // /api/v1/blipscope/blips: the proxy quantizes to its cache tile/bucket, clips,
            // sorts by distance, and caps server-side. Ask for 40 (up from 25) so a
            // wide radius over a busy area fills most of the scope instead of
            // clustering the nearest 25 in the centre. NOT the full MAX_AIRCRAFT
            // (60): each tracked contact carries a 60-point trail + enrichment, and
            // requesting 60 dropped the largest contiguous heap block toward the TLS
            // floor (~28 KB), starving handshakes (hardFail + DATA STALE observed).
            // 40 keeps a comfortable margin while covering the useful picture.
            //
            // CONFIRMED 2026-08-02 against a tile that actually saturates it (LA
            // basin, count pinned at 40 for 14 min): largest contiguous block held
            // at 52,212 B with allocFail=0 -- the heap headroom this bound exists
            // to protect is intact at 40 -- which is the bound that matters here,
            // since this limit exists for heap and not for frame time.
            // (An earlier version of this note also claimed 40 contacts render
            // cheaper than 25. That was a measurement artefact and is retracted;
            // see FRAME_P95_BUDGET_MS. The heap result above is unaffected -- it
            // was taken on the cloud build with the feed genuinely saturated.)
            constexpr int BLIPS_LIMIT = 40;
            static_assert(BLIPS_LIMIT <= (int)MAX_AIRCRAFT, "blips limit must fit the tracked cap");
            result = http.GetJson(
                CloudFeed::BlipsUrl(req->cloudBase), doc,
                { { "lat", String(req->lat, 4) },
                  { "lon", String(req->lon, 4) },
                  { "r", String((int)lround(req->rangeKm)) },
                  { "limit", String(BLIPS_LIMIT) } },
                CloudFeed::Headers(req->cloudKey, req->otaMem, req->usage));
        }
#endif
        else {
            std::vector<std::pair<String, String>> headers = {};
            if (!req->token.isEmpty()) headers.push_back({ "Authorization", "Bearer " + req->token });

            result = http.GetJson(
                "https://opensky-network.org/api/states/all",
                doc,
                {
                  // 6 decimals (~0.1 m): String(double) defaults to only 2, which would
                  // quantize small km/mi radii into a coarse ~1 km box or collapse it
                  {"lamin", String(req->lat - req->radLat, 6)},
                  {"lamax", String(req->lat + req->radLat, 6)},
                  {"lomin", String(req->lon - req->radLon, 6)},
                  {"lomax", String(req->lon + req->radLon, 6)},
                  // category (state vector index 17) is omitted from the default
                  // response; without extended=1 the array stops at index 16 and the
                  // Category info line is always blank
                  {"extended", "1"}
                },
                headers
            );
        }

        const char* sourceName = req->local ? "Local ADS-B" : "OpenSky";
        bool isOpenSky = !req->local;
#ifdef FEATURE_CLOUD_FEED
        if (req->cloud) { sourceName = "Blipscope Cloud"; isOpenSky = false; }
#endif
        if (!result.success) {
            Serial.printf("[WARN] %s request failed: %s\n", sourceName, result.errorMessage.c_str());
        } else if (result.statusCode < 200 || result.statusCode >= 300) {
            // A JSON-bodied error page (OpenSky 401/429/5xx, a proxy, a captive
            // portal) parses fine but is NOT "zero aircraft in the box": treating
            // it as data would wipe every tracked contact, then re-fire alerts and
            // inflate the logbook when the feed recovers. Keep the last picture.
            // The cloud proxy's fast 503s ("warming": it is filling its cache in
            // the background) land here too -- the next poll hits the warm tile.
            Serial.printf("[WARN] %s returned HTTP %d; keeping current picture\n", sourceName, result.statusCode);
            // 401/403 means the cached bearer token is bad (expired server-side);
            // flag it so the loop drops the cache and the next cycle re-auths.
            // (OpenSky only: a cloud 401 is a key mismatch -- retrying can't fix it,
            // and it must not invalidate the unrelated OpenSky token cache.)
            const bool rejected = (result.statusCode == 401 || result.statusCode == 403);
            res->authFailed = isOpenSky && rejected;
#ifdef FEATURE_CLOUD_FEED
            // The cloud counterpart, and it is a DIFFERENT KIND of problem: retrying
            // cannot fix it, so unlike every other failure in this branch it will not
            // clear on its own. Recorded here and debounced on the loop task.
            //
            // ONLY 401/403. Not !result.success (a network drop), not 5xx, not the
            // proxy's fast 503 "warming" -- all of those land in this same branch and
            // all of them recover by themselves. Latching on any of them would put a
            // "needs re-verifying" message on a board that needs nothing, which is
            // worse than saying nothing at all.
            if (req->cloud) res->cloudAuthRejected = rejected;
#endif
        }
#ifdef FEATURE_CLOUD_FEED
        else if (req->cloud) {
            if (CloudFeed::ParseBlips(doc, res->aircraft, res->dataEpoch))
                res->ok = true;
            else // schema-version mismatch or malformed body: never wipe the picture over it
                Serial.println("[WARN] Blipscope Cloud: blips schema mismatch; keeping current picture");
        }
#endif
        else if (req->local) {
            // dump1090/readsb returns objects under "aircraft"; convert each and
            // clip to the scan box ourselves (OpenSky does this server-side, but a
            // local receiver reports everything it hears).
            for (JsonVariantConst entry : doc["aircraft"].as<JsonArrayConst>()) {
                Aircraft ac;
                if (!JsonParser::ParseLocalAircraft(entry, ac))
                    continue;
                if (ac.latitude  < req->lat - req->radLat || ac.latitude  > req->lat + req->radLat ||
                    ac.longitude < req->lon - req->radLon || ac.longitude > req->lon + req->radLon)
                    continue;
                res->aircraft.push_back(ac);
            }
            res->ok = true;
        } else {
            res->aircraft = JsonParser::ParseArray<Aircraft>(doc["states"]);
            res->ok = true;
        }

        // Cap to the nearest N to the device (BOTH sources) so a busy feed can't flood RAM or the
        // render loop. nth_element partitions in O(n) -- cheaper than a full sort, and we only need
        // "the closest N", not them ordered. Distance is a cheap planar metric (longitude scaled by
        // cos(lat)); exact great-circle isn't needed to rank neighbours.
        // Before the cut: what the feed actually sent us. See FetchResult.
        res->receivedCount = res->aircraft.size();
        res->bodyBytes = result.bodyBytes;
        res->parseMs = result.parseMs;
        res->requestMs = result.requestMs;
        res->cacheState = result.cacheState;
        res->upstream = result.upstream;

        if (res->ok && res->aircraft.size() > MAX_AIRCRAFT) {
            const double clat = cos(req->lat * DEG_TO_RAD);
            std::nth_element(res->aircraft.begin(), res->aircraft.begin() + MAX_AIRCRAFT,
                             res->aircraft.end(), [&](const Aircraft& a, const Aircraft& b) {
                const double ax = (a.longitude - req->lon) * clat, ay = a.latitude - req->lat;
                const double bx = (b.longitude - req->lon) * clat, by = b.latitude - req->lat;
                return (ax * ax + ay * ay) < (bx * bx + by * by);
            });
            res->aircraft.resize(MAX_AIRCRAFT);
        }

        // Heap health check right after the decode -- the cycle's low point, the same
        // pressure TLS handshakes and the config web server fight for. Stay silent when
        // healthy; warn only when a handshake-sized block can no longer be served --
        // the early sign we're sliding back toward the TLS / config-page failures this
        // all fixed. This used to test largest against a fixed floor, which on this
        // board never once became true in 54 h (#163); largest is still PRINTED so the
        // plateau stays visible, but it no longer decides anything.
        if (const uint32_t largest = ESP.getMaxAllocHeap(); !heaphealth::CanHandshake())
            Serial.printf("[fetch] LOW HEAP after %s: free=%u largest=%u aircraft=%u\n",
                          sourceName, (unsigned)ESP.getFreeHeap(), (unsigned)largest, (unsigned)res->aircraft.size());

        delete req;

        if (!result.success && result.statusCode <= 0)
            fetchHardFailures++; // hard network class; an upstream 503 is not counted

#if defined(SOAK_TEST) || defined(FETCH_TRACE)
        Serial.printf("[fetch] task: done ok=%d http=%d reuse=%d in %lums\n",
                      (int)res->ok, result.statusCode, (int)result.reusedConnection,
                      millis() - soakReqStartMs);
#endif

        // hand the result back; the loop consumed the previous one before requesting
        // again, so the depth-1 queue always has room
        if (xQueueSend(fetchResultQueue, &res, 0) != pdTRUE)
            delete res; // unreachable in practice; just don't leak if it ever happens
    }
}

void AircraftManager::RequestFetch()
{
    FetchRequest* req = new FetchRequest();
    req->lat = lat; req->lon = lon; req->radLat = radLat; req->radLon = radLon;
    req->local = useLocalSource;

#ifdef FEATURE_CLOUD_FEED
    if (useCloudSource) {
        // Proxy fetch: no token dance, just the snapshot of base/key/radius. Skip
        // if no base URL resolved (no build flag and no config value) -- an
        // unconfigured device must never open a socket.
        if (cloudUrl.isEmpty()) {
            delete req;
            return;
        }
        req->cloud = true;
        req->cloudBase = cloudUrl;
        req->cloudKey = cloudKey;
        req->rangeKm = rangeKmCfg;
        req->otaMem = TakeOtaMemReport(); // "" unless an OTA happened; clears on read
        req->usage  = usageStore.Take(millis()); // "" unless an hour has passed
    } else
#endif
    if (useLocalSource) {
        // local receiver: no auth, no token lookup. Skip entirely if no URL is set
        // yet (local selected but the field left blank) rather than GET an empty URL.
        if (localUrl.isEmpty()) {
            delete req;
            return;
        }
        req->url = localUrl;
    } else {
        // Token lookup is normally instant (cached); it only blocks the loop on the rare
        // ~29-minute refresh, so it stays here rather than racing the fetch task's client.
        req->token = authHandler.GetValidToken(
            configServer.GetStoredString("opensky-id"),
            configServer.GetStoredString("opensky-secret")
        );
    }

    if (xQueueSend(fetchRequestQueue, &req, 0) == pdTRUE) {
        fetchInFlight = true;
    } else {
        // "Shouldn't happen: we only request when !fetchInFlight" was the old
        // comment here, and it may well be true -- but it was never observable.
        // The feed poll self-heals (the next interval retries), so this line is
        // for diagnosis, not recovery.
        Serial.println("[feed] fetch request DROPPED: queue full");
        delete req;
    }
}

#ifdef FEATURE_CLOUD_FEED
bool AircraftManager::RequestCloudConfig()
{
    if (cloudUrl.isEmpty())
        return false;
    FetchRequest* req = new FetchRequest();
    req->kind = FetchKind::CloudConfig;
    req->cloudBase = cloudUrl;
    req->cloudKey = cloudKey;
    // Whichever check-in is built first after an OTA carries the report; the
    // config fetch is normally it (boot runs it ahead of the first feed poll).
    req->otaMem = TakeOtaMemReport();
    // NOT req->usage. The config fetch runs at boot and on every config reload,
    // which is exactly when a bench session reloads it repeatedly -- and the
    // usage take COMMITS its delta, so a report riding this request would be
    // taken (and its counts consumed) at a moment that has nothing to do with
    // the hourly cadence. The feed poll is the periodic carrier; this one is not.
    if (xQueueSend(fetchRequestQueue, &req, 0) == pdTRUE) {
        fetchInFlight = true;
        return true;
    }
    // A drop here used to be COMPLETELY SILENT, which is why the 2026-08-02
    // bench observation could not be diagnosed: config and airport refetches
    // both stopped landing for 10+ min after repeated config saves, with no
    // evidence of where they went. Everything else on this path logs.
    Serial.println("[cloud] config request DROPPED: fetch queue full");
    delete req;
    return false;
}

bool AircraftManager::RequestCloudAirports()
{
    if (cloudUrl.isEmpty())
        return false;
    FetchRequest* req = new FetchRequest();
    req->kind = FetchKind::Airports;
    req->cloudBase = cloudUrl;
    req->cloudKey = cloudKey;
    // The configured radar radius, not the current zoom: one fetch covers
    // every zoom level, and DrawAirports culls to the visible scan box.
    req->lat = lat;
    req->lon = lon;
    req->rangeKm = rangeKmCfg;
    if (xQueueSend(fetchRequestQueue, &req, 0) == pdTRUE) {
        fetchInFlight = true;
        return true;
    }
    Serial.println("[cloud] airports request DROPPED: fetch queue full");
    delete req;
    return false;
}
#endif

void AircraftManager::ConsumeFetchResult()
{
    if (fetchResultQueue == nullptr)
        return;

    FetchResult* res = nullptr;
    if (xQueueReceive(fetchResultQueue, &res, 0) != pdTRUE)
        return; // nothing ready

    fetchInFlight = false;

#ifdef FEATURE_CLOUD_FEED
    if (res->kind == FetchKind::CloudConfig) {
        if (res->ok) {
            cloudCfg = res->config;
            cloudCfgEverApplied = true;
            Serial.printf("[cloud] config rev=%d: poll %lu/%lu/%lu ms idleAfter=%lus stale=x%d enrich=%d minFw=%d\n",
                          cloudCfg.rev, cloudCfg.pollActiveMs, cloudCfg.pollIdleMs, cloudCfg.pollNightMs,
                          cloudCfg.idleAfterMs / 1000, cloudCfg.staleFactor, (int)cloudCfg.enrich, cloudCfg.minFw);
            // The fleet raised the firmware floor past this build: run the normal
            // OTA check now (main.cpp consumes the flag) instead of waiting for
            // the daily timer.
            if (cloudCfg.minFw > FW_VERSION)
                otaCheckRequested = true;
        } else {
            // Retry in 15 min rather than tomorrow; the defaults keep serving.
            lastCloudCfgFetch = millis() - (24UL * 60UL * 60UL * 1000UL) + (15UL * 60UL * 1000UL);
        }
        delete res;
        return;
    }

    if (res->kind == FetchKind::Airports) {
        if (res->ok) {
            cloudAirports = std::move(res->airports);
            cloudAirportsRetryMs = 0; // back to the default rule
            Serial.printf("[cloud] airports: %u within %d km\n",
                          (unsigned)cloudAirports.size(), (int)lround(rangeKmCfg));
        } else {
            // Retry in 15 min; the baked majors table keeps serving meanwhile.
            // An explicit interval, not a rewound timestamp: the old trick
            // assumed the due time was always a fixed 24 h, so it would have
            // collapsed to a per-loop retry against the 5 min empty-overlay rule.
            cloudAirportsRetryMs = 15UL * 60UL * 1000UL;
            Serial.println("[cloud] airports fetch failed; retry in 15 min (baked majors serving)");
        }
        delete res;
        return;
    }
#endif

    // OpenSky rejected the bearer token: drop the cache (on the loop task, which
    // owns the handler for the radar) so the next fetch cycle re-authenticates
    // instead of 401-ing until the local 29-min timer happens to lapse.
    if (res->authFailed)
        authHandler.Invalidate();

#ifdef FEATURE_CLOUD_FEED
    // DEBOUNCE for the credential-rejection latch, and BOTH thresholds are
    // load-bearing. Either one alone is wrong in a way that reaches a customer:
    //
    //   count alone -- at the 5 s active cadence five consecutive failures is
    //                  twenty-five seconds, which a redeploy or a brief edge blip
    //                  can produce. The board would announce it needs re-verifying
    //                  and then silently be fine, teaching the owner to ignore it.
    //   time alone  -- a single 401 that happens to straddle the window latches,
    //                  and a lone spurious rejection is exactly what a rolling
    //                  deploy looks like from the device.
    //
    // Requiring both means the key has been refused repeatedly AND for long enough
    // that no transient explains it. A single success clears everything: recovery
    // must be instant and unconditional, because the fix (a re-verify) is only
    // observable to the customer through this indicator going away.
    if (res->kind == FetchKind::Feed && useCloudSource) {
        constexpr uint8_t   REVERIFY_MIN_STREAK = 5;
        constexpr unsigned long REVERIFY_MIN_MS = 15UL * 60UL * 1000UL; // 15 min

        if (res->cloudAuthRejected) {
            const unsigned long now = millis();
            if (cloudAuthFailStreak == 0) cloudAuthFirstFailMs = now;
            if (cloudAuthFailStreak < 255) cloudAuthFailStreak++;
            const bool longEnough = (now - cloudAuthFirstFailMs) >= REVERIFY_MIN_MS;
            if (!cloudAuthLatched && cloudAuthFailStreak >= REVERIFY_MIN_STREAK && longEnough) {
                cloudAuthLatched = true;
                Serial.printf("[cloud] KEY REFUSED for %lus over %u fetches -- this board "
                              "needs re-verifying\n",
                              (now - cloudAuthFirstFailMs) / 1000UL, (unsigned)cloudAuthFailStreak);
            }
        } else if (res->ok) {
            // Any good fetch is proof the credential works. Clear unconditionally,
            // including the latch -- a re-verified board must return to normal
            // without a reboot, since "reboot it" is not an instruction we want to
            // be giving a customer who has just fixed the actual problem.
            if (cloudAuthLatched)
                Serial.println("[cloud] key accepted again -- clearing the re-verify state");
            cloudAuthFailStreak = 0;
            cloudAuthFirstFailMs = 0;
            cloudAuthLatched = false;
        }
        // NOTE the gap: a failure that is NOT a 401/403 (network drop, 503 warming,
        // parse failure) neither advances nor clears the streak. It is not evidence
        // either way, and treating it as a clear would let one lucky reconnect reset
        // a genuinely refused board back to silence.
    }
#endif

    if (res->ok) {
        const unsigned long now = millis();

        // Staleness anchor for the indicator: when this merge happened, plus how
        // old the server said the snapshot already was (cloud tiles served
        // stale-while-revalidate keep their original t; other sources have no
        // server-side lag to account for).
        // MEASUREMENT: the gap since the last good merge is time WE were not
        // polling (contention); dataLagAtMergeMs below is age the snapshot already
        // carried (upstream drought). Captured here, before lastGoodDataMs moves.
        if (lastGoodDataMs != 0) {
            const unsigned long gap = now - lastGoodDataMs;
            if (gap > perf.gapMaxMs) perf.gapMaxMs = gap;
        }
        lastGoodDataMs = now;
        dataLagAtMergeMs = 0;
#ifdef FEATURE_CLOUD_FEED
        if (res->dataEpoch > 0) {
            const time_t nowEpoch = time(nullptr);
            if (nowEpoch > 1600000000 && (long)nowEpoch > res->dataEpoch)
                dataLagAtMergeMs = (unsigned long)((long)nowEpoch - res->dataEpoch) * 1000UL;
        }
#endif
        perf.polls++;
        perf.fetchBusyMs += res->requestMs;
        perf.parseMs += res->parseMs;
        perf.bodyBytes += res->bodyBytes;
        perf.acReceived += (uint32_t)res->receivedCount;
        perf.acKept += (uint32_t)res->aircraft.size();
        perf.lagSumMs += dataLagAtMergeMs;
        if (dataLagAtMergeMs > perf.lagMaxMs) perf.lagMaxMs = dataLagAtMergeMs;
        if (res->cacheState == "HIT")        perf.cacheHit++;
        else if (res->cacheState == "STALE") perf.cacheStale++;
        else if (res->cacheState == "MISS")  perf.cacheMiss++;

        // TODAY counters: roll over at local midnight, then attribute fresh
        // contacts to their local hour. NTP-gated (no clock = no attribution).
        int localHour = -1;
        {
            const time_t utcNow = time(nullptr);
            if (utcNow > 1600000000) {
                const time_t localT = utcNow + utcOffsetSec;
                const uint32_t localDay = (uint32_t)(localT / 86400);
                localHour = (int)((localT / 3600) % 24);
                if (localDay != statsDayLocal) {
                    statsDayLocal = localDay;
                    todayContacts = 0;
                    todayPeak = 0;
                    memset(todayHourCounts, 0, sizeof(todayHourCounts));
                    aotdScore = 0;
                    aotdCallsign = ""; aotdLabel = ""; aotdReason = "";
                }
            }
        }

        for (auto& ac : res->aircraft) {
            // Country of registration, when the feed did not supply one. OpenSky
            // sends it in state[2]; the cloud feed and a local dump1090/readsb do
            // not, which left the highest-scoring logbook category (25 pts each)
            // reading "0 of 0 countries" forever on the DEFAULT feed -- not merely
            // unpopulated but unwinnable. Derived from the ICAO address block that
            // is already on the wire, so it costs no extra bytes on a poll that is
            // near its payload ceiling.
            //
            // Fallback ONLY, and deliberately before the emplace so the detail
            // card, the logbook, and the claim all see the same value: a feed that
            // does send a country stays authoritative, so an existing OpenSky
            // lifelist keeps the exact strings it was built with rather than
            // gaining near-duplicates under our shorter names.
            if (ac.originCountry.isEmpty())
                ac.originCountry = IcaoCountry::Lookup(ac.icao24);

            auto it = trackedAircraft.find(ac.icao24);
            if (it == trackedAircraft.end()) {
                auto emplaced = trackedAircraft.emplace(ac.icao24, TrackedAircraft{ ac, now });
                // a fresh contact entered range: bump the odometer and log its
                // origin country (now known on every feed -- see the fallback above).
                if (logbookEnabled) {
                    logbook.NoteContact();
                    logbook.NoteCountry(ac.originCountry);
                }
                if (localHour >= 0) {
                    ++todayContacts;
                    if (todayHourCounts[localHour] < 0xFFFF) ++todayHourCounts[localHour];
                }
                // tone on genuinely new arrivals (HAS_AUDIO boards), but not during the
                // initial bulk population -- that would be a burst of beeps on first sync.
                // Class-distinct patterns: military > watchlist > the generic chirp.
                // Emergency squawkers are skipped here -- UpdateVisualAlerts' per-contact
                // edge fires the (stronger) emergency pattern for them next frame.
                if constexpr (variant::HAS_AUDIO) {
                    if (initialSyncDone && !isEmergencySquawk(ac.squawk)) {
                        const WatchClass wc = ClassifyWatchlist(emplaced.first->second);
                        if (showMilitary && SpecialAircraft::IsMilitary(ac.icao24))
                            PlayTone(2, 70, 120);
                        else if (wc == WatchClass::Specific)
                            PlayTone(3, 55, 70);  // your specific aircraft: an urgent triple
                        else if (wc == WatchClass::Category)
                            PlayTone(2, 40, 80);  // a watched type: the double
                        else
                            PlayTone(1, 40, 0);   // any new contact: a single chirp
                    }
                }
            } else {
                it->second.Update(ac, now);
            }
        }

        // Follow Mode: sample the followed aircraft's position into the track.
        // After the merge, so it sees this poll's fix; on the loop task, like
        // every other mutation of shared state.
        UpdateFollowTrack();

        // offer every airborne contact's measurements to the lifetime records
        // (highest / fastest / closest ever). Feed cadence, not frame cadence,
        // so it's a handful of compares per poll. Plausibility bounds keep a
        // feed glitch from becoming a permanent record (0 = "don't offer").
        if (logbookEnabled) {
            const float clat = cosf(radians((float)lat));
            for (auto& ac : res->aircraft) {
                if (ac.onGround) continue;
                const float altFt = ac.baroAltitude * METRES_TO_FEET;
                const float spdKt = ac.velocity * MS_TO_KNOTS;
                const float dLa = ac.latitude - (float)lat;
                const float dLo = (ac.longitude - (float)lon) * clat;
                const float distKm = sqrtf(dLa * dLa + dLo * dLo) * 111.0f;
                String cs = ac.callsign;
                cs.trim();
                if (cs.isEmpty()) { cs = ac.icao24; cs.toUpperCase(); }
                logbook.NoteBest(cs,
                                 (altFt > 0.0f && altFt <= 60000.0f) ? altFt : 0.0f,
                                 (spdKt > 0.0f && spdKt <= 1200.0f) ? spdKt : 0.0f,
                                 (distKm >= 0.05f) ? distKm : 0.0f);
            }
        }

        // the first successful fetch is the baseline; arrivals after it are "new"
        initialSyncDone = true;

        // Evict planes that have been ABSENT past a grace window, not on the first
        // fetch they're missing from. OpenSky routinely drops a state vector for a
        // poll (spotty community coverage), and the local feed's box-edge clip makes
        // fringe contacts flap at 1 Hz. Erasing on the first miss reconstructs a fresh
        // TrackedAircraft on re-appearance -- re-firing flyover/overhead ntfy alerts,
        // re-bumping the logbook odometer, re-chirping, and discarding the trail +
        // adsbdb enrichment. lastSeen (updated in TrackedAircraft::Update) survives a
        // miss, so keep the entry for ~2 poll intervals (bounded 5-30 s; the cadence
        // machine's current interval in cloud mode, so idle/night cadences don't
        // evict everything between polls -- the 30 s ceiling still applies).
        const unsigned long graceMs = constrain(2UL * CurrentPollIntervalMs(), 5000UL, 30000UL);
        for (auto it = trackedAircraft.begin(); it != trackedAircraft.end(); ) {
            const bool present = std::any_of(res->aircraft.begin(), res->aircraft.end(),
                [&](const Aircraft& ac) { return ac.icao24 == it->first; });
            if (!present && (now - it->second.lastSeen) > graceMs)
                it = trackedAircraft.erase(it);
            else
                ++it;
        }

        // TODAY peak: the most simultaneous airborne contacts seen since midnight
        if (localHour >= 0) {
            uint16_t airborne = 0;
            for (const auto& [icao, t] : trackedAircraft)
                if (!t.state.onGround) ++airborne;
            if (airborne > todayPeak) todayPeak = airborne;
        }
    }

    delete res;
}

void AircraftManager::Draw(BandCanvas& backbuffer, bool firstPass)
{
    // the detail card overlays whichever screen is active; fall back to it if the
    // selected aircraft has since dropped out of the feed
    if (inDetail) {
        auto it = trackedAircraft.find(selectedIcao);
        if (it != trackedAircraft.end()) {
            DrawDetailCard(backbuffer, it->second);
            DrawVisualAlert(backbuffer); // the edge ring stays visible around the card
            DrawRankToast(backbuffer);   // a rank-up toast shows over the card too
            return;
        }
        ExitDetail(); // selected aircraft left the feed (idempotent across band passes)
    }

    // The reset menu overlays everything, including the detail card check above
    // -- it is opened from Stats and nothing else may cover it. Drawn before the
    // screen switch so an early return keeps the menu the only thing on glass:
    // a destructive confirmation competing with a radar sweep for the customer's
    // attention is a confirmation they will misread.
    if (resetMenu != ResetMenu::Closed) {
        DrawResetMenu(backbuffer);
        return;
    }

    // A follow target cleared while the Follow screen was showing leaves the
    // customer on a screen that no longer exists. Snap back rather than draw an
    // empty face -- §13.3's "hidden entirely" has to mean hidden from every
    // route in, including the one they were already standing on.
    if (screen == Screen::Follow && !FollowScreenVisible())
        screen = Screen::Radar;   // NOT EnterScreen: the screen was taken away,
                                  // nobody navigated to Radar. See EnterScreen.

    switch (screen) {
        case Screen::List:   DrawList(backbuffer);   break;
        case Screen::Stats:  DrawStats(backbuffer);  break;
        case Screen::Follow: DrawFollow(backbuffer); break;
        case Screen::Radar:
        default:
            // NO LOCATION -> say so, ahead of everything else. A radar centred on
            // nowhere draws an empty scope that looks exactly like "no aircraft
            // nearby", and a customer cannot tell the difference between a device
            // that is working and one that has never been told where it is.
            //
            // First thing in the chain deliberately: this state outranks the night
            // clock, because a clock on an unconfigured device is a device that
            // looks finished and is not.
            if (!hasLocation) DrawNoLocation(backbuffer);
            // at solar night with an empty sky, the radar face becomes a clock
            // (opt-in) -- the device stays useful instead of showing a dead scope
            else if (NightClockActive()) DrawNightClock(backbuffer);
            else                         DrawRadar(backbuffer, firstPass);
            break;
    }
    DrawScreenIndicator(backbuffer);
    DrawClock(backbuffer);
    DrawVisualAlert(backbuffer); // military/emergency ring pulse / flash, over any screen
    DrawRankToast(backbuffer);   // transient "RANK UP" banner after a leaderboard climb
    DrawClaimToast(backbuffer);  // transient "CLAIMED <type> #N" after a tap-to-claim
}

SpecialAircraft::Class AircraftManager::SpecialClassOf(const TrackedAircraft& tracked) const
{
    // priority order matches SpecialAircraft::Class: military first, then a
    // distinctive callsign, then a plain rotorcraft. Each gated by its toggle.
    if (showMilitary && SpecialAircraft::IsMilitary(tracked.state.icao24))
        return SpecialAircraft::Class::Military;
    if (showSpecial && SpecialAircraft::IsSpecialCallsign(tracked.state.callsign))
        return SpecialAircraft::Class::Special;
    if (showHelicopters && SpecialAircraft::IsHelicopter(tracked.state.category))
        return SpecialAircraft::Class::Helicopter;
    return SpecialAircraft::Class::None;
}

uint32_t AircraftManager::SpecialColor(SpecialAircraft::Class c)
{
    switch (c) {
        case SpecialAircraft::Class::Military:   return lgfx::color888(255, 120, 0);  // orange (redder than watchlist amber)
        case SpecialAircraft::Class::Special:    return lgfx::color888(80, 170, 255);  // blue
        case SpecialAircraft::Class::Helicopter: return lgfx::color888(190, 110, 255); // violet
        default:                                  return lgfx::color888(0, 200, 0);
    }
}

void AircraftManager::AdvanceSweep()
{
    // Always advance the beam (even off-radar) so it stays continuous when the
    // user returns to the radar screen.
    prevSweepAngle = sweepAngle;
    sweepAngle = std::fmod((float)millis() / SWEEP_PERIOD_MS * TWO_PI, TWO_PI);

    // Skip the per-contact crossing test unless paint-and-fade is active (blips
    // then glide live at full brightness, with or without the beam drawn).
    if (!PaintAndFadeActive())
        return;

    // The beam advanced from prevSweepAngle to sweepAngle this frame -- a small
    // positive arc (mod 2*PI). Any contact whose bearing from centre lies in that
    // arc was just swept: latch its position and reset its fade. Bearings use the
    // same centre and cos/sin convention as the drawn beam, so the paint lands
    // exactly under the beam.
    constexpr int CENTRE = SCREEN_SIZE_DIV_2 - 1;
    float arc = sweepAngle - prevSweepAngle;
    if (arc < 0.0f) arc += TWO_PI; // beam wrapped past 0 / 2*PI this frame

    for (auto& [icao, t] : trackedAircraft) {
        if (t.state.onGround) continue;

        auto [la, lo] = t.GetDisplayPosition();
        auto [x, y] = ProjectCoordinateToScreen(la, lo);

        float rel = std::atan2((float)(y - CENTRE), (float)(x - CENTRE)) - prevSweepAngle;
        while (rel < 0.0f) rel += TWO_PI;
        while (rel >= TWO_PI) rel -= TWO_PI;

        if (rel <= arc) {
            t.Paint();
            if (displayTrails)
                t.SampleTrail(); // trail samples a return per pass, not per frame
        }
    }
}

std::pair<float, float> AircraftManager::RadarBlipPosition(const TrackedAircraft& tracked) const
{
    if (PaintAndFadeActive() && tracked.everPainted)
        return { tracked.paintLat, tracked.paintLon };
    return tracked.GetDisplayPosition();
}

float AircraftManager::RadarBlipBrightness(const TrackedAircraft& tracked) const
{
    return PaintAndFadeActive() ? tracked.PaintBrightness(SWEEP_PERIOD_MS) : 1.0f;
}

void AircraftManager::DrawRadar(BandCanvas& backbuffer, bool firstPass)
{
    DrawRadarCircles(backbuffer);
    DrawStaleIndicator(backbuffer); // escalating banner: STALE -> STALE <age> -> NO DATA <age>

    // Hoisted out of the per-aircraft loop below: one cheap age comparison per
    // frame instead of one per contact (up to MAX_AIRCRAFT of them), so the ladder
    // costs the render path effectively nothing.
    const bool noData = CurrentStaleStage() == StaleStage::NoData;

    // fixed geography under the moving traffic: airports ground the picture
    // ("that blip is landing at OUR airport")
    if (displayAirports)
        DrawAirports(backbuffer);

    // Follow Mode: the flight track, drawn BENEATH the traffic and above the
    // fixed geography. §4.5 -- "draw the track beneath everything else, in one
    // dim distinct colour", and exempt from the sweep's phosphor fade, because a
    // radar return decays for a reason the track does not share: a return is a
    // return, the track is a record.
    DrawFollowTrack(backbuffer);
    DrawFollowRouteStrip(backbuffer);

    // identify the "of interest" contacts to ring: nearest, highest, fastest
    String nearestIcao, highestIcao, fastestIcao;
    if (displayHighlight) {
        // 1 deg of longitude is 111 km * cos(lat), so the longitude delta must be
        // scaled by cos(lat) before ranking -- otherwise a plane due east ranks
        // farther than a nearer one due north (matches IsOverhead/DrawDetailCard).
        const float clat = cosf(radians((float)lat));
        float minDist2 = 1e30f, maxAlt = -1e30f, maxVel = -1e30f;
        for (auto& [icao, t] : trackedAircraft) {
            if (t.state.onGround) continue;
            auto [la, lo] = t.GetDisplayPosition();
            const float dLat = la - (float)lat, dLon = (lo - (float)lon) * clat;
            const float d2 = dLat * dLat + dLon * dLon;
            if (d2 < minDist2)                 { minDist2 = d2; nearestIcao = icao; }
            if (t.state.baroAltitude > maxAlt) { maxAlt = t.state.baroAltitude; highestIcao = icao; }
            if (t.state.velocity > maxVel)     { maxVel = t.state.velocity; fastestIcao = icao; }
        }
    }

    for (auto& [icao, tracked] : trackedAircraft) {
        if (tracked.state.onGround) continue;

        // The scene is rendered once per band; advance per-frame animation/trail state
        // only on the first band so both halves use identical positions (no seam tear).
        if (firstPass) {
            tracked.Tick();
            // With paint-and-fade on, the trail is sampled once per beam pass (in
            // AdvanceSweep); only sample per-frame when blips glide live.
            if (displayTrails && !PaintAndFadeActive())
                tracked.SampleTrail();
        }

        auto [predLat, predLon] = RadarBlipPosition(tracked);
        auto [x, y] = ProjectCoordinateToScreen(predLat, predLon);

        // The whole contact -- marker, trail, label -- fades together as its
        // radar return ages, so a dim blip doesn't sit under a bright trail/label.
        const float blip = RadarBlipBrightness(tracked);

        // draw the trail first so the marker and label sit on top of it
        if (displayTrails)
            DrawAircraftTrail(backbuffer, tracked, x, y, blip);

        if (displayInfoText)
            DrawAircraftInfo(backbuffer, x, y, tracked, blip);

        if (isEmergencySquawk(tracked.state.squawk)) {
            DrawEmergencyAlert(backbuffer, x, y, tracked);
        } else {
            // base marker fades as its radar return ages between sweep passes
            // (full bright when the sweep is off). The annotation overlays below
            // -- highlight/watchlist/pin reticles, NEW flag -- stay full bright so
            // they remain legible regardless of the fade.
            // At NoData the contacts are no longer a picture of anything -- dead
            // reckoning has capped and they are frozen wherever they last were. Draw
            // them dim grey rather than live green (or an altitude colour, which
            // would imply data we do not have). Deliberately greyed, NOT cleared:
            // the trails and enrichment survive, so recovery is instant and the
            // scope visibly snaps back to colour the moment a merge lands.
            const uint32_t baseColor =
                noData ? lgfx::color888(90, 90, 90)
                       : (displayAltColor ? altitudeColor(tracked.state.baroAltitude)
                                          : lgfx::color888(0, 255, 0));
            const uint32_t markerColor = scaleColor(baseColor, blip);

            if (displayTriangles)
                DrawAircraftTriangle(backbuffer, x, y, tracked, markerColor);
            else
                backbuffer.fillCircle(x, y, 3, markerColor);
        }

        // special contact (military / special callsign / helicopter): a coloured
        // diamond reticle + tag. All detected offline from the live feed, so they
        // work on any data source. Drawn as an overlay (not a marker replacement)
        // so altitude colour, emergency styling, and the highlight/watchlist/pin
        // rings all still stack on top.
        if (const SpecialAircraft::Class sc = SpecialClassOf(tracked); sc != SpecialAircraft::Class::None) {
            const uint32_t col = SpecialColor(sc);
            backbuffer.drawLine(x,     y - 9, x + 9, y,     col);
            backbuffer.drawLine(x + 9, y,     x,     y + 9, col);
            backbuffer.drawLine(x,     y + 9, x - 9, y,     col);
            backbuffer.drawLine(x - 9, y,     x,     y - 9, col);
            backbuffer.setTextSize(1);
            backbuffer.setTextColor(col);
            backbuffer.drawString(SpecialAircraft::Tag(sc), x + 11, y - 3);
        }

        // Claimable: a gold NEW flag plus a ring, meaning "this TYPE is not in your
        // collection yet -- tap it". It clears the moment the card is opened, and
        // it comes back for the next unclaimed type rather than never again.
        //
        // Only ever shown for a contact whose type is already known, which is a
        // real limit rather than an oversight: the type arrives from enrichment,
        // so on a board where enrichment is heap-gated (the C3) most blips carry
        // no badge and the reveal-on-tap path is what makes the mechanic work.
        if (logbookEnabled && tracked.claimable) {
            backbuffer.setTextSize(1);
            backbuffer.setTextColor(lgfx::color888(255, 215, 0));
            backbuffer.drawString("NEW", x + 11, y + 6);
            backbuffer.drawCircle(x, y, 7, lgfx::color888(255, 215, 0));
        }

        // overhead: pulsing "look up!" ring when a contact is passing near-overhead
        if (showOverhead && IsOverhead(tracked))
            DrawOverheadAlert(backbuffer, x, y);

        // ring the standout contacts; tags stack up-left to avoid the info text
        if (displayHighlight) {
            const uint32_t HL = lgfx::color888(255, 0, 255); // magenta: not an altitude or emergency color
            int tagY = y - 4;
            auto highlight = [&](const String& tag) {
                backbuffer.drawCircle(x, y, 7, HL);
                backbuffer.setTextSize(1);
                backbuffer.setTextColor(HL);
                backbuffer.drawString(tag, x - (int)backbuffer.textWidth(tag) - 9, tagY);
                tagY -= 9;
            };
            if (icao == nearestIcao) highlight("NEAR");
            if (icao == highestIcao) highlight("HIGH");
            if (icao == fastestIcao) highlight("FAST");
        }

        // watchlisted contact: amber ring + always-on callsign
        if (MatchesWatchlist(tracked)) {
            const uint32_t AMBER = lgfx::color888(255, 140, 0);
            backbuffer.drawCircle(x, y, 8, AMBER);
            String cs = tracked.state.callsign;
            cs.trim();
            if (cs.isEmpty()) { cs = icao; cs.toUpperCase(); }
            backbuffer.setTextSize(1);
            backbuffer.setTextColor(AMBER);
            backbuffer.drawString(cs, x - (int)backbuffer.textWidth(cs) / 2, y - 16);
        }

        // FOLLOWED contact: a distinct ring on the radar, so it is findable
        // without leaving the collection view (§13.3). Deliberately its own
        // colour and its own radius rather than reusing the watchlist amber:
        // "watchlisted" and "this is the aeroplane my son is flying" are not the
        // same statement, and a customer who uses both must be able to tell
        // which ring is which at a glance.
        if (!followTarget.isEmpty() && MatchesFollow(tracked)) {
            const uint32_t FOLLOW_RING = lgfx::color888(0, 170, 255); // the track's blue
            backbuffer.drawCircle(x, y, 12, FOLLOW_RING);
            backbuffer.drawCircle(x, y, 13, FOLLOW_RING);
        }

        // pinned ("tracked") contact: white reticle + always-on callsign
        if (icao == pinnedIcao) {
            const uint32_t PIN = lgfx::color888(255, 255, 255);
            backbuffer.drawCircle(x, y, 9, PIN);
            backbuffer.drawCircle(x, y, 11, PIN);
            String cs = tracked.state.callsign;
            cs.trim();
            if (cs.isEmpty()) { cs = icao; cs.toUpperCase(); }
            backbuffer.setTextSize(1);
            backbuffer.setTextColor(PIN);
            backbuffer.drawString(cs, x - (int)backbuffer.textWidth(cs) / 2, y + 13);
        }
    }

    // Follow Mode stage 1: the draw-cost readout, last so it sits on top of the
    // scene. Bench instrumentation, not product UI -- it exists so the §18.1
    // number can be read off a board without a serial console, and it disappears
    // entirely when no aircraft is being followed.
    DrawFollowHud(backbuffer);
}

void AircraftManager::DrawList(BandCanvas& backbuffer)
{
    constexpr int cx = SCREEN_SIZE_DIV_2;
    backbuffer.setTextSize(1);

    const std::vector<String> order = SortedAircraftByDistance();

    // clamp the scroll offset to the available rows
    const int maxScroll = std::max(0, (int)order.size() - LIST_ROWS);
    if (listScroll > maxScroll) listScroll = maxScroll;
    if (listScroll < 0) listScroll = 0;

    auto centered = [&](const String& s, int y) {
        backbuffer.drawString(s, cx - (int)backbuffer.textWidth(s) / 2, y);
    };

    // centre the fixed-width column block so the rows sit mid-screen on any panel size
    const int lx = (SCREEN_SIZE - LIST_BLOCK_W) / 2;

    backbuffer.setTextColor(lgfx::color888(0, 255, 0));
    centered("AIRCRAFT", 8);
    backbuffer.setTextColor(lgfx::color888(0, 130, 0));
    centered(String(order.size()) + " tracked", 23);

    // rows: callsign / type / distance / altitude, in columns within the centred
    // block (see LIST_COL_*). The list is already sorted by distance, so the
    // distance column is the number the sort order begs the eye to ask for.
    backbuffer.setTextColor(lgfx::color888(0, 200, 0));
    const float clat = cosf(radians((float)lat)); // scale longitude delta (see IsOverhead)
    for (int r = 0; r < LIST_ROWS; ++r) {
        const int idx = listScroll + r;
        if (idx >= (int)order.size()) break;
        auto it = trackedAircraft.find(order[idx]);
        if (it == trackedAircraft.end()) continue;
        const TrackedAircraft& t = it->second;

        String cs = t.state.callsign;
        cs.trim();
        if (cs.isEmpty()) { cs = order[idx]; cs.toUpperCase(); }
        const String type = t.typeCode.isEmpty() ? "--" : t.typeCode;
        const String alt = String(lroundf(t.state.baroAltitude * METRES_TO_FEET)) + "ft";

        auto [la, lo] = t.GetDisplayPosition();
        const float dLa = la - (float)lat, dLo = (lo - (float)lon) * clat;
        float dist = sqrtf(dLa * dLa + dLo * dLo) * 111.0f; // degrees -> km
        const String distStr = units::FormatKm(dist, rangeUnit);

        const int y = LIST_ROW_TOP + r * LIST_ROW_H;
        uint32_t rowColor = lgfx::color888(0, 200, 0);
        if (const SpecialAircraft::Class sc = SpecialClassOf(t); sc != SpecialAircraft::Class::None)
            rowColor = SpecialColor(sc);              // military/special/heli
        if (MatchesWatchlist(t))         rowColor = lgfx::color888(255, 140, 0); // amber
        if (order[idx] == pinnedIcao)    rowColor = lgfx::color888(255, 255, 255); // pin wins
        backbuffer.setTextColor(rowColor);
        backbuffer.drawString(cs,      lx + LIST_COL_CS,   y);
        backbuffer.drawString(type,    lx + LIST_COL_TYPE, y);
        backbuffer.drawString(distStr, lx + LIST_COL_DIST, y);
        backbuffer.drawString(alt,     lx + LIST_COL_ALT,  y);
    }
}

void AircraftManager::DrawStats(BandCanvas& backbuffer)
{
    constexpr int cx = SCREEN_SIZE_DIV_2;
    backbuffer.setTextSize(1);

    auto centered = [&](const String& s, int y) {
        backbuffer.drawString(s, cx - (int)backbuffer.textWidth(s) / 2, y);
    };

    // snapshot over the airborne contacts
    int count = 0;
    String highIcao, fastIcao, nearIcao;
    float maxAlt = -1e30f, maxVel = -1e30f, minD2 = 1e30f;
    const float clat = cosf(radians((float)lat)); // scale longitude delta (see IsOverhead)
    for (auto& [icao, t] : trackedAircraft) {
        if (t.state.onGround) continue;
        ++count;
        if (t.state.baroAltitude > maxAlt) { maxAlt = t.state.baroAltitude; highIcao = icao; }
        if (t.state.velocity > maxVel)     { maxVel = t.state.velocity; fastIcao = icao; }
        auto [la, lo] = t.GetDisplayPosition();
        const float dLa = la - (float)lat, dLo = (lo - (float)lon) * clat;
        const float d2 = dLa * dLa + dLo * dLo;
        if (d2 < minD2) { minD2 = d2; nearIcao = icao; }
    }

    auto label = [&](const String& icao) -> String {
        auto it = trackedAircraft.find(icao);
        if (it == trackedAircraft.end()) return "-";
        String cs = it->second.state.callsign;
        cs.trim();
        if (cs.isEmpty()) { cs = icao; cs.toUpperCase(); }
        return cs;
    };

    backbuffer.setTextColor(lgfx::color888(0, 255, 0));
    centered("STATS", 14);

    int y = 48;
    const int lh = backbuffer.fontHeight() + 10;
    const int clockRow = SCREEN_SIZE - 30; // matches DrawClock's y

    // RESERVED, not space-guarded: the "Reset WiFi" control gets its own row just
    // above the clock, and everything else stops short of it. It used to be drawn
    // last like any other line, which meant a busy scope silently deleted it --
    // HIGH/FAST/NEAR, the leaderboard and the feed block consumed the budget, `y`
    // passed the limit, and the row (and with it its tap target, which is derived
    // from the drawn bounds) simply never appeared. The one control a customer
    // needs when the device is off the network cannot be the first thing crowded
    // out by scoreboard rows.
    const int wifiRowTop = clockRow - lh - 2;

    // THE ADDRESS GETS THE SAME TREATMENT, for the same reason and after the same
    // failure. The paragraph above describes a bug that was fixed for the Reset
    // WiFi control and left in place for the line directly under it: the host name
    // was drawn last in the flow, so a busy scope silently deleted it, and it was
    // reported missing from a device whose only visible fault was that it had
    // things to show. "Drawn last, and only as far as it fits" is exactly the
    // policy that made the reset row vanish.
    //
    // It belongs next to the reset control on merit, not just for layout: both are
    // what a customer needs when something has gone wrong. The reset row is how you
    // get the device back on a network; this is how you reach it once it is on one.
    // Neither can be the first thing crowded out by scoreboard rows.
    //
    // The name is reserved and the IP is not, because the name is the thing you
    // type and the IP is the fallback for when mDNS does not resolve. That leaves
    // the IP sitting ABOVE the name when there is room for it, which reads slightly
    // out of order -- accepted deliberately, because the alternative is spending a
    // second guaranteed row on the less useful of the two on a 240 px panel.
    const int hostRowTop = wifiRowTop - lh;
    const int clockTop = hostRowTop;   // the ceiling every optional block below obeys

    backbuffer.setTextColor(lgfx::color888(0, 200, 0));
    // Space-guarded: a line that would reach the reserved row is dropped, so the
    // block order below is also the priority order on the small 240 px panels.
    auto line = [&](const String& s) {
        if (y + lh > clockTop) return;
        centered(s, y);
        y += lh;
    };

    // Ellipsise a string that will not fit the round face at row `yTop`.
    //
    // Every other row here is bounded by construction -- callsigns, ICAO hexes, formatted
    // numbers -- so only user-supplied text needs this. A round screen has no margin to
    // absorb an overrun: there is no edge to clip against, the glyphs simply run off the
    // glass, and a 32-character SSID is both legal and not rare.
    //
    // Width is the chord, measured at whichever edge of the glyph band sits FARTHER from
    // the centre line: the text occupies yTop..yTop+lh, and the narrower end is the one
    // that decides whether it fits.
    // The geometry moved to Layout.h (ChordWidthPx) and the ellipsis to
    // FitToDisc, because the local face's bottom readout ran off both ends of
    // the curve for exactly this reason and a second copy would have drifted
    // from this one. Kept as a named lambda so the call sites below read the
    // same as they did.
    auto fitted = [&](const String& s, int yTop) -> String {
        return FitToDisc(backbuffer, s, yTop, lh);
    };

    line(String(count) + " aircraft");
    if (count > 0) {
        line("High " + label(highIcao) + " " + String(lroundf(maxAlt * METRES_TO_FEET)) + "ft");
        line("Fast " + label(fastIcao) + " " + String(lroundf(maxVel * MS_TO_KNOTS)) + "kt");
        float distance = sqrtf(minD2) * 111.0f;
        line("Near " + label(nearIcao) + " " + units::FormatKm(distance, rangeUnit));
    }

    // TODAY -- contacts since local midnight, peak simultaneous count, busiest
    // hour + an hourly sparkline. RAM-only session stats (see the members).
    if (todayContacts > 0) {
        y += 6;
        backbuffer.setTextColor(lgfx::color888(0, 255, 0));
        line("TODAY");
        backbuffer.setTextColor(lgfx::color888(0, 200, 0));
        line(String(todayContacts) + " contacts  peak " + String(todayPeak));

        int busiest = -1;
        uint16_t maxC = 0;
        for (int h = 0; h < 24; ++h)
            if (todayHourCounts[h] > maxC) { maxC = todayHourCounts[h]; busiest = h; }
        if (busiest >= 0)
            line("busiest " + String(busiest) + ":00 (" + String(maxC) + ")");

        // 24-hour sparkline: one thin bar per hour, busiest hour full-bright
        if (maxC > 0 && y + 12 <= clockTop) {
            constexpr int BW = 3, GAP = 1;
            const int W = 24 * (BW + GAP) - GAP;
            const int x0 = cx - W / 2;
            const int base = y + 10;
            for (int h = 0; h < 24; ++h) {
                const int bh = todayHourCounts[h] == 0
                    ? 1 : 1 + (todayHourCounts[h] * 9) / maxC;
                backbuffer.fillRect(x0 + h * (BW + GAP), base - bh, BW, bh,
                                    h == busiest ? lgfx::color888(0, 255, 0)
                                                 : lgfx::color888(0, 120, 0));
            }
            y += 16;
        }
    }

    // AIRCRAFT OF THE DAY -- the day's single most notable catch (see
    // ConsiderAircraftOfDay). Only shows once something's been logged today.
    if (!aotdCallsign.isEmpty() && y + lh <= clockTop) {
        y += 6;
        backbuffer.setTextColor(lgfx::color888(255, 210, 0)); // gold: a highlight
        line("AIRCRAFT OF THE DAY");
        backbuffer.setTextColor(lgfx::color888(0, 200, 0));
        line(aotdCallsign + "  " + aotdReason);
        if (!aotdLabel.isEmpty())
            line(aotdLabel);
    }

    // spotting logbook totals (the persistent "lifelist")
    if (logbookEnabled) {
        y += 6;
        backbuffer.setTextColor(lgfx::color888(0, 255, 0));
        line("LIFELIST");
        backbuffer.setTextColor(lgfx::color888(0, 200, 0));
        // CLAIMED / SEEN, and it REPLACES the old counts rather than adding a row.
        // Every line here is space-guarded and silently dropped when the face
        // fills (that is how the Reset-WiFi row once vanished -- see the reserved
        // row above), so the gap has to earn its place from the existing budget.
        //
        // It lives here, not in the LEADERBOARD block below, because that block is
        // behind an opt-in AND a returned standing. The gap is local, always
        // available, and it is the number that makes somebody want to tap -- so it
        // must not be hidden behind a cloud feature the owner may never enable.
        // CLAIMED COUNTS ONLY, no denominator. "75/220" invited the reading that
        // 220 is how many types there are; it is how many this device has stored,
        // and once a store hits its cap it is the cap rather than a truth about
        // the sky. A number that changes meaning when a limit is reached should
        // not be shown as a total on a face with no room to explain it. The gap
        // worth closing still exists and is still shown -- on the Collection page,
        // where the unclaimed entries are visible as chips you can actually go
        // and get, and where there is room to label the second number "seen".
        line(String(logbook.ClaimedTypeCount()) + " types claimed");
        line(String(logbook.ClaimedOperatorCount()) + " operators  " +
             String(logbook.ClaimedCountryCount()) + " countries");
        line(String(logbook.Contacts()) + " contacts seen");

        // lifetime record holders, one compact line: highest / fastest / closest ever
        const Logbook::Record& rh = logbook.HighRecord();
        const Logbook::Record& rf = logbook.FastRecord();
        const Logbook::Record& rn = logbook.NearRecord();
        if (rh.set || rf.set || rn.set) {
            String best = "Best";
            if (rh.set) best += " " + String(lroundf(rh.value / 1000.0f)) + "kft";
            if (rf.set) best += " " + String(lroundf(rf.value)) + "kt";
            if (rn.set) {
                float d = rn.value;
                best += " " + units::FormatKm(d, rangeUnit);
            }
            line(best);
        }
    }

#ifdef FEATURE_CLOUD_FEED
    // LEADERBOARD -- this device's public standing, once a submit has returned
    // one. Opt-in; shown only when enabled and a rank has arrived.
    if (lbEnabled && lbHaveStanding && y + lh <= clockTop) {
        y += 6;
        backbuffer.setTextColor(lgfx::color888(255, 210, 0)); // gold: a score
        line("LEADERBOARD");
        backbuffer.setTextColor(lgfx::color888(0, 200, 0));
        String rankLine = "#" + String(lbRank);
        if (lbTotal > 0) rankLine += "/" + String(lbTotal);
        rankLine += "  " + String(lbPoints) + " pts";
        line(rankLine);
        if (lbSeasonRank > 0)
            line("season #" + String(lbSeasonRank) + "  " + String(lbSeasonPoints) + " pts");
        if (!lbRarestType.isEmpty())
            line("rarest " + lbRarestType + " (" + String(lbRarestPct) + "%)");
    }
#endif

    // FEED health -- source, honest data age (device wait + server-side lag),
    // poll cadence, and hard-fail count. Most valuable to local-receiver users
    // (Blipscope doubles as a monitor for their dump1090/readsb), and it makes a
    // quietly failing feed diagnosable from the device itself. Space-guarded per
    // line like THIS DEVICE below so the small C3 panel never collides.
    {
        const char* src =
#ifdef FEATURE_CLOUD_FEED
            useCloudSource ? "cloud" :
#endif
            (useLocalSource ? "local" : "OpenSky");

        String ageStr = "--";
        if (lastGoodDataMs != 0) {
            const unsigned long ageS = (millis() - lastGoodDataMs + dataLagAtMergeMs) / 1000UL;
            ageStr = String(ageS) + "s";
        }
        const bool stale = IsDataStale();

        if (y + lh <= clockTop) {
            y += 6;
            backbuffer.setTextColor(stale ? lgfx::color888(255, 176, 0)   // amber: worth a look
                                          : lgfx::color888(0, 255, 0));
            line(String("FEED ") + src + " " + ageStr + (stale ? " STALE" : ""));
        }
        if (y + lh <= clockTop) {
            backbuffer.setTextColor(lgfx::color888(0, 200, 0));
            line("poll " + String(CurrentPollIntervalMs() / 1000UL) + "s  fails " +
                 String(FetchHardFailCount()));
        }
    }

    // live tilt from the on-board IMU (HAS_IMU boards) -- proves the sensor is alive and gives
    // the Stats screen something board-specific. Signed degrees: P = pitch, R = roll.
    if constexpr (variant::HAS_IMU) {
        if (imuValid) {
            y += 6;
            backbuffer.setTextColor(lgfx::color888(0, 200, 0));
            char buf[24];
            snprintf(buf, sizeof(buf), "Tilt P%+d R%+d", (int)lroundf(imuPitch), (int)lroundf(imuRoll));
            line(String(buf));
        }
    }

    // NETWORK -- the first thing a customer looks for when "it stopped working",
    // and the answer to the question they actually have ("is it even on my WiFi?").
    // The reset row directly beneath it is the recovery path that does NOT require
    // the network, unlike the /reset-wifi button on the config page. Its bounds are
    // recorded for HandleTap; -1 means it wasn't drawn this frame (no room), and
    // then it cannot be tapped either -- the hit test and the pixels agree by
    // construction.
    {
        const bool up = WiFi.status() == WL_CONNECTED;

        // The SSID line is a nicety and stays space-guarded -- it answers "is it
        // even on my WiFi?", but it is not the thing that must survive.
        if (y + lh <= clockTop) {
            y += 6;
            backbuffer.setTextColor(up ? lgfx::color888(0, 255, 0) : lgfx::color888(255, 176, 0));
            String ssid = up ? WiFi.SSID() : String();
            if (up && ssid.isEmpty()) ssid = "(unnamed)";
            // The SSID is the only string on this screen the customer chose, so it is the
            // only one that can be any length at all -- fit it to the chord at its row.
            line(up ? fitted("Wi-Fi " + ssid, y) : String("Wi-Fi NOT CONNECTED"));
        }

        // The control itself is drawn UNCONDITIONALLY in its reserved row.
        //
        // Counting down OUT LOUD is not decoration. Silent detection would be
        // worse than none: the customer has to know both that it is working and
        // that letting go cancels, and a destructive action they cannot see
        // progressing is one they cannot abort. Same wording as the boot-time
        // reset, because it is the same gesture doing the same thing.
        // A TAP, and it opens a menu rather than doing anything. Nothing
        // destructive happens on this screen at all now -- which is what makes a
        // single stray contact here harmless (#165's accident lands on a menu
        // with a large Cancel, not on a wipe).
        backbuffer.setTextColor(lgfx::color888(0, 200, 0));
        centered(String("[ Reset ]"), wifiRowTop);
        // Tap target = the drawn row, padded to a fingertip. Derived from the same
        // constant the text uses, so the hit box cannot drift from the pixels.
        resetRowY0 = wifiRowTop - 8;
        resetRowY1 = wifiRowTop + lh;
    }

    // THIS DEVICE -- the config page is at http://<name>.local, so a customer who
    // forgot the name can read it here. The IP stays space-guarded as the mDNS
    // fallback (Android and some Windows setups will not resolve .local).
    y += 6;
    if (y + lh <= clockTop) {
        backbuffer.setTextColor(lgfx::color888(0, 200, 0));
        line(WiFi.localIP().toString());
    }

    // RESERVED, not space-guarded -- see hostRowTop. Drawn unconditionally in its
    // own row directly above the Reset WiFi control, so no amount of traffic can
    // delete the one string that gets a customer to the config page.
    backbuffer.setTextColor(lgfx::color888(0, 255, 0));
    centered(DeviceIdentity::Name() + ".local", hostRowTop);
}

void AircraftManager::DrawScreenIndicator(BandCanvas& backbuffer) const
{
    constexpr int cx = SCREEN_SIZE_DIV_2;
    const int y = SCREEN_SIZE - 16;
    // Dots for the screens that EXIST, not for the enum. Follow is hidden when
    // no aircraft is being followed (§13.3), and a dot for a screen no swipe can
    // reach is a promise the device does not keep -- the same reason the cycle
    // walks visible screens instead of counting to four.
    const int shown = SCREEN_COUNT - (FollowScreenVisible() ? 0 : 1);
    int slot = 0;
    for (int i = 0; i < SCREEN_COUNT; ++i) {
        if ((Screen)i == Screen::Follow && !FollowScreenVisible())
            continue;
        const bool active = (i == (int)screen);
        backbuffer.fillCircle(cx - (shown - 1) * 6 + slot * 12, y, active ? 3 : 2,
                              active ? lgfx::color888(0, 255, 0) : lgfx::color888(0, 80, 0));
        ++slot;
    }
}

void AircraftManager::DrawClock(BandCanvas& backbuffer) const
{
    const time_t utc = time(nullptr);
    if (utc < 1600000000) return; // NTP not synced yet

    const time_t local = utc + utcOffsetSec;
    struct tm t;
    gmtime_r(&local, &t);
    char buf[6];
    snprintf(buf, sizeof(buf), "%02d:%02d", t.tm_hour, t.tm_min);

    // ON THE FOLLOW SCREEN THIS SLOT CARRIES ARRIVAL, NOT NOW.
    //
    // Every other screen already shows the current time, and on a face whose
    // hero slot is counting DOWN ("43m TO ARRIVAL") the useful companion is the
    // clock time that countdown lands on -- not a second copy of now.
    //
    // ALWAYS LABELLED "ARR", never a bare number. A clock that means something
    // different on one screen is a clock that lies: 18:45 on Radar is the time,
    // and an unlabelled 18:45 here would read as the time while meaning
    // something else entirely. The label is what makes the reuse honest, so it
    // is not optional and there is no branch that omits it.
    //
    // Falls back to the wall clock whenever an arrival cannot be computed --
    // MinutesToArrival declines rather than guess at an unknown groundspeed --
    // because the alternative is an empty slot on every screen but this one.
    String out = buf;
    uint32_t ink = lgfx::color888(0, 170, 0);
    if (screen == Screen::Follow) {
        const RouteView v = FollowRouteView();
        const int mins = FollowMinutesToArrival(v);
        if (mins >= 0) {
            const time_t arrive = local + (time_t)mins * 60;
            struct tm a;
            gmtime_r(&arrive, &a);
            char ab[16];
            snprintf(ab, sizeof(ab), "ARR %02d:%02d", a.tm_hour, a.tm_min);
            out = ab;
            // THE CONFIDENCE TREATMENT IS ONE TREATMENT, NOT FOUR.
            //
            // This was a hardcoded green and it STAYED green in NO COVERAGE,
            // while the hero dimmed, the band ahead went dashed, the bearing
            // wedge faded to 40% and everything else turned amber. A slot
            // holding full-confidence styling while the rest of the face says
            // "inferred" is not a fifth signal, it contradicts the other four --
            // and this is the most inferred number on the screen there, resting
            // on a dead-reckoned position AND an assumed groundspeed.
            //
            // So it takes the state's colour and the same 0.55 fade 8 gives the
            // estimated readout, from the two functions the face already uses.
            ink = FollowStateColour(v.st, /*benignApproach=*/true);
            // THE SAME CONDITION THE HERO USES, not a similar one. The first
            // version faded on IsAbsent, which is a SUPERSET: ApproachLost is
            // absent, so the arrival time dimmed while the hero above it stayed
            // full green -- reintroducing the split treatment one state over
            // from where it was fixed. 8 fades the estimated readout in
            // NO COVERAGE specifically, because that is where the position is
            // dead-reckoned rather than last-known.
            if (v.st == follow::State::NoCoverage)
                ink = FollowFade(ink, 0.55f);
        }
    }

    backbuffer.setTextSize(1);
    backbuffer.setTextColor(ink);
    backbuffer.drawString(out, SCREEN_SIZE_DIV_2 - (int)backbuffer.textWidth(out) / 2, SCREEN_SIZE - 30);
}

// Minutes to arrival for the followed flight, or -1 when it cannot be known.
//
// Deliberately a thin wrapper over the same call the arc face makes, rather
// than a second derivation: two places computing "when does he land" from the
// same inputs is two places to drift, and the clock slot disagreeing with the
// countdown above it would be worse than showing neither.
int AircraftManager::FollowMinutesToArrival() const
{
    return FollowMinutesToArrival(FollowRouteView());
}

int AircraftManager::FollowMinutesToArrival(const RouteView& v) const
{
    // EN ROUTE FIRST, INPUTS SECOND. A jet at a gate has an origin, a
    // destination and a position -- every input this function needs -- and
    // "arrives 23:19" is still false. Availability of data is not applicability
    // of the conclusion; see follow::Machine::IsEnRoute.
    if (!follow::Machine::IsEnRoute(v.st)) return -1;
    if (!v.org.known || !v.dst.known || !v.havePos) return -1;
    const float totalKm = follow::GreatCircleKm(v.org.lat, v.org.lon, v.dst.lat, v.dst.lon);
    const float progress = follow::ProgressAlong(v.org, v.dst, v.acLat, v.acLon);
    return follow::MinutesToArrival(totalKm * (1.0f - progress), v.gsKt);
}

bool AircraftManager::NightClockActive() const
{
    if (!nightClockEnabled || !nightNow || inDetail || screen != Screen::Radar)
        return false;
    if (time(nullptr) < 1600000000)
        return false; // no NTP yet -- nothing honest to show
    for (const auto& [icao, t] : trackedAircraft)
        if (!t.state.onGround)
            return false; // traffic in range: the radar always wins
    return true;
}

// The unconfigured state, made legible.
//
// WHAT A CUSTOMER SEES WITHOUT THIS: a correct, empty radar. The sweep turns,
// the rings are drawn, nothing ever appears -- which is indistinguishable from a
// quiet sky, and stays that way forever. This is the state a factory-fresh unit
// and a just-factory-reset unit are both in, so it is the first screen a
// customer meets and the one that has to tell them what to do next.
//
// It gives the ADDRESS rather than an instruction to "open the config page",
// because the address is the part they cannot guess. Both forms are shown: the
// .local name for anything that resolves mDNS, and the raw IP for Android and
// the Windows setups that do not.
void AircraftManager::DrawNoLocation(BandCanvas& backbuffer) const
{
    constexpr int cx = SCREEN_SIZE_DIV_2;
    const int lh = SCREEN_SIZE / 16;

    backbuffer.setTextSize(1);
    auto centered = [&](const String& t, int y, uint32_t colour) {
        backbuffer.setTextColor(colour);
        backbuffer.drawString(t, cx - (int)backbuffer.textWidth(t) / 2, y);
    };

    const uint32_t amber = lgfx::color888(255, 176, 0);
    const uint32_t green = lgfx::color888(0, 200, 0);
    const uint32_t dim   = lgfx::color888(140, 140, 140);

    // A ring, so the screen still reads as a scope rather than as an error page.
    // The device is not broken and should not look it.
    backbuffer.drawCircle(cx, cx, SCREEN_SIZE_DIV_2 - 6, lgfx::color888(0, 60, 0));
    backbuffer.drawCircle(cx, cx, SCREEN_SIZE_DIV_2 / 2, lgfx::color888(0, 40, 0));

    int y = SCREEN_SIZE / 2 - lh * 2;
    centered("SET YOUR LOCATION", y, amber);
    y += lh + lh / 2;
    centered("Open the config page:", y, dim);
    y += lh;

    // The name is the stable address and comes first. The IP is the fallback and
    // is only shown once there is one -- printing "0.0.0.0" would be worse than
    // printing nothing, because it looks like an address and is not.
    centered(DeviceIdentity::Name() + ".local", y, green);
    const IPAddress ip = WiFi.localIP();
    if (WiFi.status() == WL_CONNECTED && ip[0] != 0) {
        y += lh;
        centered(ip.toString(), y, green);
    }
}

void AircraftManager::DrawNightClock(BandCanvas& backbuffer) const
{
    const time_t local = time(nullptr) + utcOffsetSec;
    struct tm t;
    gmtime_r(&local, &t);

    // HH : MM in four seven-segment cells + a blinking colon, centred. All
    // geometry scales from SCREEN_SIZE so every panel gets the same face.
    const int cellW  = SCREEN_SIZE * 3 / 20;
    const int cellH  = SCREEN_SIZE / 4;
    const int gap    = SCREEN_SIZE / 40;
    const int colonW = cellW / 2;
    const int total  = 4 * cellW + colonW + 4 * gap;
    int x = (SCREEN_SIZE - total) / 2;
    const int y = (SCREEN_SIZE - cellH) / 2;

    // dim green LED look, matching the radar palette at night
    const uint32_t LIT   = lgfx::color888(0, 190, 0);
    const uint32_t GHOST = lgfx::color888(0, 22, 0);
    const uint32_t BLOOM = lgfx::color888(0, 64, 0);

    sevenseg::DrawSevenSeg(backbuffer, x, y, cellW, cellH, t.tm_hour / 10, LIT, GHOST, BLOOM);
    x += cellW + gap;
    sevenseg::DrawSevenSeg(backbuffer, x, y, cellW, cellH, t.tm_hour % 10, LIT, GHOST, BLOOM);
    x += cellW + gap;
    sevenseg::DrawColon(backbuffer, x, y, colonW, cellH, (t.tm_sec & 1) == 0, LIT, GHOST);
    x += colonW + gap;
    sevenseg::DrawSevenSeg(backbuffer, x, y, cellW, cellH, t.tm_min / 10, LIT, GHOST, BLOOM);
    x += cellW + gap;
    sevenseg::DrawSevenSeg(backbuffer, x, y, cellW, cellH, t.tm_min % 10, LIT, GHOST, BLOOM);
}

void AircraftManager::UpdateBrightness()
{
    const unsigned long now = millis();
    if (now - lastBrightnessCheck < 20000) return; // re-evaluate every 20s
    lastBrightnessCheck = now;

    uint8_t target = configuredBrightness;

    const time_t utc = time(nullptr);
    const bool synced = utc > 1600000000; // NTP has set the clock (>~2020)
    const bool night = synced && isNightNow(lat, lon, utc);
    nightNow = night; // cached for NightClockActive (same 20 s cadence)
    if (autoDim && night) {
        target = configuredBrightness / 5; // ~20% of day level at night
        if (target < 10) target = 10;
    }

    // An active visual alert may punch through the night dim (config
    // "visual-night") so the ring/flash is actually visible across a dark room.
    // UpdateVisualAlerts resets the 20 s throttle on state changes, so the
    // override engages -- and releases -- the frame the alert starts/ends.
    if (visualAlertActive && visualNightOverride)
        target = configuredBrightness;

    if (target != currentBrightness) {
        tft.setBrightness(target);
        currentBrightness = target;
        Serial.printf("[dim] brightness -> %u (%s)\n",
                      target, target < configuredBrightness ? "night" : "day");
    }
}

std::vector<String> AircraftManager::SortedAircraftByDistance()
{
    std::vector<std::pair<float, String>> v;
    const float clat = cosf(radians((float)lat)); // scale longitude delta (see IsOverhead)
    for (auto& [icao, t] : trackedAircraft) {
        if (t.state.onGround) continue;
        auto [la, lo] = t.GetDisplayPosition();
        const float dLa = la - (float)lat, dLo = (lo - (float)lon) * clat;
        v.push_back({ dLa * dLa + dLo * dLo, icao });
    }
    std::sort(v.begin(), v.end(), [](const auto& a, const auto& b) { return a.first < b.first; });

    std::vector<String> out;
    out.reserve(v.size());
    for (auto& p : v) out.push_back(p.second);
    return out;
}

void AircraftManager::DrawRadarCircles(BandCanvas& backbuffer) const
{
    constexpr int CENTRE = SCREEN_SIZE_DIV_2 - 1;
    constexpr int OUTER = SCREEN_SIZE_DIV_2 - 1;

    backbuffer.drawCircle(CENTRE, CENTRE, OUTER, lgfx::color888(0, 200, 0));
    backbuffer.drawCircle(CENTRE, CENTRE, (OUTER / 3) * 2, lgfx::color888(0, 64, 0));
    backbuffer.drawCircle(CENTRE, CENTRE, OUTER / 3, lgfx::color888(0, 32, 0));

    backbuffer.setTextSize(1);

    // range-ring distance labels, stacked just right of the vertical centre line
    backbuffer.setTextColor(lgfx::color888(0, 110, 0));
    const int ringPx[3] = { OUTER, (OUTER / 3) * 2, OUTER / 3 };
    const float ringFrac[3] = { 1.0f, 2.0f / 3.0f, 1.0f / 3.0f };
    const int inset[3] = { 14, 3, 3 }; // push the outer label down off the bezel/N
    for (int i = 0; i < 3; ++i) {
        // rangeRadiusDisplay is ALREADY in display units (converted at setup),
        // so this formats the number without re-converting.
        const float value = rangeRadiusDisplay * ringFrac[i];
        String label = String(value, value < 10.0f ? 1 : 0);
        if (i == 0) label += rangeUnit; // unit on the outer ring only
        backbuffer.drawString(label, CENTRE + 4, CENTRE - ringPx[i] + inset[i]);
    }

    // compass rose at the bezel, rotated for window-up: each cardinal sits at
    // its bearing relative to the screen-top bearing (radarUpDeg; 0 = north-up)
    backbuffer.setTextColor(lgfx::color888(0, 150, 0));
    const struct { const char* c; int bearing; } cardinals[4] =
        { {"N", 0}, {"E", 90}, {"S", 180}, {"W", 270} };
    const int labelR = CENTRE - 7;
    for (const auto& p : cardinals) {
        const float a = radians((float)(p.bearing - radarUpDeg) - 90.0f);
        const int px = CENTRE + (int)lroundf(labelR * cosf(a));
        const int py = CENTRE + (int)lroundf(labelR * sinf(a));
        backbuffer.drawString(p.c, px - (int)backbuffer.textWidth(p.c) / 2, py - 4);
    }
}

void AircraftManager::DrawAirports(BandCanvas& backbuffer) const
{
    // Dim, neutral ink: geography is context, not signal, so it must never
    // compete with a blip. Cull on the scan box first -- the whole table is a
    // few hundred float compares, cheap even per band on the C3.
    const uint32_t MARK  = lgfx::color888(130, 130, 130);
    const uint32_t LABEL = lgfx::color888(100, 100, 100);
    backbuffer.setTextSize(1);
    backbuffer.setTextColor(LABEL);

    // Cull + draw one entry; shared between the two sources below.
    const auto draw = [&](float apLat, float apLon, const char* code) {
        if (fabsf(apLat - (float)lat) > (float)radLat ||
            fabsf(apLon - (float)lon) > (float)radLon)
            return;
        auto [x, y] = ProjectCoordinateToScreen(apLat, apLon);
        backbuffer.drawRect(x - 2, y - 2, 5, 5, MARK);
        backbuffer.drawString(code, x + 5, y - 3);
    };

#ifdef FEATURE_CLOUD_FEED
    // The /api/v1/blipscope/airports long tail supersedes the baked majors once loaded. At
    // wide zooms only large/medium fields draw -- ~60 grass strips would
    // confetti a 240 px face; zoom in past ~60 km half-width and they appear.
    // The airportsMin config filter is a hard floor on top of that: a user in a
    // busy-GA area can hide the strips entirely and keep just the fields with
    // scheduled service.
    if (!cloudAirports.empty()) {
        const bool wide = radLat > 0.55f; // ~60 km of half-box in degrees
        for (const CloudFeed::CloudAirport& ap : cloudAirports) {
            if (airportsMin == AirportsMin::LargeOnly && ap.kind != 'L')
                continue;
            if (airportsMin == AirportsMin::MedLarge && ap.kind == 'S')
                continue;
            if (airportsMin == AirportsMin::All && wide && ap.kind == 'S')
                continue; // default: only the zoom rule hides strips
            draw(ap.lat, ap.lon, ap.code);
        }
        return;
    }
#endif

    for (size_t i = 0; i < AIRPORT_COUNT; ++i) {
        const Airport& ap = AIRPORTS[i];
        draw(ap.lat, ap.lon, ap.code);
    }
}

std::pair<int, int> AircraftManager::ProjectCoordinateToScreen(float predLat, float predLon) const
{
    const float dLon = predLon - lon;
    const float dLat = predLat - lat;

    const float normLon = (dLon + radLon) / (2.0f * radLon);
    const float normLat = (dLat + radLat) / (2.0f * radLat);

    float x = normLon * SCREEN_SIZE;
    float y = SCREEN_SIZE - (normLat * SCREEN_SIZE);

    // Window-up: rotate the picture about the centre so bearing radarUpDeg reads
    // "up". Screen space is isotropic here (radLon is pre-scaled by cos(lat) so
    // the scan box is square on the ground), which makes a plain screen-space
    // rotation geometrically correct. Everything downstream of this projection
    // (blips, trails, taps, the sweep's paint-crossing test) rotates with it.
    if (rotSin != 0.0f || rotCos != 1.0f) {
        constexpr float C = SCREEN_SIZE / 2.0f;
        const float px = x - C, py = y - C;
        x = C + px * rotCos + py * rotSin;
        y = C - px * rotSin + py * rotCos;
    }

    return { static_cast<int>(x), static_cast<int>(y) };
}

void AircraftManager::DrawAircraftInfo(BandCanvas& backbuffer, int x, int y, const TrackedAircraft& tracked, float brightness) const
{
    const int lineHeight = tft.fontHeight() + 1;

    backbuffer.setTextSize(1);
    // fades with the rest of the contact; scaleColor's brightness floor keeps it legible
    backbuffer.setTextColor(scaleColor(lgfx::color888(0, 128, 0), brightness));

    // Stack only the enabled fields; a field that formats to "" (e.g. no squawk
    // reported) is skipped so it doesn't leave a blank gap between lines.
    int line = 0;
    for (size_t i = 0; i < AIRCRAFT_INFO_FIELD_COUNT; ++i) {
        if (i >= infoFieldEnabled.size() || !infoFieldEnabled[i])
            continue;

        const String text = AIRCRAFT_INFO_FIELDS[i].format(tracked);
        if (text.isEmpty())
            continue;

        backbuffer.drawString(text, x + 5, y + 5 + lineHeight * line);
        ++line;
    }
}

bool AircraftManager::AircraftLabelBox(const TrackedAircraft& tracked, int x, int y,
                                       int& bx, int& by, int& bw, int& bh) const
{
    if (!displayInfoText) return false;
    tft.setTextSize(1);
    const int lineHeight = tft.fontHeight() + 1;
    // Same field walk as DrawAircraftInfo: count the non-empty enabled lines and the widest.
    int lines = 0, maxW = 0;
    for (size_t i = 0; i < AIRCRAFT_INFO_FIELD_COUNT; ++i) {
        if (i >= infoFieldEnabled.size() || !infoFieldEnabled[i])
            continue;
        const String text = AIRCRAFT_INFO_FIELDS[i].format(tracked);
        if (text.isEmpty())
            continue;
        const int w = tft.textWidth(text);
        if (w > maxW) maxW = w;
        ++lines;
    }
    if (lines == 0) return false;
    bx = x + 5;                 // first line drawn at x+5, y+5; lines stack by lineHeight
    by = y + 5;
    bw = maxW;
    bh = lineHeight * lines;
    return true;
}

void AircraftManager::DrawAircraftTriangle(BandCanvas& backbuffer, int x, int y, const TrackedAircraft& tracked, uint32_t color) const
{
    // Type-aware marker, keyed off the ADS-B emitter category (normalised to
    // OpenSky's numbering for both the cloud and local feeds). Heading-less types
    // (rotorcraft, balloons) get a fixed glyph; everything else is a heading dart
    // whose size grows with the aircraft's weight class so a heavy reads
    // differently from a light single at a glance.
    const int cat = tracked.state.category;

    // rotorcraft: a hub with two crossed rotor blades. Non-directional, since
    // helicopters routinely hover and yaw independently of their ground track.
    if (cat == 8) {
        backbuffer.drawLine(x - 5, y - 5, x + 5, y + 5, color);
        backbuffer.drawLine(x - 5, y + 5, x + 5, y - 5, color);
        backbuffer.fillCircle(x, y, 2, color);
        return;
    }

    // lighter-than-air (balloon / airship): a simple ring, also non-directional.
    if (cat == 10) {
        backbuffer.drawCircle(x, y, 4, color);
        return;
    }

    // heading unit vector (forward) and its perpendicular (right "wing").
    // Window-up subtracts the screen-top bearing so darts stay aligned with the
    // rotated picture (positions rotate inside ProjectCoordinateToScreen).
    const float headingScreen = radians(tracked.state.trueTrack - (float)radarUpDeg);
    const float dx = std::sin(headingScreen);
    const float dy = -std::cos(headingScreen);
    const float px = -dy;
    const float py = dx;

    // a dart pointing along the heading: tip ahead of the point, base behind it
    auto dart = [&](float tip, float base, float halfWidth) {
        const float tipX  = x + dx * tip,                 tipY  = y + dy * tip;
        const float leftX = x - dx * base + px * halfWidth, leftY = y - dy * base + py * halfWidth;
        const float rightX= x - dx * base - px * halfWidth, rightY= y - dy * base - py * halfWidth;
        backbuffer.fillTriangle(tipX, tipY, leftX, leftY, rightX, rightY, color);
    };

    // glider: a long, slender dart.
    if (cat == 9) {
        dart(9.0f, 2.0f, 1.0f);
        return;
    }

    // heavy / large jets (Large, High-vortex large, Heavy): a bigger dart with a
    // stub cross-wing so the airliners stand out from light traffic.
    const bool heavy = (cat == 4 || cat == 5 || cat == 6);
    if (heavy) {
        dart(8.0f, 4.0f, 2.5f);
        backbuffer.drawLine(x + px * 4.0f, y + py * 4.0f, x - px * 4.0f, y - py * 4.0f, color);
        return;
    }

    // everything else (light, small, high-performance, unknown): the standard dart
    dart(6.0f, 3.0f, 1.5f);
}

void AircraftManager::DrawEmergencyAlert(BandCanvas& backbuffer, int x, int y, const TrackedAircraft& tracked) const
{
    // expanding, fading "ping" ring to draw the eye to the emergency
    const float phase = (millis() % 900) / 900.0f;   // 0..1, ~0.9s period
    const int ringRadius = 4 + static_cast<int>(phase * 16.0f);
    const uint8_t ringBrightness = static_cast<uint8_t>((1.0f - phase) * 255.0f);
    backbuffer.drawCircle(x, y, ringRadius, lgfx::color888(ringBrightness, 0, 0));

    // steady red marker
    backbuffer.fillCircle(x, y, 3, lgfx::color888(255, 0, 0));

    // always-visible red label: squawk code + what it means
    const char* descriptor = "EMERG";
    if (tracked.state.squawk == "7500")      descriptor = "HIJACK";
    else if (tracked.state.squawk == "7600") descriptor = "NORDO";

    backbuffer.setTextSize(1);
    backbuffer.setTextColor(lgfx::color888(255, 0, 0));
    backbuffer.drawString(tracked.state.squawk + " " + descriptor, x + 6, y - 10);
}

void AircraftManager::DrawOverheadAlert(BandCanvas& backbuffer, int x, int y) const
{
    // expanding, fading cyan "ping" plus a steady "LOOK UP" label, to pull your
    // eye to the sky as the contact passes near-overhead
    const float phase = (millis() % 1200) / 1200.0f;            // 0..1, ~1.2s period
    const int ringRadius = 6 + static_cast<int>(phase * 18.0f);
    const uint8_t b = static_cast<uint8_t>((1.0f - phase) * 255.0f);
    backbuffer.drawCircle(x, y, ringRadius, lgfx::color888(0, b, b));

    backbuffer.setTextSize(1);
    backbuffer.setTextColor(lgfx::color888(0, 255, 255));
    const char* label = "LOOK UP";
    backbuffer.drawString(label, x - (int)backbuffer.textWidth(label) / 2, y - 18);
}

// Visual-alert flash burst timing: three ~160 ms pulses on a 500 ms period
// (2 flashes/sec -- deliberately under the WCAG 2.3.1 "three flashes per
// second" photosensitivity threshold), then the layer settles to the edge ring.
namespace {
constexpr unsigned long FLASH_BURST_MS  = 1500; // total burst length (3 pulses)
constexpr unsigned long FLASH_PERIOD_MS = 500;  // one pulse cycle
constexpr unsigned long FLASH_ON_MS     = 160;  // lit portion of each cycle
}

void AircraftManager::UpdateVisualAlerts()
{
    const uint32_t EMG_RED = lgfx::color888(255, 0, 0);
    const uint32_t MIL_ORANGE = SpecialColor(SpecialAircraft::Class::Military);
    const unsigned long now = millis();

    // Scan the live picture for alerting classes. Class membership is noted (the
    // *FlashFired flags) even while a mode is Off, so enabling Flash in the config
    // later never fires a burst for every contact already on screen -- the same
    // reason the buzzer stays silent through the initial bulk sync.
    // Is the contact inside the visible radar circle? The scan box is square but the
    // display is round, so a contact can be tracked (and still fetched) while sitting
    // in an off-screen corner or just past the outer ring. The ring/flash only make
    // sense for something you can actually see, so gate them on this -- otherwise the
    // ring keeps pulsing at an aircraft that has already left the picture.
    constexpr int SCR_C = SCREEN_SIZE_DIV_2 - 1;
    constexpr int SCR_OUTER = SCREEN_SIZE_DIV_2 - 1;
    auto onScreen = [&](const TrackedAircraft& t) {
        auto [la, lo] = RadarBlipPosition(t);
        auto [x, y] = ProjectCoordinateToScreen(la, lo);
        const int dx = x - SCR_C, dy = y - SCR_C;
        return dx * dx + dy * dy <= SCR_OUTER * SCR_OUTER;
    };

    bool milActive = false, emgActive = false;
    for (auto& [icao, t] : trackedAircraft) {
        if (t.state.onGround) continue;

        // squawks can turn emergency mid-track, so the edge is per-contact and
        // fires at most once (a cleared-then-reset squawk doesn't re-burst)
        if (isEmergencySquawk(t.state.squawk)) {
            const bool vis = onScreen(t);
            if (vis && emgVisual != VisualAlertMode::Off) emgActive = true;
            if (!t.emgFlashFired) {
                t.emgFlashFired = true;
                if (vis && emgVisual == VisualAlertMode::Flash && initialSyncDone) {
                    flashBurstUntilMs = now + FLASH_BURST_MS;
                    flashBurstColor = EMG_RED;
                }
                // the emergency tone fires even for a contact already squawking at
                // boot, and even off-screen -- an active emergency is worth hearing
                // about (unlike a flash, which is tied to something you can see)
                PlayTone(4, 80, 100);
            }
        }

        // overhead alert tone: one-shot per contact, same "look up" condition as
        // the ring/ntfy. IsOverhead is only evaluated when the feature is on.
        if constexpr (variant::HAS_AUDIO) {
            if ((showOverhead || alertOverhead) && !t.overheadToneFired && IsOverhead(t)) {
                t.overheadToneFired = true;
                if (initialSyncDone)
                    PlayTone(3, 40, 70);
            }
        }

        if (SpecialAircraft::IsMilitary(t.state.icao24)) {
            // Gate on visibility, and mark the edge only once visible, so a jet that
            // first shows up in an off-screen corner still flashes when it crosses in.
            if (onScreen(t)) {
                if (milVisual != VisualAlertMode::Off) milActive = true;
                if (!t.milFlashFired) {
                    t.milFlashFired = true;
                    // an in-progress emergency burst outranks a new military one
                    const bool emgBurstActive = now < flashBurstUntilMs && flashBurstColor == EMG_RED;
                    if (milVisual == VisualAlertMode::Flash && initialSyncDone && !emgBurstActive) {
                        flashBurstUntilMs = now + FLASH_BURST_MS;
                        flashBurstColor = MIL_ORANGE;
                    }
                }
            }
        }
    }

    // Manual dismiss (tap): suppress the current episode until the screen is clear of
    // alerting contacts, then re-arm. With the on-screen gate above, a contact leaving
    // the picture clears milActive/emgActive on its own, so this re-arms naturally.
    if (!milActive && !emgActive)
        visualAlertDismissed = false;
    else if (visualAlertDismissed) {
        milActive = emgActive = false;
        flashBurstUntilMs = 0; // kill any burst still in flight
    }

    visualRingColor = emgActive ? EMG_RED : (milActive ? MIL_ORANGE : 0);

    const bool active = visualRingColor != 0 || now < flashBurstUntilMs;
    if (active != visualAlertActive) {
        visualAlertActive = active;
        lastBrightnessCheck = 0; // let UpdateBrightness apply/release the night override now
    }
}

void AircraftManager::PlayTone(uint8_t count, uint16_t onMs, uint16_t gapMs)
{
    if constexpr (!variant::HAS_AUDIO)
        return;
    if (!tonesEnabled || count == 0)
        return;
    if (toneRemaining > 0)
        return; // never interrupt a pattern in progress -- the first signal wins
    toneRemaining = count;
    toneOnMs = onMs;
    toneGapMs = gapMs;
    nextToneAtMs = millis(); // first chirp on the next UpdateTones() pass
}

void AircraftManager::UpdateTones()
{
    if constexpr (!variant::HAS_AUDIO)
        return;
    if (toneRemaining == 0)
        return;
    const unsigned long now = millis();
    if ((long)(now - nextToneAtMs) < 0)
        return;
    board::BuzzerChirp(toneOnMs);
    --toneRemaining;
    nextToneAtMs = now + toneOnMs + toneGapMs;
}

void AircraftManager::DrawVisualAlert(BandCanvas& backbuffer) const
{
    const unsigned long now = millis();

    // Full-screen flash burst: overwrite the frame with the class colour during
    // each pulse's lit window. The screen is fully redrawn every frame, so the
    // dark part of the cycle is just the normal picture -- no state to restore.
    if (now < flashBurstUntilMs && (now % FLASH_PERIOD_MS) < FLASH_ON_MS) {
        backbuffer.fillScreen(flashBurstColor);
        return; // nothing else would be legible this frame anyway
    }

    if (visualRingColor == 0) return;

    // Edge ring pulse: a ~1 Hz breathing band at the outermost edge, where it
    // reads across a room without covering the radar picture (or the detail
    // card, which it stays visible around).
    constexpr int CENTRE = SCREEN_SIZE_DIV_2 - 1;
    constexpr int OUTER = SCREEN_SIZE_DIV_2 - 1;
    const float breathe = 0.5f + 0.5f * sinf((float)(now % 1000) / 1000.0f * TWO_PI);
    const float level = 0.35f + 0.65f * breathe; // floor keeps the ring visible at the dim end
    backbuffer.fillArc(CENTRE, CENTRE, OUTER - 5, OUTER, 0.0f, 360.0f,
                       scaleColor(visualRingColor, level));
}

bool AircraftManager::TapDismissesAlert(int tx, int ty) const
{
    // During a full-screen flash burst the whole display is the alert, so any tap
    // (lit window or dark) is a dismiss.
    if ((long)(millis() - flashBurstUntilMs) < 0)
        return true;
    if (visualRingColor == 0)
        return false; // no ring showing -> nothing to dismiss
    // Ring mode: only a tap that lands in the outer edge band counts, so taps aimed
    // at blips near the centre still open cards. The band is finger-generous.
    constexpr int CENTRE = SCREEN_SIZE_DIV_2 - 1;
    constexpr int OUTER = SCREEN_SIZE_DIV_2 - 1;
    constexpr int BAND = 26;
    const int dx = tx - CENTRE, dy = ty - CENTRE;
    const int inner = OUTER - BAND;
    return dx * dx + dy * dy >= inner * inner;
}

void AircraftManager::DismissVisualAlert()
{
    visualAlertDismissed = true;
    visualRingColor = 0;
    flashBurstUntilMs = 0;
    if (visualAlertActive) {
        visualAlertActive = false;
        lastBrightnessCheck = 0; // release any night-brightness override now
    }
    Serial.printf("[alert] %lu visual alert dismissed by tap\n", millis());
}

constexpr unsigned long RANK_TOAST_MS = 6000; // how long the rank-up banner stays up

void AircraftManager::PushClaimToast(const String& text)
{
    if (claimToastCount >= CLAIM_TOAST_QUEUE)
        return; // full: drop the newest rather than evict one already promised
    claimToastText[claimToastCount++] = text;
    if (claimToastUntilMs == 0)
        claimToastUntilMs = millis() + CLAIM_TOAST_MS; // start the front entry's dwell
}

void AircraftManager::UpdateClaimToast()
{
    if (claimToastCount == 0)
        return;

    // HOLD THE DWELL WHILE THE CARD IS UP. The claim fires when the detail card
    // OPENS, but Draw() returns straight after drawing the card and never reaches
    // DrawClaimToast -- so the dwell was counting down behind a screen that
    // covers the toast's seat. Anyone who read the card for more than ~2.2 s
    // closed it to nothing, which is every normal use: the confirmation was
    // invisible in the exact flow that produces it. Re-arming here starts the
    // dwell when the toast can actually be seen. Nothing is left unconfirmed in
    // the meantime -- the card draws its own "* CLAIMED #n *" line.
    if (inDetail) {
        claimToastUntilMs = millis() + CLAIM_TOAST_MS;
        return;
    }

    if ((long)(millis() - claimToastUntilMs) < 0)
        return; // front entry still showing
    // Retire the front entry and start the next one, if any.
    for (size_t i = 1; i < claimToastCount; ++i)
        claimToastText[i - 1] = claimToastText[i];
    --claimToastCount;
    claimToastUntilMs = claimToastCount > 0 ? millis() + CLAIM_TOAST_MS : 0;
}

void AircraftManager::DrawClaimToast(BandCanvas& backbuffer) const
{
    if (claimToastCount == 0)
        return;

    // Same gold pill as the rank toast, but seated BELOW centre so a claim landing
    // during a rank-up doesn't collide with it. Deliberately the same visual
    // language: both mean "you gained something".
    const String& text = claimToastText[0];
    backbuffer.setTextSize(1);
    const int tw = (int)backbuffer.textWidth(text);
    const int th = backbuffer.fontHeight();
    const int padX = 10, padY = 6;
    const int w = tw + 2 * padX;
    const int h = th + 2 * padY;
    const int cx = SCREEN_SIZE_DIV_2;
    const int cy = SCREEN_SIZE_DIV_2 + SCREEN_SIZE / 5;
    const int x = cx - w / 2, y = cy - h / 2;

    backbuffer.fillRoundRect(x, y, w, h, 5, lgfx::color888(40, 30, 0));
    backbuffer.drawRoundRect(x, y, w, h, 5, lgfx::color888(255, 210, 0));
    backbuffer.setTextColor(lgfx::color888(255, 210, 0));
    backbuffer.drawString(text, cx - tw / 2, cy - th / 2);

    // More than one waiting: a small tally so three fast claims read as three.
    if (claimToastCount > 1) {
        const String more = "+" + String((int)claimToastCount - 1);
        backbuffer.drawString(more, x + w + 4, cy - th / 2);
    }
}

void AircraftManager::ClaimTappedAircraft(TrackedAircraft& tracked)
{
    // THE CLAIM. Deliberately NOT bound to the tap itself: the type code arrives
    // from enrichment, not from the position feed, so a tap on an un-enriched
    // blip has nothing to claim yet. Binding it to "card open AND type known"
    // instead means the claim lands with the REVEAL -- tap an anonymous contact,
    // it resolves to a C-5 nobody has claimed, and it is yours in that moment.
    // Binding it to the tap would silently drop every claim on a cold contact.
    if (!logbookEnabled || tracked.typeCode.isEmpty() || tracked.claimFired)
        return;
    if (!logbook.ClaimType(tracked.typeCode))
        return; // already claimed (or never seen): nothing to celebrate

    tracked.claimFired = true;
    tracked.claimable = false;
    usageStore.LogbookClaim();

    // A tap claims EVERYTHING the aircraft carries. One deliberate action credits
    // the whole card, which keeps airlines/countries/airports meaningful as
    // collection categories without inventing a second mechanic for each -- and
    // without letting them accrue passively, which is what made a busy sky win.
    logbook.ClaimOperator(tracked.operatorName);
    logbook.ClaimCountry(tracked.state.originCountry);
    logbook.ClaimAirport(tracked.routeOrigin);
    logbook.ClaimAirport(tracked.routeDest);

    // Claiming is per TYPE, so every other contact of this type on screen stops
    // being claimable in the same instant. Without this sweep their badges would
    // sit there until each one happened to be re-enriched -- inviting taps that
    // silently earn nothing, which is exactly the "did that do anything?" feeling
    // the confirmation exists to avoid. One pass over at most a few dozen
    // contacts, on an event that happens a handful of times a day.
    const String& claimedType = tracked.typeCode;
    for (auto& [icao, other] : trackedAircraft)
        if (other.claimable && other.typeCode == claimedType)
            other.claimable = false;

    const String label = !tracked.typeCode.isEmpty() ? tracked.typeCode : String("TYPE");
    PushClaimToast("CLAIMED " + label + "  #" + String((int)logbook.ClaimedTypeCount()));
    Serial.printf("[claim] %s claimed (%u/%u types)\n", label.c_str(),
                  (unsigned)logbook.ClaimedTypeCount(), (unsigned)logbook.TypeCount());
}

// One [perf] line a minute: the whole feed-vs-contention question in a form that
// can be read straight off a serial capture, with no post-processing and no join.
//
// HOW TO READ IT. `busy` is the share of the window the shared HTTP client spent
// working, split fetch/enrich -- that is the contention term, and it is the ONLY
// thing device-side scheduling can improve. `lag` is how old the data already was
// when it arrived, which no amount of device scheduling can fix. `gapMax` is the
// longest we went without a good merge.
//
//   lag high, busy low     -> upstream drought. Feed workstream. Build nothing here.
//   busy high, lag low     -> contention. The capacity plan is justified.
//   both low, episodes > 0 -> neither: look at the relay log for the window.
//
// `ac` is received/kept: a large gap is parse and bandwidth spent on aircraft
// thrown away by the MAX_AIRCRAFT cut, and is the case for Worker-side
// truncation. `cache` is the proxy's own attribution (HIT/STALE/MISS).
void AircraftManager::ReportPerf()
{
    if (perf.polls == 0 && perf.enrichReqs == 0)
        return; // nothing happened; a line of zeros is noise

    const unsigned long windowMs = 60000UL;
    const unsigned long busyMs = perf.fetchBusyMs + perf.enrichBusyMs;
    const uint32_t n = perf.polls ? perf.polls : 1;

    // UTC so an episode lines up with the relay log. Falls back to uptime before NTP.
    char stamp[24];
    const time_t utc = time(nullptr);
    if (utc > 1600000000) {
        struct tm t;
        gmtime_r(&utc, &t);
        snprintf(stamp, sizeof(stamp), "%04d-%02d-%02dT%02d:%02d:%02dZ",
                 t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec);
    } else {
        snprintf(stamp, sizeof(stamp), "up+%lus", millis() / 1000UL);
    }

    Serial.printf("[perf] %s polls=%lu busy=%lu%%(fetch=%lu%% enrich=%lu%%) "
                  "parse=%lums/poll bytes=%lu/poll ac=%lu/%lu "
                  "lag=avg%lums,max%lums gapMax=%lums episodes=%lu "
                  "cache=H%lu/S%lu/M%lu enrichReqs=%lu\n",
                  stamp,
                  (unsigned long)perf.polls,
                  busyMs * 100UL / windowMs,
                  perf.fetchBusyMs * 100UL / windowMs,
                  perf.enrichBusyMs * 100UL / windowMs,
                  perf.parseMs / n,
                  (unsigned long)(perf.bodyBytes / n),
                  (unsigned long)(perf.acReceived / n),
                  (unsigned long)(perf.acKept / n),
                  perf.lagSumMs / n, perf.lagMaxMs,
                  perf.gapMaxMs,
                  (unsigned long)perf.episodes,
                  (unsigned long)perf.cacheHit, (unsigned long)perf.cacheStale,
                  (unsigned long)perf.cacheMiss,
                  (unsigned long)perf.enrichReqs);

    perf = PerfWindow{}; // windows are independent; a running total hides the episode
}

void AircraftManager::DrawRankToast(BandCanvas& backbuffer) const
{

#ifdef FEATURE_CLOUD_FEED
    if ((long)(millis() - rankToastUntilMs) >= 0)
        return; // expired (or never armed)

    // A gold pill centred on the face, above the middle so it doesn't bury the
    // clock/rank block. Transient (~6 s) and celebratory; the picture underneath
    // is redrawn every frame, so nothing needs restoring when it clears.
    char buf[32];
    snprintf(buf, sizeof(buf), "RANK UP  #%d  +%d", rankToastRank, rankToastDelta);

    backbuffer.setTextSize(1);
    const int tw = (int)backbuffer.textWidth(buf);
    const int th = backbuffer.fontHeight();
    const int padX = 10, padY = 6;
    const int w = tw + 2 * padX;
    const int h = th + 2 * padY;
    const int cx = SCREEN_SIZE_DIV_2;
    const int cy = SCREEN_SIZE_DIV_2 - SCREEN_SIZE / 6; // a touch above centre
    const int x = cx - w / 2, y = cy - h / 2;

    backbuffer.fillRoundRect(x, y, w, h, 5, lgfx::color888(40, 30, 0)); // dim gold ground
    backbuffer.drawRoundRect(x, y, w, h, 5, lgfx::color888(255, 210, 0));
    backbuffer.setTextColor(lgfx::color888(255, 210, 0));               // gold: a score
    backbuffer.drawString(buf, cx - tw / 2, cy - th / 2);
#else
    (void)backbuffer;
#endif
}

// ---------------------------------------------------------------------------
// Follow Mode stage 1 (docs/follow-mode-consolidated.md §4)
// ---------------------------------------------------------------------------

// Identity match against the single follow field. Deliberately the same
// semantics as ClassifyWatchlist's WatchClass::Specific arm -- callsign, ICAO
// address or registration prefix -- because §14 says to reuse that path and
// because a second, subtly different notion of "this exact aircraft" is how the
// two end up disagreeing. TYPE is NOT matched here: "follow every C172" is a
// category, and following a category is not a thing this feature means.
bool AircraftManager::MatchesFollow(const TrackedAircraft& tracked) const
{
    // The SESSION target wins while it is set (docs/tap-to-peek.md). One
    // accessor rather than a branch at every call site: a second place that
    // decided which target was in force is a second place to get it wrong.
    const String followTarget = EffectiveFollowTarget();
    if (followTarget.isEmpty())
        return false;

    String callsign = tracked.state.callsign; callsign.trim(); callsign.toLowerCase();
    if (!callsign.isEmpty() && callsign.startsWith(followTarget)) return true;
    String icao = tracked.state.icao24; icao.toLowerCase();
    if (!icao.isEmpty() && icao.startsWith(followTarget)) return true;
    String reg = tracked.registration; reg.toLowerCase();
    if (!reg.isEmpty() && reg.startsWith(followTarget)) return true;
    return false;
}

// Feed the track from whatever the contact table currently holds.
//
// Called once per merge, on the loop task, like everything else that touches
// shared state. The track deliberately does NOT live on the TrackedAircraft: a
// contact absent past the grace window is evicted and rebuilt from scratch
// (§3), which for a trainer at pattern altitude is the normal case rather than
// an edge case. Losing the aircraft loses nothing here -- the buffer is the
// manager's and simply stops being appended to until it comes back.
void AircraftManager::UpdateFollowTrack()
{
    // The state machine runs whenever a target is set -- it is the feature
    // (§19), and it does not depend on the track buffer existing. A board that
    // degraded to notification-only still needs to know whether he is airborne.
    const bool following = !EffectiveFollowTarget().isEmpty();
    followMachine.SetTarget(following);
    if (!following)
        return;

    followHome.lat = (float)lat;
    followHome.lon = (float)lon;
    // WHERE home is: known whenever the device has a location. This is the same
    // point the radar is centred on, so it is exactly as good as everything else
    // on the screen.
    followHome.positionKnown = hasLocation;
    // HOW HIGH the field is: C5's delivery half is not built (the CC0 corpus
    // carries AltitudeFeet for 34,128 fields and no running code writes it to
    // KV), so AGL reasoning is unavailable and the machine declines the two
    // arguments that need it rather than substituting MSL.
    //
    // These were ONE flag until the local face was built, and gating the first
    // on the second is what made every circuit dropout read SIGNAL LOST -- the
    // failure §10 names in bold. See follow::HomeContext.
    followHome.elevationKnown = false;

    ResolveHomeField();


    const unsigned long nowMs = millis();
    bool seen = false;
    for (auto& [hex, tracked] : trackedAircraft) {
        if (!MatchesFollow(tracked))
            continue;
        auto [aLat, aLon] = tracked.GetDisplayPosition();

        follow::Fix f;
        f.onGround        = tracked.state.onGround;
        f.baroAltFt       = tracked.state.baroAltitude * 3.28084f;
        f.geoAltFt        = tracked.state.geoAltitude * 3.28084f;
        f.velocityKt      = tracked.state.velocity * 1.94384f;
        f.verticalRateFpm = tracked.state.verticalRate * 196.850f;
        f.lat = aLat; f.lon = aLon;
        followMachine.OnFix(f, (uint32_t)nowMs, followHome);
        followStats.OnFix(f, (uint32_t)nowMs, followHome);

        // §8: the route is captured HERE, from the live contact, and held for
        // the flight. It has to be -- the arc's whole job is to keep drawing
        // through the states in which this loop finds nothing, and the codes
        // arrive on the aircraft that has just vanished from the table.
        //
        // §17/C2: this is a READ off an enrich result the radar already fetched
        // for a contact that is overhead. Nothing is requested because of the
        // follow, which is the line C2 draws: a route fetched FOR the follow
        // target would be an outbound request whose existence names it.
        if (!tracked.routeOrigin.isEmpty() && !tracked.routeDest.isEmpty()) {
            followRouteOrigin = tracked.routeOrigin;
            followRouteDest   = tracked.routeDest;
        }
        // C4: the aircraft exists, so the stale-follow nudge must never fire.
        followLog.Disarm();

        // A taxiing aeroplane is not a flight path: the state machine wants
        // the on-ground fix, the TRACK does not.
        if (!tracked.state.onGround && followTrack.Active())
            followTrack.Append(aLat, aLon);
        seen = true;
        break; // one followed aircraft; first identity match wins
    }
    if (!seen) {
        // Not in the contact table this pass. Absence is a state, not a gap --
        // the machine decides which KIND after trackLostMs (§5.1).
        // The destination, when the route is known -- so ApproachLost asks
        // about where the flight is GOING rather than where the owner lives.
        // Empty codes leave it unknown and the machine falls back to home,
        // which is the correct destination in the local regime.
        follow::DestContext dctx;
        {
            const follow::Endpoint d = follow::LookupAirport(followRouteDest.c_str());
            if (d.known) {
                dctx.lat = d.lat; dctx.lon = d.lon;
                dctx.radiusKm = follow::APPROACH_RADIUS_KM;
                dctx.known = true;
            }
        }
        followMachine.OnNoFix((uint32_t)nowMs, followHome, dctx);
        // C4: start the stale clock the first time we are waiting on an
        // aircraft we have never seen. Arm() is a no-op once armed and a no-op
        // without a synced clock, so this is one NVS write per follow target.
        if (followMachine.Current() == follow::State::Waiting) {
            const time_t nowUtc = time(nullptr);
            if (nowUtc > 1600000000) followLog.Arm((uint32_t)nowUtc);
        }
    }
    // ONE call, on both paths. It used to sit only under OnNoFix, behind an
    // early return from the loop -- which meant the two transitions §13.3
    // actually names, takeoff and landing, were the only two it could never
    // see: both are reached from OnFix.
    HandleFollowTransition();
}

// The home field's CODE, from the airport data the device already has.
//
// §10 says the local face adds no dataset, and this adds none: the cloud
// airports overlay is already fetched and cached for the radar, and
// include/Airports.h is the baked fallback that ships on every board. What is
// missing is elevation -- neither source carries it -- which is C5's delivery
// half and is why followHome.elevationKnown stays false.
//
// THE CODE, NEVER A NAME. §10 is explicit: it is "Local flight: EGYD", never
// "Local flight: Cranwell". The device has identifiers and coordinates; names
// would be a new table and a flash cost for something nothing in Follow needs.
//
// Resolved ONCE. The airport tables do not move and neither does the configured
// location, so re-walking a few hundred entries every merge would be work done
// to reach the same answer. Cleared when the location changes (Initialise).
void AircraftManager::ResolveHomeField()
{
    if (followHomeCodeResolved || !hasLocation)
        return;
    followHomeCodeResolved = true;
    followHomeCode = "";

    // A field further than this is not "the home field" in any sense a pilot
    // would recognise, and claiming one would put a wrong code under the marker
    // the whole face is built around. Better to draw HOME.
    constexpr float CLAIM_KM = 12.0f;
    float bestKm = CLAIM_KM;

    const auto consider = [&](float apLat, float apLon, const char* code) {
        const float d = follow::SeparationKm(apLat, apLon, (float)lat, (float)lon);
        if (d < bestKm) { bestKm = d; followHomeCode = code; }
    };

#ifdef FEATURE_CLOUD_FEED
    for (const CloudFeed::CloudAirport& ap : cloudAirports)
        consider(ap.lat, ap.lon, ap.code);
#endif
    if (followHomeCode.isEmpty())
        for (size_t i = 0; i < AIRPORT_COUNT; ++i)
            consider(AIRPORTS[i].lat, AIRPORTS[i].lon, AIRPORTS[i].code);

    followHomeCode.toUpperCase();
    Serial.printf("[follow] home field: %s (%.1f km)\n",
                  followHomeCode.isEmpty() ? "(none within 12 km -- drawing HOME)"
                                           : followHomeCode.c_str(),
                  (double)bestKm);
}

// Everything that happens when the state CHANGES: the flight is frozen (§11), a
// new one is started, and the screen surfaces for a dwell (§13.3).
//
// One function because the three are one event and splitting them is how the
// record ends up written on a transition the screen does not surface on, or
// vice versa. It already happened once in miniature: the auto-surface lived
// under OnNoFix, behind an early return, so takeoff and landing -- the only two
// transitions §13.3 names -- were the two it could never see.
//
// §13.3's restraint is the design. Follow gets a screen; it never gets THE
// screen. A followed aircraft airborne while something rare flies over must not
// steal the display: the rare aircraft keeps its NEW highlight, the followed
// aircraft keeps its ring, and neither wins. The only thing that ever takes the
// screen is a transition, twice a flight.
void AircraftManager::HandleFollowTransition()
{
    const follow::State now = followMachine.Current();
    const follow::State was = followLastState;
    followLastState = now;

    // Return first, so a dwell that expired this pass frees the screen even if
    // nothing transitioned.
    if (followAutoUntilMs && millis() >= followAutoUntilMs) {
        followAutoUntilMs = 0;
        if (screen == Screen::Follow)
            screen = followAutoReturnTo;
    }

    if (now == was || !FollowScreenVisible())
        return;

    // ---- LANDED: freeze the flight (§11) ------------------------------------
    //
    // Written HERE and nowhere else, on the transition, so it is one write per
    // flight by construction rather than by a debounce that has to be trusted.
    // The state machine's rail is what makes this safe to write at all: Landed
    // fires only on confident evidence, so a card can never appear for a flight
    // that merely stopped being heard (§5.4).
    if (now == follow::State::Landed) {
        follow::FlightRecord summary;
        summary.durationSec   = followStats.DurationSec();
        summary.maxAltMslFt   = (int32_t)lroundf(followStats.maxAltMslFt);
        // Clamped, not wrapped. A garbage velocity in one fix must cost a wrong
        // top speed, never a top speed that reads 12 kt because it overflowed.
        summary.topSpeedKt    = (uint16_t)std::min(65535.0f, std::max(0.0f, followStats.topSpeedKt));
        summary.furthestKmX10 = (uint16_t)std::min(65535.0f, std::max(0.0f, followStats.furthestKm * 10.0f));
        // 0 means the clock was never synced. Recorded as 0 and rendered as
        // "time unknown" rather than as 1970, which would be a date the card
        // states confidently and is wrong.
        const time_t nowUtc = time(nullptr);
        summary.landedEpoch = (nowUtc > 1600000000) ? (uint32_t)nowUtc : 0u;
        followLog.Save(followTrack, summary);
    }

    // ---- AIRBORNE after a landing: a NEW flight ------------------------------
    //
    // The buffer's write index moves; the ALLOCATION is untouched. §4.3 keeps
    // those two as separate calls for exactly this moment -- freeing and
    // re-allocating 12 KB twice a flight is the fragmentation behaviour the
    // whole discipline exists to avoid.
    //
    // Ordering matters and is the reason this sits after the block above: the
    // card is written from the track, so resetting the track before saving
    // would leave a flight with no shape.
    if (now == follow::State::Airborne &&
        (was == follow::State::Landed || was == follow::State::Ground ||
         was == follow::State::Waiting)) {
        followTrack.ResetFlight();
        followStats.Reset();
        // The route belongs to the FLIGHT, not the aircraft. Yesterday's
        // LHR->JFK on today's departure would draw a confident arc to the wrong
        // continent, and the regime -- which is read off followStats -- has
        // just been reset with it, so the two stay consistent.
        followRouteOrigin = "";
        followRouteDest   = "";
    }

    SendFollowAlert(was, now);

    // Takeoff and landing. Not the absence states: a dropout is the NORMAL
    // operating condition at pattern altitude (§3), and a screen that jumped on
    // every one of them would be the device looking broken every circuit in a
    // second, louder way than the copy could ever fix.
    const bool worthShowing = (now == follow::State::Airborne) ||
                              (now == follow::State::Landed);
    if (!worthShowing)
        return;
    // Never over a card or a destructive confirmation.
    if (inDetail || resetMenu != ResetMenu::Closed)
        return;

    if (screen != Screen::Follow) {
        followAutoReturnTo = screen;
        screen = Screen::Follow;   // NOT EnterScreen: the device surfaced this,
                                   // the customer did not. See EnterScreen.
    }
    followAutoUntilMs = millis() + FOLLOW_AUTO_DWELL_MS;
    Serial.printf("[follow] auto-surfaced on %s\n", follow::Headline(now));
}

#ifdef FOLLOW_BENCH
// Set a session follow from the bench, with a canned route.
//
// The real entry point takes a TrackedAircraft off the contact table, and a
// bench has no aeroplanes. This forges the same END STATE -- session target,
// route, reset track and stats -- so what Saturday judges is the real face
// reading real members, not a mock.
void AircraftManager::BenchSessionFollow(const char* label, const char* org, const char* dst)
{
    followSessionTarget = label;
    followSessionTarget.toLowerCase();
    followRouteOrigin = org;
    followRouteDest = dst;
    followTrack.ResetFlight();
    followStats.Reset();
    // Push the extent past the home radius so §7.1's inference lands on the
    // airline regime -- the same input a real departure moves, rather than a
    // flag that bypasses the routing being judged.
    followStats.furthestKm = 4000.0f;
    followMachine.SetTarget(true);
    followForce = true;
    followForced = follow::State::Airborne;
    followBenchLongHaul = true;
    screen = Screen::Follow;   // NOT EnterScreen: synthetic. A bench key must not
                               // move a shipping counter. See EnterScreen.
    followAutoUntilMs = 0;   // no dwell: the bench wants it to STAY
    // Length computed first: the token must not appear in the printf
    // statement even when only its LENGTH is used. I made this exact
    // mistake twice in one session, which is the argument for the check
    // having no allow list.
    const unsigned n = (unsigned)followSessionTarget.length();
    Serial.printf("[bench] session follow: %s->%s (%u chars)\n",
                  org, dst, n);
}

// Force the display state from serial, so §6's copy can be judged on glass.
//
// THE ABSENCE COPY IS THE EMOTIONAL CORE OF THE FEATURE (§6) and it is the one
// part that cannot be graded anywhere but on the panel. The host suite asserts
// that the strings differ and that the machine picks the right one; neither
// tells you whether "BELOW COVERAGE / Ground receivers do not reach where he is
// now." reads as reassuring at 240 px in a dim room. That needs eyes.
//
// Reaching those states honestly means cutting the network and waiting three
// minutes per state, on a bench where the router is not ours to pull. So the
// state is forced FOR DISPLAY ONLY: followMachine is untouched, the transitions
// stay exactly as graded, and nothing here exists outside FOLLOW_BENCH.
//
// Keys, one character, no newline needed:
//   1  AIRBORNE          4  ON APPROACH - SIGNAL LOST
//   2  ON THE GROUND     5  SIGNAL LOST
//   3  BELOW COVERAGE    0  release, back to the real machine
//   n  next state (cycles) -- one key, so it can be driven blind
void AircraftManager::PollBenchSerial()
{
    static bool announced = false;
    if (!announced) {
        announced = true;
        Serial.println("[bench] follow state override: 1=AIRBORNE 2=GROUND 3=BELOW-COVERAGE "
                       "4=APPROACH-LOST 5=SIGNAL-LOST 0=release n=next");
        Serial.println("[bench] arc face: l=synthetic DEN->DEL long-haul (toggle) "
                       "p=step progress 15/45/80% w=WAITING (C4 face)");
        Serial.println("[bench] session follow: s=LHR->JFK (globe) a=LHR->BCN (arc) "
                       "u=unresolvable codes (code-only arc) r=resolving x=clear");
    }
    while (Serial.available()) {
        const int ch = Serial.read();
        follow::State want = followForced;
        bool set = true;
        switch (ch) {
            case '1': want = follow::State::Airborne;     break;
            case '2': want = follow::State::Ground;       break;
            case '3': want = follow::State::NoCoverage;   break;
            case '4': want = follow::State::ApproachLost; break;
            case '5': want = follow::State::SignalLost;   break;
            case '0':
                followForce = false;
                Serial.println("[bench] follow state override RELEASED -- showing the real machine");
                continue;
            case 'n': case 'N': {
                static const follow::State CYCLE[5] = {
                    follow::State::Airborne, follow::State::Ground,
                    follow::State::NoCoverage, follow::State::ApproachLost,
                    follow::State::SignalLost };
                int i = 0;
                for (; i < 5; ++i) if (CYCLE[i] == followForced) break;
                want = CYCLE[(i + 1) % 5];
                break;
            }
            // ---- stage 2: the synthetic long-haul -------------------------
            //
            // The bench cannot produce an airliner over the pole, and the two
            // things the arc face needs are exactly the two a bench cannot
            // supply: a ROUTE (which only ever arrives on a live enriched
            // contact) and a POSITION along it. Both are forged here and
            // nowhere else -- followMachine is untouched, and none of this
            // exists outside FOLLOW_BENCH.
            //
            // DEN->DEL is §9's worked example on purpose: 12,406 km over the
            // pole is the case that justifies the globe, so the same key
            // exercises both faces and the scale threshold between them.
            case 'l': case 'L':
                followBenchLongHaul = !followBenchLongHaul;
                if (followBenchLongHaul) {
                    followRouteOrigin = "DEN";
                    followRouteDest   = "DEL";
                    // Push the flight's extent past the home radius so §7.1's
                    // inference lands on the airline regime -- the same input
                    // a real departure would move, rather than a flag that
                    // bypasses the routing being tested.
                    followStats.furthestKm = 4000.0f;
                } else {
                    followRouteOrigin = ""; followRouteDest = "";
                    followStats.furthestKm = 0.0f;
                }
                Serial.printf("[bench] synthetic long-haul %s (DEN->DEL, %.0f%%)\n",
                              followBenchLongHaul ? "ON" : "OFF",
                              (double)(followBenchProgress * 100.0f));
                continue;
            case 'p': case 'P': {
                // 15% (near the origin), 45% (mid-ocean -- the NO_COVERAGE
                // case), 80% (nearly there -- the APPROACH_LOST case).
                followBenchProgress = followBenchProgress < 0.2f ? 0.45f
                                    : (followBenchProgress < 0.6f ? 0.80f : 0.15f);
                float pLat = 0.0f, pLon = 0.0f;
                const follow::Endpoint o = follow::LookupAirport(followRouteOrigin.c_str());
                const follow::Endpoint d = follow::LookupAirport(followRouteDest.c_str());
                if (o.known && d.known)
                    follow::InterpolateGreatCircle(o, d, followBenchProgress, pLat, pLon);
                Serial.printf("[bench] synthetic progress %.0f%% -> %.2f, %.2f\n",
                              (double)(followBenchProgress * 100.0f),
                              (double)pLat, (double)pLon);
                continue;
            }
            case 'w': case 'W':
                want = follow::State::Waiting;
                break;
            // ---- session follow, the three route cases (docs/tap-to-peek.md) --
            //
            // Saturday needs all THREE on glass, not just the happy path: a
            // resolvable long haul, a resolvable short hop, and codes the baked
            // table cannot place. The third is the one that would otherwise
            // never be seen until a customer met it.
            case 's': case 'S':   // long haul -> globe (5,540 km >= 4,000)
                BenchSessionFollow("bench-lhr-jfk", "LHR", "JFK");
                continue;
            case 'a': case 'A':   // short hop -> arc (1,147 km < 4,000)
                BenchSessionFollow("bench-lhr-bcn", "LHR", "BCN");
                continue;
            case 'u': case 'U':   // codes the table cannot resolve -> code-only arc
                BenchSessionFollow("bench-unknown", "ZQX", "QZY");
                continue;
            case 'r': case 'R':
                followBenchResolving = !followBenchResolving;
                Serial.printf("[bench] resolving state %s\n",
                              followBenchResolving ? "FORCED" : "off");
                continue;
            case 'x': case 'X':
                followBenchResolving = false;
                followBenchLongHaul = false;
                ClearSessionFollow();
                continue;
            default: set = false; break;
        }
        if (!set) continue;
        followForced = want;
        followForce = true;
        Serial.printf("[bench] follow state FORCED: %s | %s\n",
                      follow::Headline(want, FollowRegime()),
                      follow::Explanation(want, FollowRegime()));
    }
}
#endif

// The measurement this whole build exists to take (§18.1).
//
// The frame budget note above records 27.5-31.1 ms under full load with overlay
// and trails. Projecting and drawing up to 1024 extra segments per frame could
// blow that outright, so the cap is a DRAW-TIME cap with adaptive stride: the
// buffer keeps all 1024 points, and the draw walks it in steps sized to land at
// or under FOLLOW_DRAW_SEGMENT_CAP however full it is. Decimating the STORAGE
// instead would be cheaper here and would throw away the shape the feature is
// about.
//
// Timed around the track alone. The existing [health] frame figure cannot answer
// "what did the track cost" -- it is one number for everything -- and that is
// precisely the question that decides whether Follow is a track product or a
// notification product.
void AircraftManager::DrawFollowTrack(BandCanvas& backbuffer)
{
    followDrawSegments = 0;
    if (!followDrawTrack || !followTrack.Active())
        return;
    const size_t n = followTrack.Size();
    if (n < 2)
        return;

    const int64_t t0 = esp_timer_get_time();

    const size_t stride = follow::Track::StrideFor(n, FOLLOW_DRAW_SEGMENT_CAP);

    // §4.5: one dim distinct colour, drawn BENEATH everything else, and exempt
    // from the sweep's phosphor fade. A radar return decays because it is a
    // RETURN; the track is a RECORD. Holding it steady while returns pulse around
    // it is what tells you at a glance which marks are live and which are
    // history -- so no brightness argument is taken here, deliberately.
    constexpr uint32_t TRACK_COLOR = 0x0060A0u; // dim cyan-blue: not the green of a return
    int prevX = 0, prevY = 0;
    bool havePrev = false;
    size_t segments = 0;

    for (size_t i = 0; i < n; i += stride) {
        auto [x, y] = ProjectCoordinateToScreen(followTrack.At(i).lat, followTrack.At(i).lon);
        if (havePrev) {
            backbuffer.drawLine(prevX, prevY, x, y, TRACK_COLOR);
            ++segments;
        }
        prevX = x; prevY = y; havePrev = true;
    }
    // The newest point always gets drawn even when the stride skipped it -- the
    // head of the track is the one segment a viewer is actually looking at.
    if (havePrev && ((n - 1) % stride) != 0) {
        auto [x, y] = ProjectCoordinateToScreen(followTrack.At(n - 1).lat, followTrack.At(n - 1).lon);
        backbuffer.drawLine(prevX, prevY, x, y, TRACK_COLOR);
        ++segments;
    }

    const uint32_t us = (uint32_t)(esp_timer_get_time() - t0);
    followDrawUs = us;
    followDrawSegments = segments;
    if (us > followDrawMaxUs) followDrawMaxUs = us;
    followDrawSumUs += us;
    ++followDrawFrames;
}

// The on-screen half of the measurement, so the number can be read off the bench
// without a serial console attached. Serial carries the same figures in the
// [health] line; this exists because the person measuring is looking at a board.
void AircraftManager::DrawFollowHud(BandCanvas& backbuffer) const
{
    if (followTarget.isEmpty())
        return;

    char buf[64];
    if (followTrack.Active()) {
        const uint32_t meanUs = followDrawFrames ? (followDrawSumUs / followDrawFrames) : 0;
        snprintf(buf, sizeof(buf), "trk %u/%u s%u  %.1f/%.1fms",
                 (unsigned)followTrack.Size(), (unsigned)follow::Track::CAPACITY,
                 (unsigned)followDrawSegments, meanUs / 1000.0f, followDrawMaxUs / 1000.0f);
    } else if (followTrack.Degraded()) {
        // Say the real state. A blank track that looks like a bug is worse than
        // a line admitting the allocation failed (§4.3).
        snprintf(buf, sizeof(buf), "trk OFF: no PSRAM (notify only)");
    } else {
        snprintf(buf, sizeof(buf), "trk off");
    }

    // Plain top-left draw, x centred by hand from the 6 px default-font advance.
    // BandCanvas forwards a deliberately small surface (drawString/drawLine/
    // setTextColor/setTextSize and little else) and does NOT forward
    // setTextDatum, so a datum call here would not compile -- and adding one to
    // the wrapper for a bench HUD would widen a shared type for a temporary
    // readout.
    constexpr int CHAR_W = 6;
    const int w = (int)strlen(buf) * CHAR_W;
    backbuffer.setTextSize(1);
    backbuffer.setTextColor(lgfx::color888(0, 150, 200));
    backbuffer.drawString(buf, SCREEN_SIZE_DIV_2 - w / 2, SCREEN_SIZE - 12);
}

// ===========================================================================
// THE LOCAL FACE (§10) -- the flight-school regime, which ships first (§1.1)
// ===========================================================================
//
// A radar scope, which is what the product already is. What it deliberately is
// NOT is a map:
//
//   "A circuit is roughly 2-5 km across. Fitting 5 km to 120 px needs a radius
//    around 153,000 px, at which one pixel is about 20 m. Coastlines are
//    meaningless at that scale; the reference you would actually want is runways
//    and taxiways, which the device does not carry and should not. So the local
//    face draws no map at all. THE TRAIL IS THE PICTURE."
//
// That is the good news in this whole feature: no dataset, no Worker delivery,
// no zoom dilemma, no licence question. Everything below is generated from
// positions the device already receives.

// Fit a string to the chord of the round panel at row `yTop`.
//
// The geometry is in Layout.h (ChordWidthPx) and is graded on the workstation;
// only the ellipsis needs a display object, because only the display knows how
// wide a glyph is. Same helper the Stats screen uses for the SSID -- there is
// one implementation, because the bench found the second surface with the same
// defect and a third would have found a third.
String AircraftManager::FitToDisc(BandCanvas& backbuffer, const String& t,
                                  int yTop, int lineH) const
{
    const int avail = ChordWidthPx(yTop, lineH);
    if (avail <= 0) return String();
    if ((int)backbuffer.textWidth(t) <= avail) return t;
    String out = t;
    while (out.length() > 1 && (int)backbuffer.textWidth(out + "...") > avail)
        out.remove(out.length() - 1);
    return out + "...";
}

const TrackedAircraft* AircraftManager::FollowedAircraft() const
{
    if (followTarget.isEmpty())
        return nullptr;
    for (const auto& [hex, tracked] : trackedAircraft) {
        (void)hex;
        if (MatchesFollow(tracked))
            return &tracked;
    }
    return nullptr; // the normal case at pattern altitude, not an error (§3)
}

// Auto-fit, and §9's no-continuous-zoom rule does not apply here: that rule is
// about SAMPLED geography, where zooming past the decimation exposes it. The
// reference here is GENERATED -- rings drawn at whatever radius the data needs
// and labelled with it -- so there is nothing to expose. "Auto-scaling is
// correct here, and only here."
follow::LocalView AircraftManager::BuildLocalView() const
{
    follow::LocalView v;
    v.centreLat = followHome.lat;
    v.centreLon = followHome.lon;
    v.rings = 3;

    // The extent the rings have to contain: every stored track point, plus where
    // he is right now. Including the live position matters -- fitting the track
    // alone would let the aeroplane sit outside the outermost ring on the one
    // frame it flies beyond everything it has already flown, which is every
    // frame of an outbound leg.
    float maxKm = 0.0f;
    const size_t n = followTrack.Size();
    for (size_t i = 0; i < n; ++i) {
        const follow::TrackPoint& p = followTrack.At(i);
        const float d = follow::SeparationKm(p.lat, p.lon, v.centreLat, v.centreLon);
        if (d > maxKm) maxKm = d;
    }
    if (const TrackedAircraft* t = FollowedAircraft()) {
        auto [aLat, aLon] = t->GetDisplayPosition();
        const float d = follow::SeparationKm(aLat, aLon, v.centreLat, v.centreLon);
        if (d > maxKm) maxKm = d;
    }

    // THE LADDER IS WALKED IN THE CUSTOMER'S UNIT, not in kilometres. A step
    // that is round in km is 0.62 / 1.24 / 3.11 in miles, and a ring labelled
    // 3.11 is a ring nobody reads. One conversion out, one back, both through
    // include/DisplayUnits.h -- see FollowGeometry.h.
    const float maxDisplay = units::FromKm(maxKm, rangeUnit);
    v.stepDisplay = follow::NiceStep(maxDisplay, v.rings);
    v.radiusKm = (float)units::ToKm(v.stepDisplay * v.rings, rangeUnit);
    return v;
}

std::pair<int, int> AircraftManager::ProjectLocal(float pLat, float pLon,
                                                  const follow::LocalView& v) const
{
    float x = 0.0f, y = 0.0f;
    follow::ProjectLocal(pLat, pLon, v, rotSin, rotCos,
                         (float)SCREEN_SIZE, (float)FOLLOW_FACE_RADIUS_PX, x, y);
    return { (int)lroundf(x), (int)lroundf(y) };
}

// The router (C6). One screen slot, several faces, chosen by regime and state
// per §7.1 -- never picked by the customer. The local face is the only one
// built; the arc face, the globe and the post-flight card are §19 items 4 and
// 8-10, and until they exist an airline-regime follow gets the local face's
// honest states rather than a face that pretends to know a route.
bool AircraftManager::ShowPostFlightCard() const
{
    if (!followLog.Has())
        return false;
    // 7.1: LANDED until the next takeoff. WAITING is in the list because it
    // is the state a device boots into -- and the card is the one part of
    // Follow that survives a power cycle precisely so that a reboot on a
    // Tuesday still shows Saturday's flight rather than an empty scope.
    //
    // The three ABSENCE states are deliberately NOT here. They have words
    // that matter (6) and a live picture behind them; a souvenir on top of
    // 'BELOW COVERAGE' would bury the thing the customer needs to read.
    const follow::State st = followMachine.Current();
    return st == follow::State::Landed || st == follow::State::Waiting ||
           st == follow::State::Ground  || st == follow::State::Idle;
}


// ===========================================================================
// THE SESSION FOLLOW TARGET, AND THE RADAR ROUTE STRIP
// docs/tap-to-peek.md
// ===========================================================================

// Swipe down on a detail card: follow that flight for this session.
//
// SESSION ONLY, AND THE ABSENCE OF A WRITE IS THE FEATURE. Nothing here touches
// NVS. The config page stays the only path by which this device STORES an
// aircraft somebody cares about, which is what keeps C2's line intact -- a
// gesture that persisted a target would quietly make the device remember a
// preference it was never explicitly given.
//
// The configured target is not overwritten, so dismissing this restores it.
void AircraftManager::SetSessionFollow(const TrackedAircraft& tracked)
{
    // Prefer the callsign, since that is what the route mirror is keyed on and
    // therefore what gives this flight an arc to draw. Fall back to the hex,
    // which always exists.
    String id = tracked.state.callsign; id.trim();
    if (id.isEmpty()) id = tracked.state.icao24;
    id.toLowerCase();
    if (id.isEmpty()) return;

    followSessionTarget = id;

    // A new subject means the previous flight's track, stats and route belong to
    // somebody else. Reset them here rather than letting HandleFollowTransition
    // discover it later: the first frame after the gesture would otherwise draw
    // the OLD aeroplane's arc under the NEW one's callsign.
    followTrack.ResetFlight();
    followStats.Reset();
    followRouteOrigin = tracked.routeOrigin;
    followRouteDest   = tracked.routeDest;
    followMachine.SetTarget(true);

    // Surface the face once so the gesture visibly took effect, then hand the
    // screen straight back to the rotation -- §13.3: Follow gets a screen, never
    // THE screen. This is the same dwell the state transitions use.
    followAutoReturnTo = (screen == Screen::Follow) ? Screen::Radar : screen;
    EnterScreen(Screen::Follow);   // customer-initiated: the swipe asked for this
    followAutoUntilMs = millis() + FOLLOW_AUTO_DWELL_MS;

    // §17: the target is NOT printed. The count and the route are.
    Serial.printf("[follow] session target set (%u chars) route=%s->%s\n",
                  (unsigned)id.length(),
                  followRouteOrigin.isEmpty() ? "--" : followRouteOrigin.c_str(),
                  followRouteDest.isEmpty() ? "--" : followRouteDest.c_str());
}

// Swipe down on the follow face: stop following, and let a configured target
// resume if there is one.
void AircraftManager::ClearSessionFollow()
{
    if (followSessionTarget.isEmpty()) return;
    followSessionTarget = "";
    followTrack.ResetFlight();
    followStats.Reset();
    followRouteOrigin = "";
    followRouteDest = "";
    followMachine.SetTarget(!followTarget.isEmpty());
    // The other half of the ownership rule above. With the session target gone
    // and no configured one behind it, nothing is being followed and the buffer
    // has no owner -- so this is the second place 4.3's free can legitimately
    // happen, and leaving it out would strand 12 KB for the rest of the boot.
    if (followTarget.isEmpty() && followTrack.Active())
        followTrack.Disable();
    followAutoUntilMs = 0;
    EnterScreen(Screen::Radar);    // customer-initiated: the swipe asked for this
    // The BOOLEAN is computed first and the token never enters the printf
    // statement. §17's per-statement check has no allow list, and it is right
    // not to: "only the emptiness is printed" is exactly what the author of the
    // next leak will also believe.
    const bool configuredResumes = !followTarget.isEmpty();
    Serial.printf("[follow] session target cleared; configured target %s\n",
                  configuredResumes ? "resumes" : "absent");
}

// The route strip on the RADAR face.
//
// This is the answer to "the flight should be on screen the entire way" that
// does not cost the radar. §13.3 keeps Follow in the rotation; the strip and the
// existing followed-contact ring together mean the flight IS continuously
// visible -- on the screen the owner is already looking at -- for the price of
// one line rather than the whole display.
//
// DRAWN ONLY WHEN THERE IS SOMETHING TRUE TO SAY: a followed flight, with both
// route codes. No codes, no strip; the ring alone carries it. Same rule as the
// faces -- the strip draws from the STRINGS, so an unresolved code still shows
// its route, and only the progress marker needs coordinates.
//
// Its cost is added to the SAME counters as the faces (§18), because unlike them
// it lands on the radar path, where the 85 ms budget is provisional (#264) and
// the globe's cost is still unmeasured. A strip that quietly costs 4 ms on every
// radar frame is exactly the kind of thing that shows up as "the soak got
// slower" three weeks later.
void AircraftManager::DrawFollowRouteStrip(BandCanvas& backbuffer)
{
    if (!FollowScreenVisible() || followRouteOrigin.isEmpty() || followRouteDest.isEmpty())
        return;

    const uint32_t t0 = micros();
    const float k = (float)SCREEN_SIZE / 240.0f;
    const auto Si = [k](float v) { return (int)lroundf(v * k); };
    const int cx = SCREEN_SIZE_DIV_2;

    // Sits just below the disc's vertical centre-line text, inside the chord so
    // nothing runs off the curve -- the same rule every other row obeys.
    const int y = Si(214.0f);
    backbuffer.setTextSize(1);
    const int lineH = backbuffer.fontHeight() > 0 ? backbuffer.fontHeight() : 8;
    const int avail = ChordWidthPx(y, lineH);
    if (avail < Si(70.0f)) return;   // no room on this panel: draw nothing

    String o = followRouteOrigin, d = followRouteDest;
    o.toUpperCase(); d.toUpperCase();

    const follow::Endpoint org = follow::LookupAirport(o.c_str());
    const follow::Endpoint dst = follow::LookupAirport(d.c_str());

    // Progress needs coordinates AND a position. Without either the strip still
    // draws the pair, just without a marker -- honest rather than absent.
    float progress = -1.0f;
    if (org.known && dst.known) {
        if (const TrackedAircraft* live = FollowedAircraft()) {
            auto [aLat, aLon] = live->GetDisplayPosition();
            progress = follow::ProgressAlong(org, dst, aLat, aLon);
        } else if (followMachine.LastFixMs() != 0) {
            const follow::Fix& f = followMachine.LastFix();
            progress = follow::ProgressAlong(org, dst, f.lat, f.lon);
        }
    }

    const int codeW = (int)backbuffer.textWidth("XXXX");
    const int barX0 = cx - avail / 2 + codeW + Si(4.0f);
    const int barX1 = cx + avail / 2 - codeW - Si(4.0f);
    if (barX1 <= barX0) return;

    const uint32_t colour = FollowStateColour(followMachine.Current(), true);
    backbuffer.setTextColor(FOLLOW_DIM);
    backbuffer.drawString(o, cx - avail / 2, y);
    backbuffer.drawString(d, barX1 + Si(4.0f), y);

    const int barY = y + lineH / 2;
    backbuffer.drawLine(barX0, barY, barX1, barY, FOLLOW_NEUTRAL);
    if (progress >= 0.0f) {
        const int mx = barX0 + (int)lroundf((barX1 - barX0) * progress);
        backbuffer.drawLine(barX0, barY, mx, barY, colour);
        backbuffer.fillCircle(mx, barY, Si(2.5f), colour);
    }

    // ASSIGN, DO NOT ACCUMULATE. This was `+=`, which made every strip number
    // reported today meaningless: the faces assign per draw and only
    // followArcMaxUs is reset after a report, so on the radar face -- where the
    // strip draws and the faces do not -- followArcUs summed EVERY FRAME across
    // the whole report window. It read 260.88 ms for a draw that costs well
    // under a millisecond, and the "max" was just that total climbing.
    //
    // It hid because the strip is the one thing here that draws on a screen
    // whose counter nobody was watching. The faces and the strip never draw in
    // the same frame, so one counter is fine -- as long as both write it the
    // same way.
    const uint32_t us = micros() - t0;
    followArcUs = us;
    if (followArcUs > followArcMaxUs) followArcMaxUs = followArcUs;
}

// §7.1's routing table, in order. The face is CHOSEN, never picked -- there is
// no config key for this and there must not be, because the customer following
// a trainer and the customer following a son's airliner both just typed a tail
// number into the same box.
//
// The globe (§9) is not in this list yet; when it lands it takes the long-haul
// half of the arc branch, gated on great-circle distance, and nothing else here
// changes.
void AircraftManager::DrawFollow(BandCanvas& backbuffer)
{
#ifdef FOLLOW_BENCH
    const follow::State st = followForce ? followForced : followMachine.Current();
#else
    const follow::State st = followMachine.Current();
#endif
    // C4 comes FIRST, and only when there is no souvenir to show instead. A
    // device that has flown before shows Saturday's flight; a device that never
    // has shows the pre-departure face, which is the one the owner meets
    // seconds after configuring a follow.
    if (st == follow::State::Waiting && !followLog.Has()) {
        DrawFollowWaitingFace(backbuffer);
        return;
    }
    if (ShowPostFlightCard()) { DrawFollowPostFlightCard(backbuffer); return; }

    // THE ARC FACE REQUIRES A ROUTE, WHICH IS A DEVIATION FROM §7.1 AND IS
    // DELIBERATE. The table sends "airline regime, otherwise" to the arc face
    // unconditionally, but an arc with no origin, no destination and no
    // progress is a dim ring with nothing on it -- strictly less than the local
    // face, whose rings auto-scale to whatever extent the flight has and whose
    // track is still a picture of the flight. A GA cross-country is exactly
    // this case: outside the home radius, no route, and well served by rings.
    // So the arc is chosen when it has something to draw, and the local face is
    // the fallback rather than the other way round.
    if (FollowRegime() == follow::Regime::Airline && FollowRouteKnown()) {
        // A GLOBE FOR EVERY AIRLINER, AT WHATEVER SCALE THE ROUTE NEEDS (#274).
        //
        // The 4,000 km threshold is gone. It was two things at once -- a
        // legibility argument, and (within 7 %) the radius past which the coarse
        // coastline set draws visible straight edges -- and route framing plus
        // the dense LOD set removes both. A 900 km hop now zooms to a close-up
        // of curved terrain instead of falling back to the arc.
        //
        // Both codes must still RESOLVE, and that is not a distance rule: the
        // arc draws a route from its STRINGS, the globe cannot place a single
        // pixel without coordinates. That is the code-only degradation, and it
        // is the only reason left to prefer the arc.
        const follow::Endpoint o = follow::LookupAirport(followRouteOrigin.c_str());
        const follow::Endpoint d = follow::LookupAirport(followRouteDest.c_str());
        if (o.known && d.known) {
            DrawRouteGlobe(backbuffer, FollowRouteView());
            return;
        }
        DrawRouteArc(backbuffer, FollowRouteView());
        return;
    }
    DrawFollowLocalFace(backbuffer);
}

void AircraftManager::DrawFollowLocalFace(BandCanvas& backbuffer)
{
    constexpr int cx = SCREEN_SIZE_DIV_2;
#ifdef FOLLOW_BENCH
    // 6's copy can be forced from serial so it can be judged on glass without
    // cutting the network. The DRAWING is what needs a human; the transitions
    // are graded in the host suite.
    const follow::State st = followForce ? followForced : followMachine.Current();
#else
    const follow::State st = followMachine.Current();
#endif

    backbuffer.setTextSize(1);
    const auto centred = [&](const String& s, int y, uint32_t colour) {
        backbuffer.setTextColor(colour);
        backbuffer.drawString(s, cx - (int)backbuffer.textWidth(s) / 2, y);
    };

    // §6: expected absence must not look like a fault. Amber explains, red
    // alarms, and the whole state machine exists so the two are never confused.
    // ONE palette for both faces -- that argument is worth nothing if the arc
    // face and this one disagree about which colour means which.
    const uint32_t AMBER = FOLLOW_AMBER;
    const uint32_t DIM   = FOLLOW_DIM;
    const uint32_t TRACK = FOLLOW_TRACK;
    const uint32_t HOME  = FOLLOW_HOME;

    // benignApproach FALSE here: this face keeps amber for APPROACH_LOST until
    // the bench judges §8's green beside it. See FollowStateColour.
    const uint32_t stateColour = FollowStateColour(st, /*benignApproach=*/false);

    // No location means no home, and the local face is built entirely around
    // home -- it would draw rings around a point in the Gulf of Guinea. Same
    // reasoning as the radar's DrawNoLocation, and it outranks everything below
    // for the same reason: a face that looks finished and is not is worse than
    // one that says what is missing.
    if (!hasLocation) {
        backbuffer.drawCircle(cx, cx, FOLLOW_FACE_RADIUS_PX, lgfx::color888(0, 60, 0));
        centred("SET YOUR LOCATION", cx - 14, AMBER);
        centred("Follow needs a home field", cx + 2, DIM);
        return;
    }

    // ---- the state, first, because it is the product (§19) -----------------
    centred(FitToDisc(backbuffer, follow::Headline(st), 8,
                      backbuffer.fontHeight() > 0 ? backbuffer.fontHeight() : 8),
            8, stateColour);

    // Who, and where from. §10: the airport is a CODE, never a name.
    String who = followTarget; who.toUpperCase();
    if (const TrackedAircraft* t = FollowedAircraft()) {
        String cs = t->state.callsign; cs.trim();
        if (!cs.isEmpty()) { cs.toUpperCase(); who = cs; }
    }
    if (!followHomeCode.isEmpty())
        who += "  at " + followHomeCode;
    // The callsign is feed-supplied and the home code is ours, so this row is
    // the one on this face whose length is not bounded by construction.
    centred(FitToDisc(backbuffer, who, 21,
                      backbuffer.fontHeight() > 0 ? backbuffer.fontHeight() : 8),
            21, DIM);

    // ---- the scope ----------------------------------------------------------
    const follow::LocalView v = BuildLocalView();

    // Rings, equally spaced, each labelled with the radius it was drawn at.
    // Only the outer one carries the unit, matching DrawRadarCircles -- three
    // labels each repeating "mi" is three times the ink for no more meaning.
    for (int i = 1; i <= v.rings; ++i) {
        const int r = (FOLLOW_FACE_RADIUS_PX * i) / v.rings;
        backbuffer.drawCircle(cx, cx, r, i == v.rings ? lgfx::color888(0, 90, 0)
                                                      : lgfx::color888(0, 45, 0));
        const float value = v.stepDisplay * i;
        String label = String(value, value < 10.0f ? 1 : 0);
        if (i == v.rings) label += rangeUnit;
        backbuffer.setTextColor(lgfx::color888(0, 110, 0));
        backbuffer.drawString(label, cx + 4, cx - r + (i == v.rings ? 3 : 2));
    }

    // The track. §4.5: one dim distinct colour, beneath everything, and NOT
    // faded -- a return decays because it is a return; the track is a record.
    // The same draw-time cap and adaptive stride as the radar face, so a full
    // 1024-point buffer costs what §18.1 measured rather than four times it.
    const size_t n = followTrack.Size();
    if (followDrawTrack && followTrack.Active() && n >= 2) {
        const size_t stride = follow::Track::StrideFor(n, FOLLOW_DRAW_SEGMENT_CAP);
        int prevX = 0, prevY = 0; bool havePrev = false;
        for (size_t i = 0; i < n; i += stride) {
            auto [x, y] = ProjectLocal(followTrack.At(i).lat, followTrack.At(i).lon, v);
            if (havePrev) backbuffer.drawLine(prevX, prevY, x, y, TRACK);
            prevX = x; prevY = y; havePrev = true;
        }
        if (havePrev && ((n - 1) % stride) != 0) {
            auto [x, y] = ProjectLocal(followTrack.At(n - 1).lat, followTrack.At(n - 1).lon, v);
            backbuffer.drawLine(prevX, prevY, x, y, TRACK);
        }
    }

    // Home field: the anchor the whole view is built around, so it is drawn
    // after the track and reads through it.
    backbuffer.drawLine(cx - 4, cx, cx + 4, cx, HOME);
    backbuffer.drawLine(cx, cx - 4, cx, cx + 4, HOME);
    backbuffer.drawCircle(cx, cx, 3, HOME);

    // Where he is now, with heading. Reuses the radar's dart so a followed
    // aircraft looks like the same aircraft on both faces (§7.2).
    const TrackedAircraft* live = FollowedAircraft();
    if (live) {
        auto [aLat, aLon] = live->GetDisplayPosition();
        auto [x, y] = ProjectLocal(aLat, aLon, v);
        DrawAircraftTriangle(backbuffer, x, y, *live, stateColour);
        backbuffer.drawCircle(x, y, 8, stateColour);
    }

    // ---- readouts -----------------------------------------------------------
    //
    // ALTITUDE IS LABELLED FOR WHAT IT IS. §10 asks for AGL, and C5's delivery
    // half does not exist, so the honest render is the MSL figure the feed
    // carries with "MSL" beside it -- not an AGL-shaped number that is wrong by
    // the field elevation. At Bend that error is 3,460 ft, and a readout that
    // says a trainer is at 4,400 ft AGL in the circuit is worse than one that
    // declines to say.
    // BUILT AS FIELDS, IN PRIORITY ORDER, AND ADDED ONLY WHILE THEY STILL FIT.
    //
    // The bench found this row running off BOTH ends of the curve. At
    // SCREEN_SIZE-26 on a 240 panel the chord is 118 px -- nineteen characters --
    // and "-900 ft MSL  67 kt  148mi" is twenty-five. Ellipsising one long
    // string would cut a number in half, which is worse than dropping a field:
    // a truncated "14..." reads as a value. So fields are appended while the
    // whole line fits and the least important one is simply absent when it
    // does not.
    constexpr int READOUT_Y = SCREEN_SIZE - 26;
    const int lineH = backbuffer.fontHeight() > 0 ? backbuffer.fontHeight() : 8;
    String readout;
    const auto addField = [&](const String& part) {
        const String next = readout.isEmpty() ? part : readout + "  " + part;
        if ((int)backbuffer.textWidth(next) <= ChordWidthPx(READOUT_Y, lineH))
            readout = next;
    };

    if (live) {
        const follow::Fix& f = followMachine.LastFix();
        const bool airborne = (st == follow::State::Airborne);
        // DECLINE RATHER THAN PRINT. The bench showed "-900 ft MSL" under an
        // AIRBORNE headline and the readout stated it as confidently as a real
        // number. -900 ft is legal SOMEWHERE (Bar Yehuda sits at -1,266 ft), so
        // a bounds check alone does not catch it -- what catches it is that the
        // altitude and the state contradict each other. See ReportableAltFt.
        if (followHome.elevationKnown) {
            const float agl = follow::AglFt(f, followHome);
            if (follow::PlausibleAltFt(agl))
                addField(String((int)lroundf(agl)) + " AGL");
        } else {
            const float msl = follow::AltitudeMslFt(f);
            if (follow::ReportableAltFt(msl, airborne))
                addField(String((int)lroundf(msl)) + " ft MSL");
            else
                addField("-- ft MSL");
        }
        if (follow::PlausibleSpeedKt(f.velocityKt))
            addField(String((int)lroundf(f.velocityKt)) + " kt");
        // From the DISPLAYED position, not the last raw fix. The dart is drawn
        // dead-reckoned; a range computed off the stale fix would disagree with
        // the ring the customer can see it sitting on.
        auto [rLat, rLon] = live->GetDisplayPosition();
        const float rangeKm = follow::SeparationKm(rLat, rLon, v.centreLat, v.centreLon);
        addField(units::FormatKm(rangeKm, rangeUnit));
    } else if (followTrack.Active() && n) {
        addField(String((unsigned)n) + " track points");
    }
    // CIRCUIT COUNT IS NOT HERE. §10 lists it and §11 defers it in the same
    // breath -- see the note at the bottom of include/FollowGeometry.h. A wrong
    // count is a claim the customer cannot check.
    if (!readout.isEmpty())
        centred(readout, READOUT_Y, DIM);

    // ---- the reassurance, when there is something to reassure about ---------
    //
    // Drawn OVER the scope on purpose. In an absence state there is nothing live
    // underneath to obscure -- the track is frozen and the dart is gone -- and
    // the middle of the disc is the only place on a 240 px round panel wide
    // enough to set a sentence. §6: "Name the mechanism. 'No receivers here' is
    // calming because it explains."
    const char* why = follow::Explanation(st);
    if (why[0] != '\0') {
        // Width from SCREEN_SIZE, not a literal: 22 characters fills the 240
        // round and leaves two thirds of a 480 square empty. The 11 is the
        // 6 px glyph advance plus margin, solved for "the box spans a bit over
        // half the disc" -- which is the widest a centred sentence can be while
        // still sitting inside the round panel's chord at that height.
        constexpr int CHAR_W = 6, LINE_H = 11;
        constexpr int MAX_CHARS = SCREEN_SIZE / 11;
        // Wrap on words, into at most three lines.
        String lines[3]; int nLines = 0;
        String word, cur;
        for (const char* p = why; ; ++p) {
            if (*p && *p != ' ') { word += *p; continue; }
            if (word.length()) {
                if (cur.isEmpty()) cur = word;
                else if ((int)(cur.length() + 1 + word.length()) <= MAX_CHARS) cur += " " + word;
                else { if (nLines < 3) lines[nLines++] = cur; cur = word; }
                word = "";
            }
            if (!*p) break;
        }
        if (!cur.isEmpty() && nLines < 3) lines[nLines++] = cur;

        const int boxH = nLines * LINE_H + 8;
        const int boxW = MAX_CHARS * CHAR_W + 10;
        const int top = cx - boxH / 2 + 6;
        backbuffer.fillRoundRect(cx - boxW / 2, top, boxW, boxH, 4, lgfx::color888(0, 0, 0));
        backbuffer.drawRoundRect(cx - boxW / 2, top, boxW, boxH, 4, stateColour);
        for (int i = 0; i < nLines; ++i)
            centred(lines[i], top + 5 + i * LINE_H, DIM);
    }
}

// ===========================================================================
// THE ARC FACE (§8) -- the airline default
// ===========================================================================
//
// "A round panel affords two independent circular readings. Spend both, and
// keep the centre clear." The ROUTE is the inner arc; the BEARING is the bezel;
// the centre holds the one number the customer came for.
//
// ---------------------------------------------------------------------------
// EVERY NUMBER BELOW IS THE SPEC'S 240 px FIGURE TIMES SCREEN_SIZE/240
//
// §8's geometry is written for the 1.28" disc. Hardcoding those figures is the
// mistake CLAUDE.md names by hand ("Don't reintroduce hardcoded 240/pins"), and
// on the 412 px SPD2010 it would draw the whole face inside the middle half of
// the glass with a bezel ring floating in the centre. Si() is the only way a
// radius or a text row gets into this file.
//
// ---------------------------------------------------------------------------
// ONE ANGLE CONVENTION, AND IT IS FollowArc.h's
//
// LovyanGFX has fillArc and it would be cheaper than stepping. It is not used,
// because then the BAND would be placed by LovyanGFX's convention while the
// marker, the codes and the wedge are placed by follow::ArcAngleDeg -- two
// implementations of one fact, which is this repo's most reliable way to ship a
// bug. If the two disagreed by even a few degrees the marker would sit beside
// the arc rather than on it, and in a photograph that reads as a PROGRESS
// error, not a geometry error: you would go looking at ProgressAlong, which is
// correct and graded.
//
// The dashes need per-step control anyway (§8's NO_COVERAGE projection), so the
// stepped form is not a detour. Its cost is measured, not assumed -- see the
// [follow] arc= line.
// ===========================================================================

// A thick arc as radial strokes, one per pixel of arc length, optionally
// dashed. `dashPx` 0 draws solid. Returns the number of strokes drawn, which is
// what the cost line reports.
static int DrawArcBand(BandCanvas& bb, int cx, int cy, float r, float stroke,
                       float a0Deg, float a1Deg, uint32_t colour, float dashPx)
{
    if (!(r > 1.0f) || a1Deg <= a0Deg) return 0;
    const float half = stroke * 0.5f;
    const float stepDeg = follow::ARC_RAD2DEG / r;   // ~1 px of arc per step
    int drawn = 0;
    float travelled = 0.0f;
    for (float a = a0Deg; a <= a1Deg; a += stepDeg, travelled += 1.0f) {
        if (dashPx > 0.0f && fmodf(travelled / dashPx, 2.0f) >= 1.0f)
            continue;
        const float rad = a * follow::ARC_DEG2RAD;
        const float c = cosf(rad), s = sinf(rad);
        bb.drawLine((int)lroundf(cx + c * (r - half)), (int)lroundf(cy + s * (r - half)),
                    (int)lroundf(cx + c * (r + half)), (int)lroundf(cy + s * (r + half)),
                    colour);
        ++drawn;
    }
    return drawn;
}

// §7.1's inference. The arithmetic, and the reason its input is the flight's
// MAXIMUM extent rather than the live separation, is in FollowState.h.
follow::Regime AircraftManager::FollowRegime() const
{
    return follow::RegimeFor(followStats.furthestKm, followHome.radiusKm,
                             followHome.positionKnown);
}

// Everything the route faces need, gathered in ONE place from Follow's members.
//
// This is the seam the signature refactor exists to create: the faces below take
// a RouteView and know nothing about `followTarget` or the follow machine, so a
// second caller supplies its own view rather than having to mutate Follow's
// state to borrow its renderer.
AircraftManager::RouteView AircraftManager::FollowRouteView() const
{
    RouteView v;
#ifdef FOLLOW_BENCH
    v.st = followForce ? followForced : followMachine.Current();
#else
    v.st = followMachine.Current();
#endif
    v.origCode = followRouteOrigin;
    v.destCode = followRouteDest;
    v.org = follow::LookupAirport(followRouteOrigin.c_str());
    v.dst = follow::LookupAirport(followRouteDest.c_str());

    // The fallback: charset-filtered, because the live-callsign path below
    // overwrites it with a string the feed already constrained. NOT length-
    // capped here -- FitToDisc truncates for the panel, visibly and by measured
    // width, and a second cap in front of it was silent and cruder. See
    // include/FollowLabel.h for why the hyphen is in the charset.
    {
        char lbl[follow::LABEL_COST_BOUND + 4];
        follow::SanitiseLabel(EffectiveFollowTarget().c_str(), lbl, sizeof(lbl));
        v.label = lbl;
    }
    const TrackedAircraft* live = FollowedAircraft();
    if (live) {
        auto [a, b] = live->GetDisplayPosition();
        v.acLat = a; v.acLon = b; v.havePos = true;
        v.gsKt = live->state.velocity * 1.94384f;
        String cs = live->state.callsign; cs.trim();
        if (!cs.isEmpty()) { cs.toUpperCase(); v.label = cs; }
    } else if (followMachine.LastFixMs() != 0) {
        // THE ABSENCE STATES ARE WHY THIS BRANCH EXISTS. The aeroplane is not in
        // the contact table -- that is what NO_COVERAGE and SIGNAL_LOST MEAN --
        // so the last fix the machine holds is the only position there is.
        const follow::Fix& f = followMachine.LastFix();
        v.acLat = f.lat; v.acLon = f.lon; v.havePos = true;
        v.gsKt = f.velocityKt;
    }
    v.sinceSec = followMachine.LastFixMs()
        ? (uint32_t)((millis() - followMachine.LastFixMs()) / 1000UL) : 0u;

    const float msl = follow::AltitudeMslFt(followMachine.LastFix());
    v.altMslFt = follow::ReportableAltFt(msl, v.st == follow::State::Airborne) ? msl : NAN;

#ifdef FOLLOW_BENCH
    v.resolving = followBenchResolving;
    if (followBenchLongHaul && v.org.known && v.dst.known) {
        follow::InterpolateGreatCircle(v.org, v.dst, followBenchProgress, v.acLat, v.acLon);
        v.havePos = true;
        v.gsKt = 480.0f;
    }
#endif
    return v;
}

void AircraftManager::DrawRouteArc(BandCanvas& backbuffer, const RouteView& view)
{
    const uint32_t t0 = micros();
    constexpr int cx = SCREEN_SIZE_DIV_2;
    constexpr int cy = SCREEN_SIZE_DIV_2;
    const float k = (float)SCREEN_SIZE / 240.0f;
    const auto S  = [k](float v) { return v * k; };
    const auto Si = [k](float v) { return (int)lroundf(v * k); };

    const follow::State st = view.st;
    const uint32_t colour = FollowStateColour(st, /*benignApproach=*/true);
    const bool degraded = follow::Machine::IsAbsent(st);

    backbuffer.setTextSize(1);
    const int lineH = backbuffer.fontHeight() > 0 ? backbuffer.fontHeight() : 8;
    // EVERY ROW IS PLACED BY ITS VERTICAL CENTRE, including the big one. §8's
    // stack has the primary readout at y=124 and its label at y=139; at
    // 34-point-equivalent type the primary is 24 px tall, so read as a TOP it
    // would run through the label. Centres make the slot-sharing arithmetic
    // (altitude and the state chip both at y=158) obvious instead of arithmetic
    // nobody re-derives.
    const auto row = [&](const String& s, float y240, uint32_t c, int size) {
        backbuffer.setTextSize(size);
        backbuffer.setTextColor(c);
        const String fit = FitToDisc(backbuffer, s, Si(y240) - (lineH * size) / 2, lineH * size);
        backbuffer.drawString(fit, cx - (int)backbuffer.textWidth(fit) / 2,
                              Si(y240) - (lineH * size) / 2);
        backbuffer.setTextSize(1);
    };
    const auto at = [&](float deg, float r, int& x, int& y) {
        const float rad = deg * follow::ARC_DEG2RAD;
        x = cx + (int)lroundf(cosf(rad) * r);
        y = cy + (int)lroundf(sinf(rad) * r);
    };

    // ---- what we actually know ---------------------------------------------
    //
    // The codes are STRINGS first and coordinates second, and that split is the
    // whole degradation story: an unresolved code still draws at the end of the
    // arc, it just cannot place a marker. "Honest rather than wrong."
    const follow::Endpoint& org = view.org;
    const follow::Endpoint& dst = view.dst;
    const bool placed = org.known && dst.known;

    const float acLat = view.acLat, acLon = view.acLon;
    const bool  havePos = view.havePos;
    const float gsKt = view.gsKt;
    const uint32_t sinceSec = view.sinceSec;
    const TrackedAircraft* live = FollowedAircraft();

    const float totalKm = placed
        ? follow::GreatCircleKm(org.lat, org.lon, dst.lat, dst.lon) : 0.0f;
    const float fixProgress = (placed && havePos)
        ? follow::ProgressAlong(org, dst, acLat, acLon) : 0.0f;

    // §8, state by state. NO_COVERAGE carries the marker forward; SIGNAL_LOST
    // freezes it, because "we do not know, so we do not draw."
    float shown = fixProgress;
    if (st == follow::State::NoCoverage)
        shown = follow::ProgressDeadReckoned(fixProgress, totalKm, gsKt, (float)sinceSec);

    // ---- the bezel: panel ring, cardinal ticks, bearing wedge ---------------
    backbuffer.drawCircle(cx, cy, Si(118.5f), FOLLOW_NEUTRAL);
    backbuffer.drawCircle(cx, cy, Si(117.0f), FOLLOW_NEUTRAL);

    // A TRUE bearing becomes a screen angle through the SAME window-up rotation
    // the radar uses: screen-up is the compass bearing `radarUpDeg`, and this
    // face's 0 degrees is 3 o'clock. Getting this wrong points the wedge at a
    // plausible but wrong quadrant, which is unfalsifiable from a photograph.
    const auto screenAngle = [this](float trueBearingDeg) {
        return trueBearingDeg - (float)radarUpDeg - 90.0f;
    };

    // §8: "The cardinal ticks are not decoration. A pointer with no reference
    // frame is meaningless; if the ticks are cut, cut the wedge too." They are
    // therefore drawn together or not at all -- both need a home position.
    if (hasLocation) {
        for (int b = 0; b < 360; b += 90) {
            int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
            at(screenAngle((float)b), S(107.0f), x0, y0);
            at(screenAngle((float)b), S(112.0f), x1, y1);
            backbuffer.drawLine(x0, y0, x1, y1, FOLLOW_DIM);
        }
        int nx = 0, ny = 0;
        at(screenAngle(0.0f), S(97.0f), nx, ny);
        backbuffer.setTextColor(FOLLOW_DIM);
        backbuffer.drawString("N", nx - (int)backbuffer.textWidth("N") / 2, ny - lineH / 2);

        if (havePos) {
            // A filled triangle pointing outward, ~4.6 degrees of half-width.
            const float b = follow::BearingDeg((float)lat, (float)lon, acLat, acLon);
            const float a = screenAngle(b);
            int tipX = 0, tipY = 0, lX = 0, lY = 0, rX = 0, rY = 0;
            at(a, S(115.0f), tipX, tipY);
            at(a - 4.6f, S(105.0f), lX, lY);
            at(a + 4.6f, S(105.0f), rX, rY);
            // §8: dimmed to ~40% in NO_COVERAGE -- one of the three simultaneous
            // signals that the position is inferred rather than measured.
            const uint32_t wedge = (st == follow::State::NoCoverage)
                ? FollowFade(colour, 0.4f) : colour;
            backbuffer.fillTriangle(tipX, tipY, lX, lY, rX, rY, wedge);
        }
    }

    // ---- the route arc -------------------------------------------------------
    const float a0 = follow::ARC_START_DEG;
    const float a1 = follow::ARC_START_DEG + follow::ARC_SWEEP_DEG;
    const float aNow = follow::ArcAngleDeg(shown);
    int strokes = 0;

    // The unflown remainder, in the dim neutral. Drawn only where the accent
    // will NOT cover it: overdrawing the whole sweep first is a stroke-for-
    // stroke waste on the one face where the arc is the most expensive thing on
    // the screen, and this face is measured (below), so the waste would show.
    strokes += DrawArcBand(backbuffer, cx, cy, S(100.0f), S(5.0f),
                           (placed && havePos) ? aNow : a0, a1, FOLLOW_NEUTRAL, 0.0f);

    if (placed && havePos) {
        // Flown, in the state's colour. SIGNAL_LOST freezes here and draws
        // nothing ahead: §8, "No dashed projection. We do not know, so we do
        // not draw."
        strokes += DrawArcBand(backbuffer, cx, cy, S(100.0f), S(5.0f), a0, aNow, colour, 0.0f);
        if (st == follow::State::NoCoverage)
            strokes += DrawArcBand(backbuffer, cx, cy, S(100.0f), S(5.0f), aNow, a1,
                                   colour, S(4.0f));
    }

    // ---- the codes, at each end of the arc ----------------------------------
    //
    // Drawn from the STRINGS, so they appear even when the table cannot place
    // them -- that is the code-only degradation, and it is the reason widening
    // coverage later (C1's ap:<CODE>) touches no drawing code at all.
    const auto code = [&](const String& s, float deg, uint32_t c) {
        if (s.isEmpty()) return;
        String up = s; up.toUpperCase();
        int x = 0, y = 0;
        at(deg, S(84.0f), x, y);
        backbuffer.setTextColor(c);
        backbuffer.drawString(up, x - (int)backbuffer.textWidth(up) / 2, y - lineH / 2);
    };
    // §8: APPROACH_LOST highlights the destination -- he is nearly there, and
    // that is the reassuring half of the picture.
    const bool nearlyThere = (st == follow::State::ApproachLost);
    code(view.origCode, a0, nearlyThere ? FOLLOW_DIM : FOLLOW_DIM);
    code(view.destCode, a1, nearlyThere ? colour : FOLLOW_DIM);

    // ---- the marker ---------------------------------------------------------
    if (placed && havePos) {
        int mx = 0, my = 0;
        at(aNow, S(100.0f), mx, my);
        backbuffer.drawCircle(mx, my, Si(9.5f), FollowFade(colour, 0.35f));
        // Hollow whenever the position is not a live measurement (§8). Three
        // of the four states draw it hollow; only IN_CONTACT is solid.
        if (degraded) backbuffer.drawCircle(mx, my, Si(5.0f), colour);
        else          backbuffer.fillCircle(mx, my, Si(5.0f), colour);
    }

    // ---- the centre stack ----------------------------------------------------
    const String& who = view.label;
    row(who, 86.0f, FOLLOW_DIM, 1);

    // THE PRIMARY SLOT ALWAYS HOLDS A MAGNITUDE AND THE LABEL ALWAYS NAMES IT.
    //
    // §8 words APPROACH_LOST's readout as "ON APPROACH with distance out",
    // which would put a phrase in a slot that holds a number in the other three
    // states. At this type size a phrase either overflows the chord or forces
    // the number smaller, and a stack whose type size changes with state is the
    // jump §8 designed the shared altitude/chip slot to avoid. So the state's
    // word goes to the label line, which exists to name the magnitude, and the
    // distance goes in the slot. Recorded as a deviation in the spec.
    // NEVER AN EMPTY NUMERIC SLOT (glass, 2026-08-29). This started as
    // `primary = "--"`, and on the code-only face that rendered two dashes in
    // the hero slot with nothing under them -- which reads as a screen that
    // FAILED TO LOAD rather than one reporting a lesser state. The absence
    // states got careful copy precisely so degradation would look deliberate;
    // an empty value slot throws that away in the one case where the face has
    // least else to say.
    //
    // So: a field with no number is not drawn, and the hero carries the most
    // informative TRUE thing available. The ladder is at the bottom of this
    // block -- every branch here now either produces a number or leaves
    // `primary` empty and lets the ladder speak.
    String primary = "", label = "";
    int psize = 3;
    if (!placed) {
        // RESOLVING IS NOT UNRESOLVABLE, and this is where that distinction has
        // to be drawn -- "one clears in a second, the other never will".
        //
        // It used to be tested further down, in the `placed` chain, where it was
        // UNREACHABLE IN EVERY CASE IT WAS WRITTEN FOR: `placed` means both ends
        // are already resolved, so there is nothing left to locate, and the
        // branch's own `org.known ? destCode : origCode` only makes sense when
        // one end is missing -- which forces `placed` false and skipped it.
        // Found on glass 2026-08-29 by pressing the bench's resolving key and
        // watching the screen not change.
        if (view.resolving) {
            // The label names what is happening to the thing in the hero slot,
            // which the ladder below fills with the route pair. A distance would
            // be a magnitude this label does not name -- see the primary-slot
            // rule further down.
            label = "LOCATING " + (view.org.known ? view.destCode : view.origCode);
        } else if (havePos && hasLocation) {
            // No route, or a code the baked table does not carry. The honest
            // readout is the one thing we do know: how far away he is.
            primary = units::FormatKm(
                follow::SeparationKm(acLat, acLon, (float)lat, (float)lon), rangeUnit, true);
            label = "AWAY";
        }
    } else if (st == follow::State::SignalLost) {
        char b[16];
        follow::FormatElapsed(sinceSec, b, sizeof(b));
        primary = b;
        label = "SINCE CONTACT";
    } else if (st == follow::State::ApproachLost) {
        primary = units::FormatKm(
            follow::GreatCircleKm(acLat, acLon, dst.lat, dst.lon), rangeUnit, true);
        label = "ON APPROACH";
    } else if (follow::Machine::IsEnRoute(st)) {
        // Same rule as the ARR slot above: Ground, Landed and Waiting reach
        // here with every input satisfied and no arrival to claim. They fall
        // through to the ladder, which states the route rather than inventing
        // a landing time for a parked aeroplane.
        const int mins = follow::MinutesToArrival(totalKm * (1.0f - shown), gsKt);
        if (mins >= 0) {
            char b[16];
            follow::FormatElapsed((uint32_t)mins * 60u, b, sizeof(b));
            primary = b;
            // The label is set INSIDE the guard on purpose: MinutesToArrival
            // returns -1 rather than guess at an unknown groundspeed, and a
            // label left behind by a declined number names nothing.
            label = (st == follow::State::NoCoverage) ? "EST. ARRIVAL" : "TO ARRIVAL";
        }
    }

    // ---- the ladder: the most informative TRUE thing, or nothing ------------
    //
    // Reached whenever nothing above produced a number. In descending order of
    // what it means to the person watching:
    //   1. the route LENGTH, if both ends are placed -- a real measured number;
    //   2. the route itself, if the codes exist but cannot be placed. This is
    //      the code-only state, and the route is exactly what it knows;
    //   3. nothing at all. An empty hero slot is better than a filled one that
    //      says nothing, and the state chip below still names the situation.
    if (primary.isEmpty()) {
        if (placed && totalKm > 0.0f) {
            primary = units::FormatKm(totalKm, rangeUnit, true);
            if (label.isEmpty()) label = "ROUTE LENGTH";
        } else if (!view.origCode.isEmpty() && !view.destCode.isEmpty()) {
            String o = view.origCode, d = view.destCode;
            o.toUpperCase(); d.toUpperCase();
            primary = o + " -> " + d;
            psize = 2;                       // a 10-char pair cannot be size 3
            if (label.isEmpty()) label = "ROUTE ONLY";
        }
    }
    // §8 dims the readout in NO_COVERAGE: the number is an estimate and must
    // not carry the same weight as a measured one.
    if (!primary.isEmpty())
        row(primary, 124.0f,
            (st == follow::State::NoCoverage) ? FollowFade(colour, 0.55f) : colour, psize);
    if (!label.isEmpty()) row(label, 139.0f, FOLLOW_DIM, 1);

    // The shared slot. §8: "Altitude and the state chip occupy the same slot
    // deliberately: when anything degrades, altitude is what gets displaced,
    // and the layout does not jump."
    // THE CHIP DREW ONLY FOR ABSENT STATES, so ON THE GROUND could not appear
    // on this face at all -- Ground is not absent, so the altitude branch ran
    // instead and a parked aircraft was described entirely by "0 ft MSL".
    // Ground and Landed have headlines; they should use them. Airborne keeps
    // the altitude, which is the §8 arrangement: the chip displaces altitude
    // exactly when there is something more important to say than a number.
    const bool chipState = degraded || st == follow::State::Ground
                                    || st == follow::State::Landed;
    if (chipState) {
        const String chip = follow::Headline(st, follow::Regime::Airline);
        const int w = (int)backbuffer.textWidth(chip) + Si(14.0f);
        const int h = Si(17.0f);
        const int top = Si(158.0f) - h / 2;
        backbuffer.drawRoundRect(cx - w / 2, top, w, h, h / 2, colour);
        backbuffer.setTextColor(colour);
        backbuffer.drawString(chip, cx - (int)backbuffer.textWidth(chip) / 2,
                              top + (h - lineH) / 2);
        const String why = follow::Explanation(st, follow::Regime::Airline);
        // MOVED OFF THE LABEL ROW, 2026-08-29, and bounded so it cannot drift
        // back. At y=176 this line's band (176..184) overlapped BOTH airport
        // code boxes (y 175..183), and the widest centred span clearing the
        // destination label there is 100 px against a 195 px chord -- narrower
        // than the shortest explanation we have. Fitting to the chord therefore
        // ran every one of them through the code. y=186 clears the labels
        // entirely and keeps ~180 px.
        //
        // The boxes are passed rather than assumed: if the codes ever move, the
        // width follows them instead of silently overlapping again.
        if (!why.isEmpty()) {
            const int ey = Si(follow::ARC_EXPLAIN_Y);
            int ox = 0, oy = 0, dx = 0, dy = 0;
            at(a0, S(84.0f), ox, oy);
            at(a1, S(84.0f), dx, dy);
            const int ow = (int)backbuffer.textWidth(view.origCode) / 2 + Si(2.0f);
            const int dw = (int)backbuffer.textWidth(view.destCode) / 2 + Si(2.0f);
            const discgeom::Box boxes[2] = {
                { ox - ow, oy - lineH / 2, ox + ow, oy + lineH / 2 },
                { dx - dw, dy - lineH / 2, dx + dw, dy + lineH / 2 },
            };
            const int avail = discgeom::ClearCentredWidthPx(ey, lineH, SCREEN_SIZE,
                                                            boxes, 2);
            // TWO LINES, WHOLE, OR NOTHING IS CUT. The clamp that used to live
            // here turned "Out of receiver range, not off the radar." into
            // "Out of receiver range, not" -- a different sentence that reads as
            // a finished one. Right-truncation always removes the end, and the
            // reassurance is at the end of every one of these strings.
            //
            // WrapBreaks refuses rather than shortening, so a string that does
            // not fit is a fact about the STRING and the copy changes
            // deliberately. The host test asserts the drawn text EQUALS the
            // source, which is the only assertion that can catch this.
            const int ey2 = ey + lineH + 1;
            const int avail2 = discgeom::ClearCentredWidthPx(ey2, lineH, SCREEN_SIZE,
                                                             boxes, 2);
            const int widths[2] = { avail, avail2 };
            int st_[2] = {0,0}, ln_[2] = {0,0};
            const int used = follow::WrapBreaks(why.c_str(), widths, 2,
                                                (int)backbuffer.textWidth("M"), st_, ln_);
            backbuffer.setTextColor(FOLLOW_DIM);
            for (int i = 0; i < used; ++i) {
                const String part = why.substring(st_[i], st_[i] + ln_[i]);
                const int ly = (i == 0) ? ey : ey2;
                backbuffer.drawString(part, cx - (int)backbuffer.textWidth(part) / 2, ly);
            }
        }
    } else if (havePos) {
        // Same discipline as the local face: state the MSL figure with "MSL"
        // beside it, or decline. C5's elevation delivery is what would make an
        // AGL figure honest, and it is not built.
        if (!std::isnan(view.altMslFt))
            row(String((int)lroundf(view.altMslFt)) + " ft MSL", 158.0f, FOLLOW_DIM, 1);
    }

    // §18: the draw cost is INSTRUMENTED, not assumed -- and reported against
    // the stroke count, because a cost quoted without the geometry it was
    // measured at is the same mistake as a frame p95 with no contact count.
    followArcUs = micros() - t0;
    followArcStrokes = (size_t)strokes;
    if (followArcUs > followArcMaxUs) followArcMaxUs = followArcUs;
}

// ===========================================================================
// C4 -- THE PRE-DEPARTURE FACE. The state the owner sees FIRST.
// ===========================================================================
//
// You set up a follow the night before and then look at the device. If WAITING
// renders as an empty face -- or worse, as one of the loss states -- the
// feature looks broken at exactly the moment someone has finished configuring
// it. It must (1) prove the device understood them, (2) prove it is working,
// and (3) tell them to do nothing.
//
// ---------------------------------------------------------------------------
// C4 PROPOSES TWO FACES CHOSEN BY REGIME, AND THE REGIME IS UNKNOWABLE HERE
//
// This is a finding, not a shortcut. C4 asks for the arc face's not-started
// state in the airline regime and the local face's rings in the local one. But
// §7.1 infers the regime from how far the aircraft got FROM HOME -- and in
// WAITING it has never been seen. There is no flight, so there is no extent, so
// there is no regime. Picking one anyway means picking a face that is confidently
// wrong half the time, on the screen whose entire job is honest nothing-yet.
//
// So the pre-departure face is regime-AGNOSTIC: a dim ring, the target, and the
// copy. It cannot preview a route it has not been told (the route arrives on a
// tracked contact, and asking a server for one BY CALLSIGN would name the follow
// target in an outbound request -- C2's resolution forbids exactly that), so it
// previews the disc rather than the arc.
//
// If a route survives from a previous flight the codes are drawn, which is the
// preview C4 wanted, obtained without a single new request.
void AircraftManager::DrawFollowWaitingFace(BandCanvas& backbuffer)
{
    constexpr int cx = SCREEN_SIZE_DIV_2;
    const float k = (float)SCREEN_SIZE / 240.0f;
    const auto Si = [k](float v) { return (int)lroundf(v * k); };
    backbuffer.setTextSize(1);
    const int lineH = backbuffer.fontHeight() > 0 ? backbuffer.fontHeight() : 8;
    const auto centred = [&](const String& s, int y, uint32_t c) {
        backbuffer.setTextColor(c);
        const String fit = FitToDisc(backbuffer, s, y, lineH);
        backbuffer.drawString(fit, cx - (int)backbuffer.textWidth(fit) / 2, y);
    };

    backbuffer.drawCircle(cx, cx, Si(118.5f), FOLLOW_NEUTRAL);
    backbuffer.drawCircle(cx, cx, Si(100.0f), FOLLOW_NEUTRAL);

    centred(follow::Headline(follow::State::Waiting), Si(86.0f), FOLLOW_DIM);

    String who = followTarget; who.toUpperCase();
    if (FollowRouteKnown()) {
        String o = followRouteOrigin, d = followRouteDest;
        o.toUpperCase(); d.toUpperCase();
        who += "   " + o + " -> " + d;     // ASCII arrow: see RouteLabel.h
    } else if (!followHomeCode.isEmpty()) {
        who += "   at " + followHomeCode;
    }
    centred(who, Si(104.0f), FOLLOW_HOME);

    // THE THIRD LINE IS THE LOAD-BEARING ONE. It answers the question the owner
    // actually has -- "is it broken, or is he just not flying?" -- and tells
    // them there is no action to take.
    //
    // After seven days with no fix EVER seen it is replaced by the nudge: a tail
    // that is sold, re-registered or mistyped would otherwise be promised
    // "changes on its own" every day for a year. Only when a fix has never been
    // seen: an aeroplane parked for a fortnight after a real flight is a
    // different situation and already gets the post-flight card.
    const time_t nowUtc = time(nullptr);
    const uint32_t armed = followLog.ArmedEpoch();
    const bool stale = armed != 0 && nowUtc > 1600000000 &&
                       (uint32_t)nowUtc > armed + FOLLOW_STALE_NUDGE_SEC;

    String body = stale
        ? ("Nothing heard from " + followTarget + " in " +
           String((unsigned)((((uint32_t)nowUtc - armed)) / 86400u)) +
           " days. Check the tail number on the config page.")
        : String(follow::Explanation(follow::State::Waiting));

    // Wrapped on words into at most four lines, same rule as the local face's
    // explanation box: the chord, not the bounding box.
    constexpr int MAX_CHARS = SCREEN_SIZE / 11;
    String lines[4]; int nLines = 0;
    String word, cur;
    for (const char* p = body.c_str(); ; ++p) {
        if (*p && *p != ' ') { word += *p; continue; }
        if (word.length()) {
            if (cur.isEmpty()) cur = word;
            else if ((int)(cur.length() + 1 + word.length()) <= MAX_CHARS) cur += " " + word;
            else { if (nLines < 4) lines[nLines++] = cur; cur = word; }
            word = "";
        }
        if (!*p) break;
    }
    if (!cur.isEmpty() && nLines < 4) lines[nLines++] = cur;
    const int top = Si(132.0f);
    for (int i = 0; i < nLines; ++i)
        centred(lines[i], top + i * (lineH + 3), stale ? FOLLOW_AMBER : FOLLOW_DIM);
}

// ===========================================================================
// THE GLOBE FACE (§9) -- long-haul only
// ===========================================================================
//
// "An orthographic projection of a sphere IS a circle, so on a round panel it
// fills the glass with nothing cropped. No rectangular display can claim that.
// This is the one place where the hardware's shape is an advantage."
//
// Chosen over the arc face at 4,000 km (see the findings box at §9): below that
// the globe's route spans under ~58 px and a 270 degree arc is strictly more
// legible; above it the arc face can only say "42%" while the globe can say
// "over the pole, north of Siberia."
//
// THE PROJECTION AND THE COASTLINES ARE THE ANIMATION'S, not a second copy --
// include/GlobeProjection.h, extracted from src/anim/ so both products call one
// implementation. Nothing is cached into a sprite: the module measured a 240x240
// PSRAM blit at 6.24 ms against 0.34 ms to draw the whole globe live, so
// redrawing is ~18x cheaper than remembering.
//
// TILT IS ZERO HERE, AND THAT IS A DEPARTURE FROM THE MODULE'S 30 DEGREES. The
// animation tilts so a near-meridional missile arc bows across the disc instead
// of running down its spine. §9 asks for something incompatible with that:
// "centre the globe on the great-circle midpoint so both endpoints are visible."
// A tilt moves the midpoint off centre by construction. The composition rule
// wins, and the route reads as a straight line through the centre -- which is
// what a great circle through the view centre actually is.

/// Subsolar point, for the terminator. Low-precision declination (Cooper), good
/// to about half a degree.
///
/// THE EQUATION OF TIME IS OMITTED, and that is a measured decision rather than
/// an oversight: it reaches +/-16 minutes, i.e. +/-4 degrees of longitude, which
/// at r = 94 is under 1.7 px at the equator and less everywhere else. The
/// terminator is a soft edge on a 240 px disc; a correction smaller than the
/// line drawing it is not a correction.
static void FollowSubsolar(time_t utc, float& outLat, float& outLon)
{
    struct tm t;
    gmtime_r(&utc, &t);
    const int doy = t.tm_yday + 1;
    outLat = 23.44f * sinf(2.0f * 3.14159265f * (float)(doy - 81) / 365.0f);
    const float hours = (float)t.tm_hour + (float)t.tm_min / 60.0f +
                        (float)t.tm_sec / 3600.0f;
    outLon = -15.0f * (hours - 12.0f);
}

void AircraftManager::DrawRouteGlobe(BandCanvas& backbuffer, const RouteView& view)
{
    const uint32_t t0 = micros();
    const float k = (float)SCREEN_SIZE / 240.0f;
    const auto Si = [k](float v) { return (int)lroundf(v * k); };
    const int cx = Si(120.0f);
    // FULL-BLEED, 2026-08-29. §9 originally pulled the disc up to cy=102 and shrank
    // it to R=94 so the readout got a clear band underneath. That reserved a fifth
    // of the panel to avoid drawing text on a sphere -- and this function already
    // had the answer to that problem for its top row: a backing plate. So the disc
    // takes the whole screen the way Missileer's does (kGlobeR=119, and 120 would
    // touch x=240 and run off a 0..239 buffer), and every text row gets the plate
    // the top row already had.
    const int cy = Si(120.0f);
    // FRAMED TO THE ROUTE (#274 step 5), not fixed at one scale. Depends only on
    // the endpoints, so it is constant for as long as this flight is followed --
    // see GlobeRadiusForRoute for why that is what makes the LOD switch safe.
    const float routeKm = (view.org.known && view.dst.known)
        ? follow::GreatCircleKm(view.org.lat, view.org.lon, view.dst.lat, view.dst.lon)
        : 0.0f;
    const float R  = follow::GlobeRadiusForRoute(routeKm, k * 120.0f);
    const int   Ri = (int)lroundf(R);

    const follow::State st = view.st;
    const uint32_t colour = FollowStateColour(st, /*benignApproach=*/true);

    backbuffer.setTextSize(1);
    const int lineH = backbuffer.fontHeight() > 0 ? backbuffer.fontHeight() : 8;

    const follow::Endpoint& org = view.org;
    const follow::Endpoint& dst = view.dst;

    const float acLat = view.acLat, acLon = view.acLon;
    const bool  havePos = view.havePos;

    // §9: centred on the great-circle MIDPOINT so both ends are on the visible
    // hemisphere. MakeBasis does exactly that at tilt 0.
    const globeproj::Basis basis =
        globeproj::MakeBasis(org.lon, org.lat, dst.lon, dst.lat, 0.0f);

    // GREEN, taken from Missileer's globe rather than invented alongside it --
    // src/anim/FlightAnimation.cpp Ocean()/Coast(), whose palette note reads
    // "green = the world (coastlines, graticule)". Two products drawing the same
    // sphere in two different colours is a difference that means nothing, and the
    // one that already shipped a globe is the one to match.
    const uint32_t OCEAN = lgfx::color888(0x06, 0x18, 0x14);
    const uint32_t NIGHT = lgfx::color888(0x03, 0x0C, 0x0A);  // the same hue, darker
    const uint32_t COAST = lgfx::color888(0x2A, 0x9E, 0x62);
    const uint32_t TERM  = lgfx::color888(150, 96, 40);   // a thin WARM line

    // ---- the disc, and the night side --------------------------------------
    backbuffer.fillCircle(cx, cy, Ri, OCEAN);

    // The sun in CAMERA coordinates, so the day/night test is three multiplies
    // per sample instead of a rotation. A screen point (px, py) on the near
    // hemisphere is px*r + py*u + pz*v with pz = sqrt(1 - px^2 - py^2), so its
    // illumination is px*sr + py*su + pz*sv.
    float sunLat = 0.0f, sunLon = 0.0f, sun[3] = { 0.0f, 0.0f, 0.0f };
    const time_t nowUtc = time(nullptr);
    const bool clockSynced = nowUtc > 1600000000;
    if (clockSynced) {
        FollowSubsolar(nowUtc, sunLat, sunLon);
        globeproj::UnitVec(sunLon, sunLat, sun);
        const float sr = globeproj::Dot3(sun, basis.r);
        const float su = globeproj::Dot3(sun, basis.u);
        const float sv = globeproj::Dot3(sun, basis.v);

        // Scanline fill at 2 px. The night region is bounded by one great circle
        // and the limb, so a per-scanline sign scan finds it exactly to the step
        // size -- and 2 px is invisible against a terminator that is itself a
        // soft edge. Sampling per pixel would double the cost to resolve
        // something the atmosphere does not resolve either.
        // BOUNDED BY THE PANEL, NOT THE SPHERE (#274 step 1).
        //
        // This used to run `y` from cy-Ri to cy+Ri and scan the full chord at
        // each row. At the fixed R=119 that is nearly the panel and costs
        // nothing to ignore. Under route framing R reaches ~1350 for a short
        // hop, and the loop then scans 1,348 rows of which 120 are on screen --
        // 91 % of the work thrown away -- with the inner scan crossing the whole
        // sphere each time. Measured cost went as R^1.46, extrapolating to
        // ~770 ms per frame. That is the entire reason zooming looked impossible.
        //
        // Clipped, the fill is bounded by the PANEL at any zoom: the row count
        // is at most SCREEN_SIZE/STEP and each row spans at most SCREEN_SIZE.
        // The sphere may be enormous; the work is not.
        constexpr int STEP = 2;
        const int yLo = (cy - Ri) > 0 ? (cy - Ri) : 0;
        const int yHi = (cy + Ri) < (SCREEN_SIZE - 1) ? (cy + Ri) : (SCREEN_SIZE - 1);
        for (int y = yLo; y <= yHi; y += STEP) {
            const float py = (float)(cy - y) / R;
            const float halfSq = 1.0f - py * py;
            if (halfSq <= 0.0f) continue;
            const float half = sqrtf(halfSq);
            // Clip the span to the panel in x as well, in the same units the
            // scan already uses. Without this the row is cheap but still walks
            // the sphere's whole chord to find the two pixels that are visible.
            const float pxLoPanel = ((float)(0 - cx)) / R;
            const float pxHiPanel = ((float)(SCREEN_SIZE - 1 - cx)) / R;
            const float pxLo = (-half > pxLoPanel) ? -half : pxLoPanel;
            const float pxHi = ( half < pxHiPanel) ?  half : pxHiPanel;
            if (pxHi <= pxLo) continue;
            int runStart = 0;
            bool inRun = false;
            for (int i = 0; ; ++i) {
                const float px = pxLo + (float)i * (float)STEP / R;
                const bool past = px > pxHi;
                bool night = false;
                if (!past) {
                    const float pz2 = 1.0f - px * px - py * py;
                    const float pz = pz2 > 0.0f ? sqrtf(pz2) : 0.0f;
                    night = (px * sr + py * su + pz * sv) < 0.0f;
                }
                const int sx = cx + (int)lroundf(px * R);
                if (night && !inRun) { runStart = sx; inRun = true; }
                else if ((!night || past) && inRun) {
                    backbuffer.fillRect(runStart, y, sx - runStart, STEP, NIGHT);
                    inRun = false;
                }
                if (past) break;
            }
        }
    }

    // ---- coastlines ---------------------------------------------------------
    // A segment is drawn only when BOTH ends are on the near hemisphere, which is
    // the module's own rule: clipping to the limb buys at most half a pixel,
    // because the data is decimated to ~1 px and everything near the limb is
    // foreshortened below that.
    // Coarse below the LOD radius, dense above -- the extra vertices are
    // sub-pixel at whole-earth zoom and cost 19 ms for nothing there.
    int ringCount = 0;
    const globeproj::Coastline* rings = globeproj::CoastlinesFor(R, ringCount);
    int vertices = 0;
    for (int i = 0; i < ringCount; ++i) {
        const globeproj::GeoVec* v = rings[i].v;
        const int n = rings[i].n;
        float px = 0.0f, py = 0.0f;
        bool pv = false;
        for (int a = 0; a <= n; ++a) {
            const globeproj::GeoVec& p = v[a == n ? 0 : a];   // close the ring
            float x = 0.0f, y = 0.0f;
            const bool vis = globeproj::Project(basis,
                p.x * globeproj::VEC_INV, p.y * globeproj::VEC_INV,
                p.z * globeproj::VEC_INV, (float)cx, (float)cy, R, x, y);
            if (vis && pv &&
                !globeproj::SegmentOffPanel(px, py, x, y, SCREEN_SIZE, SCREEN_SIZE))
                backbuffer.drawLine((int)px, (int)py, (int)x, (int)y, COAST);
            px = x; py = y; pv = vis;
            ++vertices;
        }
    }

    // The limb: the one line that makes the disc a sphere rather than a circle.
    backbuffer.drawCircle(cx, cy, Ri, lgfx::color888(0x12, 0x46, 0x33));  // graticule green

    // The terminator itself, as the great circle perpendicular to the sun.
    if (clockSynced) {
        // Any two orthogonal vectors in the plane normal to the sun. e1 is the
        // sun crossed with world north, which degenerates only with the sun
        // exactly over a pole -- impossible, since |declination| <= 23.44.
        float north[3] = { 0.0f, 0.0f, 1.0f }, e1[3], e2[3];
        globeproj::Cross3(sun, north, e1); globeproj::Norm3(e1);
        globeproj::Cross3(sun, e1, e2);    globeproj::Norm3(e2);
        // SAMPLES PROPORTIONAL TO ZOOM (#274 step 3). A fixed 120 samples of the
        // whole great circle is ~6 px per segment at R=119. At R=1350 the
        // visible cap is ~10 degrees of arc, so about THREE samples land on the
        // panel and the terminator becomes a three-segment polyline. Scaling
        // with R holds the on-screen segment length roughly constant; the
        // off-panel majority is now thrown away by the outcode cull above
        // rather than drawn.
        const int termSteps = (int)fminf(1440.0f, fmaxf(120.0f, R));
        float px = 0.0f, py = 0.0f; bool pv = false;
        for (int i = 0; i <= termSteps; ++i) {
            const float a = (float)i * (2.0f * 3.14159265f / (float)termSteps);
            const float ca = cosf(a), sa = sinf(a);
            float x = 0.0f, y = 0.0f;
            const bool vis = globeproj::Project(basis,
                e1[0] * ca + e2[0] * sa, e1[1] * ca + e2[1] * sa,
                e1[2] * ca + e2[2] * sa, (float)cx, (float)cy, R, x, y);
            if (vis && pv &&
                !globeproj::SegmentOffPanel(px, py, x, y, SCREEN_SIZE, SCREEN_SIZE))
                backbuffer.drawLine((int)px, (int)py, (int)x, (int)y, TERM);
            px = x; py = y; pv = vis;
        }
    }

    // ---- the route ----------------------------------------------------------
    //
    // §9: dashed AHEAD, solid BEHIND. The split is at the aircraft's along-track
    // progress, not at its projection onto the line -- see ProgressAlong.
    const float progress = havePos ? follow::ProgressAlong(org, dst, acLat, acLon) : 0.0f;
    {
        constexpr int STEPS = 96;
        float px = 0.0f, py = 0.0f; bool pv = false;
        for (int i = 0; i <= STEPS; ++i) {
            const float f = (float)i / (float)STEPS;
            float w[3];
            globeproj::GreatCirclePoint(org.lon, org.lat, dst.lon, dst.lat, f, w);
            float x = 0.0f, y = 0.0f;
            const bool vis = globeproj::Project(basis, w[0], w[1], w[2],
                                                (float)cx, (float)cy, R, x, y);
            if (vis && pv) {
                const bool behind = f <= progress;
                // The dash is on the UNFLOWN half only, so "how far along" reads
                // off the solid run without a marker being needed to say it.
                if (behind || (i & 2))
                    backbuffer.drawLine((int)px, (int)py, (int)x, (int)y,
                                        behind ? colour : FollowFade(colour, 0.5f));
            }
            px = x; py = y; pv = vis;
        }
    }

    // Endpoints, then the aircraft.
    const auto place = [&](float lat0, float lon0, int r, uint32_t c, bool fill) {
        float w[3], x = 0.0f, y = 0.0f;
        globeproj::UnitVec(lon0, lat0, w);
        if (!globeproj::Project(basis, w[0], w[1], w[2], (float)cx, (float)cy, R, x, y))
            return;
        if (fill) backbuffer.fillCircle((int)x, (int)y, r, c);
        else      backbuffer.drawCircle((int)x, (int)y, r, c);
    };
    place(org.lat, org.lon, Si(3.0f), FOLLOW_DIM, false);
    place(dst.lat, dst.lon, Si(3.0f), FOLLOW_DIM, true);

    // §9: THE AIRCRAFT IS DRAWN AT ITS REAL ADS-B POSITION, NOT INTERPOLATED
    // ONTO THE LINE. "The gap between the two is the actual routing and is more
    // interesting than a bead on a wire."
    if (havePos) {
        const bool degraded = follow::Machine::IsAbsent(st);
        place(acLat, acLon, Si(4.0f), colour, !degraded);
        float w[3], x = 0.0f, y = 0.0f;
        globeproj::UnitVec(acLon, acLat, w);
        if (globeproj::Project(basis, w[0], w[1], w[2], (float)cx, (float)cy, R, x, y))
            backbuffer.drawCircle((int)x, (int)y, Si(8.0f), FollowFade(colour, 0.35f));
    }

    // ---- text, plated, over the disc ----------------------------------------
    const String& who = view.label;
    String o = view.origCode, d = view.destCode;
    o.toUpperCase(); d.toUpperCase();
    // The backing plate: text over ocean is legible, text over the terminator is
    // not, and which one a given route produces is not knowable in advance.
    //
    // EVERY row gets one now, not just this one. While the disc stopped at R=94
    // the lower rows sat on bare background and needed nothing; full-bleed puts
    // them over coastline, terminator and night side at once, which is the exact
    // condition this plate was written for. One lambda so a row cannot be added
    // later without it.
    const auto plated = [&](const String& sTxt, int y, uint32_t c) {
        const String fit = FitToDisc(backbuffer, sTxt, y, lineH);
        const int w = (int)backbuffer.textWidth(fit) + Si(8.0f);
        backbuffer.fillRect(cx - w / 2, y - 2, w, lineH + 4, lgfx::color888(0, 0, 0));
        backbuffer.setTextColor(c);
        backbuffer.drawString(fit, cx - (int)backbuffer.textWidth(fit) / 2, y);
    };
    plated(who + "  " + o + " -> " + d, Si(26.0f), FOLLOW_DIM);

    if (follow::Machine::IsAbsent(st))
        plated(follow::Headline(st, follow::Regime::Airline), Si(206.0f), colour);
    plated(String((int)lroundf(progress * 100.0f)) + "%  of  " +
           units::FormatKm(follow::GreatCircleKm(org.lat, org.lon, dst.lat, dst.lon),
                           rangeUnit, true),
           Si(223.0f), FOLLOW_DIM);

    followArcUs = micros() - t0;
    followArcStrokes = (size_t)vertices;
    if (followArcUs > followArcMaxUs) followArcMaxUs = followArcUs;
}

// ===========================================================================
// THE POST-FLIGHT CARD (§11) -- "the answer to 'the screen is empty most of the
// week'"
// ===========================================================================
//
// SHAPE RATHER THAN POSITION, and that is the whole design. There is no arc, no
// bearing, no live data and no home marker: the flight is drawn about its OWN
// bounding box, not about the field, so what the customer sees is the figure
// they flew rather than where it sat on a map.
//
//   "The alternative was to drop shape and show four numbers. Rejected because
//    the shape IS the emotional payload: a racetrack of circuits is the picture
//    that says 'he practised landings today' without a word of text."
//
// Origin hollow, destination filled -- so a pattern reads as a loop that started
// and ended at the same place, which for circuits is the truth and is exactly
// what makes six touch-and-goes legible as six.
void AircraftManager::DrawFollowPostFlightCard(BandCanvas& backbuffer)
{
    constexpr int cx = SCREEN_SIZE_DIV_2;
    const follow::FlightRecord& r = followLog.Record();
    const size_t n = followLog.Size();

    backbuffer.setTextSize(1);
    const auto centred = [&](const String& s, int y, uint32_t colour) {
        backbuffer.setTextColor(colour);
        backbuffer.drawString(s, cx - (int)backbuffer.textWidth(s) / 2, y);
    };
    const uint32_t DIM   = lgfx::color888(110, 110, 110);
    const uint32_t INK   = lgfx::color888(0, 190, 220);
    const uint32_t LABEL = lgfx::color888(0, 150, 170);

    centred("LAST FLIGHT", 8, LABEL);

    // The clock is quoted only when it was real. A device that never reached NTP
    // records 0, and 0 rendered as a date is 1970 -- a wrong fact stated with the
    // same confidence as a right one, on the one face whose entire claim is that
    // nothing here can be wrong.
    if (r.landedEpoch) {
        const time_t local = (time_t)r.landedEpoch + utcOffsetSec;
        struct tm t; gmtime_r(&local, &t);
        char when[24];
        snprintf(when, sizeof(when), "%02d %s  %02d:%02d",
                 t.tm_mday,
                 (const char*[]){"Jan","Feb","Mar","Apr","May","Jun",
                                 "Jul","Aug","Sep","Oct","Nov","Dec"}[t.tm_mon],
                 t.tm_hour, t.tm_min);
        centred(when, 21, DIM);
    } else {
        centred("time not known", 21, DIM);
    }

    // ---- the shape ----------------------------------------------------------
    //
    // Auto-fit about the track's OWN extent, isotropically. A per-axis fit would
    // stretch a circuit into an oval and quietly change the figure -- the one
    // thing the card is of.
    if (n >= 2) {
        float minLat = 1e9f, maxLat = -1e9f, minLon = 1e9f, maxLon = -1e9f;
        for (size_t i = 0; i < n; ++i) {
            float la, lo; followLog.PointAt(i, la, lo);
            if (la < minLat) minLat = la;
            if (la > maxLat) maxLat = la;
            if (lo < minLon) minLon = lo;
            if (lo > maxLon) maxLon = lo;
        }
        const float midLat = (minLat + maxLat) * 0.5f;
        const float midLon = (minLon + maxLon) * 0.5f;
        const float kmLat = 111.0f;
        const float kmLon = 111.0f * cosf(radians(midLat));
        const float spanNorthKm = (maxLat - minLat) * kmLat;
        const float spanEastKm  = (maxLon - minLon) * kmLon;
        const float spanKm = std::max(0.001f, std::max(spanNorthKm, spanEastKm));

        const int boxPx = SCREEN_SIZE / 3; // half-extent of the drawing area
        const float pxPerKm = (float)boxPx / (spanKm * 0.5f) * 0.5f;

        const auto project = [&](float la, float lo, int& px, int& py) {
            const float dN = (la - midLat) * kmLat;
            const float dE = (lo - midLon) * kmLon;
            // North-up, always. The card is a record, not a view out of a
            // window, so `radar-up` deliberately does NOT apply -- rotating a
            // souvenir by a setting made for live traffic would mean the same
            // flight looked different on two devices.
            px = cx + (int)lroundf(dE * pxPerKm);
            py = cx - (int)lroundf(dN * pxPerKm);
        };

        int prevX = 0, prevY = 0;
        for (size_t i = 0; i < n; ++i) {
            float la, lo; followLog.PointAt(i, la, lo);
            int x, y; project(la, lo, x, y);
            if (i) backbuffer.drawLine(prevX, prevY, x, y, INK);
            prevX = x; prevY = y;
        }

        float la0, lo0, la1, lo1;
        followLog.PointAt(0, la0, lo0);
        followLog.PointAt(n - 1, la1, lo1);
        int ox, oy, dx, dy;
        project(la0, lo0, ox, oy);
        project(la1, lo1, dx, dy);
        backbuffer.drawCircle(ox, oy, 4, lgfx::color888(200, 200, 200)); // origin, hollow
        backbuffer.fillCircle(dx, dy, 4, lgfx::color888(255, 255, 255)); // destination, filled
    }

    // ---- the numbers --------------------------------------------------------
    //
    // Altitude is MSL and labelled for it. C5's elevation half is not delivered,
    // and a souvenir is the worst place for a figure that is quietly wrong by
    // the field elevation -- it is the number the customer looks at afterwards,
    // repeatedly, with nothing live beside it to contradict it.
    const uint32_t sec = r.durationSec;
    char dur[16];
    if (sec >= 3600) snprintf(dur, sizeof(dur), "%luh %02lum",
                              (unsigned long)(sec / 3600), (unsigned long)((sec % 3600) / 60));
    else             snprintf(dur, sizeof(dur), "%lum", (unsigned long)(sec / 60));

    centred(String(dur) + " in the air", SCREEN_SIZE - 40, INK);
    centred(String((long)r.maxAltMslFt) + " ft MSL   " + String((unsigned)r.topSpeedKt) + " kt",
            SCREEN_SIZE - 28, DIM);
    centred(units::FormatKm(r.furthestKmX10 / 10.0f, rangeUnit, /*space=*/true) + " out",
            SCREEN_SIZE - 16, DIM);
    // CIRCUIT COUNT belongs on this line and is not here -- §11 defers it in the
    // same paragraph that asks for it. See the note in include/FollowGeometry.h.
}

void AircraftManager::DrawAircraftTrail(BandCanvas& backbuffer, const TrackedAircraft& tracked, int headX, int headY, float brightness) const
{
    const int n = tracked.TrailSize();
    if (n < 1) return;

    int prevX = 0, prevY = 0;
    bool havePrev = false;
    for (int i = 0; i < n; ++i) {
        auto [lat, lon] = tracked.TrailPointAt(i);
        auto [x, y] = ProjectCoordinateToScreen(lat, lon);

        if (havePrev) {
            // Fade from dim (oldest) to bright (newest). The floor of 40 keeps
            // the tail above the 8-bit display's green quantization step so it
            // doesn't vanish. brightness scales the whole trail with the blip so
            // the contact fades as a unit when paint-and-fade is on.
            const uint8_t g = static_cast<uint8_t>((40 + (180 * i) / n) * brightness);
            backbuffer.drawLine(prevX, prevY, x, y, lgfx::color888(0, g, 0));
        }

        prevX = x;
        prevY = y;
        havePrev = true;
    }

    // connect the most recent sample to the live aircraft position so the trail
    // stays attached to the marker between samples
    backbuffer.drawLine(prevX, prevY, headX, headY, lgfx::color888(0, (uint8_t)(220 * brightness), 0));
}

void AircraftManager::HandleTouch()
{
    int32_t tx = 0, ty = 0;
    bool touched;

    // Dual-core (S3): touch sits on its own I2C bus and the network runs over WiFi on a
    // separate core, so there is no touch/TLS wedge to prevent. Gating the poll on the HTTP
    // mutex here would silently drop taps whenever always-on enrichment held the bus -- which
    // it does constantly -- so a blip tap could take many tries to land. Poll every loop.
    //
    // The retired single-core C3 DID gate this on http.TryAcquireBus(), on the radar view
    // only, because an overlapping touch I2C transfer wedged the CST816 off the bus until
    // reboot (PR #8 / commit 56a3df2). Removed 2026-08-09 with the board. Do not reinstate it
    // on an S3 as a precaution -- see the cost above, and TouchPoll.h for the same note.
    touched = tft.getTouch(&tx, &ty);
    if constexpr (variant::TOUCH_WATCHDOG)
        TouchWatchdog::OnPoll(tft, touched, true); // no bus serialization on these boards

#ifdef SOAK_TEST
    // Realistic-duty soak: the sparse gesture script drives classification only
    // while one of its gestures is in flight; between bursts real touches pass
    // through unchanged (so a human poke at the bench still behaves normally).
    bool sTouched; int sx, sy;
    if (SoakHarness::NextTouchSample(sTouched, sx, sy))
        ProcessTouchSample(sTouched, sx, sy);
    else
        ProcessTouchSample(touched, tx, ty);
#else
    ProcessTouchSample(touched, tx, ty);
#endif
}

void AircraftManager::ProcessTouchSample(bool touched, int32_t tx, int32_t ty)
{
    const unsigned long now = millis();
    if (touched) {
        lastTouchActivityMs = now; // proof the controller is alive
        if (!wasTouched) {
            touchStartX = tx; touchStartY = ty; // press edge
            touchPressMs = now;
            Serial.printf("[touch] %lu press (%d,%d) inDetail=%d\n", now, (int)tx, (int)ty, (int)inDetail);
        }
        touchLastX = tx;
        touchLastY = ty;
    }

    // Before the tap/swipe classifier: a hold is a property of the contact WHILE
    // it is happening, and by release it is already over. Also runs on the
    // not-touched samples, which is where the release grace is measured.


    if (!touched && wasTouched) {
        // release: classify the stroke as a tap or a 4-way swipe from its delta
        const int dx = touchLastX - touchStartX;
        const int dy = touchLastY - touchStartY;
        const int adx = abs(dx), ady = abs(dy);
        constexpr int SWIPE_MIN = 40;

        Serial.printf("[touch] %lu release start=(%d,%d) end=(%d,%d) d=(%d,%d) held=%lums -> %s\n",
                      now, touchStartX, touchStartY, touchLastX, touchLastY, dx, dy,
                      now - touchPressMs,
                      (adx < SWIPE_MIN && ady < SWIPE_MIN) ? "TAP" : "SWIPE");

        if (adx < SWIPE_MIN && ady < SWIPE_MIN)
            HandleTap(touchStartX, touchStartY);
        else if (adx >= ady)
            HandleSwipe(dx > 0 ? Swipe::Right : Swipe::Left);
        else
            HandleSwipe(dy > 0 ? Swipe::Down : Swipe::Up);
    }

    wasTouched = touched;
}

// Backfill the logbook from contacts already being tracked, for the moment the
// owner switches the logbook (or the leaderboard) on with a populated scope.
//
// Deliberately mirrors the two places the logbook is normally written -- the
// first-emplacement branch in the merge, and ApplyEnrichment -- rather than
// inventing a third path, so a contact seeded here is indistinguishable from one
// that arrived a second later. Enrichment fields are only noted when already
// present: an unenriched contact is left alone and picked up by ApplyEnrichment
// in the normal way when its lookup lands.
//
// NoteContact is an odometer, so this is an EDGE-ONLY call (Initialise runs on
// every config save; seeding on each one would inflate the count by the size of
// the sky every time somebody pressed Save).
void AircraftManager::SeedLogbookFromTracked()
{
    if (trackedAircraft.empty())
        return;

    uint16_t seededTypes = 0, seededOps = 0, seededPorts = 0, seededCountries = 0;
    for (auto& kv : trackedAircraft) {
        TrackedAircraft& t = kv.second;
        logbook.NoteContact();
        if (logbook.NoteCountry(t.state.originCountry)) ++seededCountries;
        if (!t.typeCode.isEmpty()) {
            if (logbook.NoteType(t.typeCode)) ++seededTypes;
            // Same live question ApplyEnrichment asks: is this type unclaimed?
            // Without it the gold NEW badge would not appear until the contact
            // happened to be re-enriched, and the owner would be looking at a
            // scope full of claimable aircraft that do not say so.
            t.claimable = logbook.IsTypeClaimable(t.typeCode);
        }
        if (!t.operatorName.isEmpty() && logbook.NoteOperator(t.operatorName)) ++seededOps;
        if (logbook.NoteAirport(t.routeOrigin)) ++seededPorts;
        if (logbook.NoteAirport(t.routeDest)) ++seededPorts;
    }
    Serial.printf("[logbook] seeded from %u contact(s) already tracked: "
                  "%u types, %u airlines, %u countries, %u airports\n",
                  (unsigned)trackedAircraft.size(), (unsigned)seededTypes,
                  (unsigned)seededOps, (unsigned)seededCountries, (unsigned)seededPorts);
}

void AircraftManager::ExitDetail()
{
    inDetail = false;
    detailPage = 0;
    // RETAIN the photo sprite on PSRAM boards; free it only where it is actually
    // contended. The original reasoning -- "holding it allocated is what tips the
    // heap below what an adsbdb/photo TLS handshake needs" -- is a C3 statement:
    // true on a banded, PSRAM-less board where the sprite lives in the same scarce
    // internal heap mbedTLS draws from, and FALSE on every shipping SKU, where
    // setPsram(true) at the allocation site puts it in PSRAM that a handshake can
    // never allocate from. Measured 2026-08-09 on the bench s3-128: creating the
    // backbuffer plus a 150x100 and a 240x240 photo sprite moved psram_free by
    // 73,532 B and left the internal heap untouched.
    //
    // So on S3 the free bought nothing and cost a malloc/free of the sprite on
    // every single card open/close. That is small today (15,000 B) and stops being
    // small under the full-bleed round card (issue #209), where it becomes a
    // 57,600 B PSRAM alloc/free per tap.
    //
    // Retention is safe because the buffer is never READ without the two flags
    // below agreeing: `hasPhoto` is `photoReady && photoIcao == selectedIcao &&
    // <buffer>`, and both of those are cleared here. With the sprite retained the
    // third conjunct is merely always-true after the first decode, not a second
    // opinion that can disagree with the first two. The fillScreen before drawJpg
    // at the allocation site stays load-bearing for a NEW reason: it now clears
    // the PREVIOUS aircraft's pixels rather than uninitialised memory.
    if constexpr (variant::BANDED_RENDER) {
        if (photoSprite.getBuffer() != nullptr)
            photoSprite.deleteSprite();
    }
    photoReady = false;
    photoResolved = false;
    photoIcao = "";
    // Arm the reopen refractory (see the member comment): if this close was one
    // half of a glitch-split tap (or swipe), the trailing half arrives within a
    // frame or two and must not open a new card. Also armed by the idle
    // auto-close, where it's harmless -- nobody is touching.
    tapSuppressUntilMs = millis() + 400;
}

void AircraftManager::HandleTap(int tx, int ty)
{
    // The reset menu takes every tap while it is open, before anything else can
    // claim one. It is drawn over the whole screen, so any other handler acting
    // here would be acting on pixels the customer cannot see.
    if (HandleResetMenuTap(tx, ty))
        return;

    // Tap the alert ring (or anywhere during a full-screen flash burst) to dismiss
    // the current military/emergency episode. It re-arms once every alerting contact
    // has left the screen, so this quiets a lingering ring without disabling the
    // feature. Checked before card/blip handling so an edge tap always reaches it.
    if (TapDismissesAlert(tx, ty)) {
        DismissVisualAlert();
        return;
    }

    // detail card: flip the photo page to the data page, else close
    if (inDetail) {
        const bool hasPhoto = photoReady && photoIcao == selectedIcao && photoSprite.getBuffer() != nullptr;
        Serial.printf("[touch] tap-in-detail hasPhoto=%d page=%d -> %s\n",
                      (int)hasPhoto, detailPage, (hasPhoto && detailPage == 0) ? "flip-to-data" : "CLOSE");
        if (hasPhoto && detailPage == 0)
            detailPage = 1;
        else ExitDetail();
        return;
    }

    // Inside the card-close refractory this "tap" is almost certainly the trailing
    // half of the tap that just closed the card: swallow it rather than open a card
    // for whatever sits under the finger (bench-observed: close-tap over a contact
    // instantly reopened that contact's card). Wrap-safe signed comparison.
    if ((long)(millis() - tapSuppressUntilMs) < 0)
        return;

    // Stats screen: a tap on the "Reset" row OPENS THE MENU. It is not itself
    // destructive -- see the header note; that is the whole point of the menu
    // existing. Swallowed here rather than falling through, so the tap cannot
    // also be reinterpreted as some other screen's gesture.
    if (screen == Screen::Stats && resetRowY0 >= 0 && ty >= resetRowY0 && ty <= resetRowY1) {
        Serial.println("[reset] menu opened from the Stats row");
        resetMenu = ResetMenu::Choosing;
        return;
    }

    if (screen == Screen::Radar) {
        // Pick the contact under the finger. Markers are tiny (~3 px) and a fingertip lands a
        // couple of mm off -- worse for the low/edge contacts -- so the hit region is generous:
        // the marker within TAP_RADIUS, OR the info label (drawn below-right, which the eye reads
        // as "the aircraft"). Dense areas stack several contacts within a finger-width, so we
        // gather every candidate and, on repeated taps at ~the same spot, cycle through them
        // (nearest marker first) -- otherwise a buried contact is unreachable.
        constexpr int TAP_RADIUS = 28;
        std::vector<std::pair<int, String>> cands; // (dist2 to marker, icao)
        for (auto& [icao, tracked] : trackedAircraft) {
            if (tracked.state.onGround) continue;
            // hit-test where the blip is drawn (latched under paint-and-fade), not its live
            // position -- otherwise taps miss a blip paused mid-sweep waiting for the next pass
            auto [la, lo] = RadarBlipPosition(tracked);
            auto [x, y] = ProjectCoordinateToScreen(la, lo);
            // A contact projected well off the face isn't drawn, so it can't be
            // tapped. This also keeps the dist2 math below from overflowing on a
            // far-extrapolated ghost during a long stale period (a huge dx makes
            // dx*dx wrap negative, which would satisfy the radius test everywhere).
            constexpr int OFF_MARGIN = 40; // a little past the edge for label boxes
            if (x < -OFF_MARGIN || x > SCREEN_SIZE + OFF_MARGIN ||
                y < -OFF_MARGIN || y > SCREEN_SIZE + OFF_MARGIN)
                continue;
            const int dx = x - tx, dy = y - ty;
            const int dist2 = dx * dx + dy * dy;
            bool hit = dist2 <= TAP_RADIUS * TAP_RADIUS;
            if (!hit) {
                int bx, by, bw, bh;
                if (AircraftLabelBox(tracked, x, y, bx, by, bw, bh))
                    hit = (tx >= bx && tx <= bx + bw && ty >= by && ty <= by + bh);
            }
            if (hit) cands.emplace_back(dist2, icao);
        }
        if (cands.empty()) {
            pinnedIcao = ""; // tap on empty radar clears the pin
            lastTapX = lastTapY = -1000;
        } else {
            std::sort(cands.begin(), cands.end()); // nearest marker first
            const int ddx = tx - lastTapX, ddy = ty - lastTapY;
            const bool sameSpot = lastTapX > -1000 && (ddx * ddx + ddy * ddy) <= 18 * 18;
            tapCycleIndex = sameSpot ? (tapCycleIndex + 1) % (int)cands.size() : 0;
            lastTapX = tx;
            lastTapY = ty;
            selectedIcao = cands[tapCycleIndex].second;
            inDetail = true;
            detailPage = 0;
            usageStore.CardOpened();
            // Already enriched: claim now, so the confirmation lands with the tap.
            // Cold contacts claim later, when enrichment reveals the type.
            auto sel = trackedAircraft.find(selectedIcao);
            if (sel != trackedAircraft.end())
                ClaimTappedAircraft(sel->second);
        }
    } else if (screen == Screen::List) {
        // map the tapped row to an aircraft (same layout as DrawList)
        if (ty >= LIST_ROW_TOP) {
            const int r = (ty - LIST_ROW_TOP) / LIST_ROW_H;
            if (r >= 0 && r < LIST_ROWS) {
                const std::vector<String> order = SortedAircraftByDistance();
                const int idx = listScroll + r;
                if (idx >= 0 && idx < (int)order.size()) {
                    selectedIcao = order[idx];
                    inDetail = true;
                    detailPage = 0;
                    usageStore.CardOpened();
                    auto sel = trackedAircraft.find(selectedIcao); // same claim-on-open as the radar
                    if (sel != trackedAircraft.end())
                        ClaimTappedAircraft(sel->second);
                }
            }
        }
    }
    // Stats screen: tap does nothing
}

void AircraftManager::EnterScreen(Screen s)
{
    screen = s;
    // Follow's counter was reserved in the wire format before Follow existed, so
    // the arity did not shift when it merged -- the case below is all that had to
    // be added. That was the point of reserving it.
    switch (s) {
        case Screen::Radar:  usageStore.ScreenRadar();  break;
        case Screen::List:   usageStore.ScreenList();   break;
        case Screen::Stats:  usageStore.ScreenStats();  break;
        case Screen::Follow: usageStore.ScreenFollow(); break;
    }
}

// WHAT COUNTS AS A SCREEN SWITCH, and why three sites deliberately do not call
// the function above.
//
// The counter answers "did the customer use this screen", so it counts screen
// changes THE CUSTOMER ASKED FOR: swipes, and the two session-follow gestures.
// It does not count the device moving itself.
//
// Three sites assign `screen` directly, on purpose:
//
//   - the auto-surface on a Follow state transition, and its dwell return. The
//     device raised the face; nobody navigated to it. Counting it would inflate
//     screenFollow with events the customer did not cause, and Follow would read
//     as used on a device whose owner never touched it -- a metric that argues
//     for keeping a feature by counting its own notifications.
//   - the forced eviction when a followed flight lands and the Follow screen
//     stops being visible underneath someone. That is the screen being taken
//     away, not a visit to Radar.
//   - the FOLLOW_BENCH forcing key, which is synthetic by construction.
//
// Each is marked at its site. If a fourth appears, decide which kind it is
// rather than defaulting -- the honest reading of this counter depends on it.

void AircraftManager::HandleSwipe(Swipe swipe)
{
    // detail card: swipe up pins ("tracks") the aircraft and returns to the
    // radar; any other swipe just closes the card
    if (inDetail) {
        if (swipe == Swipe::Up) {
            pinnedIcao = (pinnedIcao == selectedIcao) ? "" : selectedIcao;
            EnterScreen(Screen::Radar);
        }
        // SWIPE DOWN FOLLOWS THIS FLIGHT for the session -- but only when there
        // is a route to draw. docs/tap-to-peek.md's first rule: no route, no
        // affordance, and the swipe keeps its old meaning of "close". An
        // affordance that sometimes does nothing teaches people it does nothing.
        if (swipe == Swipe::Down) {
            auto sel = trackedAircraft.find(selectedIcao);
            if (sel != trackedAircraft.end() &&
                !sel->second.routeOrigin.isEmpty() && !sel->second.routeDest.isEmpty()) {
                SetSessionFollow(sel->second);
                inDetail = false;   // NOT ExitDetail(): SetSessionFollow already
                                    // chose the screen, and ExitDetail would
                                    // undo it.
                return;
            }
        }
        ExitDetail();
        return;
    }

    // Swipe down on the follow face stops a SESSION follow. A configured target
    // is not touched -- it was set deliberately on the config page, and a
    // gesture must not delete a setting somebody typed.
    if (screen == Screen::Follow && swipe == Swipe::Down && FollowSessionActive()) {
        ClearSessionFollow();
        return;
    }

    // list view: vertical swipe scrolls
    if (screen == Screen::List && (swipe == Swipe::Up || swipe == Swipe::Down)) {
        listScroll += (swipe == Swipe::Up) ? LIST_ROWS - 1 : -(LIST_ROWS - 1);
        if (listScroll < 0) listScroll = 0; // upper bound clamped in DrawList
        return;
    }

    // horizontal swipe cycles the top-level screens (left = next, right = prev).
    //
    // A swipe while the reset menu is open CLOSES IT and changes nothing else.
    // The menu covers the screen, so cycling underneath it would leave a
    // destructive confirmation sitting over a screen it does not belong to --
    // and a cloth dragged over the panel reads as a swipe at least as readily as
    // it reads as a tap.
    if (resetMenu != ResetMenu::Closed) {
        Serial.println("[reset] menu dismissed by a swipe");
        CloseResetMenu();
        return;
    }
    //
    // A swipe is also the customer taking the wheel, so it CANCELS an
    // auto-surfaced Follow dwell (§13.3). Snapping back to Follow a few seconds
    // after someone deliberately swiped away from it is the device arguing.
    followAutoUntilMs = 0;
    if (swipe == Swipe::Left)  AdvanceScreen(+1);
    if (swipe == Swipe::Right) AdvanceScreen(-1);
}

// Walk to the next VISIBLE screen. Follow is hidden entirely when no aircraft is
// being followed (§13.3), so the cycle cannot be a fixed modulus -- a `% 4`
// would give every collection customer who never uses Follow a dead screen
// between Stats and Radar, which is the failure the other editions avoid by
// skipping empty feeds.
void AircraftManager::AdvanceScreen(int dir)
{
    int next = (int)screen;
    for (int i = 0; i < SCREEN_COUNT; ++i) {
        next = (next + dir + SCREEN_COUNT) % SCREEN_COUNT;
        if ((Screen)next == Screen::Follow && !FollowScreenVisible())
            continue;
        EnterScreen((Screen)next);
        return;
    }
}

void AircraftManager::CloseResetMenu()
{
    resetMenu = ResetMenu::Closed;
    // Bounds cleared with the state. A hit box left behind by a screen that is
    // no longer drawn is a tap target the customer cannot see, which is the one
    // failure mode a "the pixels and the hit test agree" rule exists to stop.
    resetOptWifiY0 = resetOptWifiY1 = -1;
    resetOptFactoryY0 = resetOptFactoryY1 = -1;
    resetConfirmY0 = resetConfirmY1 = -1;
    resetCancelY0 = resetCancelY1 = -1;
}

// The reset menu. Two screens, both drawn here, both entirely made of taps.
//
// GEOMETRY COMES FROM SCREEN_SIZE, never a literal. This runs on a 240 round,
// a 412 round and a 480 square, and the confirm target must be a deliberate aim
// on all three -- a hit box sized for one of them is a mis-tap on another.
void AircraftManager::DrawResetMenu(BandCanvas& backbuffer)
{
    constexpr int cx = SCREEN_SIZE_DIV_2;
    const int lh = 14;

    backbuffer.fillScreen(lgfx::color888(0, 0, 0));
    backbuffer.setTextSize(1);
    auto centered = [&](const String& t, int y, uint32_t colour) {
        backbuffer.setTextColor(colour);
        backbuffer.drawString(t, cx - (int)backbuffer.textWidth(t) / 2, y);
    };

    // A boxed, centred row. Returns the vertical bounds so the caller records the
    // hit box from the SAME numbers that drew it.
    auto rowBox = [&](const String& t, int top, int height, uint32_t colour, int halfWidth,
                      int& y0, int& y1) {
        backbuffer.drawRoundRect(cx - halfWidth, top, halfWidth * 2, height, 4, colour);
        centered(t, top + (height - 8) / 2, colour);
        y0 = top;
        y1 = top + height;
    };

    const uint32_t green = lgfx::color888(0, 200, 0);
    const uint32_t amber = lgfx::color888(255, 176, 0);
    const uint32_t red   = lgfx::color888(255, 80, 80);
    const uint32_t grey  = lgfx::color888(150, 150, 150);

    // Cancel is the LARGEST target and sits lowest, nearest the thumb. Sized off
    // the screen so it stays the biggest thing on the panel at every SKU.
    const int cancelH = SCREEN_SIZE / 7;
    const int cancelHalfW = SCREEN_SIZE * 5 / 16;
    const int cancelTop = SCREEN_SIZE - cancelH - SCREEN_SIZE / 12;

    // Option and confirm targets are deliberately SMALLER and higher up. The
    // asymmetry is the safety mechanism: backing out is the easy gesture and
    // destroying data is the one that takes aim.
    const int optH = SCREEN_SIZE / 10;
    const int optHalfW = SCREEN_SIZE * 4 / 16;

    if (resetMenu == ResetMenu::Choosing) {
        centered("RESET", SCREEN_SIZE / 5, grey);

        int top = SCREEN_SIZE / 5 + lh + 6;
        rowBox("Reset Wi-Fi", top, optH, green, optHalfW, resetOptWifiY0, resetOptWifiY1);

        top += optH + 8;
        rowBox("Factory Reset", top, optH, red, optHalfW, resetOptFactoryY0, resetOptFactoryY1);

        resetConfirmY0 = resetConfirmY1 = -1;  // no confirm target on this screen
        rowBox("CANCEL", cancelTop, cancelH, grey, cancelHalfW, resetCancelY0, resetCancelY1);
        return;
    }

    const bool factory = (resetMenu == ResetMenu::ConfirmFactory);

    // WHAT IT CLEARS, ON GLASS, BEFORE THE CONFIRM. A customer cannot consent to
    // a description they were never shown, and "Factory Reset" is a label rather
    // than a description -- the logbook is the thing they would actually miss.
    centered(factory ? "FACTORY RESET" : "RESET WI-FI", SCREEN_SIZE / 5, factory ? red : amber);
    int y = SCREEN_SIZE / 5 + lh + 4;
    if (factory) {
        centered("Erases logbook,", y, grey);            y += lh;
        centered("location, leaderboard", y, grey);      y += lh;
        centered("name and Wi-Fi.", y, grey);            y += lh + 2;
        centered("Cannot be undone.", y, red);
    } else {
        centered("Forgets this network.", y, grey);      y += lh;
        centered("Logbook and settings", y, grey);       y += lh;
        centered("are kept.", y, grey);
    }

    resetOptWifiY0 = resetOptWifiY1 = -1;
    resetOptFactoryY0 = resetOptFactoryY1 = -1;
    rowBox(factory ? "ERASE" : "CONFIRM", cancelTop - optH - 10, optH, factory ? red : amber,
           optHalfW, resetConfirmY0, resetConfirmY1);
    rowBox("CANCEL", cancelTop, cancelH, grey, cancelHalfW, resetCancelY0, resetCancelY1);
}

// Returns true when the tap belonged to the menu, which is every tap while it is
// open -- a tap on nothing is swallowed rather than falling through to whatever
// screen sits underneath, because that screen is not the one being looked at.
bool AircraftManager::HandleResetMenuTap(int tx, int ty)
{
    if (resetMenu == ResetMenu::Closed)
        return false;
    (void)tx;  // every target spans the usable width; the row is what identifies it

    // CANCEL FIRST, so that if two boxes ever overlapped after a layout change the
    // non-destructive one would win the ambiguity.
    if (resetCancelY0 >= 0 && ty >= resetCancelY0 && ty <= resetCancelY1) {
        Serial.println("[reset] cancelled");
        CloseResetMenu();
        return true;
    }

    if (resetMenu == ResetMenu::Choosing) {
        if (resetOptWifiY0 >= 0 && ty >= resetOptWifiY0 && ty <= resetOptWifiY1) {
            Serial.println("[reset] tier=wifi selected -- confirm or cancel");
            resetMenu = ResetMenu::ConfirmWifi;
            return true;
        }
        if (resetOptFactoryY0 >= 0 && ty >= resetOptFactoryY0 && ty <= resetOptFactoryY1) {
            Serial.println("[reset] tier=factory selected -- confirm or cancel");
            resetMenu = ResetMenu::ConfirmFactory;
            return true;
        }
        return true;  // tapped the background; stay put
    }

    if (resetConfirmY0 >= 0 && ty >= resetConfirmY0 && ty <= resetConfirmY1) {
        const factoryreset::Tier tier = (resetMenu == ResetMenu::ConfirmFactory)
                                            ? factoryreset::Tier::Factory
                                            : factoryreset::Tier::Wifi;
        Serial.printf("[reset] confirmed on device tier=%s\n", factoryreset::TierName(tier));
        // REQUEST ONLY. main.cpp performs it on the loop task and reboots -- the
        // same rule the web page follows, so there is exactly one place that
        // touches NVS for a reset.
        resetTierRequested = (uint8_t)factoryreset::Larger((factoryreset::Tier)resetTierRequested, tier);
        CloseResetMenu();
        return true;
    }
    return true;
}

void AircraftManager::ProcessDetailLookups()
{
    if (!inDetail)
        return;

    auto it = trackedAircraft.find(selectedIcao);
    if (it == trackedAircraft.end())
        return;
    TrackedAircraft& tracked = it->second;

    // One enrichment request is outstanding at a time (shared with the radar
    // metadata path); wait for it to land before issuing the next step.
    if (enrichInFlight)
        return;

    // Same for a leaderboard submit that is already due -- but the check MUST be
    // here, at the top, and not left to the Request* calls below. This function
    // commits state BEFORE it enqueues ("photoIcao = selectedIcao" is marked
    // attempted regardless of outcome, with no retry; metadataState goes to
    // Fetching). A deferral swallowed inside RequestPhoto would therefore burn
    // the single attempt and the card would never load its photo -- the exact
    // symptom this whole change is meant to relieve. Bailing early re-runs the
    // step next loop, which is what the enrichInFlight gate above already does.
    if (EnrichDeferredForSubmit())
        return;

    // same TLS-heap guard as the radar path: a handshake with too little contiguous
    // heap only fails and churns, so defer the detail lookups until heap recovers.
    //
    // THROTTLED because this function is reached once per loop iteration. At a
    // 41-43 ms frame that is ~23 trial allocations a second, every one of them
    // failing, for as long as the sky stays busy -- measured on COM119. The
    // throttled form can only ever be MORE restrictive, so deferral behaviour is
    // unchanged; what goes away is 22 of every 23 pointless allocator walks.
    if (!heaphealth::CanHandshakeThrottled())
        return;

    // Same rule as the background sweep: the card stays receiver-only when the
    // chosen detail source is Off, or is Cloud but unusable (no URL/key). Falling
    // through to adsbdb here would silently substitute a third party for the
    // source the user actually picked.
    if (useLocalSource && !UseCloudEnrich()) {
        photoIcao = selectedIcao;   // mark resolved so the card stops saying "Loading"
        photoResolved = true;
        return;
    }

#ifdef FEATURE_CLOUD_FEED
    if (UseCloudEnrich()) {
        // Cloud detail path: ONE pre-joined /api/v1/blipscope/enrich GET covers what used to be
        // two adsbdb lookups (metadata + route), and (when the proxy's stock
        // library has an image for this hex/type) a licensed photo path in `p`.
        // A local-receiver device on details=Cloud lands here too: same single
        // host, same keep-alive client -- it REPLACES the adsbdb pair rather than
        // adding to it, so a local card costs one external host, never two.
        // Step 1 -- metadata/route/photo-path via the single enrich GET.
        if (tracked.metadataState == TrackedAircraft::MetadataState::NotFetched) {
            if (millis() < tracked.metadataRetryAfter)
                return;
            // A non-ICAO address is a TIS-B/ADS-R track ID, not an airframe: no
            // registry holds a record, so this GET is certain to return blank.
            // Settle it offline instead of spending a TLS handshake and a round
            // trip on the tight path to learn nothing. These were ~44% of the
            // fleet's failed type lookups when measured (2026-08-12), so this is
            // real traffic removed, not a micro-optimisation. The card shows
            // exactly what it would have shown anyway.
            if (SpecialAircraft::IsNonIcaoAddress(selectedIcao)) {
                ApplyEnrichment(tracked, CloudFeed::Enrichment{});
                return;
            }
            // The LRU keeps the last few enrichments across aircraft eviction, so
            // re-inspecting a contact that flapped out of range is instant and
            // network-free. On a hit, fall through to the photo step below.
            if (const CloudFeed::Enrichment* cached = enrichCache.Find(selectedIcao)) {
                ApplyEnrichment(tracked, *cached);
            } else {
                String callsign = tracked.state.callsign;
                callsign.trim();
                auto [acLat, acLon] = tracked.GetDisplayPosition();
                tracked.metadataState = TrackedAircraft::MetadataState::Fetching;
                RequestCloudEnrich(selectedIcao, callsign, acLat, acLon);
                return;
            }
        }
        if (tracked.metadataState == TrackedAircraft::MetadataState::Fetching)
            return; // await the enrich result before deciding on a photo

        // Step 2 -- photo, once per aircraft. Mirrors the BYO photo step but
        // authenticated with the cloud key (the /api/v1/blipscope/photo route requires it).
        // photoUrl is the absolute proxy URL ApplyEnrichment built from `p`, or
        // "" when the library has no image (-> silhouette, as before).
        if (photoIcao != selectedIcao) {
            photoIcao = selectedIcao; // mark attempted regardless of outcome (no retry)
            photoReady = false;
            if (!tracked.photoUrl.isEmpty()) {
                photoResolved = false; // a fetch is coming; card shows "Loading..." until it lands
                RequestPhoto(selectedIcao, tracked.photoUrl, cloudKey);
            } else {
                photoResolved = true;  // no licensed image: resolved now, card shows the silhouette
            }
        }
        return;
    }
#endif

    // 1. metadata (type/operator/registration + photo URL). Queued here -- not only
    //    in the throttled radar path -- so the card is complete even when the
    //    enrichment fields are disabled. The photo step below needs the photoUrl it
    //    resolves, so wait for it (Fetching) before moving on.
    if (tracked.metadataState == TrackedAircraft::MetadataState::NotFetched) {
        // Honor the 30 s cooldown a transient failure set (ConsumeEnrichResults):
        // without this, a card open during an adsbdb outage re-queues metadata every
        // frame, hammering the shared TLS path and holding the bus off the feed task.
        if (millis() < tracked.metadataRetryAfter)
            return;
        // ONE request covers metadata AND route: /v1/enrich pre-joins them, which
        // is why the separate route step below is gone.
        //
        // The state is set to Fetching ONLY on the branch that actually issues a
        // request. Marking it before the guard would strand the aircraft in
        // Fetching forever on a device with no proxy configured -- a card that
        // says "Loading..." with nothing loading, which is worse than an empty one.
        // GUARDED, because RequestCloudEnrich is only declared under
        // FEATURE_CLOUD_FEED and not every env defines it -- blipscope-pro-s3-175-amoled
        // does not. Without this the SKU fails to COMPILE, which is the good
        // outcome: with adsbdb gone there is no other detail source, so an
        // unguarded call would otherwise have been a silent behaviour gap on a
        // board nobody was building locally. The background lookup below already
        // had this guard; this call site was added without it.
#ifdef FEATURE_CLOUD_FEED
        if (UseCloudEnrich()) {
            String cs = tracked.state.callsign;
            cs.trim();
            tracked.metadataState = TrackedAircraft::MetadataState::Fetching;
            auto [dLat, dLon] = tracked.GetDisplayPosition();
            RequestCloudEnrich(selectedIcao, cs, dLat, dLon);
        }
#endif
        return;
    }
    if (tracked.metadataState == TrackedAircraft::MetadataState::Fetching)
        return;

    // 2. route -- NO SEPARATE STEP. The proxy's /v1/enrich returns metadata and
    //    route in one response, so the route arrives with step 1. The old second
    //    lookup existed only because adsbdb had two endpoints.

    // 3. photo, once per aircraft
    if (photoIcao != selectedIcao) {
        photoIcao = selectedIcao; // mark attempted regardless of outcome (no retry)
        photoReady = false;
        if (!tracked.photoUrl.isEmpty()) {
            photoResolved = false; // a fetch is coming; the card shows "Loading..." until it lands
            RequestPhoto(selectedIcao, tracked.photoUrl);
        } else {
            photoResolved = true;  // adsbdb has none: resolved now, so the card shows the silhouette
        }
    }
}

void AircraftManager::StartEnrichTask()
{
    if (enrichTaskHandle != nullptr)
        return; // already running; survive Initialise() being re-run on a config reload

    // Depth 1: only ever one enrichment outstanding (gated by enrichInFlight), so a
    // single slot for the request and one for the result is enough.
    if (enrichRequestQueue == nullptr) {
        enrichRequestQueue = xQueueCreate(1, sizeof(EnrichRequest*));
        enrichResultQueue  = xQueueCreate(1, sizeof(EnrichResult*));
    }

    // Same workload class as the fetch task (HTTPS GET + small JSON decode, plus a
    // ~10 KB photo body carried back in the result), so the same 12 KB stack and
    // priority 1. It spends almost all its life blocked on the request queue.
    // Pinned to core 0 (the WiFi core) for the same reason as the fetch task: keep network
    // work off the panel-driving loop on core 1 (S3); no-op on the single-core C3.
    xTaskCreatePinnedToCore(EnrichTaskTrampoline, "enrich", 12288, this, 1, &enrichTaskHandle, 0);
}

void AircraftManager::EnrichTaskTrampoline(void* arg)
{
    static_cast<AircraftManager*>(arg)->RunEnrichTask();
}

void AircraftManager::RunEnrichTask()
{
    for (;;) {
        // block until the loop requests an enrichment
        EnrichRequest* req = nullptr;
        if (xQueueReceive(enrichRequestQueue, &req, portMAX_DELAY) != pdTRUE || req == nullptr)
            continue;

        EnrichResult* res = nullptr;
        const unsigned long enrichStartMs = millis(); // MEASUREMENT: the other consumer
                                                      // of the shared client
        switch (req->kind) {
            case EnrichKind::Photo:    res = fetchPhoto(http, req->url, req->cloudKey);   break;
#ifdef FEATURE_CLOUD_FEED
            case EnrichKind::CloudEnrich: res = fetchCloudEnrich(http, *req);          break;
            case EnrichKind::Leaderboard: res = postLeaderboard(http, *req);           break;
#endif
            case EnrichKind::Ntfy:     res = postNtfy(http, *req);                     break;
            default: res = new EnrichResult(); break; // unknown kind: clear the in-flight gate
        }

        if (res != nullptr) {
            res->kind = req->kind;
            res->icao24 = req->icao24; // who the result applies to (route req carries it too)
            res->busyMs = millis() - enrichStartMs;

            // hand the result back; the loop consumed the previous one before
            // requesting again, so the depth-1 queue always has room
            if (xQueueSend(enrichResultQueue, &res, 0) != pdTRUE)
                delete res;
        }

        delete req;
    }
}

// Yield the next free enrich slot to a leaderboard submit that is already due.
// Checked by every enrichment entry point below: the queue is depth 1 and one
// request is outstanding at a time, so without this a continuous enrichment
// stream simply never leaves a gap for the hourly submit (see the Update() note).
// Nothing is lost -- the deferred lookup is re-issued on the next sweep of the
// tracked set, one cycle later.
bool AircraftManager::EnrichDeferredForSubmit() const
{
#ifdef FEATURE_CLOUD_FEED
    return lbSubmitPending;
#else
    return false;
#endif
}

// RequestMetadata() and RequestRoute() are deleted with the producers they fed.
// Every detail lookup now goes through RequestCloudEnrich().

void AircraftManager::RequestPhoto(const String& icao24, const String& url, const String& authKey)
{
    if (EnrichDeferredForSubmit())
        return;
    EnrichRequest* req = new EnrichRequest{ EnrichKind::Photo, icao24, "", url };
    req->cloudKey = authKey; // "" for the BYO adsbdb thumbnail; the proxy key in cloud mode
    if (enqueueEnrich(enrichRequestQueue, req))
        enrichInFlight = true;
}

#ifdef FEATURE_CLOUD_FEED
void AircraftManager::RequestCloudEnrich(const String& icao24, const String& callsign,
                                         float acLat, float acLon)
{
    if (EnrichDeferredForSubmit())
        return;
    EnrichRequest* req = new EnrichRequest{};
    req->kind = EnrichKind::CloudEnrich;
    req->icao24 = icao24;
    req->callsign = callsign;
    req->cloudBase = cloudUrl;
    req->cloudKey = cloudKey;
    req->acLat = acLat;
    req->acLon = acLon;
    req->hasPos = true;
    if (enqueueEnrich(enrichRequestQueue, req))
        enrichInFlight = true;
}

void AircraftManager::ApplyEnrichment(TrackedAircraft& tracked, const CloudFeed::Enrichment& e)
{
    tracked.metadataState = TrackedAircraft::MetadataState::Fetched;
    tracked.typeCode = e.typeCode;
    tracked.typeName = e.typeName;
    tracked.operatorName = e.operatorName;
    tracked.registration = e.registration;
    // Stock-photo join: when the proxy has a licensed image for this hex/type it
    // sends a relative path (`p`); make it absolute against the same cloud host
    // so the photo fetch rides the existing keep-alive connection (no new TLS
    // host). Empty `p` -> photo-less, as before (the card shows the silhouette).
    if (!e.photoPath.isEmpty()) {
        tracked.photoUrl = cloudUrl + e.photoPath;
        tracked.photoRepresentative = e.photoRepresentative;
    } else {
        tracked.photoUrl = "";
        tracked.photoRepresentative = false;
    }
    tracked.routeOrigin = e.routeOrigin;
    tracked.routeDest = e.routeDest;
    // Mark the route resolved for the CURRENT callsign (even when empty), so the
    // detail path doesn't keep asking; a callsign change re-queries as usual.
    String callsign = tracked.state.callsign;
    callsign.trim();
    tracked.routeCallsign = callsign;

    if (logbookEnabled) {
        const bool newType = logbook.NoteType(tracked.typeCode);
        const bool newOperator = logbook.NoteOperator(tracked.operatorName);
        // SEEN is recorded unconditionally; CLAIMABLE is a live question about the
        // type, asked again every time enrichment lands rather than latched at
        // first sighting. That is what lets a type seen months ago still be worth
        // tapping today, and what makes the badge disappear the instant it is
        // claimed on any aircraft of that type.
        tracked.claimable = !tracked.typeCode.isEmpty() && logbook.IsTypeClaimable(tracked.typeCode);
        // route endpoints feed the airports-seen lifelist; they are claimed as
        // riders when the card is opened, never on their own
        logbook.NoteAirport(tracked.routeOrigin);
        logbook.NoteAirport(tracked.routeDest);
        ConsiderAircraftOfDay(tracked, newType, newOperator);
        // Enrichment can land while the card is already open -- that is the
        // reveal-then-claim path, and it is the only one that works for a contact
        // tapped before its type was known.
        if (inDetail && selectedIcao == tracked.state.icao24)
            ClaimTappedAircraft(tracked);
    }
}

void AircraftManager::ConsiderAircraftOfDay(const TrackedAircraft& tracked, bool newType, bool newOperator)
{
    // Priority bands, widely separated so class always outranks the altitude
    // tiebreak within a band. Emergency > new type > military > new airline >
    // just-the-highest-flying, so on a quiet day something still shows.
    int score;
    String reason;
    if (isEmergencySquawk(tracked.state.squawk)) { score = 5000; reason = "EMERGENCY"; }
    else if (newType)                            { score = 4000; reason = "New type"; }
    else if (alertMilitary && SpecialAircraft::IsMilitary(tracked.state.icao24))
                                                 { score = 3000; reason = "Military"; }
    else if (newOperator)                        { score = 2000; reason = "New airline"; }
    else                                         { score = 1000; reason = "Highest today"; }
    // Altitude tiebreak (0..~600 for 0..60 kft): decides within a band, and lets
    // the "Highest today" fallback actually track the highest contact.
    const int altFt = (int)lroundf(tracked.state.baroAltitude * METRES_TO_FEET);
    score += altFt / 100;

    if (score <= aotdScore) return;
    aotdScore = score;
    String cs = tracked.state.callsign; cs.trim();
    if (cs.isEmpty()) { cs = tracked.state.icao24; cs.toUpperCase(); }
    aotdCallsign = cs;
    aotdLabel = !tracked.typeName.isEmpty() ? tracked.typeName
              : !tracked.typeCode.isEmpty() ? tracked.typeCode
              : tracked.operatorName;
    aotdReason = reason;
}

bool AircraftManager::CloudShouldBackgroundEnrich(const TrackedAircraft& tracked) const
{
    switch (cloudCfg.enrich) {
        case CloudFeed::Config::Enrich::Off:
            return false;
        case CloudFeed::Config::Enrich::Watchlist: {
            // Only the fields available WITHOUT enrichment can gate here (hex +
            // callsign); registration/type watchlist entries still match once an
            // aircraft is enriched some other way (a tap, the LRU). Documented
            // limitation of the C3-default level -- the point is not to enrich
            // the whole sky on the tight board.
            if (watchlist.empty())
                return false;
            String callsign = tracked.state.callsign; callsign.trim(); callsign.toLowerCase();
            String icao = tracked.state.icao24; icao.toLowerCase();
            for (const String& w : watchlist) {
                if (!icao.isEmpty() && icao.startsWith(w))         return true;
                if (!callsign.isEmpty() && callsign.startsWith(w)) return true;
            }
            return false;
        }
        case CloudFeed::Config::Enrich::Full:
        default:
            // Full still respects metadataNeeded: no enrichment-consuming feature
            // on (no lookup fields, no watchlist, no logbook) means no traffic.
            return metadataNeeded;
    }
}
#endif // FEATURE_CLOUD_FEED

void AircraftManager::ConsumeEnrichResults()
{
    if (enrichResultQueue == nullptr)
        return;

    EnrichResult* res = nullptr;
    if (xQueueReceive(enrichResultQueue, &res, 0) != pdTRUE)
        return; // nothing ready

    enrichInFlight = false;
    perf.enrichReqs++;              // MEASUREMENT: the enrich half of `busy`
    perf.enrichBusyMs += res->busyMs;

    // The aircraft may have left range while the lookup was outstanding. Metadata
    // and route results target a map entry (gone -> nothing to apply); the photo is
    // keyed to photoIcao instead, since the sprite is shared, not per-entry.
    auto it = trackedAircraft.find(res->icao24);

    switch (res->kind) {
        // EnrichKind::Metadata handled the adsbdb metadata reply. Deleted with it --
        // CloudEnrich below carries the same fields from our own proxy.

#ifdef FEATURE_CLOUD_FEED
        case EnrichKind::CloudEnrich:
            if (it != trackedAircraft.end()) {
                TrackedAircraft& t = it->second;
                if (res->definitive) {
                    CloudFeed::Enrichment e;
                    e.registration = res->registration;
                    e.typeCode     = res->typeCode;
                    e.typeName     = res->typeName;
                    e.operatorName = res->operatorName;
                    e.routeOrigin  = res->routeOrigin;
                    e.routeDest    = res->routeDest;
                    e.photoPath          = res->photoPath;
                    e.photoRepresentative = res->photoRepresentative;
                    ApplyEnrichment(t, e);
                    enrichCache.Insert(res->icao24, e); // re-taps after eviction stay instant
                } else if (t.enrichAttempts + 1 >= 3) {
                    // Three all-empty/failed answers: accept "unknown aircraft"
                    // rather than polling the proxy forever. (Empty responses are
                    // ambiguous between a warming cache and a truly unknown hex;
                    // by the third answer the proxy has long finished warming.)
                    ApplyEnrichment(t, CloudFeed::Enrichment{});
                } else {
                    t.enrichAttempts++;
                    t.metadataState = TrackedAircraft::MetadataState::NotFetched;
                    t.metadataRetryAfter = millis() + res->retryCooldownMs;
                }
            }
            break;
#endif

        case EnrichKind::Ntfy:
            // Nothing to apply: the POST already happened (and logged) on the
            // enrichment task; this result only clears the in-flight gate.
            break;

#ifdef FEATURE_CLOUD_FEED
        case EnrichKind::Leaderboard:
            // Adopt this device's standing for the Stats rank block. A failed
            // submit (lbOk false) keeps the previous standing until next hour.
            if (res->lbOk) {
                const int prevRank = lbRank;
                const bool hadStanding = lbHaveStanding;
                lbRank = res->lbRank;
                lbPoints = res->lbPoints;
                lbSeasonRank = res->lbSeasonRank;
                lbSeasonPoints = res->lbSeasonPoints;
                lbTotal = res->lbTotal;
                lbRarestType = res->lbRarestType;
                lbRarestPct = res->lbRarestPct;
                lbHaveStanding = true;
                // Rank-up toast: only after a standing already exists (so the first
                // rank never celebrates) and only on a genuine climb (rank is a
                // position, so lower is better).
                if (hadStanding && res->lbRank > 0 && prevRank > 0 && res->lbRank < prevRank) {
                    rankToastRank = res->lbRank;
                    rankToastDelta = prevRank - res->lbRank;
                    rankToastUntilMs = millis() + RANK_TOAST_MS;
                    Serial.printf("[leaderboard] rank up #%d -> #%d (+%d)\n",
                                  prevRank, res->lbRank, rankToastDelta);
                }
                PersistLeaderboardStanding();
                lbConsecutiveFails = 0;
                lbRetryBackoffMs = 0;
            } else {
                // ESCALATING BACKOFF. A submit that fails the same way every hour
                // is not self-correcting: the payload that overran the server is
                // the payload the next attempt sends, so a slow server wedged
                // submits permanently and the only symptom was a read timeout.
                // (Seen for real: a 40-type catch-up blew the 5 s read timeout
                // against a server doing two serial KV round trips per type, and
                // the backlog it needed to clear was what kept it failing.)
                //
                // Retrying HARDER cannot help, so retry LATER: 2 min doubling to
                // 60 min, with +-12.5 percent jitter so a fleet that hits a
                // server-side fault together does not resynchronise into a
                // thundering herd on every retry. The hourly schedule is
                // unchanged on success -- this only ever lengthens the gap after
                // a failure.
                if (lbConsecutiveFails < 255)
                    lbConsecutiveFails++;
                constexpr unsigned long LB_BACKOFF_BASE_MS = 2UL * 60UL * 1000UL;
                constexpr unsigned long LB_BACKOFF_MAX_MS  = 60UL * 60UL * 1000UL;
                unsigned long back = LB_BACKOFF_BASE_MS;
                for (uint8_t i = 1; i < lbConsecutiveFails && back < LB_BACKOFF_MAX_MS; ++i)
                    back <<= 1;
                if (back > LB_BACKOFF_MAX_MS) back = LB_BACKOFF_MAX_MS;
                const long jitter = (long)(back / 8) - (long)random(back / 4 + 1);
                lbRetryBackoffMs = (unsigned long)max(1000L, (long)back - jitter);
                // Distinct line: "failed" is a single event, this is a STATE, and
                // the two need to be separable when reading a long log.
                Serial.printf("[leaderboard] BACKOFF: %u consecutive failures, next attempt in %lu s\n",
                              (unsigned)lbConsecutiveFails, lbRetryBackoffMs / 1000UL);
            }
            break;
#endif

        // EnrichKind::Route handled the adsbdb route reply. Deleted with it -- the
        // proxy returns the route in the same response as the metadata.

        case EnrichKind::Photo:
            // decode here, on the loop, so the photo sprite is only ever touched by
            // one task. Only apply it if this photo is still wanted: ExitDetail()
            // clears photoIcao when the card closes, so a late-arriving result must
            // not paint the closed card's aeroplane into the sprite -- where the
            // next card would inherit it, since the sprite is now retained rather
            // than freed on PSRAM boards (see ExitDetail).
            if (photoIcao == res->icao24) {
                photoResolved = true; // this aircraft's photo attempt is done (decoded below, or not)
                if (res->photoFetched && res->photoBytes.length() > 0) {
                    if (photoSprite.getBuffer() == nullptr) {
                        // PSRAM boards keep the photo off the scarce internal heap (where WiFi/TLS
                        // and the JPEG decoder also live), so decoding a card doesn't fragment it.
                        // Depth must MATCH the backbuffer's (see main.cpp): a
                        // 16bpp photo pushed into an 8bpp backbuffer is quantized
                        // straight back to RGB332, so moving one without the other
                        // buys nothing and looks identical.
                        if constexpr (!variant::BANDED_RENDER) {
                            photoSprite.setPsram(true);
                            photoSprite.setColorDepth(16);
                        } else {
                            photoSprite.setColorDepth(8);
                        }
                        photoSprite.createSprite(PHOTO_W, PHOTO_H);
                    }
                    photoSprite.fillScreen(lgfx::color888(0, 0, 0));
                    // Plain top-left draw. A middle_center datum was tried here as
                    // "cheap insurance" for an unexpectedly small image and BROKE
                    // THE CORRECT CASE on hardware: with (x,y) at the sprite centre
                    // the image landed at (120,120) and only its top-left corner
                    // fell inside the sprite -- a mostly-blank card with a fragment
                    // in the bottom-right, while drawJpg still returned true and
                    // hasPhoto still read 1, so nothing logged a fault.
                    //
                    // The artifact is emitted at exactly this size, so there is
                    // nothing to centre; a mismatched one leaves black margins,
                    // which is obvious rather than silent. Insurance that can
                    // break the case it is insuring is not insurance.
                    photoReady = photoSprite.drawJpg((const uint8_t*)res->photoBytes.c_str(),
                                                     res->photoBytes.length(), 0, 0, PHOTO_W, PHOTO_H);
                    // A failed decode after a successful fetch must be LOUD: a progressive
                    // JPEG (undecodable by TJpgDec), a truncated body, or an HTML error page
                    // all land here, and silently showing the silhouette hides the bug
                    // (cost a bench hour on the cloud stock-photo bring-up).
                    if (!photoReady)
                        Serial.printf("[photo] %s: decode FAILED (bytes=%u head=%02x%02x)\n",
                                      res->icao24.c_str(), (unsigned)res->photoBytes.length(),
                                      res->photoBytes.length() > 0 ? (uint8_t)res->photoBytes[0] : 0,
                                      res->photoBytes.length() > 1 ? (uint8_t)res->photoBytes[1] : 0);
                }
            }
            break;
    }

    delete res;
}

AircraftManager::WatchClass AircraftManager::ClassifyWatchlist(const TrackedAircraft& tracked) const
{
    if (watchlist.empty())
        return WatchClass::None;

    String callsign = tracked.state.callsign; callsign.trim(); callsign.toLowerCase();
    String icao = tracked.state.icao24; icao.toLowerCase();
    String reg = tracked.registration; reg.toLowerCase();
    String type = tracked.typeCode; type.toLowerCase();

    // A specific-identity match (this exact aircraft) outranks a category (type)
    // match, so scan for identity across all entries first, then types.
    bool category = false;
    for (const String& w : watchlist) {
        if (callsign.startsWith(w) && !callsign.isEmpty()) return WatchClass::Specific;
        if (icao.startsWith(w) && !icao.isEmpty())         return WatchClass::Specific;
        if (reg.startsWith(w) && !reg.isEmpty())           return WatchClass::Specific;
        if (type.startsWith(w) && !type.isEmpty())         category = true;
    }
    return category ? WatchClass::Category : WatchClass::None;
}

bool AircraftManager::IsOverhead(const TrackedAircraft& tracked) const
{
    if (tracked.state.onGround)
        return false;
    auto [aLat, aLon] = tracked.GetDisplayPosition();
    const float dLatKm = (aLat - (float)lat) * 111.0f;
    const float dLonKm = (aLon - (float)lon) * 111.0f * cosf(radians((float)lat));
    return sqrtf(dLatKm * dLatKm + dLonKm * dLonKm) <= (float)overheadKm;
}

void AircraftManager::ProcessAlerts()
{
    if (ntfyTopic.isEmpty())
        return;
    const bool flyoverEnabled = !watchlist.empty() || alertMilitary;
    if (!flyoverEnabled && !alertOverhead && !alertEmergency)
        return;

    const unsigned long now = millis();
    if (now - lastNotifyCheck < 2000) // one blocking POST at a time, spaced out
        return;
    lastNotifyCheck = now;

    for (auto& [icao, tracked] : trackedAircraft) {
        if (tracked.state.onGround) continue;

        // emergency squawk -- highest priority, one-shot per tracking session.
        // Fires even for a contact already squawking when it entered range: an
        // active emergency is worth knowing about, unlike a stale backlog.
        if (alertEmergency && !tracked.emgNotified && isEmergencySquawk(tracked.state.squawk)) {
            if (SendEmergencyNotification(tracked))
                tracked.emgNotified = true;
            return; // at most one notification per tick
        }

        // overhead "look up" alert -- one-shot per tracking session. The notified
        // flag is only set when the POST actually queued (the enrichment task's
        // depth-1 queue may be busy), so a deferred alert retries next tick
        // instead of being lost.
        if (alertOverhead && !tracked.overheadNotified && IsOverhead(tracked)) {
            if (SendOverheadNotification(tracked))
                tracked.overheadNotified = true;
            return; // at most one notification per tick
        }

        // watchlist / military flyover alert
        if (flyoverEnabled && !tracked.watchNotified) {
            const bool military = alertMilitary && SpecialAircraft::IsMilitary(tracked.state.icao24);
            if (military || MatchesWatchlist(tracked)) {
                if (SendFlyoverNotification(tracked, military))
                    tracked.watchNotified = true;
                return;
            }
        }
    }
}

// Queue an ntfy POST onto the enrichment task. The POST used to run right here on
// the loop -- a slow ntfy.sh or a cold TLS handshake stalled rendering for seconds
// and ate taps (the loop polls touch once per pass). False = queue busy, try later.
bool AircraftManager::QueueNtfyPost(const String& title, const String& tags, const String& body)
{
    if (enrichInFlight)
        return false; // one outstanding request at a time, shared with enrichment
    EnrichRequest* req = new EnrichRequest{};
    req->kind = EnrichKind::Ntfy;
    req->url = "https://ntfy.sh/" + ntfyTopic;
    req->ntfyTitle = title;
    req->ntfyTags = tags;
    req->ntfyBody = body;
    if (!enqueueEnrich(enrichRequestQueue, req))
        return false;
    enrichInFlight = true;
    return true;
}

#ifdef FEATURE_CLOUD_FEED
// Queue the hourly leaderboard submission onto the enrichment task (off-loop,
// like ntfy). Builds the JSON body from the logbook tallies + the salted device
// id + the opted-in spotter name.
//
// WHAT CHANGED IN v4 AND WHY IT IS AN ADDITION, NOT AN EDIT. `counts` still means
// SEEN and keeps its old meaning exactly. The scoring list is the NEW field
// `claimedTypes`. Silently redefining the old `typeCodes` to mean "claimed"
// instead would have been far worse than a schema break: a device still running
// v3 firmware would keep submitting its SEEN list into a field the server now
// scores as claimed, handing every un-updated unit the exact antenna advantage
// this rework exists to remove -- invisibly, and in their favour. The server
// instead treats a submission with no `claimedTypes` as legacy and does not rank
// it, which fails in the direction that cannot be gamed.
//
// `typeCodes` (the seen list) is no longer sent at all. Rarity is computed over
// claims now, so it had no remaining purpose -- and it was the largest thing the
// device disclosed. Airlines/countries/airports stay counts-only, as before,
// because those lists would fingerprint the user's location.
// Mirror the current standing into NVS so the config page's Collection tab can
// show it. The page is served by the ASYNC WEB TASK, which cannot read these
// members -- they live on the loop task and are mutated there. NVS is the
// existing, already-serialized channel between the two, and it is the same one
// every other config value crosses.
//
// One compact record instead of five keys: it is written together, read
// together, and is meaningless in pieces. Hourly at most, so flash wear is not a
// consideration (the logbook rewrites several KB every ten minutes by design).
void AircraftManager::PersistLeaderboardStanding()
{
    Preferences p;
    if (!p.begin("config", false))
        return; // not fatal: the tab falls back to "no standing yet"
    p.putString("lb-standing", String(lbRank) + "/" + String(lbTotal) + "/" + String(lbPoints) +
                               "/" + String(lbSeasonRank) + "/" + String(lbSeasonPoints));
    p.end();
}

bool AircraftManager::QueueLeaderboardSubmit()
{
    if (enrichInFlight || cloudUrl.isEmpty())
        return false;

    JsonDocument doc;
    doc["id"] = DeviceIdentity::LeaderboardId();
    doc["name"] = lbName;
    doc["radiusKm"] = (int)lround(rangeKmCfg);
    JsonObject counts = doc["counts"].to<JsonObject>();
    counts["types"]     = (uint32_t)logbook.TypeCount();
    counts["airlines"]  = (uint32_t)logbook.OperatorCount();
    counts["countries"] = (uint32_t)logbook.CountryCount();
    counts["airports"]  = (uint32_t)logbook.AirportCount();
    JsonObject claimed = doc["claimed"].to<JsonObject>();
    claimed["types"]     = (uint32_t)logbook.ClaimedTypeCount();
    claimed["airlines"]  = (uint32_t)logbook.ClaimedOperatorCount();
    claimed["countries"] = (uint32_t)logbook.ClaimedCountryCount();
    claimed["airports"]  = (uint32_t)logbook.ClaimedAirportCount();
    JsonArray types = doc["claimedTypes"].to<JsonArray>();
    for (const auto& kv : logbook.Types())
        if (kv.second.claimDay != 0)
            types.add(kv.first);

    EnrichRequest* req = new EnrichRequest{};
    req->kind = EnrichKind::Leaderboard;
    req->url = CloudFeed::LeaderboardUrl(cloudUrl);
    req->cloudKey = cloudKey;
    serializeJson(doc, req->lbBody);

    if (!enqueueEnrich(enrichRequestQueue, req))
        return false;
    enrichInFlight = true;
    return true;
}
#endif

bool AircraftManager::SendFlyoverNotification(const TrackedAircraft& tracked, bool military)
{
    String callsign = tracked.state.callsign;
    callsign.trim();
    if (callsign.isEmpty()) { callsign = tracked.state.icao24; callsign.toUpperCase(); }

    String body = military ? "MILITARY " + callsign : callsign;
    if (!tracked.typeCode.isEmpty())     body += " (" + tracked.typeCode + ")";
    if (!tracked.operatorName.isEmpty()) body += " " + tracked.operatorName;
    body += " at " + String(lroundf(tracked.state.baroAltitude * METRES_TO_FEET)) + " ft";

    return QueueNtfyPost(military ? "Blipscope military flyover" : "Blipscope flyover",
                         military ? "rotating_light" : "airplane", body);
}

bool AircraftManager::SendOverheadNotification(const TrackedAircraft& tracked)
{
    String callsign = tracked.state.callsign;
    callsign.trim();
    if (callsign.isEmpty()) { callsign = tracked.state.icao24; callsign.toUpperCase(); }

    String body = callsign + " passing overhead";
    if (!tracked.typeCode.isEmpty()) body += " (" + tracked.typeCode + ")";
    body += " at " + String(lroundf(tracked.state.baroAltitude * METRES_TO_FEET)) + " ft";

    return QueueNtfyPost("Blipscope overhead - look up!", "eyes", body);
}

bool AircraftManager::SendEmergencyNotification(const TrackedAircraft& tracked)
{
    String callsign = tracked.state.callsign;
    callsign.trim();
    if (callsign.isEmpty()) { callsign = tracked.state.icao24; callsign.toUpperCase(); }

    const char* descriptor = "general emergency";
    if (tracked.state.squawk == "7500")      descriptor = "unlawful interference (hijack)";
    else if (tracked.state.squawk == "7600") descriptor = "radio failure (NORDO)";

    String body = callsign + " squawking " + tracked.state.squawk + " - " + descriptor;
    if (!tracked.typeCode.isEmpty())     body += " (" + tracked.typeCode + ")";
    if (!tracked.operatorName.isEmpty()) body += " " + tracked.operatorName;
    body += " at " + String(lroundf(tracked.state.baroAltitude * METRES_TO_FEET)) + " ft";

    return QueueNtfyPost("Blipscope EMERGENCY squawk", "sos", body);
}

// Follow Mode alerts (§14 toggles, §6 copy).
//
// =============================================================================
// THIS IS THE ONE PLACE THE FOLLOW TARGET IS ALLOWED TO LEAVE THE DEVICE
//
// §17: the follow target must never leave the device *except in the ntfy
// notification body*, which is the one channel the owner explicitly opted into
// by naming an aircraft (C3). A tail number tied to a named person is a
// different class of data from a type code -- it must not appear in the
// leaderboard submission, the enrollment payload, any feed or enrich request,
// the OTA telemetry headers, or serial output.
//
// So this function exists on purpose and is the exception, not an oversight.
// Everything else that builds an outbound body must be free of it, and the §17
// test is what will assert that rather than a comment like this one.
//
// The words come from FollowState.h, not from here, because §6 is the product:
// "A hobbyist radar with a coverage gap is a shrug. A device someone's spouse is
// watching that goes dark mid-Atlantic is frightening."
bool AircraftManager::SendFollowAlert(follow::State was, follow::State now)
{
    (void)was;
    if (followTarget.isEmpty())
        return false;

    // §15's asymmetry: a missed lost-alert costs mild worry and an unwanted one
    // costs panic, so SIGNAL LOST is screen-only unless it was asked for.
    switch (now) {
        case follow::State::Airborne:     if (!followAlertUp)   return false; break;
        case follow::State::Landed:       if (!followAlertDown) return false; break;
        case follow::State::SignalLost:   if (!followAlertLost) return false; break;
        // APPROACH_LOST rides the LANDED toggle rather than the LOST one. It is
        // the probably-landed case (§6) -- somebody who asked to hear about
        // landings wants this, and somebody who deliberately declined the
        // alarming alert must not receive it under a different name.
        case follow::State::ApproachLost: if (!followAlertDown) return false; break;
        default: return false;
    }

    // Uppercase, so the phone shows what the customer typed rather than the
    // lowercased form the matcher uses.
    String who = followTarget; who.toUpperCase();
    char title[96];
    if (!follow::AlertTitle(now, who.c_str(), title, sizeof(title))) {
        // SIGNAL LOST has no title in FollowState.h because it is screen-only by
        // default. Reaching here means the owner opted in, so it gets one --
        // built here rather than there, so the pure module keeps stating the
        // DEFAULT policy and this states the opt-in.
        if (now != follow::State::SignalLost)
            return false;
        snprintf(title, sizeof(title), "%s - signal lost", who.c_str());
    }

    // The body names the mechanism (§6 principle 1). "No receivers here" is
    // calming because it explains; "signal lost" alone is not.
    String body;
    const follow::Fix& f = followMachine.LastFix();
    char clock[8] = "";
    {
        const time_t utc = time(nullptr);
        if (utc > 1600000000) {
            const time_t local = utc + utcOffsetSec;
            struct tm t; gmtime_r(&local, &t);
            snprintf(clock, sizeof(clock), "%02d:%02d", t.tm_hour, t.tm_min);
        }
    }
    switch (now) {
        case follow::State::Airborne:
            body = clock[0] ? ("Off at " + String(clock) + ".") : String("Airborne.");
            break;
        case follow::State::Landed:
            body = clock[0] ? ("Landed " + String(clock) + ".") : String("Down.");
            if (followStats.DurationSec() >= 60) {
                const uint32_t m = followStats.DurationSec() / 60;
                body += " " + (m >= 60 ? String(m / 60) + " h " + String(m % 60) + " m"
                                       : String(m) + " m") + " in the air.";
            }
            break;
        case follow::State::ApproachLost:
            body = "Signal lost";
            if (followHome.elevationKnown)
                body += " at " + String((int)lroundf(follow::AglFt(f, followHome))) + " ft";
            body += " over the field";
            if (clock[0]) body += ", " + String(clock);
            body += ". Coverage near the ground is patchy, so this usually means landed.";
            break;
        case follow::State::SignalLost:
            body = "Last seen";
            if (clock[0]) body += " " + String(clock);
            body += " at " + String((int)lroundf(follow::AltitudeMslFt(f))) + " ft MSL. "
                    // The same sentence as follow::Explanation(SignalLost),
                    // written out a second time. The pronoun fix had to be made
                    // in both places and the ELF grep is what found this one --
                    // the source edit looked unique because it WAS unique in
                    // that file. Worth folding into one string when the ntfy
                    // bodies are next touched.
                    "Out of receiver range, not off the radar.";
            break;
        default:
            return false;
    }

    const char* tag = (now == follow::State::Airborne) ? "airplane"
                    : (now == follow::State::Landed)   ? "checkered_flag"
                                                       : "grey_question";
    return QueueNtfyPost(String(title), tag, body);
}

void AircraftManager::PublishMqttState()
{
    if (!mqtt.Connected())
        return;

    int count = 0;
    String highIcao, fastIcao, nearIcao, milIcao, ovhIcao;
    float maxAlt = -1e30f, maxVel = -1e30f, minD2 = 1e30f;
    bool anyMil = false, anyOvh = false;
    const bool overheadActive = showOverhead || alertOverhead;
    const float clat = cosf(radians((float)lat)); // scale longitude delta (see IsOverhead)

    for (auto& [icao, t] : trackedAircraft) {
        if (t.state.onGround) continue;
        ++count;
        if (t.state.baroAltitude > maxAlt) { maxAlt = t.state.baroAltitude; highIcao = icao; }
        if (t.state.velocity > maxVel)     { maxVel = t.state.velocity; fastIcao = icao; }
        auto [la, lo] = t.GetDisplayPosition();
        const float dLa = la - (float)lat, dLo = (lo - (float)lon) * clat;
        const float d2 = dLa * dLa + dLo * dLo;
        if (d2 < minD2) { minD2 = d2; nearIcao = icao; }
        if (SpecialAircraft::IsMilitary(t.state.icao24)) { anyMil = true; if (milIcao.isEmpty()) milIcao = icao; }
        if (overheadActive && IsOverhead(t))             { anyOvh = true; if (ovhIcao.isEmpty()) ovhIcao = icao; }
    }

    auto callsignOf = [&](const String& icao) -> String {
        auto it = trackedAircraft.find(icao);
        if (it == trackedAircraft.end()) return "";
        String cs = it->second.state.callsign; cs.trim();
        if (cs.isEmpty()) { cs = icao; cs.toUpperCase(); }
        return cs;
    };

    JsonDocument doc;
    doc["count"] = count;
    doc["military"] = anyMil;
    doc["overhead"] = anyOvh;

    if (count > 0) {
        auto it = trackedAircraft.find(nearIcao);
        if (it != trackedAircraft.end()) {
            const TrackedAircraft& n = it->second;
            auto [aLat, aLon] = n.GetDisplayPosition();
            const float dLatKm = (aLat - (float)lat) * 111.0f;
            const float dLonKm = (aLon - (float)lon) * 111.0f * cosf(radians((float)lat));
            float bearing = degrees(atan2f(dLonKm, dLatKm));
            if (bearing < 0.0f) bearing += 360.0f;

            JsonObject nearest = doc["nearest"].to<JsonObject>();
            nearest["callsign"] = callsignOf(nearIcao);
            nearest["type"] = n.typeCode;
            nearest["registration"] = n.registration;
            nearest["operator"] = n.operatorName;
            nearest["dist_km"] = roundf(sqrtf(minD2) * 111.0f * 10.0f) / 10.0f;
            nearest["alt_m"] = (int)lroundf(n.state.baroAltitude);
            nearest["speed_ms"] = (int)lroundf(n.state.velocity);
            nearest["bearing"] = (int)lroundf(bearing);
        }
        doc["highest_callsign"] = callsignOf(highIcao);
        doc["highest_alt_m"] = (int)lroundf(maxAlt);
        doc["fastest_callsign"] = callsignOf(fastIcao);
        doc["fastest_speed_ms"] = (int)lroundf(maxVel);
    }
    if (anyMil) doc["military_callsign"] = callsignOf(milIcao);
    if (anyOvh) doc["overhead_callsign"] = callsignOf(ovhIcao);

    String payload;
    serializeJson(doc, payload);
    mqtt.Publish(mqttBase + "/summary", payload, true);
}

void AircraftManager::PublishMqttDiscovery()
{
    const String id = DeviceIdentity::Name(); // e.g. "Blipscope-A1B2C3"
    String uid = id;
    uid.toLowerCase();
    const String summaryTopic = mqttBase + "/summary";
    const String statusTopic = mqttBase + "/status";

    // common fields shared by every entity: state/availability topics, unique id,
    // and the parent device so HA groups them under one "Blipscope" device.
    auto base = [&](JsonDocument& doc, const char* name, const String& key) {
        doc["name"] = name;
        doc["state_topic"] = summaryTopic;
        doc["availability_topic"] = statusTopic;
        doc["unique_id"] = uid + "_" + key;
        JsonObject dev = doc["device"].to<JsonObject>();
        dev["identifiers"].to<JsonArray>().add(uid);
        dev["name"] = id;
        dev["manufacturer"] = "Valar Systems";
        dev["model"] = "Blipscope";
    };
    auto send = [&](const char* component, const String& key, JsonDocument& doc) {
        String payload;
        serializeJson(doc, payload);
        mqtt.Publish(String("homeassistant/") + component + "/" + uid + "_" + key + "/config", payload, true);
    };

    {
        JsonDocument d; base(d, "Aircraft in range", "count");
        d["value_template"] = "{{ value_json.count }}";
        d["unit_of_measurement"] = "aircraft";
        d["icon"] = "mdi:airplane";
        send("sensor", "count", d);
    }
    {
        JsonDocument d; base(d, "Nearest aircraft", "nearest");
        d["value_template"] = "{{ value_json.nearest.callsign if value_json.nearest is defined else 'none' }}";
        d["json_attributes_topic"] = summaryTopic;
        d["json_attributes_template"] = "{{ value_json.nearest | tojson }}";
        d["icon"] = "mdi:airplane-search";
        send("sensor", "nearest", d);
    }
    {
        JsonDocument d; base(d, "Aircraft overhead", "overhead");
        d["value_template"] = "{{ 'ON' if value_json.overhead else 'OFF' }}";
        d["payload_on"] = "ON";
        d["payload_off"] = "OFF";
        d["device_class"] = "occupancy";
        send("binary_sensor", "overhead", d);
    }
    {
        JsonDocument d; base(d, "Military aircraft in range", "military");
        d["value_template"] = "{{ 'ON' if value_json.military else 'OFF' }}";
        d["payload_on"] = "ON";
        d["payload_off"] = "OFF";
        d["icon"] = "mdi:shield-airplane";
        send("binary_sensor", "military", d);
    }

    // Device triggers: HA-native automation triggers that fire on the moment a
    // contact of each class first appears (see PublishMqttEvents). Unlike the
    // binary sensors above, an automation on these fires per-appearance, with
    // the aircraft's details attached -- the "events, not state" ask. All match
    // one shared event topic, discriminated by value_json.event.
    const String eventTopic = mqttBase + "/event";
    auto trigger = [&](const char* subtype, const char* eventName) {
        JsonDocument d;
        d["automation_type"] = "trigger";
        d["type"] = "aircraft_alert";
        d["subtype"] = subtype;
        d["topic"] = eventTopic;
        d["value_template"] = "{{ value_json.event }}";
        d["payload"] = eventName;
        JsonObject dev = d["device"].to<JsonObject>();
        dev["identifiers"].to<JsonArray>().add(uid);
        dev["name"] = id;
        dev["manufacturer"] = "Valar Systems";
        dev["model"] = "Blipscope";
        String payload;
        serializeJson(d, payload);
        mqtt.Publish(String("homeassistant/device_automation/") + uid + "_ev_" + eventName + "/config", payload, true);
    };
    trigger("Watchlist aircraft appeared", "watchlist");
    trigger("Emergency squawk appeared", "emergency");
    trigger("Military aircraft appeared", "military");
    trigger("Aircraft overhead", "overhead");

    Serial.printf("[mqtt] published HA discovery for %s\n", uid.c_str());
}

void AircraftManager::PublishMqttEvents()
{
    // One-shot per contact per class: HA automations fire on appearance, not on
    // every poll. Independent of the ntfy alert toggles -- an MQTT-only user
    // still gets events. Non-retained (an event is a moment, not a state).
    const String eventTopic = mqttBase + "/event";
    auto fire = [&](TrackedAircraft& t, uint8_t bit, const char* eventName) {
        if (t.mqttEventFlags & (1 << bit)) return;
        t.mqttEventFlags |= (1 << bit);
        JsonDocument d;
        d["event"] = eventName;
        String cs = t.state.callsign; cs.trim();
        if (cs.isEmpty()) { cs = t.state.icao24; cs.toUpperCase(); }
        d["callsign"] = cs;
        d["hex"] = t.state.icao24;
        if (!t.typeCode.isEmpty())     d["type"] = t.typeCode;
        if (!t.operatorName.isEmpty()) d["operator"] = t.operatorName;
        d["alt_ft"] = (int)lroundf(t.state.baroAltitude * METRES_TO_FEET);
        String payload;
        serializeJson(d, payload);
        mqtt.Publish(eventTopic, payload, false);
    };

    for (auto& [icao, tracked] : trackedAircraft) {
        if (tracked.state.onGround) continue;
        if (isEmergencySquawk(tracked.state.squawk))
            fire(tracked, 1, "emergency");
        if (alertMilitary && SpecialAircraft::IsMilitary(tracked.state.icao24))
            fire(tracked, 2, "military");
        if (MatchesWatchlist(tracked))
            fire(tracked, 0, "watchlist");
        if (IsOverhead(tracked))
            fire(tracked, 3, "overhead");
    }
}

void AircraftManager::DrawAircraftSilhouette(BandCanvas& g, int cx, int cy, const TrackedAircraft& tracked) const
{
    // Dim, so it reads as a placeholder rather than data. Same vector language as the radar
    // markers, varied by emitter category like DrawAircraftTriangle.
    const uint32_t col = lgfx::color888(0, 115, 0);
    const int cat = tracked.state.category;

    if (cat == 8) { // rotorcraft: cabin + crossed main rotor + tail boom & rotor
        g.drawLine(cx - 38, cy - 20, cx + 38, cy + 8, col);
        g.drawLine(cx - 38, cy + 8, cx + 38, cy - 20, col);
        g.fillRect(cx - 2, cy - 6, 4, 46, col);   // tail boom
        g.fillRect(cx - 12, cy + 36, 24, 3, col); // tail rotor
        g.fillCircle(cx, cy - 8, 9, col);         // cabin
        return;
    }
    if (cat == 10) { // lighter-than-air: envelope + gondola
        g.fillCircle(cx, cy - 6, 28, col);
        g.fillTriangle(cx - 9, cy + 14, cx + 9, cy + 14, cx, cy + 30, col);
        return;
    }

    // fixed-wing airliner, viewed top-down, nose up
    g.fillTriangle(cx, cy - 44, cx - 5, cy - 30, cx + 5, cy - 30, col);      // nose
    g.fillRoundRect(cx - 5, cy - 34, 10, 74, 5, col);                        // fuselage
    g.fillTriangle(cx - 4, cy - 12, cx - 50, cy + 14, cx - 4, cy + 12, col); // left wing (swept)
    g.fillTriangle(cx + 4, cy - 12, cx + 50, cy + 14, cx + 4, cy + 12, col); // right wing
    g.fillTriangle(cx - 4, cy + 26, cx - 22, cy + 38, cx - 4, cy + 36, col); // left tailplane
    g.fillTriangle(cx + 4, cy + 26, cx + 22, cy + 38, cx + 4, cy + 36, col); // right tailplane
}

void AircraftManager::DrawDetailCard(BandCanvas& backbuffer, const TrackedAircraft& tracked)
{
    const Aircraft& s = tracked.state;
    constexpr int cx = SCREEN_SIZE_DIV_2;

    auto centered = [&](const String& str, int yy) {
        const int x = cx - static_cast<int>(backbuffer.textWidth(str)) / 2;
        backbuffer.drawString(str, x, yy);
    };

    // title: callsign, or the ICAO address if there's no callsign
    String title = s.callsign;
    title.trim();
    if (title.isEmpty()) {
        title = s.icao24;
        title.toUpperCase();
    }

    // Three layouts, not two:
    //   showPhoto  -- page 0 with a decoded photo: FULL BLEED (below, returns early)
    //   !hasPhoto  -- no photo at all: the slot layout, silhouette or "Loading...",
    //                 with full telemetry beneath it
    //   otherwise  -- the data page, tapped over from a full-bleed photo
    const bool hasPhoto = photoReady && photoIcao == selectedIcao && photoSprite.getBuffer() != nullptr;
    const bool showPhoto = hasPhoto && detailPage == 0;
    const bool photoSettled = photoIcao == selectedIcao && photoResolved; // we now know there's no photo
    const bool useSlot = !hasPhoto;                                       // silhouette / loading layout

    // ---- FULL-BLEED PHOTO PAGE ---------------------------------------------
    // The photo fills the disc and the only text over it is the callsign.
    //
    // WHY ONLY THE CALLSIGN. Every row of text over the photo costs a row of
    // aircraft, and the scrim has to be tall enough to cover the lowest line --
    // so four lines of text is what made the old scrim a tall arc rather than a
    // band. The photo already answers the fields that describe the CATEGORY
    // (type, operator, and largely the route); the callsign is the one thing it
    // cannot tell you. Those fields are not lost: they are on the data page,
    // which this card has always had (detailPage 1, one tap away, hint below).
    //
    // The callsign specifically, rather than any other single field:
    //   - it identifies THIS airframe, not its category
    //   - it is what a person says out loud ("SKW6042 is overhead")
    //   - it is the only field that is ALWAYS present. Type, operator and route
    //     all wait on enrichment; the position feed carries the callsign, so any
    //     other choice leaves the primary line empty on a fresh tap.
    //
    // Measured, not assumed: the band ramp (0.66-0.80 of height, baked at ingest
    // by proxy/src/framing.ts) holds >= 8.1:1 against the card green on every
    // photo in the hostile set, three of which contain a pure-white pixel in
    // these rows. See issue #209 for the table.
    int y;
    backbuffer.setTextColor(lgfx::color888(0, 255, 0));
    if (showPhoto) {
        // No fillScreen: the photo covers every pixel of the disc, which is what
        // pays for the larger blit (BlitProbe: fillScreen 240x240 = 1.653 ms of
        // the 2.652 ms the bigger sprite costs).
        photoSprite.pushSprite(&backbuffer.sprite(), 0, -backbuffer.offsetY());
        backbuffer.drawCircle(cx - 1, cx - 1, SCREEN_SIZE_DIV_2 - 1, lgfx::color888(0, 200, 0));

        // Badges ride at the TOP of the disc on their own filled pill rather than
        // in the scrim. They are intermittent and high-salience, and putting them
        // in the band would force the band taller for text most cards never show
        // -- which is the whole cost this layout exists to avoid.
        int badgeY = 18;
        auto badge = [&](const String& text, uint32_t fg) {
            backbuffer.setTextSize(1);
            const int w = static_cast<int>(backbuffer.textWidth(text));
            const int h = backbuffer.fontHeight();
            backbuffer.fillRoundRect(cx - w / 2 - 6, badgeY - 3, w + 12, h + 6, 3,
                                     lgfx::color888(0, 0, 0));
            backbuffer.setTextColor(fg);
            centered(text, badgeY);
            badgeY += h + 9;
        };
        if (const SpecialAircraft::Class sc = SpecialClassOf(tracked); sc != SpecialAircraft::Class::None) {
            const char* label = "";
            switch (sc) {
                case SpecialAircraft::Class::Military:   label = "MILITARY";   break;
                case SpecialAircraft::Class::Special:    label = "SPECIAL";    break;
                case SpecialAircraft::Class::Helicopter: label = "HELICOPTER"; break;
                default: break;
            }
            badge(label, SpecialColor(sc));
        }
        // The claim is a MOMENT, not a field -- opening the card is what earns it,
        // so it stays on the page the owner is looking at when it fires.
        if (logbookEnabled && tracked.claimFired)
            badge("CLAIMED #" + String((int)logbook.ClaimedTypeCount()), lgfx::color888(255, 215, 0));
        else if (logbookEnabled && tracked.claimable)
            badge("NEW", lgfx::color888(255, 215, 0));

        backbuffer.setTextSize(2);
        backbuffer.setTextColor(lgfx::color888(86, 235, 60));
        centered(title, FULLBLEED_TITLE_Y);
        // THE HINT LINE GIVES WAY TO PROVENANCE WHEN THERE IS ANY DOUBT WHOSE
        // AIRCRAFT THIS IS.
        //
        // The full-bleed card is callsign-only because the photograph already
        // says everything the category fields did. That holds for a Cessna. It
        // does NOT hold for an airliner, because every airliner stock photo is a
        // real aircraft in a real operator's scheme -- so ASA713 drew a United
        // 737-9 with nothing on screen to say the picture was of the TYPE. The
        // caption that used to prevent that moved to the data page with the other
        // category-descriptive fields, and this is where the confusion happens.
        //
        // Read a livery and you read IDENTITY, not category. That is the one
        // field the photo cannot replace, whatever else it replaces.
        //
        // It costs no pixels: this line is drawn either way, and "tap: details"
        // is the most redundant text on the card -- every other screen teaches
        // the tap, and a customer learns it once. A per-airframe photo (pk:"hex")
        // IS that aircraft, so it keeps the hint and stays uncaptioned.
        backbuffer.setTextSize(1);
        backbuffer.setTextColor(lgfx::color888(0, 150, 0));
        centered(CaptionForDisc(backbuffer,
                                tracked.photoRepresentative ? "representative photo" : "tap: details",
                                tracked.photoRepresentative ? "stock photo" : "tap: details"),
                 FULLBLEED_HINT_Y);
        return; // nothing else belongs over the photograph
    }

    backbuffer.fillScreen(lgfx::color888(0, 0, 0));
    // frame ring to match the round display
    backbuffer.drawCircle(cx - 1, cx - 1, SCREEN_SIZE_DIV_2 - 1, lgfx::color888(0, 200, 0));
    if (useSlot) {
        if (photoSettled) {
            DrawAircraftSilhouette(backbuffer, cx, 76, tracked);
            backbuffer.setTextSize(1);
            backbuffer.setTextColor(lgfx::color888(0, 120, 0));
            centered("No photo available", 120);
        } else {
            backbuffer.setTextSize(1);
            backbuffer.setTextColor(lgfx::color888(0, 120, 0));
            centered("Loading photo...", 74);
        }
        backbuffer.setTextSize(2);
        backbuffer.setTextColor(lgfx::color888(0, 255, 0));
        centered(title, 136);
        y = 162;
    } else {
        backbuffer.setTextSize(2);
        centered(title, 36);
        y = 70;
        if (tracked.photoRepresentative) {
            // Provenance for the stock library: this is a generic shot of the TYPE,
            // not this airframe (a per-hex override is uncaptioned). It moved here
            // from the photo page with the other category-descriptive fields --
            // it describes where the picture came from, which is the same kind of
            // fact as "Type:" and belongs beside it.
            backbuffer.setTextSize(1);
            backbuffer.setTextColor(lgfx::color888(0, 130, 0));
            centered("representative photo", 58);
            backbuffer.setTextColor(lgfx::color888(0, 200, 0));
        }
    }

    backbuffer.setTextSize(1);
    backbuffer.setTextColor(lgfx::color888(0, 200, 0));
    const int lineHeight = backbuffer.fontHeight() + 5;
    // 240 px screens: the flowed stat block can reach the fixed footer hints
    // (drawn at SCREEN_SIZE-46/-34), interleaving text -- seen on the s3-128's
    // photo-less military card, whose flow starts at y=162 and needs 5 lines.
    // Telemetry outranks the static gesture hints, so the flow owns the space:
    // the hints render only if the flow never entered their zone (sparse cards
    // keep them), and the flow itself stops at the bezel margin so no line can
    // ever draw off the round panel.
    const int hintZoneY   = SCREEN_SIZE - 58; // hints need everything below this
    const int hardBottomY = SCREEN_SIZE - 12; // bezel margin: last drawable line
    auto line = [&](const String& str) {
        if (str.isEmpty()) return;
        if (y + backbuffer.fontHeight() > hardBottomY) return; // no room: drop
        centered(str, y);
        y += lineHeight;
    };

    // flag a special contact up top, in the same colour as its radar marker
    if (const SpecialAircraft::Class sc = SpecialClassOf(tracked); sc != SpecialAircraft::Class::None) {
        const char* label = "";
        switch (sc) {
            case SpecialAircraft::Class::Military:   label = "- MILITARY -";   break;
            case SpecialAircraft::Class::Special:    label = "- SPECIAL -";    break;
            case SpecialAircraft::Class::Helicopter: label = "- HELICOPTER -"; break;
            default: break;
        }
        backbuffer.setTextColor(SpecialColor(sc));
        line(label);
        backbuffer.setTextColor(lgfx::color888(0, 200, 0));
    }

    // The claim line. Opening this card IS the claim, so by the time it is drawn
    // the usual state is "just claimed" -- the card reports what the tap earned
    // rather than advertising something still to do. `claimable` only survives to
    // here when the type is not yet known (enrichment still in flight), which is
    // exactly when "NEW" is still the honest word.
    if (logbookEnabled && tracked.claimFired) {
        backbuffer.setTextColor(lgfx::color888(255, 215, 0));
        line("* CLAIMED #" + String((int)logbook.ClaimedTypeCount()) + " *");
        backbuffer.setTextColor(lgfx::color888(0, 200, 0));
    } else if (logbookEnabled && tracked.claimable) {
        backbuffer.setTextColor(lgfx::color888(255, 215, 0));
        line("* NEW *");
        backbuffer.setTextColor(lgfx::color888(0, 200, 0));
    }

    // identity first (route + type + operator), shown in both layouts.
    // routelabel::CardLine returns "" when there is no route, and renders a
    // flight that came back to its departure field as "Local flight: EGYD"
    // instead of "EGYD -> EGYD" -- which is real data, but reads exactly like
    // the manufactured self-loop the mirror's rev-3 rule exists to prevent.
    // Shared with the List screen's Route field; see include/RouteLabel.h.
    {
        const String routeLine = routelabel::CardLine(tracked.routeOrigin, tracked.routeDest);
        if (!routeLine.isEmpty()) line(routeLine);
    }
    if (!tracked.typeCode.isEmpty())     line("Type: " + tracked.typeCode);
    if (!showPhoto && !tracked.typeName.isEmpty()) line(tracked.typeName); // full model, data page only
    if (!tracked.operatorName.isEmpty()) line(tracked.operatorName);

    // the photo page hides the full telemetry for space; the data page (and
    // photo-less aircraft) show everything
    if (!showPhoto) {
        // distance + bearing from the radar centre
        auto [aLat, aLon] = tracked.GetDisplayPosition();
        const float dLatKm = (aLat - (float)lat) * 111.0f;
        const float dLonKm = (aLon - (float)lon) * 111.0f * cosf(radians((float)lat));
        float distance = sqrtf(dLatKm * dLatKm + dLonKm * dLonKm);
        float bearing = degrees(atan2f(dLonKm, dLatKm));
        if (bearing < 0.0f) bearing += 360.0f;
        static const char* DIRS[8] = { "N", "NE", "E", "SE", "S", "SW", "W", "NW" };
        const char* dir = DIRS[((int)roundf(bearing / 45.0f)) % 8];
        line("Dist: " + units::FormatKm(distance, rangeUnit, /*space=*/true) + " " + dir);

        if (!tracked.registration.isEmpty()) line("Reg: " + tracked.registration);
        line("Alt: " + String(lroundf(s.baroAltitude * METRES_TO_FEET)) + " ft");
        line("Spd: " + String(lroundf(s.velocity * MS_TO_KNOTS)) + " kt");
        line("Hdg: " + String(lroundf(s.trueTrack)) + " deg");
        if (!s.squawk.isEmpty()) line("Sqk: " + s.squawk);
    }

    if (y <= hintZoneY) {
        backbuffer.setTextColor(lgfx::color888(0, 110, 0));
        centered(pinnedIcao == selectedIcao ? "swipe up: unpin" : "swipe up: pin", SCREEN_SIZE - 46);
        centered(showPhoto ? "tap: details" : "tap: back", SCREEN_SIZE - 34);
    }
}

void AircraftManager::ProcessMetadataLookups()
{
    // Local receiver: do no background enrichment unless the CHOSEN source is
    // actually usable. "Off" obviously sends nothing. But "Cloud" with no URL or
    // key must ALSO send nothing -- it must never fall through to the adsbdb path
    // below, because that would quietly hand a third party the data of a user who
    // explicitly picked us. A chosen source that cannot run stays silent.
    if (useLocalSource && !UseCloudEnrich())
        return;

#ifdef FEATURE_CLOUD_FEED
    if (useCloudSource) {
        // The /api/v1/blipscope/config enrich level is the master switch in cloud mode. Off:
        // nothing (taps still enrich). Watchlist: the per-aircraft filter below
        // decides -- the watchlist itself is the need, so metadataNeeded doesn't
        // gate it. Full: same metadataNeeded economy as the adsbdb path.
        if (cloudCfg.enrich == CloudFeed::Config::Enrich::Off)
            return;
        if (cloudCfg.enrich == CloudFeed::Config::Enrich::Full && !metadataNeeded)
            return;
    } else
#endif
    if (!metadataNeeded)
        return;

    // one enrichment outstanding at a time, shared with the detail-card path
    if (enrichInFlight)
        return;

    // not enough contiguous heap for a TLS handshake -> don't even try. Attempting
    // it only fails ("BIGNUM alloc failed") and the churn starves the web server.
    //
    // Throttled for the same reason as the detail-card gate, though this path is
    // already paced by METADATA_LOOKUP_INTERVAL below -- the gate sits ABOVE that
    // pacing, so it was still being asked every loop even though it could only
    // act every 5 s.
    if (!heaphealth::CanHandshakeThrottled())
        return;

    const unsigned long now = millis();

    // Pause background enrichment for a few seconds after any touch. The enrichment task's
    // TLS holds the touch I2C bus (touch is serialized against it on the radar view), so
    // enriching while the user is interacting gates taps out -- during a burst of new
    // aircraft that makes touch look dead until enrichment catches up. Pausing keeps the bus
    // free while the user is active; an idle radar still enriches and catches up normally.
    constexpr unsigned long ENRICH_TOUCH_PAUSE = 4000;
    if (now - lastTouchActivityMs < ENRICH_TOUCH_PAUSE)
        return;

    // Space lookups out so a burst of new aircraft doesn't fire many HTTP calls back to back
    // (each holds the bus, gating touch) and to stay friendly to the free adsbdb service.
    // The spacing also guarantees a free-bus window between lookups so a tap can land and
    // trigger the pause above.
    constexpr unsigned long METADATA_LOOKUP_INTERVAL = 5000;
    if (now - lastMetadataLookup < METADATA_LOOKUP_INTERVAL)
        return;

    // Queue the NEAREST aircraft still awaiting a lookup, then stop for this tick --
    // not just the first in map (icao-order) order. On a full scope the fast movers
    // crossing the middle are what a user actually taps, and a map-order round-robin
    // let them age out of the fleet before their turn came up -- so airliners kept
    // coming back un-enriched. Nearest-first spends the (deliberately still-paced)
    // enrich cadence where it's most likely to be looked at; far contacts are enriched
    // once the near ones are done, and one that leaves before its turn was a plane the
    // user was never going to tap. Cache hits are free, so apply those to EVERY eligible
    // plane while scanning; only the nearest cache-MISS actually makes a network call.
    const float cosLat = cosf((float)lat * (float)DEG_TO_RAD);
    TrackedAircraft* best = nullptr;
    const String* bestIcao = nullptr;
    float bestD2 = 0.0f, bestLat = 0.0f, bestLon = 0.0f;
    for (auto& [icao, tracked] : trackedAircraft) {
        if (tracked.metadataState != TrackedAircraft::MetadataState::NotFetched)
            continue;
        if (now < tracked.metadataRetryAfter)
            continue; // still in post-failure cooldown; skip so others get a turn

#ifdef FEATURE_CLOUD_FEED
        if (UseCloudEnrich()) {
            // NB a local-receiver device never fetches /api/v1/blipscope/config, so cloudCfg here
            // is the baked default (Enrich::Full, which still respects
            // metadataNeeded). That is deliberate: it matches the economy of the
            // adsbdb path it replaces, without adding a config round trip to a
            // device whose whole point is not depending on us for the feed.
            if (!CloudShouldBackgroundEnrich(tracked))
                continue;
            // A cache hit costs nothing -- apply it and keep scanning.
            if (const CloudFeed::Enrichment* cached = enrichCache.Find(icao)) {
                ApplyEnrichment(tracked, *cached);
                continue;
            }
        }
#endif
        // Cheap planar range to the device centre (lon scaled by cos lat); we only
        // need to RANK, not measure, so the squared value is enough.
        auto [acLat, acLon] = tracked.GetDisplayPosition();
        const float dLat = acLat - (float)lat;
        const float dLon = (acLon - (float)lon) * cosLat;
        const float d2 = dLat * dLat + dLon * dLon;
        if (best == nullptr || d2 < bestD2) {
            best = &tracked;
            bestIcao = &icao;
            bestD2 = d2;
            bestLat = acLat;
            bestLon = acLon;
        }
    }

    if (best == nullptr)
        return; // nothing needs a network lookup this tick

#ifdef FEATURE_CLOUD_FEED
    if (UseCloudEnrich()) {
        lastMetadataLookup = now;
        best->metadataState = TrackedAircraft::MetadataState::Fetching;
        String callsign = best->state.callsign;
        callsign.trim();
        RequestCloudEnrich(*bestIcao, callsign, bestLat, bestLon);
        return;
    }
#endif
    // No proxy configured: nothing to ask, and nothing is marked in flight.
    // Leaving metadataState at Fetching here would strand every aircraft in a
    // permanent "Loading..." on a device that will never issue the request.
    // The radar is unaffected -- it runs on its own position source.
}
