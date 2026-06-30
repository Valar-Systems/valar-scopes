#pragma once

#include <Arduino.h>
#include <vector>
#include <utility>
#include <ArduinoJson.h>

// Typed models for Spacescope's data feeds plus the parsers that build them off the wire, and the
// request/result envelopes the background poller hands across the task boundary. Mirrors the EAM
// models' shape (feed-agnostic structs + free-function parsers + a fetch envelope).
//
// Stage 2 covers three keyless public sources: ISS position (wheretheiss.at), the next rocket
// launch (RocketLaunch.Live), and the planetary Kp index (NOAA SWPC). All fields are best-effort;
// the screens degrade gracefully when a poll hasn't landed or a field is missing.
namespace space {

// ------------------------------------------------------------------------------- ISS position
struct IssState {
    bool valid = false;
    double lat = 0.0;          // degrees
    double lon = 0.0;          // degrees
    double altKm = 0.0;        // km
    double velocityKmh = 0.0;  // km/h
    double footprintKm = 0.0;  // visible-circle diameter, km (optional)
    bool sunlit = true;        // visibility == "daylight" (vs "eclipsed")
    long timestamp = 0;        // unix seconds from the feed (optional)
};

// ----------------------------------------------------------------------------------- launches
struct Launch {
    String mission;            // payload / mission (best-effort; parsed from the name)
    String provider;           // e.g. "SpaceX"
    String vehicle;            // e.g. "Falcon 9"
    String pad;                // pad + location, e.g. "SLC-40, Cape Canaveral"
    long t0Epoch = 0;          // best-available launch instant (UTC unix), 0 if unknown
    bool precise = false;      // true when t0Epoch came from t0/win_open (a real time), not a coarse date
    String dateStr;            // coarse fallback for the display, e.g. "Jun 30"
};

// ------------------------------------------------------------------------------ space weather
struct SpaceWx {
    bool valid = false;
    float kp = -1.0f;          // latest planetary Kp 0..9 (-1 = unknown)
    long timeTagEpoch = 0;     // UTC unix of the latest sample (optional)
    std::vector<float> history; // recent Kp values, oldest..newest, bounded (for a sparkline)
};

// --------------------------------------------------------------------------- solar wind
struct SolarWind {
    bool valid = false;
    float speedKms = 0;     // bulk speed, km/s
    float densityPcc = 0;   // proton density, p/cm^3
    float bzNt = 0;         // IMF Bz, nT (negative = southward = aurora-driving)
    long timeEpoch = 0;
};

// ------------------------------------------------------------------------- NOAA scales (R/S/G)
struct NoaaScales {
    bool valid = false;
    int r = 0;  // radio blackout 0..5
    int s = 0;  // solar radiation 0..5
    int g = 0;  // geomagnetic storm 0..5
};

// --------------------------------------------------------------------- solar X-ray flares
struct Flare {
    bool valid = false;
    float fluxWm2 = 0;      // latest GOES long-band (0.1-0.8nm) X-ray flux, W/m^2
    float peakFluxWm2 = 0;  // peak over the fetched ~6h window
    long timeEpoch = 0;
};

// ----------------------------------------------------------------------- humans in space
struct Crew {
    bool valid = false;
    int number = 0;
    std::vector<std::pair<String, String>> people; // {craft, name}
};

// ------------------------------------------------------------- Deep Space Network (DSN Now)
// One active radio link: a dish currently talking to (down) or commanding (up) a spacecraft.
struct DsnLink {
    String dish;            // antenna, e.g. "DSS14"
    String spacecraft;      // spacecraft code, e.g. "VGR1" / "MRO"
    String band;            // "X" / "S" / "Ka"
    double dataRateBps = 0; // bits/sec
    bool up = false;        // true = uplink (to craft), false = downlink (to Earth)
};

struct DsnState {
    bool valid = false;     // a successful fetch+parse populated this (even with zero links)
    std::vector<DsnLink> links;
};

// ----------------------------------------------------------------- deep-space distances
struct DeepSpaceTarget {
    String name;            // friendly name, e.g. "Voyager 1" (set by the client, not the parser)
    bool valid = false;
    double distanceAu = 0;  // range from Earth, AU
    double speedKms = 0;    // |range-rate|, km/s
    bool receding = true;   // range-rate sign
};

// ----------------------------------------------------------------- poller request / result
enum class SpaceEndpoint : uint8_t { Iss, Launch, Kp, Dsn, DeepSpace, Flare, Humans, SolarWind, Scales };

// Loop -> worker: a single GET to perform, fully built on the loop task.
struct SpaceFetchRequest {
    SpaceEndpoint endpoint = SpaceEndpoint::Iss;
    String url;
    std::vector<std::pair<String, String>> params;
    std::vector<std::pair<String, String>> headers;
    int targetIdx = -1;     // DeepSpace round-robin: which target this request is for
};

// Worker -> loop: the parsed payload (only the field matching `endpoint` is populated). ok is the
// HTTP/parse-level outcome; an ok-but-empty result (e.g. no upcoming launch) is valid "no data".
struct SpaceFetchResult {
    SpaceEndpoint endpoint = SpaceEndpoint::Iss;
    bool ok = false;
    int targetIdx = -1;     // echoed from the request for DeepSpace
    IssState iss;
    std::vector<Launch> launches;
    SpaceWx wx;
    DsnState dsn;
    DeepSpaceTarget deepTarget;
    Flare flare;
    Crew crew;
    SolarWind solarWind;
    NoaaScales scales;
};

// -------------------------------------------------------------------------------- parsers
// Each takes an already-decoded ArduinoJson value and fills the typed struct, tolerating missing
// optional fields. Caps bound retained vectors so a hostile/huge response can't exhaust RAM.
bool ParseIss(JsonObjectConst root, IssState& out);                              // wheretheiss.at flat object
void ParseLaunches(JsonObjectConst root, std::vector<Launch>& out, size_t cap);  // reads root["result"]
bool ParseKp(JsonArrayConst root, SpaceWx& out, size_t historyCap);              // SWPC array of {time_tag,Kp} objects
void ParseDsn(const String& xml, DsnState& out, size_t cap);                     // eyes.nasa.gov DSN XML
// Parse the first $$SOE data line of a Horizons OBSERVER+QUANTITIES=20 result (delta AU, deldot km/s).
bool ParseHorizonsRange(const String& result, double& deltaAu, double& deldotKms);
bool ParseFlare(JsonArrayConst root, Flare& out);                               // SWPC GOES xrays-6-hour
bool ParseCrew(JsonObjectConst root, Crew& out, size_t cap);                    // corquaid people-in-space mirror

bool ParseSolarWind(JsonArrayConst root, SolarWind& out);                       // SWPC propagated-solar-wind
bool ParseNoaaScales(JsonObjectConst root, NoaaScales& out);                    // SWPC noaa-scales.json

// GOES long-band flux (W/m^2) -> NOAA class string, e.g. 1.95e-6 -> "C1.9", 2.4e-5 -> "M2.4".
String XrayClass(float fluxWm2);
// Centered-dipole geomagnetic latitude (deg) for a geographic lat/lon -- drives the aurora oval.
double GeomagLatitude(double latDeg, double lonDeg);
// Geomagnetic latitude of the visible auroral-oval equatorward edge for a given Kp (rough).
float AuroraOvalLat(float kp);

// Parse "YYYY-MM-DD(T| )hh:mm[:ss][.fff][Z|+oo:oo]" (treated as UTC) to a Unix epoch; 0 on failure.
long Iso8601ToEpoch(const String& s);

} // namespace space
