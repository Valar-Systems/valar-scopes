#pragma once

#include <Arduino.h>
#include <vector>
#include <utility>
#include <ArduinoJson.h>

// Typed models for the Birding edition's data feeds plus the parsers that build them off the wire,
// and the request/result envelopes the background poller hands across the task boundary. Mirrors the
// Space/Seismic models' shape (feed-agnostic structs + free-function parsers + a fetch envelope).
//
// The source is the Cornell Lab **eBird API 2.0** (api.ebird.org/v2). It needs the user's OWN free
// API token (entered in config, sent as the X-eBirdApiToken header) -- never a baked-in key, the same
// BYO-credential pattern as the radar's OpenSky login. All fields are best-effort; the screens
// degrade gracefully when a poll hasn't landed or a field is missing.
namespace birding {

// ----------------------------------------------------------------------------- one sighting
struct Sighting {
    String speciesCode;        // eBird species code (e.g. "painbun"); the natural watch/dedupe key
    String comName;            // common name, e.g. "Painted Bunting"
    String sciName;            // scientific name
    String locId;              // eBird location id
    String locName;            // human location name
    String obsDt;              // raw "YYYY-MM-DD HH:MM" (observation-local time; shown compactly)
    int howMany = 0;           // count reported (0 = present but uncounted, shown as "X")
    double lat = 0.0;          // degrees
    double lon = 0.0;          // degrees (eBird calls it "lng")
    bool notable = false;      // came from the notable feed (rare/unusual for the area)

    // Filled in by the manager relative to the device location (not from the feed).
    double distanceKm = 0.0;
    double bearingDeg = 0.0;
};

// ------------------------------------------------------------------------------- one hotspot
struct Hotspot {
    String locId;
    String name;
    double lat = 0.0, lon = 0.0;
    int numSpecies = 0;        // numSpeciesAllTime
    double distanceKm = 0.0, bearingDeg = 0.0;
};

// -------------------------------------------------------------- poller request / result
enum class BirdEndpoint : uint8_t { Notable, Recent, Hotspots };

struct BirdFetchRequest {
    BirdEndpoint endpoint = BirdEndpoint::Notable;
    String url;
    std::vector<std::pair<String, String>> params;
    std::vector<std::pair<String, String>> headers;
};

struct BirdFetchResult {
    BirdEndpoint endpoint = BirdEndpoint::Notable;
    bool ok = false;
    std::vector<Sighting> sightings; // for Notable / Recent
    std::vector<Hotspot> hotspots;   // for Hotspots
};

// -------------------------------------------------------------------------------- parsers
// eBird obs endpoints return a bare JSON array of observation objects; `notable` tags each result.
bool ParseObs(JsonArrayConst root, std::vector<Sighting>& out, size_t cap, bool notable); // false = body was not an array
// eBird ref/hotspot/geo (fmt=json) returns a bare JSON array of hotspot objects.
bool ParseHotspots(JsonArrayConst root, std::vector<Hotspot>& out, size_t cap);

// ----------------------------------------------------------------------------- geo helpers
double DistanceKm(double lat1, double lon1, double lat2, double lon2);
double BearingDeg(double lat1, double lon1, double lat2, double lon2);

} // namespace birding
