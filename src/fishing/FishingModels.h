#pragma once

#include <Arduino.h>
#include <vector>
#include <utility>
#include <math.h>
#include <ArduinoJson.h>

// Typed models for the Fishing edition (product "Reelscope") data feeds plus the parsers that build
// them off the wire, and the request/result envelopes the background poller hands across the task
// boundary. Mirrors the Seismic/Birding models' shape (feed-agnostic structs + free-function parsers
// + a fetch envelope), but spans BOTH water types:
//   - freshwater: USGS Water Services instantaneous-values API (waterservices.usgs.gov/nwis/iv/)
//   - saltwater:  NOAA CO-OPS tides & currents (api.tidesandcurrents.noaa.gov) + NDBC buoys
//   - shared:     basic weather (keyless Open-Meteo); sun/moon/solunar are computed on-device
//                 (see Solunar.cpp) rather than fetched.
// All fields are best-effort; the screens degrade gracefully when a poll hasn't landed or a site
// doesn't report a given parameter. Values are normalised to US-imperial units at parse time.
namespace fishing {

// ---------------------------------------------------------------- unit helpers (parse-time)
inline float CtoF(float c)   { return c * 9.0f / 5.0f + 32.0f; }
inline float MtoFt(float m)  { return m * 3.28084f; }

// ------------------------------------------------------------------ freshwater (USGS /iv/)
// One river site's most-recent instantaneous values. USGS parameter codes: 00065 gauge height (ft),
// 00060 discharge (ft3/s "CFS"), 00010 water temp (C -> F here), 63680 turbidity (FNU).
struct RiverGauge {
    bool  valid = false;
    float gaugeHeightFt = NAN;
    float dischargeCfs  = NAN;
    float waterTempF    = NAN;
    float turbidityFnu  = NAN;
    long  timeEpoch     = 0;   // most-recent sample time (Unix s)
    int   flowTrend     = 0;   // -1 falling / 0 steady / +1 rising, from the discharge series
    String siteId;
    String siteName;
};

// ------------------------------------------------------------- saltwater (NOAA CO-OPS)
struct TideEvent {
    long  timeEpoch = 0;
    float heightFt  = 0.0f;
    char  type      = 'H';     // 'H' high / 'L' low
};

// Upcoming hi/lo predictions for one station, chronological.
struct TideState {
    bool valid = false;
    std::vector<TideEvent> events;
    String stationId;
    String stationName;
};

// CO-OPS water_temperature (a separate product; many tide stations lack the sensor).
struct WaterTemp {
    bool  valid = false;
    float tempF = NAN;
    long  timeEpoch = 0;
    String stationId;
};

// ------------------------------------------------------------- saltwater (NDBC buoy, text)
struct BuoyObs {
    bool  valid = false;
    float waveHeightFt    = NAN; // WVHT (m -> ft)
    float dominantPeriodS = NAN; // DPD (s)
    float pressureHpa     = NAN; // PRES (hPa)
    float waterTempF      = NAN; // WTMP (C -> F)
    long  timeEpoch       = 0;
    String buoyId;
};

// ------------------------------------------------- shared (on-device solunar / sun / moon)
// One feeding period. Majors (~2 h, at lunar transit/anti-transit) fish harder than minors (~1 h,
// at moonrise/moonset). Computed on-device from sun+moon position -- never fetched.
struct SolunarWindow {
    long startEpoch = 0;
    long endEpoch   = 0;
    bool major      = false;
};

struct SolunarDay {
    bool  valid        = false;
    long  sunriseEpoch = 0, sunsetEpoch = 0;
    long  moonriseEpoch = 0, moonsetEpoch = 0;
    float moonPhase    = 0.0f;   // 0..1 (0/1 = new, 0.5 = full)
    float moonIllum    = 0.0f;   // 0..1 illuminated fraction
    int   dayRating    = 0;      // 0..4 overall bite-day rating
    std::vector<SolunarWindow> windows; // chronological, majors + minors for the local day
};

// Compute sun/moon rise-set, moon phase, and the day's major/minor solunar feeding windows for a
// location, entirely on-device (no network). `nowUtc` seeds "today"; `tzOffsetSec` places the local
// day boundaries. Implemented in Solunar.cpp.
void ComputeSolunar(double lat, double lon, time_t nowUtc, long tzOffsetSec, SolunarDay& out);

// ----------------------------------------------------------------- shared (Open-Meteo)
struct WeatherObs {
    bool  valid = false;
    float airTempF    = NAN;   // display units (F when imperial, C when metric -- requested per units)
    float windMph     = NAN;   // display units (mph / km/h -- requested per units)
    int   windDirDeg  = -1;
    float precipIn    = NAN;   // display units (in / mm)
    float pressureHpa = NAN;   // always hPa (converted to inHg for display)
    int   pressureTrend = 0;   // -1 falling / 0 steady / +1 rising, from the hourly pressure series
    long  timeEpoch   = 0;
    // Recent sea-level pressure history (UTC epoch, hPa), oldest..newest -- drives the 24h sparkline
    // and the ~6h-ago rate. Bounded by the feed client's PRESS_HISTORY.
    std::vector<std::pair<long, float>> pressHist;
};

// ------------------------------------------------- shared (Open-Meteo Marine, worldwide fallback)
// Modeled sea state at the device location (no station needed). Always SI on the wire; converted on
// the screen. Each field is independently present (inland cells report neither).
struct MarineObs {
    bool  valid = false;
    bool  haveWave = false;  float waveHeightM = NAN;  // significant wave height, metres
    bool  haveSst  = false;  float seaTempC    = NAN;  // sea-surface temperature, degrees C
};

// ------------------------------------------------------------------ poller request / result
enum class FishingEndpoint : uint8_t { Flow, Tides, TideCurve, WaterTemp, Buoy, Weather, Marine };

// Loop -> worker: a single request to perform, fully built on the loop task. `isText` selects the
// plain-GET (NDBC fixed-width text) path over the streaming GetJson path.
struct FishingFetchRequest {
    FishingEndpoint endpoint = FishingEndpoint::Flow;
    String url;
    bool   isText = false;
    std::vector<std::pair<String, String>> params;
    std::vector<std::pair<String, String>> headers;
};

// Worker -> loop: the parsed payload for one endpoint. `ok` is the HTTP/parse-level outcome; only the
// member matching `endpoint` is meaningful. An ok-but-empty result is valid "no data".
struct FishingFetchResult {
    FishingEndpoint endpoint = FishingEndpoint::Flow;
    bool ok = false;
    RiverGauge gauge;
    TideState  tide;
    std::vector<std::pair<long, float>> tideCurve; // 6-min predicted-height curve (UTC epoch, ft/m)
    WaterTemp  wtemp;
    BuoyObs    buoy;
    WeatherObs weather;
    MarineObs  marine;
};

// -------------------------------------------------------------------------------- parsers
// Each reads the upstream shape (or a normalized aggregator shape) into `out`, tolerating missing
// fields. The JSON parsers take a JsonObjectConst (streamed + decoded by the shared HTTP client);
// the NDBC parser takes the raw fixed-width text body.
void ParseUsgsFlow(JsonObjectConst root, RiverGauge& out);
void ParseCoopsTides(JsonObjectConst root, TideState& out);
// CO-OPS 6-minute prediction curve (predictions WITHOUT interval=hilo), decimated to at most `cap`
// (epoch, heightFt) points so the JSON/RAM stay small. Harmonic (reference) stations only; a
// subordinate station errors and the Tide screen falls back to interpolating the hi/lo events.
void ParseCoopsTideCurve(JsonObjectConst root, std::vector<std::pair<long, float>>& out, size_t cap);
void ParseCoopsWaterTemp(JsonObjectConst root, WaterTemp& out);
void ParseNdbcBuoy(const String& body, BuoyObs& out);
// Open-Meteo forecast: current{...} + hourly{time,pressure_msl}. `histCap` bounds pressHist.
void ParseOpenMeteo(JsonObjectConst root, WeatherObs& out, size_t histCap);
// Open-Meteo Marine: current{wave_height, sea_surface_temperature} (falls back to hourly[0]).
void ParseOpenMeteoMarine(JsonObjectConst root, MarineObs& out);

// ----------------------------------------------------------------------------- geo helpers
// Great-circle distance (km) and initial bearing (deg, 0 = N, clockwise) from point 1 to 2.
double DistanceKm(double lat1, double lon1, double lat2, double lon2);
double BearingDeg(double lat1, double lon1, double lat2, double lon2);

} // namespace fishing
