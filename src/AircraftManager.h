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
    double rangeRadiusDisplay = 0.0; // outer ring distance in the user's unit, for range labels
    String rangeUnit = "km";
    std::map<String, TrackedAircraft> trackedAircraft;

    bool displayInfoText = true;
    bool displayTriangles = true;
    bool displayTrails = true;
    bool displayAltColor = true;  // color aircraft markers by altitude band
    bool displayHighlight = true; // ring the nearest/highest/fastest contacts

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
    enum class Swipe { Up, Down, Left, Right };

    // Touch-controller liveness watchdog. The CST816S can drop off the I2C bus
    // (standby wedge / bus lockup), and the LovyanGFX driver never recovers it
    // -- it just keeps reporting "no touch" forever. We can't distinguish that
    // from a genuinely idle panel through getTouch(), so during quiet gaps we
    // periodically pulse the controller's reset and re-init it. See HandleTouch.
    unsigned long lastTouchActivityMs = 0; // last frame getTouch() actually read a touch
    unsigned long lastTouchReinitMs = 0;   // last time we reset + re-inited the controller

    // Decoded aircraft photo for the detail view. The sprite is created once and
    // reused; photoIcao/photoReady track which aircraft it currently holds.
    LGFX_Sprite photoSprite;
    String photoIcao = "";
    bool photoReady = false;

    // Parallel to AIRCRAFT_INFO_FIELDS: which info lines the user has enabled.
    // Populated once in Initialise() (config changes restart the device).
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

    unsigned long fetchInterval = 0;
    unsigned long lastFetch = 999999;

    // backlight + clock
    uint8_t configuredBrightness = 255; // day/base level from the slider
    uint8_t currentBrightness = 255;    // currently applied level (avoids redundant writes)
    bool autoDim = true;                // dim at night based on solar elevation
    long utcOffsetSec = 0;              // local time = UTC + this, for the clock
    unsigned long lastBrightnessCheck = 0;

    ConfigurationWebServer& configServer;
    OpenSkyAuthTokenHandler& authHandler;
    HttpRequestManager& http;
    LGFX& tft;

    void DrawRadarCircles(LGFX_Sprite& backbuffer) const;
    std::pair<int, int> ProjectCoordinateToScreen(float predLat, float predLon) const;
    void DrawAircraftInfo(LGFX_Sprite& backbuffer, int x, int y, const TrackedAircraft& tracked) const;
    void DrawAircraftTriangle(LGFX_Sprite& backbuffer, int x, int y, const TrackedAircraft& tracked, uint32_t color) const;
    void DrawAircraftTrail(LGFX_Sprite& backbuffer, const TrackedAircraft& tracked, int headX, int headY) const;
    void DrawEmergencyAlert(LGFX_Sprite& backbuffer, int x, int y, const TrackedAircraft& tracked) const;
    void DrawDetailCard(LGFX_Sprite& backbuffer, const TrackedAircraft& tracked);

    void DrawRadar(LGFX_Sprite& backbuffer);
    void DrawList(LGFX_Sprite& backbuffer);
    void DrawStats(LGFX_Sprite& backbuffer);
    void DrawScreenIndicator(LGFX_Sprite& backbuffer) const;
    void DrawClock(LGFX_Sprite& backbuffer) const;
    std::vector<String> SortedAircraftByDistance();

    void UpdateBrightness(); // apply solar day/night dimming (throttled)

    void HandleTouch();             // poll the touch panel, classify tap vs swipe
    void HandleTap(int tx, int ty); // route a tap to selection / dismissal
    void HandleSwipe(Swipe swipe);  // route a swipe to navigation / pin

    // Resolve the selected aircraft's metadata, route, then photo -- one blocking
    // lookup per frame so the detail card fills in progressively. Runs for the
    // inspected aircraft even when the radar's enrichment fields are disabled.
    void ProcessDetailLookups();
    void LookupRoute(const String& callsign, TrackedAircraft& tracked);
    void LoadPhoto(const String& url);

    // Resolve type/operator/registration for tracked aircraft via adsbdb.com,
    // one at a time and throttled, so the blocking HTTP calls don't stall the
    // render loop more than the existing OpenSky fetch already does.
    void ProcessMetadataLookups();
    void LookupAircraftMetadata(const String& icao24, TrackedAircraft& tracked);

    bool MatchesWatchlist(const TrackedAircraft& tracked) const;
    void ProcessWatchlistNotifications();
    void SendFlyoverNotification(const TrackedAircraft& tracked);

public:
    AircraftManager(ConfigurationWebServer& config, OpenSkyAuthTokenHandler& auth, HttpRequestManager& httpManager, LGFX& tftGfx)
        : configServer(config), authHandler(auth), http(httpManager), tft(tftGfx)
    {
    }
    ~AircraftManager() = default;

    void Initialise();
    void Update();
    void Draw(LGFX_Sprite& backbuffer);
    bool IsRadarView() const { return screen == Screen::Radar && !inDetail; }
};