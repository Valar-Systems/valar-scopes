#include "AircraftManager.h"
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
    // ap=<airports>: the overlay symbol count, added 2026-08-02 because it is
    // the leading suspect for what actually drives frame p95 (n does not -- see
    // the budget note below) and there was no way to test that without it.
    // Whichever overlay is actually in force: the cloud long tail while it has
    // landed, else the baked majors table DrawAirports falls back to -- so the
    // number always means "symbols available to draw", on every build.
#ifdef FEATURE_CLOUD_FEED
    const unsigned apCount = cloudAirports.empty() ? (unsigned)AIRPORT_COUNT
                                                   : (unsigned)cloudAirports.size();
#else
    const unsigned apCount = (unsigned)AIRPORT_COUNT;
#endif
    Serial.printf("[health] frame avg=%.1fms p95=%.1fms max=%.1fms  n=%u ap=%u  heap free=%u largest=%u free8=%u tlsOk=%d rej=%lu ball=%d/%lu  allocFail=%lu hardFail=%lu  tls=%lu/%lu  tlsmem=%lu/%lu/%lu  interval=%lums%s\n",
                  avgMs, p95Ms, maxMs, (unsigned)trackedAircraft.size(), apCount,
                  (unsigned)heapFree, (unsigned)largest, (unsigned)free8, tlsOk,
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
    // 85, not 60 -- see the re-baseline note above. The 60 was derived from
    // boards whose info labels had been silently switched off in 2026-08.
    constexpr float FRAME_P95_BUDGET_MS = 85.0f;
    constexpr uint32_t LARGEST_BLOCK_BUDGET = 20000;
    if (p95Ms > FRAME_P95_BUDGET_MS) {
        budgetBreaches++;
        Serial.printf("[health] BUDGET BROKEN: frame p95 %.1fms > %.0fms\n", p95Ms, FRAME_P95_BUDGET_MS);
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
                                              CloudFeed::Headers(req->cloudKey, req->otaMem));
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
                CloudFeed::Headers(req->cloudKey, req->otaMem));
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

    switch (screen) {
        case Screen::List:  DrawList(backbuffer);  break;
        case Screen::Stats: DrawStats(backbuffer); break;
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
    auto fitted = [&](const String& s, int yTop) -> String {
        const float r  = (float)SCREEN_SIZE_DIV_2;
        const int   d0 = yTop - SCREEN_SIZE_DIV_2;
        const int   d1 = yTop + lh - SCREEN_SIZE_DIV_2;
        const float dy = (float)((d0 < 0 ? -d0 : d0) > (d1 < 0 ? -d1 : d1)
                                     ? (d0 < 0 ? -d0 : d0)
                                     : (d1 < 0 ? -d1 : d1));
        if (dy >= r) return String();
        const int avail = (int)(2.0f * sqrtf(r * r - dy * dy)) - 8; // inset off the bezel
        if (avail <= 0) return String();
        if ((int)backbuffer.textWidth(s) <= avail) return s;

        String out = s;
        while (out.length() > 1 && (int)backbuffer.textWidth(out + "...") > avail)
            out.remove(out.length() - 1);
        return out + "...";
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
    for (int i = 0; i < 3; ++i) {
        const bool active = (i == (int)screen);
        backbuffer.fillCircle(cx - 12 + i * 12, y, active ? 3 : 2,
                              active ? lgfx::color888(0, 255, 0) : lgfx::color888(0, 80, 0));
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

    backbuffer.setTextSize(1);
    backbuffer.setTextColor(lgfx::color888(0, 170, 0));
    backbuffer.drawString(buf, SCREEN_SIZE_DIV_2 - (int)backbuffer.textWidth(buf) / 2, SCREEN_SIZE - 30);
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
                    auto sel = trackedAircraft.find(selectedIcao); // same claim-on-open as the radar
                    if (sel != trackedAircraft.end())
                        ClaimTappedAircraft(sel->second);
                }
            }
        }
    }
    // Stats screen: tap does nothing
}

void AircraftManager::HandleSwipe(Swipe swipe)
{
    // detail card: swipe up pins ("tracks") the aircraft and returns to the
    // radar; any other swipe just closes the card
    if (inDetail) {
        if (swipe == Swipe::Up) {
            pinnedIcao = (pinnedIcao == selectedIcao) ? "" : selectedIcao;
            screen = Screen::Radar;
        }
        ExitDetail();
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
    if (swipe == Swipe::Left)  screen = (Screen)(((int)screen + 1) % 3);
    if (swipe == Swipe::Right) screen = (Screen)(((int)screen + 2) % 3);
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
