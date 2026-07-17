#include "AircraftManager.h"

#include <algorithm>
#include <cmath>
#include <time.h>
#include <utility>

#include <WiFi.h> // WiFi.localIP() for the device address on the Stats screen

#include "SpecialAircraft.h"
#include "DeviceIdentity.h"
#include "Layout.h"
#include "Board.h"
#include "OtaUpdater.h" // FW_VERSION, compared against the cloud config's minFw gate
#include "TouchWatchdog.h" // CST816 supervisor; inert unless variant::TOUCH_WATCHDOG
#ifdef BISECT_TEST
#include "BisectHarness.h" // synthetic gesture-storm source (bisection builds only)
#endif
#ifdef SOAK_TEST
#include "SoakHarness.h" // sparse human-scale gesture script (soak builds only)
#endif

// adsbdb thumbnails (airport-data.com) are a standard 150x100
constexpr int PHOTO_W = 150;
constexpr int PHOTO_H = 100;

// Display-unit conversions. The feeds are normalised to OpenSky's SI internally
// (metres, m/s); aviation/US convention shows altitude in feet and ground speed
// in knots, so convert at each on-screen/notification site that shows telemetry.
constexpr float METRES_TO_FEET = 3.28084f;
constexpr float MS_TO_KNOTS    = 1.94384f;

// Critical-heap floor below which we don't even start an enrichment HTTPS lookup -- a
// last-ditch guard, not a routine throttle. The post-banding/streaming build runs with
// only ~24-28 KB largest free block yet the OpenSky fetch (same mbedTLS path) handshakes
// fine there, so the gate-time largest-block reading is a poor predictor of handshake
// success; keep this well below normal operation so enrichment behaves like the (ungated)
// OpenSky fetch -- attempt the handshake and let the 30 s failure backoff handle a miss.
// Also the threshold for the [fetch] low-heap warning. Raising it starves enrichment off.
constexpr uint32_t ENRICH_TLS_HEAP_FLOOR = 16000;

#include <esp_heap_caps.h>

namespace {
    // Outcome-based soak criteria (2026-07-10): count what actually fails instead of
    // policing a fixed heap floor (7.7 KB largest is the measured operating level at
    // 25 aircraft, and feed TLS re-handshakes demonstrably succeed there).
    uint32_t allocFailures = 0;
    uint32_t fetchHardFailures = 0; // statusCode <= 0: TLS/DNS/connect/timeout class
    void OnAllocFailed(size_t size, uint32_t caps, const char* fn)
    {
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
// The same task also serves the cloud feed and its /v1/config fetch (kind), so all
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
    double rangeKm = 100.0;          // /v1/blips r param
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
    long dataEpoch = 0;              // cloud blips snapshot time t (0 = not cloud)
    CloudFeed::Config config;        // CloudConfig kind only
    std::vector<CloudFeed::CloudAirport> airports; // Airports kind only
#endif
};

// Handoff payloads for the background enrichment task (adsbdb metadata/route +
// aircraft photo, the cloud /v1/enrich join, and the ntfy alert POSTs -- anything
// blocking that must never run on the loop). Like the fetch payloads above,
// ownership transfers by pointer through a depth-1 queue: the receiver frees it.
// The task fills a result the loop applies; it never touches trackedAircraft,
// the photo sprite, or the logbook.
enum class EnrichKind : uint8_t { Metadata, Route, Photo, CloudEnrich, Ntfy, Leaderboard };

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
    // Leaderboard: the JSON submission body, POSTed to cloudBase + /v1/leaderboard.
    String lbBody;
};

struct EnrichResult {
    EnrichKind kind = EnrichKind::Metadata;
    String icao24;
    bool definitive = false; // a final HTTP answer arrived; false = transient failure (retry)
    // How long the loop should hold off a retry after a non-definitive result.
    // adsbdb outages back off 30 s; a cloud proxy still warming its caches
    // answers in seconds, so its retries come quicker.
    unsigned long retryCooldownMs = 30000;

    // Metadata / CloudEnrich
    String typeCode, typeName, operatorName, registration, photoUrl;
    // Route (CloudEnrich carries the route in the same fields)
    String routeCallsign, routeOrigin, routeDest;
    // CloudEnrich stock-photo join: the proxy's relative /v1/photo path (made
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

EnrichResult* fetchAircraftMetadata(HttpRequestManager& http, const String& icao24)
{
    EnrichResult* res = new EnrichResult();

    const HttpResult result = http.Get("https://api.adsbdb.com/v0/aircraft/" + icao24);

    // A network-level failure (no HTTP response at all) is transient: leave it
    // non-definitive so the loop returns the aircraft to NotFetched and retries.
    if (!result.success) {
        Serial.printf("[adsbdb] lookup for %s failed: %s\n", icao24.c_str(), result.errorMessage.c_str());
        return res;
    }

    // Only a conclusive answer -- 2xx, or 404 "unknown aircraft" -- is final. A
    // 429/5xx must stay non-definitive: recording it as Fetched-with-empty-fields
    // would permanently blank enrichment after one adsbdb throttle window (a
    // Fetched aircraft is never looked up again).
    res->definitive = (result.statusCode >= 200 && result.statusCode < 300) || result.statusCode == 404;
    if (!res->definitive) {
        Serial.printf("[adsbdb] lookup for %s: HTTP %d, will retry\n", icao24.c_str(), result.statusCode);
        return res;
    }

    JsonDocument doc;
    if (deserializeJson(doc, result.response))
        return res; // malformed body: treat as no data, but don't retry

    // adsbdb wraps the record in { "response": { "aircraft": {...} } }; when the
    // address is unknown it returns { "response": "unknown aircraft" }, leaving
    // this lookup null.
    JsonObject aircraft = doc["response"]["aircraft"];
    if (aircraft.isNull()) {
        Serial.printf("[adsbdb] %s -> unknown aircraft\n", icao24.c_str());
        return res;
    }

    res->typeCode     = aircraft["icao_type"].isNull()           ? "" : aircraft["icao_type"].as<String>();
    res->typeName     = aircraft["type"].isNull()                ? "" : aircraft["type"].as<String>();
    res->operatorName = aircraft["registered_owner"].isNull()    ? "" : aircraft["registered_owner"].as<String>();
    res->registration = aircraft["registration"].isNull()        ? "" : aircraft["registration"].as<String>();
    res->photoUrl     = aircraft["url_photo_thumbnail"].isNull() ? "" : aircraft["url_photo_thumbnail"].as<String>();

    Serial.printf("[adsbdb] %s -> type=%s operator=%s reg=%s\n",
                  icao24.c_str(), res->typeCode.c_str(),
                  res->operatorName.c_str(), res->registration.c_str());
    return res;
}

EnrichResult* fetchRoute(HttpRequestManager& http, const String& callsign)
{
    EnrichResult* res = new EnrichResult();

    const HttpResult result = http.Get("https://api.adsbdb.com/v0/callsign/" + callsign);

    // network-level failure is transient: stay non-definitive so it's retried
    if (!result.success && result.statusCode <= 0) {
        Serial.printf("[adsbdb] route %s failed: %s\n", callsign.c_str(), result.errorMessage.c_str());
        return res;
    }

    // only a conclusive answer (2xx, or 404 unknown callsign) is final; a
    // 429/5xx stays non-definitive so the detail path retries it later
    res->definitive = (result.statusCode >= 200 && result.statusCode < 300) || result.statusCode == 404;
    if (!res->definitive) {
        Serial.printf("[adsbdb] route %s: HTTP %d, will retry\n", callsign.c_str(), result.statusCode);
        return res;
    }
    res->routeCallsign = callsign;

    JsonDocument doc;
    if (deserializeJson(doc, result.response))
        return res;

    JsonObject flightroute = doc["response"]["flightroute"];
    if (flightroute.isNull()) {
        Serial.printf("[adsbdb] route %s -> unknown\n", callsign.c_str());
        return res;
    }

    // prefer the short IATA code, fall back to ICAO
    auto airportCode = [](JsonObject airport) -> String {
        if (airport.isNull()) return "";
        if (!airport["iata_code"].isNull()) return airport["iata_code"].as<String>();
        if (!airport["icao_code"].isNull()) return airport["icao_code"].as<String>();
        return "";
    };

    res->routeOrigin = airportCode(flightroute["origin"]);
    res->routeDest = airportCode(flightroute["destination"]);

    Serial.printf("[adsbdb] route %s -> %s->%s\n", callsign.c_str(),
                  res->routeOrigin.c_str(), res->routeDest.c_str());
    return res;
}

EnrichResult* fetchPhoto(HttpRequestManager& http, const String& url, const String& authKey)
{
    EnrichResult* res = new EnrichResult();

    // The BYO adsbdb thumbnail host is public; the cloud /v1/photo route needs
    // the device key. authKey is "" for BYO, the proxy key in cloud mode.
    std::vector<std::pair<String, String>> headers;
    if (!authKey.isEmpty())
        headers.push_back({ "X-Blip-Key", authKey });

    HttpResult result = http.Get(url, {}, headers);
    if (!result.success) {
        Serial.printf("[photo] fetch failed: %s\n", result.errorMessage.c_str());
        return res; // photoFetched stays false
    }

    res->photoFetched = true;
    res->photoBytes = std::move(result.response); // decoded on the loop into the shared sprite
    return res;
}

#ifdef FEATURE_CLOUD_FEED
// One GET to the proxy replaces the old two adsbdb lookups: /v1/enrich pre-joins
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
    lat = configServer.GetStoredString("latitude").toDouble();
    lon = configServer.GetStoredString("longitude").toDouble();

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
    const bool inMiles = configServer.GetStoredString("radius-unit") == "mi";
    const double distanceKm = inMiles ? distance * 1.609344 : distance;

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
    if (inMiles) rangeRadiusDisplay /= 1.609344;
    rangeUnit = inMiles ? "mi" : "km";

    // configuration
    const String renderText = configServer.GetStoredString("infotext");
    const String renderTris = configServer.GetStoredString("triangle");
    const String renderAirports = configServer.GetStoredString("airports");
    if (!renderAirports.isEmpty()) displayAirports = renderAirports == "true";
    // Minimum airport size to draw (cloud /v1/airports overlay only; the baked
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
    const String logbookStr = configServer.GetStoredString("logbook");
    logbookEnabled = logbookStr.isEmpty() ? false : (logbookStr == "true");
    if (logbookEnabled) {
        metadataNeeded = true;
        logbook.Begin(); // idempotent: only the first call loads from NVS
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

    // "look up!" overhead alert. The distance is entered in the same unit as the
    // radar radius; store it in km for the centre-distance comparison.
    showOverhead = configServer.GetStoredString("lookup") == "true";
    alertOverhead = configServer.GetStoredString("lookup-alert") == "true";
    double lookupDist = configServer.GetStoredString("lookup-dist").toDouble();
    if (lookupDist <= 0.0) lookupDist = 3.0;
    overheadKm = inMiles ? lookupDist * 1.609344 : lookupDist;

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
    if constexpr (!kNoNet)
        mqtt.Begin(mc); // spawns the publisher task once; reconfigures it thereafter
    lastMqttState = millis() - 5000; // publish a first snapshot promptly once connected

    // data source: Blipscope Cloud (the proxy; default on cloud builds), the
    // OpenSky cloud API (BYO credentials -- the user's own ToS relationship), or
    // the user's own ADS-B receiver. URLs are normalised once here so the fetch
    // task gets ready-to-GET endpoints.
    const String dataSource = configServer.GetStoredString("data-source");
    useLocalSource = dataSource == "local";
    localUrl = useLocalSource ? normalizeLocalUrl(configServer.GetStoredString("local-url")) : "";

#ifdef FEATURE_CLOUD_FEED
    // An unset key defaults to cloud on cloud builds: new devices land on the
    // proxy out of the box; an explicit "opensky"/"local" choice is respected.
    useCloudSource = !useLocalSource && dataSource != "opensky";
    rangeKmCfg = distanceKm;
    cloudUrl = CloudFeed::NormalizeBaseUrl(configServer.GetStoredString("cloud-url"));
    if (cloudUrl.isEmpty())
        cloudUrl = CloudFeed::NormalizeBaseUrl(CLOUD_FEED_BASE);
    cloudKey = configServer.GetStoredString("cloud-key");
    cloudKey.trim();
    if (cloudKey.isEmpty())
        cloudKey = CLOUD_FEED_KEY;

    if (useCloudSource) {
        // Cadence is the /v1/config-driven active/idle/night state machine (see
        // CurrentPollIntervalMs); fetchInterval only seeds the pre-config default.
        fetchInterval = cloudCfg.pollActiveMs;
        lastCloudCfgFetch = 0; // re-fetch the fleet config promptly after any (re)initialise
        // Location / radius / toggle may just have changed: drop the airport
        // long tail and re-fetch (the baked majors serve in the gap).
        lastCloudAirportsFetch = 0;
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

        if constexpr (!kNoNet) {
            const String token = authHandler.GetValidToken(configServer.GetStoredString("opensky-id"), configServer.GetStoredString("opensky-secret"));
            if (!token.isEmpty())
                dailyRequestBudget = AUTHED_TOKENS_PER_DAY - TOKEN_BUFFER; // authed tokens minus buffer
        }

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

#ifdef FEATURE_CLOUD_FEED
    // Public leaderboard: submit the logbook tallies hourly (first submit ~30 s
    // after boot so the lifelist has loaded and the clock/feed have settled).
    // QueueLeaderboardSubmit self-gates on the shared enrich queue; a busy tick
    // just retries next loop, and lastLeaderboardSubmit only advances on a
    // successful enqueue so a missed hour isn't skipped.
    if (lbEnabled && useCloudSource && !cloudUrl.isEmpty()) {
        constexpr unsigned long LB_INTERVAL_MS = 60UL * 60UL * 1000UL; // hourly
        constexpr unsigned long LB_FIRST_DELAY_MS = 30UL * 1000UL;
        const bool due = lastLeaderboardSubmit == 0
            ? now >= LB_FIRST_DELAY_MS
            : now - lastLeaderboardSubmit >= LB_INTERVAL_MS;
        if (due && QueueLeaderboardSubmit())
            lastLeaderboardSubmit = now;
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
        // Fleet config (/v1/config): on boot, then daily; a failed fetch retries
        // in 15 min (ConsumeFetchResult shifts the timer). Runs on the shared
        // fetch task ahead of the next feed poll so cadence/enrich-level tunables
        // land before the picture builds up.
        if (useCloudSource && !cloudUrl.isEmpty()) {
            constexpr unsigned long CFG_REFRESH_MS = 24UL * 60UL * 60UL * 1000UL;
            if (lastCloudCfgFetch == 0 || now - lastCloudCfgFetch >= CFG_REFRESH_MS) {
                lastCloudCfgFetch = now;
                RequestCloudConfig();
                return; // the fetch task is busy now; the feed poll goes next cycle
            }
        }

        // Airport overlay long tail (/v1/airports): geography is static, so
        // once after boot and then daily is plenty. Gated on the display toggle
        // (no toggle, no traffic); Initialise() zeroes the timer on every
        // config save, so a location / range / toggle change re-fetches. A
        // failed fetch retries in 15 min (ConsumeFetchResult shifts the timer)
        // while DrawAirports serves the baked majors table.
        if (useCloudSource && displayAirports && !cloudUrl.isEmpty()) {
            constexpr unsigned long APT_REFRESH_MS = 24UL * 60UL * 60UL * 1000UL;
            if (lastCloudAirportsFetch == 0 || now - lastCloudAirportsFetch >= APT_REFRESH_MS) {
                lastCloudAirportsFetch = now;
                RequestCloudAirports();
                return; // same single-fetch-task etiquette as the config fetch
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

bool AircraftManager::IsDataStale() const
{
    if (lastGoodDataMs == 0)
        return false; // nothing merged yet: that's "starting up", not "stale"
#ifdef FEATURE_CLOUD_FEED
    const int staleFactor = useCloudSource ? cloudCfg.staleFactor : 3;
#else
    const int staleFactor = 3;
#endif
    const unsigned long ageMs = (millis() - lastGoodDataMs) + dataLagAtMergeMs;
    return ageMs > (unsigned long)staleFactor * CurrentPollIntervalMs();
}

void AircraftManager::DrawStaleIndicator(BandCanvas& backbuffer) const
{
    if (!IsDataStale())
        return;
    // Small amber tag at the top of the scope: the picture is dead-reckoning on
    // old data (feed trouble upstream or local). Deliberately quiet -- an
    // indicator, not an alarm.
    backbuffer.setTextSize(1);
    backbuffer.setTextColor(lgfx::color888(255, 176, 40));
    const char* tag = "STALE DATA";
    backbuffer.drawString(tag, SCREEN_SIZE_DIV_2 - (int)backbuffer.textWidth(tag) / 2, 14);
}

void AircraftManager::RecordFrameUs(uint32_t frameUs)
{
    // Ring of the last N frames in 0.1 ms units (u16 caps at ~6.5 s -- ample).
    const uint32_t tenths = frameUs / 100;
    frameSampleBuf[frameSampleCount % FRAME_SAMPLES] = (uint16_t)std::min(tenths, (uint32_t)UINT16_MAX);
    frameSampleCount++;

    const unsigned long now = millis();
    if (now - lastHealthReportMs < 30000)
        return;
    lastHealthReportMs = now;

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
    Serial.printf("[health] frame avg=%.1fms p95=%.1fms  heap free=%u largest=%u  interval=%lums%s\n",
                  avgMs, p95Ms, (unsigned)heapFree, (unsigned)largest,
                  CurrentPollIntervalMs(), IsDataStale() ? "  DATA STALE" : "");

#ifdef SOAK_TEST
    // Fetch-pipeline state for the soak record. Added for the 2026-07-09 stall
    // (fetches silent 22 min, loop healthy, task never dequeued): these fields
    // adjudicate loop-side (inFlight/inDetail gate) vs task-side (taskState with
    // a non-empty reqQ = blocked despite queued work) on the next occurrence.
    // taskState: 0=Running 1=Ready 2=Blocked 3=Suspended 4=Deleted.
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
    constexpr float FRAME_P95_BUDGET_MS = 60.0f;
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

    if constexpr (kNoNet)
        return; // bisection: the loop-side queues exist (BisectInjectFleet feeds the
                // result queue through the real merge) but the task never runs

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

#ifdef SOAK_TEST
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

        // Airport-overlay fetch (/v1/airports): same one-off GET pattern as the
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
            // /v1/blips: the proxy quantizes to its cache tile/bucket, clips,
            // sorts by distance, and caps server-side -- the reply is <= ~2 KB
            // with a fixed Content-Length, so the streaming decode is trivial.
            result = http.GetJson(
                CloudFeed::BlipsUrl(req->cloudBase), doc,
                { { "lat", String(req->lat, 4) },
                  { "lon", String(req->lon, 4) },
                  { "r", String((int)lround(req->rangeKm)) },
                  { "limit", "25" } },
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
            res->authFailed = isOpenSky && (result.statusCode == 401 || result.statusCode == 403);
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
        // healthy; warn only when the largest block falls to where enrichment starts
        // getting throttled (ENRICH_TLS_HEAP_FLOOR) -- the early sign we're sliding back
        // toward the TLS / config-page failures this all fixed.
        if (const uint32_t largest = ESP.getMaxAllocHeap(); largest < ENRICH_TLS_HEAP_FLOOR)
            Serial.printf("[fetch] LOW HEAP after %s: free=%u largest=%u aircraft=%u\n",
                          sourceName, (unsigned)ESP.getFreeHeap(), (unsigned)largest, (unsigned)res->aircraft.size());

        delete req;

        if (!result.success && result.statusCode <= 0)
            fetchHardFailures++; // hard network class; an upstream 503 is not counted

#ifdef SOAK_TEST
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
    if constexpr (kNoNet)
        return; // bisection: the harness injects the picture; nothing is ever fetched

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

    if (xQueueSend(fetchRequestQueue, &req, 0) == pdTRUE)
        fetchInFlight = true;
    else
        delete req; // queue full (shouldn't happen: we only request when !fetchInFlight)
}

#ifdef FEATURE_CLOUD_FEED
void AircraftManager::RequestCloudConfig()
{
    if (cloudUrl.isEmpty())
        return;
    FetchRequest* req = new FetchRequest();
    req->kind = FetchKind::CloudConfig;
    req->cloudBase = cloudUrl;
    req->cloudKey = cloudKey;
    // Whichever check-in is built first after an OTA carries the report; the
    // config fetch is normally it (boot runs it ahead of the first feed poll).
    req->otaMem = TakeOtaMemReport();
    if (xQueueSend(fetchRequestQueue, &req, 0) == pdTRUE)
        fetchInFlight = true;
    else
        delete req;
}

void AircraftManager::RequestCloudAirports()
{
    if (cloudUrl.isEmpty())
        return;
    FetchRequest* req = new FetchRequest();
    req->kind = FetchKind::Airports;
    req->cloudBase = cloudUrl;
    req->cloudKey = cloudKey;
    // The configured radar radius, not the current zoom: one fetch covers
    // every zoom level, and DrawAirports culls to the visible scan box.
    req->lat = lat;
    req->lon = lon;
    req->rangeKm = rangeKmCfg;
    if (xQueueSend(fetchRequestQueue, &req, 0) == pdTRUE)
        fetchInFlight = true;
    else
        delete req;
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
            Serial.printf("[cloud] airports: %u within %d km\n",
                          (unsigned)cloudAirports.size(), (int)lround(rangeKmCfg));
        } else {
            // Retry in 15 min; the baked majors table keeps serving meanwhile.
            lastCloudAirportsFetch = millis() - (24UL * 60UL * 60UL * 1000UL) + (15UL * 60UL * 1000UL);
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

    if (res->ok) {
        const unsigned long now = millis();

        // Staleness anchor for the indicator: when this merge happened, plus how
        // old the server said the snapshot already was (cloud tiles served
        // stale-while-revalidate keep their original t; other sources have no
        // server-side lag to account for).
        lastGoodDataMs = now;
        dataLagAtMergeMs = 0;
#ifdef FEATURE_CLOUD_FEED
        if (res->dataEpoch > 0) {
            const time_t nowEpoch = time(nullptr);
            if (nowEpoch > 1600000000 && (long)nowEpoch > res->dataEpoch)
                dataLagAtMergeMs = (unsigned long)((long)nowEpoch - res->dataEpoch) * 1000UL;
        }
#endif

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
            auto it = trackedAircraft.find(ac.icao24);
            if (it == trackedAircraft.end()) {
                auto emplaced = trackedAircraft.emplace(ac.icao24, TrackedAircraft{ ac, now });
                // a fresh contact entered range: bump the odometer and log its
                // origin country (the one logbook field the feed gives us directly).
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
            return;
        }
        ExitDetail(); // selected aircraft left the feed (idempotent across band passes)
    }

    switch (screen) {
        case Screen::List:  DrawList(backbuffer);  break;
        case Screen::Stats: DrawStats(backbuffer); break;
        case Screen::Radar:
        default:
            // at solar night with an empty sky, the radar face becomes a clock
            // (opt-in) -- the device stays useful instead of showing a dead scope
            if (NightClockActive()) DrawNightClock(backbuffer);
            else                    DrawRadar(backbuffer, firstPass);
            break;
    }
    DrawScreenIndicator(backbuffer);
    DrawClock(backbuffer);
    DrawVisualAlert(backbuffer); // military/emergency ring pulse / flash, over any screen
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
    DrawStaleIndicator(backbuffer); // quiet amber tag when the picture is dead-reckoning on old data

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
            const uint32_t markerColor = scaleColor(
                displayAltColor ? altitudeColor(tracked.state.baroAltitude)
                                : lgfx::color888(0, 255, 0),
                blip);

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

        // fresh logbook catch: a gold "NEW" flag for a never-before-seen type/airline
        if (logbookEnabled && tracked.freshCatch) {
            backbuffer.setTextSize(1);
            backbuffer.setTextColor(lgfx::color888(255, 215, 0));
            backbuffer.drawString("NEW", x + 11, y + 6);
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
        if (rangeUnit == "mi") dist /= 1.609344f;
        const String distStr = String(dist, dist < 10.0f ? 1 : 0) + rangeUnit;

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
    const int clockTop = SCREEN_SIZE - 30; // matches DrawClock's y
    backbuffer.setTextColor(lgfx::color888(0, 200, 0));
    // Space-guarded: a line that would reach the clock row is dropped, so the
    // block order below is also the priority order on the small 240 px panels.
    auto line = [&](const String& s) {
        if (y + lh > clockTop) return;
        centered(s, y);
        y += lh;
    };

    line(String(count) + " aircraft");
    if (count > 0) {
        line("High " + label(highIcao) + " " + String(lroundf(maxAlt * METRES_TO_FEET)) + "ft");
        line("Fast " + label(fastIcao) + " " + String(lroundf(maxVel * MS_TO_KNOTS)) + "kt");
        float distance = sqrtf(minD2) * 111.0f;
        if (rangeUnit == "mi") distance /= 1.609344f;
        line("Near " + label(nearIcao) + " " + String(distance, distance < 10.0f ? 1 : 0) + rangeUnit);
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
        line(String(logbook.TypeCount()) + " types  " + String(logbook.OperatorCount()) + " airlines");
        line(String(logbook.CountryCount()) + " countries  " + String(logbook.AirportCount()) + " airports");
        line(String(logbook.Contacts()) + " seen");

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
                if (rangeUnit == "mi") d /= 1.609344f;
                best += " " + String(d, d < 10.0f ? 1 : 0) + rangeUnit;
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

    // THIS DEVICE -- the config page is at http://<name>.local, so a user who forgot the name
    // can swipe here to read it (the IP is an mDNS fallback). Drawn last, and only as far as it
    // fits above the clock row, so it never collides on the small round C3 screen: the host line
    // is the one you type, so it gets priority; the IP follows only when there's vertical room.
    y += 6;
    if (y + lh <= clockTop) {
        backbuffer.setTextColor(lgfx::color888(0, 255, 0));
        line(DeviceIdentity::Name() + ".local");
    }
    if (y + lh <= clockTop) {
        backbuffer.setTextColor(lgfx::color888(0, 200, 0));
        line(WiFi.localIP().toString());
    }
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
    // The /v1/airports long tail supersedes the baked majors once loaded. At
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
    bool milActive = false, emgActive = false;
    for (auto& [icao, t] : trackedAircraft) {
        if (t.state.onGround) continue;

        // squawks can turn emergency mid-track, so the edge is per-contact and
        // fires at most once (a cleared-then-reset squawk doesn't re-burst)
        if (isEmergencySquawk(t.state.squawk)) {
            if (emgVisual != VisualAlertMode::Off) emgActive = true;
            if (!t.emgFlashFired) {
                t.emgFlashFired = true;
                if (emgVisual == VisualAlertMode::Flash && initialSyncDone) {
                    flashBurstUntilMs = now + FLASH_BURST_MS;
                    flashBurstColor = EMG_RED;
                }
                // the emergency tone fires even for a contact already squawking at
                // boot -- an active emergency is worth hearing about (unlike a flash)
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

    if constexpr (variant::SERIALIZE_TOUCH_BUS) {
        // Single-core (C3): serialize touch I2C with network TLS via the shared HTTP request
        // lock. A touch I2C transfer that overlaps a TLS handshake wedges the CST816 off the
        // bus -- the v4 regression (touch worked in v2, which had no concurrent network task).
        // The HTTP client holds this mutex for the full duration of every GET/POST on any task,
        // so taking it here guarantees no I2C transfer runs concurrently with a request.
        //
        // We gate this way ONLY on the radar view. While a detail card is open the radar isn't
        // shown, the card's own enrichment (metadata/route/photo) holds the bus for several
        // seconds, and gating would make the close tap wait that whole time (up to ~30 s when a
        // lookup times out). So on the card view we poll every loop regardless -- closing stays
        // instant. The brief overlap risk there is acceptable: the card view has no sweep to
        // protect, the enrichment is bounded and one-shot, and the controller wakes on the next
        // touch. The earlier fetchInFlight/enrichInFlight flags did NOT work for this -- they
        // don't actually span the on-task TLS window; the mutex does.
        const bool gateOnBus = !inDetail;
        const bool gotBus = http.TryAcquireBus();
        if (gateOnBus && !gotBus)
            return; // radar view: a request is mid-flight, skip this poll to avoid the wedge

        touched = tft.getTouch(&tx, &ty);

        // Touch supervisor: any I2C it issues (probe / recovery re-init) must stay
        // serialized against TLS exactly like the poll above, so it may only act
        // while we hold the bus. On the card view (polling bus-less) it just notes
        // state and defers its probe to the next held window.
        if constexpr (variant::TOUCH_WATCHDOG)
            TouchWatchdog::OnPoll(tft, touched, gotBus);

        if (gotBus)
            http.ReleaseBus();
    } else {
        // Dual-core (S3): touch sits on its own I2C bus and the network runs over WiFi on a
        // separate core, so there is no touch/TLS wedge to prevent. Gating the poll on the HTTP
        // mutex here would silently drop taps whenever always-on enrichment held the bus -- which
        // it does constantly -- so a blip tap could take many tries to land. Poll every loop.
        touched = tft.getTouch(&tx, &ty);
        if constexpr (variant::TOUCH_WATCHDOG)
            TouchWatchdog::OnPoll(tft, touched, true); // no bus serialization on these boards
    }

#ifdef BISECT_TEST
    // Bisection storm: the hardware poll above ran for real (bus traffic + watchdog
    // feed -- nobody physically touches the bench unit), and gesture classification
    // consumes the synthetic stream instead, driving the same pipeline below.
    (void)tx; (void)ty;
    bool sTouched; int sx, sy;
    BisectHarness::NextTouchSample(sTouched, sx, sy);
    ProcessTouchSample(sTouched, sx, sy);
#elif defined(SOAK_TEST)
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
    } else if (wasTouched) {
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

void AircraftManager::ExitDetail()
{
    inDetail = false;
    detailPage = 0;
    // Reclaim the ~15 KB photo sprite. Holding it allocated is what tips the heap
    // below what an adsbdb/photo TLS handshake needs (see ENRICH_TLS_HEAP_FLOOR);
    // it's lazily recreated the next time a photo decodes. Clear photoIcao so the
    // photo is re-fetched if the same aircraft is opened again.
    if (photoSprite.getBuffer() != nullptr)
        photoSprite.deleteSprite();
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

    // horizontal swipe cycles the top-level screens (left = next, right = prev)
    if (swipe == Swipe::Left)  screen = (Screen)(((int)screen + 1) % 3);
    if (swipe == Swipe::Right) screen = (Screen)(((int)screen + 2) % 3);
}

void AircraftManager::ProcessDetailLookups()
{
    if constexpr (kNoNet) {
        // Bisection: no enrichment exists. Resolve the card's photo state right away
        // (silhouette, exactly like cloud mode) so it renders its final layout under
        // the storm instead of a perpetual "Loading photo...".
        if (inDetail && photoIcao != selectedIcao) {
            photoIcao = selectedIcao;
            photoReady = false;
            photoResolved = true;
        }
        return;
    }

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

    // same TLS-heap guard as the radar path: a handshake with too little contiguous
    // heap only fails and churns, so defer the detail lookups until heap recovers.
    if (ESP.getMaxAllocHeap() < ENRICH_TLS_HEAP_FLOOR)
        return;

#ifdef FEATURE_CLOUD_FEED
    if (useCloudSource) {
        // Cloud detail path: ONE pre-joined /v1/enrich GET covers what used to be
        // two adsbdb lookups (metadata + route), and (when the proxy's stock
        // library has an image for this hex/type) a licensed photo path in `p`.
        // Step 1 -- metadata/route/photo-path via the single enrich GET.
        if (tracked.metadataState == TrackedAircraft::MetadataState::NotFetched) {
            if (millis() < tracked.metadataRetryAfter)
                return;
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
        // authenticated with the cloud key (the /v1/photo route requires it).
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
        tracked.metadataState = TrackedAircraft::MetadataState::Fetching;
        RequestMetadata(selectedIcao);
        return;
    }
    if (tracked.metadataState == TrackedAircraft::MetadataState::Fetching)
        return;

    // 2. route, once per callsign. A transient failure leaves routeCallsign unchanged
    //    (so it retries), but behind the same 30 s cooldown as metadata -- otherwise
    //    the route retries back-to-back every frame while adsbdb is unreachable.
    String callsign = tracked.state.callsign;
    callsign.trim();
    if (!callsign.isEmpty() && tracked.routeCallsign != callsign) {
        if (millis() < tracked.routeRetryAfter)
            return;
        RequestRoute(selectedIcao, callsign);
        return;
    }

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

    if constexpr (kNoNet)
        return; // bisection: no enrichment task; every request path is gated off too

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
        switch (req->kind) {
            case EnrichKind::Metadata: res = fetchAircraftMetadata(http, req->icao24); break;
            case EnrichKind::Route:    res = fetchRoute(http, req->callsign);          break;
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

            // hand the result back; the loop consumed the previous one before
            // requesting again, so the depth-1 queue always has room
            if (xQueueSend(enrichResultQueue, &res, 0) != pdTRUE)
                delete res;
        }

        delete req;
    }
}

void AircraftManager::RequestMetadata(const String& icao24)
{
    if (enqueueEnrich(enrichRequestQueue, new EnrichRequest{ EnrichKind::Metadata, icao24, "", "" }))
        enrichInFlight = true;
}

void AircraftManager::RequestRoute(const String& icao24, const String& callsign)
{
    if (enqueueEnrich(enrichRequestQueue, new EnrichRequest{ EnrichKind::Route, icao24, callsign, "" }))
        enrichInFlight = true;
}

void AircraftManager::RequestPhoto(const String& icao24, const String& url, const String& authKey)
{
    EnrichRequest* req = new EnrichRequest{ EnrichKind::Photo, icao24, "", url };
    req->cloudKey = authKey; // "" for the BYO adsbdb thumbnail; the proxy key in cloud mode
    if (enqueueEnrich(enrichRequestQueue, req))
        enrichInFlight = true;
}

#ifdef FEATURE_CLOUD_FEED
void AircraftManager::RequestCloudEnrich(const String& icao24, const String& callsign,
                                         float acLat, float acLon)
{
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
        if (newType || newOperator)
            tracked.freshCatch = true;
        // route endpoints feed the airports-seen lifelist (no NEW flag: the
        // catch mechanic stays about the aircraft, not its schedule)
        logbook.NoteAirport(tracked.routeOrigin);
        logbook.NoteAirport(tracked.routeDest);
        ConsiderAircraftOfDay(tracked, newType, newOperator);
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

    // The aircraft may have left range while the lookup was outstanding. Metadata
    // and route results target a map entry (gone -> nothing to apply); the photo is
    // keyed to photoIcao instead, since the sprite is shared, not per-entry.
    auto it = trackedAircraft.find(res->icao24);

    switch (res->kind) {
        case EnrichKind::Metadata:
            if (it != trackedAircraft.end()) {
                if (res->definitive) {
                    TrackedAircraft& t = it->second;
                    t.metadataState = TrackedAircraft::MetadataState::Fetched;
                    t.typeCode = res->typeCode;
                    t.typeName = res->typeName;
                    t.operatorName = res->operatorName;
                    t.registration = res->registration;
                    t.photoUrl = res->photoUrl;

                    // add the type / airline to the lifelist; a brand-new entry marks
                    // this a "fresh catch". On the loop -- the logbook is loop-owned.
                    if (logbookEnabled) {
                        const bool newType = logbook.NoteType(t.typeCode);
                        const bool newOperator = logbook.NoteOperator(t.operatorName);
                        if (newType || newOperator)
                            t.freshCatch = true;
                    }
                } else {
                    // transient network failure: allow a later retry, but not before
                    // a cooldown -- otherwise this same aircraft is re-picked every
                    // cycle and hammers the (often heap-starved) TLS path in a storm.
                    it->second.metadataState = TrackedAircraft::MetadataState::NotFetched;
                    it->second.metadataRetryAfter = millis() + res->retryCooldownMs;
                }
            }
            break;

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
                lbRank = res->lbRank;
                lbPoints = res->lbPoints;
                lbSeasonRank = res->lbSeasonRank;
                lbSeasonPoints = res->lbSeasonPoints;
                lbTotal = res->lbTotal;
                lbHaveStanding = true;
            }
            break;
#endif

        case EnrichKind::Route:
            if (it != trackedAircraft.end()) {
                if (res->definitive) {
                    TrackedAircraft& t = it->second;
                    t.routeCallsign = res->routeCallsign;
                    t.routeOrigin = res->routeOrigin;
                    t.routeDest = res->routeDest;
                    if (logbookEnabled) {
                        logbook.NoteAirport(t.routeOrigin);
                        logbook.NoteAirport(t.routeDest);
                    }
                } else {
                    // transient failure: leave routeCallsign unchanged so the detail
                    // path retries, but behind a cooldown (mirrors metadata) so it
                    // doesn't hammer adsbdb every frame during an outage.
                    constexpr unsigned long ROUTE_RETRY_COOLDOWN = 30000;
                    it->second.routeRetryAfter = millis() + ROUTE_RETRY_COOLDOWN;
                }
            }
            break;

        case EnrichKind::Photo:
            // decode here, on the loop, so the photo sprite is only ever touched by
            // one task. Only apply it if this photo is still wanted: ExitDetail()
            // clears photoIcao (and frees the sprite) when the card closes, so a
            // late-arriving result must not re-allocate the ~15 KB sprite behind it.
            if (photoIcao == res->icao24) {
                photoResolved = true; // this aircraft's photo attempt is done (decoded below, or not)
                if (res->photoFetched && res->photoBytes.length() > 0) {
                    if (photoSprite.getBuffer() == nullptr) {
                        // PSRAM boards keep the photo off the scarce internal heap (where WiFi/TLS
                        // and the JPEG decoder also live), so decoding a card doesn't fragment it.
                        if constexpr (!variant::BANDED_RENDER)
                            photoSprite.setPsram(true);
                        photoSprite.setColorDepth(8);
                        photoSprite.createSprite(PHOTO_W, PHOTO_H);
                    }
                    photoSprite.fillScreen(lgfx::color888(0, 0, 0));
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
    if constexpr (kNoNet)
        return; // bisection: no sockets, no alerts

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
// id + the opted-in spotter name. The full type-code list is the ONE list sent
// (rarity scoring needs it); airlines/countries/airports stay counts-only so
// those lists -- which would fingerprint the user's location -- never leave.
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
    JsonArray types = doc["typeCodes"].to<JsonArray>();
    for (const auto& kv : logbook.Types())
        types.add(kv.first);

    EnrichRequest* req = new EnrichRequest{};
    req->kind = EnrichKind::Leaderboard;
    req->url = cloudUrl + "/v1/leaderboard";
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

    backbuffer.fillScreen(lgfx::color888(0, 0, 0));
    // frame ring to match the round display
    backbuffer.drawCircle(cx - 1, cx - 1, SCREEN_SIZE_DIV_2 - 1, lgfx::color888(0, 200, 0));

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

    // The photo slot at the top of the card holds the real photo when one decoded, a generic
    // aircraft silhouette when adsbdb has none, or a "Loading..." note while it's still resolving --
    // so a photo-less card reads as designed, not broken. Only the explicit data page (tapped over
    // from a real photo) drops the slot for the full-height text layout. Photo page 0 keeps the
    // tighter text set; the silhouette/data pages show full telemetry (gated on showPhoto below).
    const bool hasPhoto = photoReady && photoIcao == selectedIcao && photoSprite.getBuffer() != nullptr;
    const bool showPhoto = hasPhoto && detailPage == 0;
    const bool photoSettled = photoIcao == selectedIcao && photoResolved; // we now know there's no photo
    const bool useSlot = showPhoto || !hasPhoto;                          // slot unless flipped to data page

    int y;
    backbuffer.setTextColor(lgfx::color888(0, 255, 0));
    if (useSlot) {
        if (showPhoto) {
            // blit onto the band sprite directly, shifting Y by the band top like BandCanvas does
            photoSprite.pushSprite(&backbuffer.sprite(), cx - PHOTO_W / 2, 30 - backbuffer.offsetY());
            if (tracked.photoRepresentative) {
                // Cloud stock photo is a generic type shot, not this exact
                // airframe -- label it honestly (a per-hex override stays
                // uncaptioned). Overlaid on the lower edge of the image.
                backbuffer.setTextSize(1);
                backbuffer.setTextColor(lgfx::color888(0, 230, 0));
                centered("representative photo", 30 + PHOTO_H - 12);
            }
        } else if (photoSettled) {
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

    // first-ever sighting of this type/airline
    if (logbookEnabled && tracked.freshCatch) {
        backbuffer.setTextColor(lgfx::color888(255, 215, 0));
        line("* NEW CATCH *");
        backbuffer.setTextColor(lgfx::color888(0, 200, 0));
    }

    // identity first (route + type + operator), shown in both layouts
    if (!tracked.routeOrigin.isEmpty() && !tracked.routeDest.isEmpty())
        line(tracked.routeOrigin + " -> " + tracked.routeDest);
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
        if (rangeUnit == "mi") distance /= 1.609344f;
        float bearing = degrees(atan2f(dLonKm, dLatKm));
        if (bearing < 0.0f) bearing += 360.0f;
        static const char* DIRS[8] = { "N", "NE", "E", "SE", "S", "SW", "W", "NW" };
        const char* dir = DIRS[((int)roundf(bearing / 45.0f)) % 8];
        line("Dist: " + String(distance, distance < 10.0f ? 1 : 0) + " " + rangeUnit + " " + dir);

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
    if constexpr (kNoNet)
        return; // bisection: no enrichment task, no lookups

#ifdef FEATURE_CLOUD_FEED
    if (useCloudSource) {
        // The /v1/config enrich level is the master switch in cloud mode. Off:
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
    if (ESP.getMaxAllocHeap() < ENRICH_TLS_HEAP_FLOOR)
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

    // Queue the first aircraft still awaiting a lookup, then stop for this tick.
    for (auto& [icao, tracked] : trackedAircraft) {
        if (tracked.metadataState != TrackedAircraft::MetadataState::NotFetched)
            continue;
        if (now < tracked.metadataRetryAfter)
            continue; // still in post-failure cooldown; skip so others get a turn

#ifdef FEATURE_CLOUD_FEED
        if (useCloudSource) {
            if (!CloudShouldBackgroundEnrich(tracked))
                continue;
            // A cache hit costs nothing -- apply it and keep scanning for one
            // that actually needs the network.
            if (const CloudFeed::Enrichment* cached = enrichCache.Find(icao)) {
                ApplyEnrichment(tracked, *cached);
                continue;
            }
            String callsign = tracked.state.callsign;
            callsign.trim();
            auto [acLat, acLon] = tracked.GetDisplayPosition();
            lastMetadataLookup = now;
            tracked.metadataState = TrackedAircraft::MetadataState::Fetching;
            RequestCloudEnrich(icao, callsign, acLat, acLon);
            return;
        }
#endif

        lastMetadataLookup = now;
        tracked.metadataState = TrackedAircraft::MetadataState::Fetching;
        RequestMetadata(icao);
        return;
    }
}
#ifdef BISECT_TEST
// ---- bisection-harness hooks (the harness itself is src/BisectHarness.cpp) ----

void AircraftManager::BisectApplyTestConfig()
{
    // Deterministic bench config, overriding whatever this device's NVS holds, so
    // every harness run tests the same thing. Maximum render load: every radar
    // adornment on, sweep + paint-and-fade on, full brightness, no auto-dim (there
    // is no NTP to drive the solar clock anyway).
    lat = 47.6;
    lon = -122.3; // fixed test home; the value is arbitrary, the determinism isn't
    constexpr double KM_PER_DEGREE = 111.0;
    constexpr double RANGE_KM = 100.0;
    radLat = RANGE_KM / KM_PER_DEGREE;
    radLon = RANGE_KM / (KM_PER_DEGREE * std::cos(radians(lat)));
    rangeRadiusDisplay = RANGE_KM;
    rangeUnit = "km";

    displayInfoText = displayTriangles = displayTrails = true;
    displayAltColor = displayHighlight = true;
    displaySweep = displayFade = true; // the sweep-render load under test
    showMilitary = showHelicopters = showSpecial = true;
    showOverhead = true;               // pulsing LOOK-UP rings on the near contacts = extra draw
    overheadKm = 8.0;
    alertMilitary = alertOverhead = false;
    watchlist.clear();
    ntfyTopic = "";
    logbookEnabled = false;
    mqttEnabled = false;
    metadataNeeded = false;
    useLocalSource = false;

    autoDim = false;
    configuredBrightness = 255;
    currentBrightness = 255;
    tft.setBrightness(255);

    fetchInterval = 0x7FFFFFFF; // belt + braces: RequestFetch is kNoNet-gated anyway
    lastFetch = millis();
}

bool AircraftManager::BisectInjectFleet(std::vector<Aircraft>&& fleet)
{
    if (fetchResultQueue == nullptr)
        return false;

    FetchResult* res = new FetchResult();
    res->ok = true;
    res->aircraft = std::move(fleet);
    if (xQueueSend(fetchResultQueue, &res, 0) != pdTRUE) {
        delete res; // a result is already pending (an open card pauses merging); drop this frame
        return false;
    }
    return true;
}
#endif // BISECT_TEST
