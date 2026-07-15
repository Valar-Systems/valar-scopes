#pragma once

// Blipscope Cloud: the device side of the proxy/ Worker (see proxy/README.md for
// the frozen v1 wire schema). Everything here is FEATURE_CLOUD_FEED-gated so
// non-cloud builds stay byte-identical. The proxy pre-joins and caches upstream
// data server-side, so the device makes ONE keep-alive HTTPS connection to ONE
// host and parses payloads measured in hundreds of bytes:
//   GET /v1/blips?lat&lon&r&limit  -> compact aircraft arrays + snapshot time t
//   GET /v1/enrich/{hex}?cs&lat&lon -> reg/type/operator/route in one response
//   GET /v1/config                  -> fleet tunables, resolved per X-Blip-Model
#ifdef FEATURE_CLOUD_FEED

#include <Arduino.h>
#include <ArduinoJson.h>
#include <utility>
#include <vector>

#include "models/Aircraft.h"

// Build-flag defaults; both runtime-overridable on the config page ("cloud-url",
// "cloud-key"). The key is a per-build shared secret -- never committed, never
// logged; supplied by the release pipeline or pasted into web config for bench.
#ifndef CLOUD_FEED_BASE
#define CLOUD_FEED_BASE ""
#endif
#ifndef CLOUD_FEED_KEY
#define CLOUD_FEED_KEY ""
#endif

namespace CloudFeed {

// Wire schema version. The firmware asserts this on every parse (the evolution
// rule: unknown trailing array fields / JSON keys are ignored; a breaking change
// bumps v and this constant together).
constexpr int SCHEMA_V = 1;

// Fleet tunables from /v1/config, already resolved server-side for this model.
// The defaults here are the pre-first-fetch fallback and match the server's
// baked "default" tier.
struct Config {
    unsigned long pollActiveMs = 5000;   // feed cadence while the user is around
    unsigned long pollIdleMs   = 15000;  // after idleAfterMs without a touch
    unsigned long pollNightMs  = 60000;  // while the solar clock says night
    unsigned long idleAfterMs  = 600000; // touch-to-idle window
    int staleFactor = 3;                 // stale indicator past staleFactor * interval
    int minFw = 0;                       // newer than FW_VERSION -> trigger the OTA check
    int rev = 0;                         // config revision, logged for fleet ops
    enum class Enrich : uint8_t { Off, Watchlist, Full };
    Enrich enrich = Enrich::Full;        // background-enrichment level
};

// One aircraft's enrichment as served by /v1/enrich. Empty string = unknown.
struct Enrichment {
    String registration;
    String typeCode;
    String typeName;
    String operatorName;
    String routeOrigin;
    String routeDest;

    // False when every field is empty -- which is either a genuinely unknown
    // aircraft or the proxy still warming its caches; the caller retries a
    // bounded number of times to tell them apart.
    bool AnyField() const {
        return !(registration.isEmpty() && typeCode.isEmpty() && typeName.isEmpty() &&
                 operatorName.isEmpty() && routeOrigin.isEmpty() && routeDest.isEmpty());
    }
};

// Trim + default the scheme + strip trailing slashes, mirroring the local-URL
// normalizer's tolerance for what users paste.
String NormalizeBaseUrl(String url);

// The device headers every proxy request carries: X-Blip-Key (auth), X-Blip-Model
// (variant::SLUG -- the server resolves per-model tunables from it), X-Blip-FW.
// otaMem: the one-shot X-Blip-OTA-Mem value (OtaUpdater.h's TakeOtaMemReport),
// added only when non-empty -- the first check-in after an update attempt.
std::vector<std::pair<String, String>> Headers(const String& key, const String& otaMem = "");

String BlipsUrl(const String& base);
String EnrichUrl(const String& base, const String& icao24);
String ConfigUrl(const String& base);

// Parsers for the three payloads. Each returns false on a schema-version
// mismatch or a shape that isn't the endpoint's (e.g. an error body).
bool ParseBlips(JsonDocument& doc, std::vector<Aircraft>& out, long& dataEpoch);
bool ParseEnrich(JsonDocument& doc, Enrichment& out);
bool ParseConfig(JsonDocument& doc, Config& out);

// Tiny LRU of recent enrichments keyed by hex. TrackedAircraft carries its own
// enrichment while tracked, but is evicted (with it) when the aircraft leaves
// range -- this keeps the last few taps instant when the user re-inspects a
// contact that flapped out and back, without re-touching the network.
class EnrichCache {
public:
    // nullptr on miss; a hit refreshes the entry's recency.
    const Enrichment* Find(const String& icao24);
    void Insert(const String& icao24, const Enrichment& data);

private:
    static constexpr size_t CAPACITY = 8;
    struct Entry {
        String icao24;            // empty = free slot
        Enrichment data;
        unsigned long lastUsed = 0;
    };
    Entry entries[CAPACITY];
};

} // namespace CloudFeed

#endif // FEATURE_CLOUD_FEED
