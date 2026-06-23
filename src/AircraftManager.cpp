#include "AircraftManager.h"

#include <algorithm>
#include <cmath>

constexpr int SCREEN_SIZE = 240;
constexpr int SCREEN_SIZE_DIV_2 = (SCREEN_SIZE / 2);

#include <ArduinoJson.h>

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
    const double distance = configServer.GetStoredString("radius").toDouble();
    const bool inMiles = configServer.GetStoredString("radius-unit") == "mi";
    const double distanceKm = inMiles ? distance * 1.609344 : distance;

    constexpr double KM_PER_DEGREE = 111.0;
    constexpr double MAX_DEGREES = 2.0; // keep the OpenSky box within rate-limit area

    double cosLat = std::cos(radians(lat));
    if (cosLat < 0.01) cosLat = 0.01; // guard against div-by-zero near the poles

    constexpr double MIN_DEGREES = 0.001; // ~111 m floor; keeps the projection from dividing by zero

    radLat = std::min(std::max(distanceKm / KM_PER_DEGREE, MIN_DEGREES), MAX_DEGREES);
    radLon = std::min(std::max(distanceKm / (KM_PER_DEGREE * cosLat), MIN_DEGREES), MAX_DEGREES);

    // configuration
    const String renderText = configServer.GetStoredString("infotext");
    const String renderTris = configServer.GetStoredString("triangle");
    const String renderTrail = configServer.GetStoredString("trail");
    if (!renderText.isEmpty()) displayInfoText = renderText == "true" ? true : false;
    if (!renderTris.isEmpty()) displayTriangles = renderTris == "true" ? true : false;
    if (!renderTrail.isEmpty()) displayTrails = renderTrail == "true" ? true : false;

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

    // Force the next Update() to fetch immediately. On a config reload this means
    // a changed location/radius refreshes the radar right away rather than after
    // a full interval; subtracting the interval (unsigned millis() wraparound)
    // makes the next "now - lastFetch >= fetchInterval" check true at any uptime.
    lastFetch = millis() - fetchInterval;
}

void AircraftManager::Update()
{
    unsigned long now = millis();

    // enrich a tracked aircraft with adsbdb metadata (throttled internally)
    ProcessMetadataLookups();

    // fetch cycle
    if (now - lastFetch >= fetchInterval) {
        lastFetch = now;

        // auth
        const String token = authHandler.GetValidToken(
            configServer.GetStoredString("opensky-id"),
            configServer.GetStoredString("opensky-secret")
        );

        std::vector<std::pair<String, String>> headers = {};
        if (!token.isEmpty()) headers.push_back({ "Authorization", "Bearer " + token });

        // request
        HttpResult result = http.Get(
            "https://opensky-network.org/api/states/all",
            {
              // 6 decimals (~0.1 m): String(double) defaults to only 2, which would
              // quantize small km/mi radii into a coarse ~1 km box or collapse it
              {"lamin", String(lat - radLat, 6)},
              {"lamax", String(lat + radLat, 6)},
              {"lomin", String(lon - radLon, 6)},
              {"lomax", String(lon + radLon, 6)},
              // category (state vector index 17) is omitted from the default
              // response; without extended=1 the array stops at index 16 and the
              // Category info line is always blank
              {"extended", "1"}
            },
            headers
        );

        // If request failed, skip this update
        if (!result.success) {
            Serial.print("[WARN] OpenSky API request failed: ");
            Serial.println(result.errorMessage);
            return;
        }

        // track
        JsonDocument doc;
        deserializeJson(doc, result.response);
        auto aircraft = JsonParser::ParseArray<Aircraft>(doc["states"]);
        now = millis(); // override with post-parse timestamp

        for (auto& ac : aircraft) {
            auto it = trackedAircraft.find(ac.icao24);
            if (it == trackedAircraft.end())
                trackedAircraft.emplace(ac.icao24, TrackedAircraft{ ac, now });
            else
                it->second.Update(ac, now);
        }

        // remove any planes that disappeared from the feed
        for (auto it = trackedAircraft.begin(); it != trackedAircraft.end(); ) {
            bool aircraftPresent = std::any_of(aircraft.begin(), aircraft.end(), [&](const Aircraft& ac) { return ac.icao24 == it->first; });
            if (!aircraftPresent)
                it = trackedAircraft.erase(it);
            else
                ++it;
        }
    }
}

void AircraftManager::Draw(LGFX_Sprite& backbuffer)
{
    DrawRadarCircles(backbuffer);

    for (auto& [icao, tracked] : trackedAircraft) {
        if (tracked.state.onGround) continue;

        tracked.Tick();

        if (displayTrails)
            tracked.SampleTrail();

        auto [predLat, predLon] = tracked.GetDisplayPosition();
        auto [x, y] = ProjectCoordinateToScreen(predLat, predLon);

        // draw the trail first so the marker and label sit on top of it
        if (displayTrails)
            DrawAircraftTrail(backbuffer, tracked, x, y);

        if (displayInfoText)
            DrawAircraftInfo(backbuffer, x, y, tracked);

        if (displayTriangles)
            DrawAircraftTriangle(backbuffer, x, y, tracked);
        else
            backbuffer.fillCircle(x, y, 3, lgfx::color888(0, 255, 0));
    }
}

void AircraftManager::DrawRadarCircles(LGFX_Sprite& backbuffer) const
{
    constexpr int CENTRE = SCREEN_SIZE_DIV_2 - 1;
    constexpr int OUTER = SCREEN_SIZE_DIV_2 - 1;

    backbuffer.drawCircle(CENTRE, CENTRE, OUTER, lgfx::color888(0, 200, 0));
    backbuffer.drawCircle(CENTRE, CENTRE, (OUTER / 3) * 2, lgfx::color888(0, 64, 0));
    backbuffer.drawCircle(CENTRE, CENTRE, OUTER / 3, lgfx::color888(0, 32, 0));
}

std::pair<int, int> AircraftManager::ProjectCoordinateToScreen(float predLat, float predLon) const
{
    const float dLon = predLon - lon;
    const float dLat = predLat - lat;

    const float normLon = (dLon + radLon) / (2.0f * radLon);
    const float normLat = (dLat + radLat) / (2.0f * radLat);

    const int x = static_cast<int>(normLon * SCREEN_SIZE);
    const int y = static_cast<int>(SCREEN_SIZE - (normLat * SCREEN_SIZE));

    return { x, y };
}

void AircraftManager::DrawAircraftInfo(LGFX_Sprite& backbuffer, int x, int y, const TrackedAircraft& tracked) const
{
    const int lineHeight = tft.fontHeight() + 1;

    backbuffer.setTextSize(1);
    backbuffer.setTextColor(lgfx::color888(0, 128, 0));

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

void AircraftManager::DrawAircraftTriangle(LGFX_Sprite& backbuffer, int x, int y, const TrackedAircraft& tracked) const
{
    const float dx = std::sin(radians(tracked.state.trueTrack));
    const float dy = -std::cos(radians(tracked.state.trueTrack));
    const float px = -dy;
    const float py = dx;

    constexpr float TRIANGLE_LENGTH = 6.0f;
    constexpr float TRIANGLE_WIDTH = 3.0f;

    const float tipX = x + dx * TRIANGLE_LENGTH;
    const float tipY = y + dy * TRIANGLE_LENGTH;
    const float leftX = x - dx * TRIANGLE_LENGTH * 0.5f + px * TRIANGLE_WIDTH * 0.5f;
    const float leftY = y - dy * TRIANGLE_LENGTH * 0.5f + py * TRIANGLE_WIDTH * 0.5f;
    const float rightX = x - dx * TRIANGLE_LENGTH * 0.5f - px * TRIANGLE_WIDTH * 0.5f;
    const float rightY = y - dy * TRIANGLE_LENGTH * 0.5f - py * TRIANGLE_WIDTH * 0.5f;

    backbuffer.fillTriangle(tipX, tipY, leftX, leftY, rightX, rightY, lgfx::color888(0, 255, 0));
}

void AircraftManager::DrawAircraftTrail(LGFX_Sprite& backbuffer, const TrackedAircraft& tracked, int headX, int headY) const
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
            // doesn't vanish.
            const uint8_t brightness = 40 + static_cast<uint8_t>((180 * i) / n);
            backbuffer.drawLine(prevX, prevY, x, y, lgfx::color888(0, brightness, 0));
        }

        prevX = x;
        prevY = y;
        havePrev = true;
    }

    // connect the most recent sample to the live aircraft position so the trail
    // stays attached to the marker between samples
    backbuffer.drawLine(prevX, prevY, headX, headY, lgfx::color888(0, 220, 0));
}

void AircraftManager::ProcessMetadataLookups()
{
    if (!metadataNeeded)
        return;

    const unsigned long now = millis();
    // Space lookups out so a burst of new aircraft doesn't fire many blocking
    // HTTP calls back to back, and to stay friendly to the free adsbdb service.
    constexpr unsigned long METADATA_LOOKUP_INTERVAL = 1500;
    if (now - lastMetadataLookup < METADATA_LOOKUP_INTERVAL)
        return;

    // Resolve the first aircraft still awaiting a lookup, then stop for this tick.
    for (auto& [icao, tracked] : trackedAircraft) {
        if (tracked.metadataState != TrackedAircraft::MetadataState::NotFetched)
            continue;

        lastMetadataLookup = now;
        LookupAircraftMetadata(icao, tracked);
        return;
    }
}

void AircraftManager::LookupAircraftMetadata(const String& icao24, TrackedAircraft& tracked)
{
    const HttpResult result = http.Get("https://api.adsbdb.com/v0/aircraft/" + icao24);

    // A network-level failure (no HTTP response at all) is transient: leave the
    // state NotFetched so a later tick retries.
    if (!result.success) {
        Serial.printf("[adsbdb] lookup for %s failed: %s\n", icao24.c_str(), result.errorMessage.c_str());
        return;
    }

    // Any definitive HTTP response -- including 404 "unknown aircraft" -- is a
    // final answer: mark Fetched so this aircraft is never looked up again.
    tracked.metadataState = TrackedAircraft::MetadataState::Fetched;

    JsonDocument doc;
    if (deserializeJson(doc, result.response))
        return; // malformed body: treat as no data, but don't retry

    // adsbdb wraps the record in { "response": { "aircraft": {...} } }; when the
    // address is unknown it returns { "response": "unknown aircraft" }, leaving
    // this lookup null.
    JsonObject aircraft = doc["response"]["aircraft"];
    if (aircraft.isNull()) {
        Serial.printf("[adsbdb] %s -> unknown aircraft\n", icao24.c_str());
        return;
    }

    tracked.typeCode     = aircraft["icao_type"].isNull()        ? "" : aircraft["icao_type"].as<String>();
    tracked.operatorName = aircraft["registered_owner"].isNull() ? "" : aircraft["registered_owner"].as<String>();
    tracked.registration = aircraft["registration"].isNull()     ? "" : aircraft["registration"].as<String>();

    Serial.printf("[adsbdb] %s -> type=%s operator=%s reg=%s\n",
                  icao24.c_str(), tracked.typeCode.c_str(),
                  tracked.operatorName.c_str(), tracked.registration.c_str());
}