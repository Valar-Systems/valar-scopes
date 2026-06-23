#pragma once

#include <map>
#include <vector>

#include "models/TrackedAircraft.h"
#include "ConfigurationWebServer.h"
#include "OpenSkyAuthTokenHandler.h"
#include "AircraftInfoFields.h"
#include "LGFX.h"

class AircraftManager
{
private:
    double lat = 0.0;
    double lon = 0.0;
    double radLat = 0.2; // latitude half-span of the scan box, in degrees
    double radLon = 0.2; // longitude half-span of the scan box, in degrees
    std::map<String, TrackedAircraft> trackedAircraft;

    bool displayInfoText = true;
    bool displayTriangles = true;
    bool displayTrails = true;

    // Tap-to-select view state. Radar is the normal scope; Detail shows a single
    // aircraft's info card. selectedIcao is the trackedAircraft key being shown.
    enum class ViewMode { Radar, Detail };
    ViewMode viewMode = ViewMode::Radar;
    String selectedIcao = "";
    bool wasTouched = false; // edge-detect so a held finger registers one tap

    // Parallel to AIRCRAFT_INFO_FIELDS: which info lines the user has enabled.
    // Populated once in Initialise() (config changes restart the device).
    std::vector<bool> infoFieldEnabled;

    // True when at least one enabled field needs the adsbdb lookup; lets us skip
    // all enrichment network traffic when the user shows none of those fields.
    bool metadataNeeded = false;
    unsigned long lastMetadataLookup = 0;

    unsigned long fetchInterval = 0;
    unsigned long lastFetch = 999999;

    ConfigurationWebServer& configServer;
    OpenSkyAuthTokenHandler& authHandler;
    HttpRequestManager& http;
    LGFX& tft;

    void DrawRadarCircles(LGFX_Sprite& backbuffer) const;
    std::pair<int, int> ProjectCoordinateToScreen(float predLat, float predLon) const;
    void DrawAircraftInfo(LGFX_Sprite& backbuffer, int x, int y, const TrackedAircraft& tracked) const;
    void DrawAircraftTriangle(LGFX_Sprite& backbuffer, int x, int y, const TrackedAircraft& tracked) const;
    void DrawAircraftTrail(LGFX_Sprite& backbuffer, const TrackedAircraft& tracked, int headX, int headY) const;
    void DrawDetailCard(LGFX_Sprite& backbuffer, const TrackedAircraft& tracked) const;

    void HandleTouch();           // poll the touchscreen and act on a new tap
    void HandleTap(int tx, int ty); // route a tap to selection / dismissal

    // Resolve type/operator/registration for tracked aircraft via adsbdb.com,
    // one at a time and throttled, so the blocking HTTP calls don't stall the
    // render loop more than the existing OpenSky fetch already does.
    void ProcessMetadataLookups();
    void LookupAircraftMetadata(const String& icao24, TrackedAircraft& tracked);

public:
    AircraftManager(ConfigurationWebServer& config, OpenSkyAuthTokenHandler& auth, HttpRequestManager& httpManager, LGFX& tftGfx)
        : configServer(config), authHandler(auth), http(httpManager), tft(tftGfx)
    {
    }
    ~AircraftManager() = default;

    void Initialise();
    void Update();
    void Draw(LGFX_Sprite& backbuffer);
    bool IsDetailView() const { return viewMode == ViewMode::Detail; }
};