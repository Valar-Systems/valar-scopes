#ifdef FEATURE_CLOUD_FEED

#include "CloudFeed.h"

#include <time.h>

#include "OtaUpdater.h"       // FW_VERSION for the X-Blip-FW header
#include "variants/Variant.h" // variant::SLUG for the X-Blip-Model header

namespace CloudFeed {

namespace {

// Same conversions as the local-receiver parser (models/Aircraft.cpp): the wire
// carries aviation units (ft, kt, ft/min); the pipeline runs OpenSky SI.
constexpr float FEET_TO_METRES = 0.3048f;
constexpr float KNOTS_TO_MS    = 0.514444f;
constexpr float FTMIN_TO_MS    = 0.00508f;

// Wire sentinels (proxy/README.md "Wire schema"): no nulls on the wire, unknown
// values use these. Missing values map to the same defaults the OpenSky parser
// uses for nulls (0), so downstream behaviour is identical across sources.
constexpr long SENTINEL_ALT_FT    = -100000;
constexpr long SENTINEL_VRATE_FPM = -100000;

// NTP has set the clock (>~2020). Below this the device epoch is meaningless,
// so age-of-data math degrades gracefully to "fresh at arrival".
bool ClockSynced(time_t t) { return t > 1600000000; }

} // namespace

String NormalizeBaseUrl(String url)
{
    url.trim();
    if (url.isEmpty())
        return url;
    if (url.indexOf("://") < 0)
        url = "https://" + url; // the proxy is TLS-only; default the scheme accordingly
    while (url.endsWith("/"))
        url.remove(url.length() - 1);
    return url;
}

std::vector<std::pair<String, String>> Headers(const String& key, const String& otaMem)
{
    std::vector<std::pair<String, String>> h = {
        { "X-Blip-Key", key },
        { "X-Blip-Model", variant::SLUG },
        { "X-Blip-FW", String(FW_VERSION) },
    };
    // One-shot OTA memory report, present only on the first check-in after an
    // update attempt (see OtaUpdater.h / README "Telemetry"). It rides a request
    // the device was making anyway: no extra traffic, no endpoint of its own.
    if (!otaMem.isEmpty())
        h.push_back({ "X-Blip-OTA-Mem", otaMem });
    return h;
}

String BlipsUrl(const String& base)  { return base + "/v1/blips"; }
String EnrichUrl(const String& base, const String& icao24) { return base + "/v1/enrich/" + icao24; }
String ConfigUrl(const String& base) { return base + "/v1/config"; }

bool ParseBlips(JsonDocument& doc, std::vector<Aircraft>& out, long& dataEpoch)
{
    if (doc["v"].as<int>() != SCHEMA_V)
        return false; // wrong or missing schema version (or an error body)
    JsonArrayConst rows = doc["a"].as<JsonArrayConst>();
    if (rows.isNull())
        return false;

    // `t` is the UPSTREAM SNAPSHOT time, not the serve time: the proxy serves
    // cached/stale tiles with their original t, so (now - t) is the honest
    // data age that feeds the stale indicator and the dead-reckoner below.
    dataEpoch = doc["t"].as<long>();
    const time_t nowEpoch = time(nullptr);

    for (JsonVariantConst row : rows) {
        // Frozen field order: [hex, cs, lat*1e4, lon*1e4, altFt, gsKt, trackDeg,
        // vrateFpm, category, ageS]. Extra trailing fields (schema evolution)
        // are ignored; short rows are skipped rather than misread.
        if (row.size() < 10)
            continue;

        Aircraft a;

        // Match the local feed's address handling: strip readsb's '~' (TIS-B /
        // anonymised) prefix and lowercase, so watchlist/logbook/dedupe keys
        // behave identically across sources.
        String hex = row[0].as<String>();
        if (hex.startsWith("~")) hex = hex.substring(1);
        hex.toLowerCase();
        if (hex.isEmpty())
            continue;
        a.icao24 = hex;

        a.callsign = row[1].as<String>(); // already trimmed server-side
        a.latitude  = row[2].as<long>() / 1e4f;
        a.longitude = row[3].as<long>() / 1e4f;

        const long altFt = row[4].as<long>();
        a.baroAltitude = (altFt == SENTINEL_ALT_FT) ? 0.0f : altFt * FEET_TO_METRES;
        a.geoAltitude = a.baroAltitude; // the wire carries one altitude

        const int gsKt = row[5].as<int>();
        a.velocity = (gsKt < 0) ? 0.0f : gsKt * KNOTS_TO_MS;

        const int track = row[6].as<int>();
        a.trueTrack = (track < 0) ? 0.0f : (float)track;

        const long vrate = row[7].as<long>();
        a.verticalRate = (vrate == SENTINEL_VRATE_FPM) ? 0.0f : vrate * FTMIN_TO_MS;

        a.category = row[8].as<int>(); // already the OpenSky enum (schema contract)

        // Dead-reckoning stamps. The predictor extrapolates by
        // (lastContact - timePosition) + locally-elapsed, so:
        //   timePosition = when the position was actually measured (t - age)
        //   lastContact  = our receipt time -- this folds in BOTH the per-aircraft
        //                  age and any proxy cache staleness (now - t) in one go.
        const int ageS = row[9].as<int>();
        if (ageS >= 0 && dataEpoch > 0 && ClockSynced(nowEpoch)) {
            a.timePosition = dataEpoch - ageS;
            a.lastContact = (long)nowEpoch >= dataEpoch ? (long)nowEpoch : dataEpoch;
        } else {
            a.timePosition = 0; // no usable clock: predictor uses local elapsed only
            a.lastContact = 0;
        }

        a.onGround = false; // /v1/blips is airborne-only (schema contract)
        a.squawk = "";
        a.spi = false;
        a.positionSource = 0;
        a.originCountry = ""; // not on the wire; logbook counts countries only on other sources

        out.push_back(a);
    }
    return true;
}

bool ParseEnrich(JsonDocument& doc, Enrichment& out)
{
    if (doc["v"].as<int>() != SCHEMA_V)
        return false;
    out.registration = doc["r"].as<String>();
    out.typeCode     = doc["t"].as<String>();
    out.typeName     = doc["tn"].as<String>();
    out.operatorName = doc["op"].as<String>();
    out.routeOrigin  = doc["o"].as<String>();
    out.routeDest    = doc["d"].as<String>();
    return true;
}

bool ParseConfig(JsonDocument& doc, Config& out)
{
    if (doc["v"].as<int>() != SCHEMA_V)
        return false;

    // Every field falls back to the struct default when absent, so a config
    // from a newer proxy (extra keys) or an older one (missing keys) both apply
    // cleanly -- the schema evolution rule in practice.
    Config c;
    if (doc["pollActiveMs"].is<unsigned long>()) c.pollActiveMs = doc["pollActiveMs"].as<unsigned long>();
    if (doc["pollIdleMs"].is<unsigned long>())   c.pollIdleMs   = doc["pollIdleMs"].as<unsigned long>();
    if (doc["pollNightMs"].is<unsigned long>())  c.pollNightMs  = doc["pollNightMs"].as<unsigned long>();
    if (doc["idleAfterMs"].is<unsigned long>())  c.idleAfterMs  = doc["idleAfterMs"].as<unsigned long>();
    if (doc["staleFactor"].is<int>())            c.staleFactor  = doc["staleFactor"].as<int>();
    if (doc["rev"].is<int>())                    c.rev          = doc["rev"].as<int>();

    // minFw is an integer FW_VERSION encoded as a string ("5"); toInt()'s 0 on
    // garbage means "no gate", which is the safe direction.
    c.minFw = doc["minFw"].as<String>().toInt();

    const String enrich = doc["enrich"].as<String>();
    if (enrich == "off")            c.enrich = Config::Enrich::Off;
    else if (enrich == "watchlist") c.enrich = Config::Enrich::Watchlist;
    else                            c.enrich = Config::Enrich::Full;

    // Floors: a fat-fingered fleet config must not be able to DoS the proxy
    // (0 ms poll) or brick the cadence (0 ms idle window).
    if (c.pollActiveMs < 1000) c.pollActiveMs = 1000;
    if (c.pollIdleMs < c.pollActiveMs) c.pollIdleMs = c.pollActiveMs;
    if (c.pollNightMs < c.pollIdleMs) c.pollNightMs = c.pollIdleMs;
    if (c.idleAfterMs < 10000) c.idleAfterMs = 10000;
    if (c.staleFactor < 1) c.staleFactor = 1;

    out = c;
    return true;
}

const Enrichment* EnrichCache::Find(const String& icao24)
{
    for (Entry& e : entries) {
        if (!e.icao24.isEmpty() && e.icao24 == icao24) {
            e.lastUsed = millis();
            return &e.data;
        }
    }
    return nullptr;
}

void EnrichCache::Insert(const String& icao24, const Enrichment& data)
{
    Entry* slot = nullptr;
    for (Entry& e : entries) {
        if (e.icao24 == icao24) { slot = &e; break; }             // refresh in place
        if (e.icao24.isEmpty() && slot == nullptr) slot = &e;      // first free slot
    }
    if (slot == nullptr) { // full: evict the least recently used
        slot = &entries[0];
        for (Entry& e : entries)
            if (e.lastUsed < slot->lastUsed) slot = &e;
    }
    slot->icao24 = icao24;
    slot->data = data;
    slot->lastUsed = millis();
}

} // namespace CloudFeed

#endif // FEATURE_CLOUD_FEED
