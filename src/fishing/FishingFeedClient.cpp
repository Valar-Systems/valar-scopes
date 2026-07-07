#include "FishingFeedClient.h"

#include <time.h>

using fishing::FishingFetchRequest;
using fishing::FishingFetchResult;
using fishing::FishingEndpoint;

namespace {

// Poll intervals (ms). Water data moves slowly; these keep the dials fresh without hammering free
// public services. Tide *predictions* barely change, so they poll least often.
constexpr uint32_t FLOW_MS      = 600000;   // 10 m: USGS river gauge/discharge/temp
constexpr uint32_t TIDES_MS     = 1800000;  // 30 m: CO-OPS hi/lo predictions
constexpr uint32_t TIDECURVE_MS = 3600000;  // 60 m: CO-OPS 6-min curve (predicted, changes slowly)
constexpr uint32_t WTEMP_MS     = 900000;   // 15 m: CO-OPS water temperature
constexpr uint32_t BUOY_MS      = 900000;   // 15 m: NDBC buoy observation
constexpr uint32_t WEATHER_MS   = 900000;   // 15 m: Open-Meteo weather + 24h barometer history
constexpr uint32_t MARINE_MS    = 1800000;  // 30 m: Open-Meteo Marine wave/SST (worldwide fallback)

constexpr uint32_t MAX_BACKOFF_MS = 1800000; // cap exponential backoff at 30 m

constexpr size_t TIDECURVE_RETAIN = 180;    // decimated 6-min curve points (bounds JSON + RAM)
constexpr size_t PRESS_HISTORY    = 24;     // 24 hourly pressure samples for the baro rate/sparkline

const char* USER_AGENT = "Blipscope-Fishing/1 (+https://github.com/Valar-Systems/valar-scopes)";

// Route through the optional aggregator: if a base URL is configured, swap the direct origin for it
// but keep the path+query, so a valar-fishing-feed can proxy/normalize. Empty base = hit the public
// API directly (the default -- the edition bakes in NO backend).
String ApplyBase(const String& base, const String& directUrl)
{
    if (base.isEmpty()) return directUrl;
    const int p = directUrl.indexOf("://");
    const int slash = (p >= 0) ? directUrl.indexOf('/', p + 3) : directUrl.indexOf('/');
    const String pathq = (slash >= 0) ? directUrl.substring(slash) : String("/");
    String b = base;
    while (b.endsWith("/")) b.remove(b.length() - 1);
    return b + pathq;
}

} // namespace

String FishingFeedClient::FirstToken(const String& s)
{
    int i = 0;
    const int n = s.length();
    while (i < n && (s[i] == ' ' || s[i] == ',' || s[i] == ';')) i++;
    const int start = i;
    while (i < n && s[i] != ' ' && s[i] != ',' && s[i] != ';') i++;
    String t = s.substring(start, i);
    t.trim();
    return t;
}

void FishingFeedClient::Begin()
{
    if (taskHandle != nullptr) return; // survives config reloads

    reqQueue = xQueueCreate(1, sizeof(FishingFetchRequest*));
    resQueue = xQueueCreate(1, sizeof(FishingFetchResult*));

    // Pinned to core 0 (the Wi-Fi core) at priority 1, like the radar/Space/Seismic fetch tasks, so
    // the blocking TLS work never competes with the loop/render task. 12 KB stack covers a TLS
    // handshake + JSON decode.
    xTaskCreatePinnedToCore(Trampoline, "fishing_fetch", 12288, this, 1, &taskHandle, 0);
}

void FishingFeedClient::Configure(const Config& newCfg)
{
    cfg = newCfg;

    // Drop any retained data for a family the new water type excludes. Config saves are hot-applied
    // without a reboot (main.cpp re-Initialise()s this same instance), and the result store otherwise
    // persists across a reconfigure -- so a narrowing (e.g. Both -> Fresh) could leave stale
    // opposite-family readings on the dials/alerts. Clearing here keeps the store consistent with the
    // BuildRequest gate. (Weather is shared, so it is left untouched.)
    const bool keepFresh = (cfg.waterType == FRESH || cfg.waterType == BOTH);
    const bool keepSalt  = (cfg.waterType == SALT  || cfg.waterType == BOTH);
    if (!keepFresh) gauge = fishing::RiverGauge();
    if (!keepSalt) { tide = fishing::TideState(); tideCurve.clear(); wtemp = fishing::WaterTemp(); buoy = fishing::BuoyObs(); }

    float sc = cfg.intervalScale;
    if (sc < 1.0f) sc = 1.0f;
    if (sc > 8.0f) sc = 8.0f;

    feeds[F_FLOW].intervalMs      = (uint32_t)(FLOW_MS * sc);
    feeds[F_TIDES].intervalMs     = (uint32_t)(TIDES_MS * sc);
    feeds[F_TIDECURVE].intervalMs = (uint32_t)(TIDECURVE_MS * sc);
    feeds[F_WTEMP].intervalMs     = (uint32_t)(WTEMP_MS * sc);
    feeds[F_BUOY].intervalMs      = (uint32_t)(BUOY_MS * sc);
    feeds[F_WEATHER].intervalMs   = (uint32_t)(WEATHER_MS * sc);
    feeds[F_MARINE].intervalMs    = (uint32_t)(MARINE_MS * sc);

    // Stage the first poll of each endpoint shortly after (re)config, fanned out by ~500 ms so they
    // don't all hit the single TLS client at once.
    const uint32_t now = millis();
    for (int i = 0; i < F_COUNT; ++i) {
        feeds[i].failCount = 0;
        feeds[i].nextDueMs = now + (uint32_t)i * 500 + 400;
        firstLogged[i] = false;
    }
}

void FishingFeedClient::Poll()
{
    // 1. apply a ready result (frees its heap) and clear the in-flight gate.
    FishingFetchResult* res = nullptr;
    if (xQueueReceive(resQueue, &res, 0) == pdTRUE && res != nullptr) {
        ApplyResult(*res);
        delete res;
        inFlight = false;
    }
    if (inFlight) return; // one fetch at a time over the shared TLS client

    // 2. dispatch the next due endpoint.
    const uint32_t now = millis();
    const int feedIdx = PickDueFeed(now);
    if (feedIdx < 0) return;

    FishingFetchRequest* req = new FishingFetchRequest();
    if (!BuildRequest(feedIdx, *req)) {
        // gated off for this water type / missing id -> re-arm and move on (no TLS opened).
        delete req;
        feeds[feedIdx].nextDueMs = now + feeds[feedIdx].intervalMs;
        return;
    }

    if (xQueueSend(reqQueue, &req, 0) == pdTRUE) {
        inFlight = true;
    } else {
        delete req; // queue unexpectedly full; try again next Poll()
    }
}

int FishingFeedClient::PickDueFeed(uint32_t now) const
{
    int best = -1;
    uint32_t bestDue = 0;
    for (int i = 0; i < F_COUNT; ++i) {
        if ((int32_t)(now - feeds[i].nextDueMs) >= 0) { // signed diff survives the ~49-day millis wrap
            if (best < 0 || (int32_t)(feeds[i].nextDueMs - bestDue) < 0) {
                best = i;
                bestDue = feeds[i].nextDueMs;
            }
        }
    }
    return best;
}

bool FishingFeedClient::BuildRequest(int feedIdx, FishingFetchRequest& req) const
{
    const bool wantFresh = (cfg.waterType == FRESH || cfg.waterType == BOTH);
    const bool wantSalt  = (cfg.waterType == SALT  || cfg.waterType == BOTH);
    const char* units = cfg.imperial ? "english" : "metric"; // NOAA CO-OPS unit system

    req.headers.push_back({"User-Agent", USER_AGENT});

    switch (feedIdx) {
        case F_FLOW: {
            if (!wantFresh) return false;
            const String site = FirstToken(cfg.usgsSite);
            if (site.isEmpty()) return false;
            req.endpoint = FishingEndpoint::Flow;
            const String u = "https://waterservices.usgs.gov/nwis/iv/?format=json&sites=" + site +
                             "&parameterCd=00065,00060,00010,63680&period=PT2H&siteStatus=all";
            req.url = ApplyBase(cfg.baseUrl, u);
            return true;
        }
        case F_TIDES: {
            if (!wantSalt) return false;
            const String st = FirstToken(cfg.noaaStation);
            if (st.isEmpty()) return false;
            req.endpoint = FishingEndpoint::Tides;
            // 48 h of hi/lo from today (so "next high" is found even late in the day). Falls back to
            // date=today before NTP is set.
            String dateArg;
            const time_t now = time(nullptr);
            if (now > 1600000000) {
                struct tm t; gmtime_r(&now, &t);
                char b[12]; strftime(b, sizeof(b), "%Y%m%d", &t);
                dateArg = String("&begin_date=") + b + "&range=48";
            } else {
                dateArg = "&date=today";
            }
            const String u = String("https://api.tidesandcurrents.noaa.gov/api/prod/datagetter?"
                             "product=predictions&application=Blipscope&datum=MLLW&interval=hilo&"
                             "units=") + units + "&time_zone=gmt&format=json&station=" + st + dateArg;
            req.url = ApplyBase(cfg.baseUrl, u);
            return true;
        }
        case F_TIDECURVE: {
            if (!wantSalt) return false;
            const String st = FirstToken(cfg.noaaStation);
            if (st.isEmpty()) return false;
            req.endpoint = FishingEndpoint::TideCurve;
            // Same predictions endpoint WITHOUT interval=hilo -> the 6-minute curve; range=30 h keeps
            // it small. Harmonic stations only; subordinate stations error and the screen interpolates.
            const String u = String("https://api.tidesandcurrents.noaa.gov/api/prod/datagetter?"
                             "product=predictions&application=Blipscope&datum=MLLW&"
                             "units=") + units + "&time_zone=gmt&format=json&range=30&station=" + st;
            req.url = ApplyBase(cfg.baseUrl, u);
            return true;
        }
        case F_WTEMP: {
            if (!wantSalt) return false;
            const String st = FirstToken(cfg.noaaStation);
            if (st.isEmpty()) return false;
            req.endpoint = FishingEndpoint::WaterTemp;
            const String u = String("https://api.tidesandcurrents.noaa.gov/api/prod/datagetter?"
                             "product=water_temperature&application=Blipscope&date=latest&"
                             "units=") + units + "&time_zone=gmt&format=json&station=" + st;
            req.url = ApplyBase(cfg.baseUrl, u);
            return true;
        }
        case F_BUOY: {
            if (!wantSalt) return false;
            const String b = FirstToken(cfg.buoyId);
            if (b.isEmpty()) return false;
            req.endpoint = FishingEndpoint::Buoy;
            req.isText = true; // NDBC realtime2 is fixed-width text, not JSON
            const String u = "https://www.ndbc.noaa.gov/data/realtime2/" + b + ".txt";
            req.url = ApplyBase(cfg.baseUrl, u);
            return true;
        }
        case F_WEATHER: {
            if (!cfg.hasLatLon) return false;
            req.endpoint = FishingEndpoint::Weather;
            String u = "https://api.open-meteo.com/v1/forecast?latitude=" + String(cfg.lat, 4) +
                       "&longitude=" + String(cfg.lon, 4) +
                       "&current=temperature_2m,wind_speed_10m,wind_direction_10m,precipitation,pressure_msl,surface_pressure"
                       "&hourly=pressure_msl&past_days=1&forecast_days=1&timezone=GMT";
            // Match display units so the current reading is already display-ready (pressure stays hPa).
            if (cfg.imperial) u += "&temperature_unit=fahrenheit&wind_speed_unit=mph&precipitation_unit=inch";
            req.url = ApplyBase(cfg.baseUrl, u);
            return true;
        }
        case F_MARINE: {
            if (!cfg.hasLatLon) return false;
            req.endpoint = FishingEndpoint::Marine;
            // Open-Meteo Marine is always SI (m / degC); converted for display. Worldwide, location-only.
            const String u = "https://marine-api.open-meteo.com/v1/marine?latitude=" + String(cfg.lat, 4) +
                             "&longitude=" + String(cfg.lon, 4) +
                             "&current=wave_height,sea_surface_temperature";
            req.url = ApplyBase(cfg.baseUrl, u);
            return true;
        }
        default:
            return false;
    }
}

int FishingFeedClient::FeedForEndpoint(FishingEndpoint e)
{
    switch (e) {
        case FishingEndpoint::Flow:      return F_FLOW;
        case FishingEndpoint::Tides:     return F_TIDES;
        case FishingEndpoint::TideCurve: return F_TIDECURVE;
        case FishingEndpoint::WaterTemp: return F_WTEMP;
        case FishingEndpoint::Buoy:      return F_BUOY;
        case FishingEndpoint::Weather:   return F_WEATHER;
        case FishingEndpoint::Marine:    return F_MARINE;
    }
    return F_FLOW;
}

void FishingFeedClient::ApplyResult(const FishingFetchResult& res)
{
    const int f = FeedForEndpoint(res.endpoint);
    const uint32_t now = millis();

    if (!res.ok) {
        // Exponential backoff; keep the previously-stored data so the dial doesn't blank.
        feeds[f].failCount++;
        const uint16_t shift = feeds[f].failCount > 5 ? 5 : feeds[f].failCount;
        uint32_t backoff = feeds[f].intervalMs << shift;
        if (backoff > MAX_BACKOFF_MS || backoff < feeds[f].intervalMs) backoff = MAX_BACKOFF_MS;
        if (backoff < feeds[f].intervalMs) backoff = feeds[f].intervalMs; // never poll a FAILING feed faster than a healthy one
        feeds[f].nextDueMs = now + backoff;
        return;
    }

    feeds[f].failCount = 0;
    feeds[f].nextDueMs = now + feeds[f].intervalMs;

    // A 2xx fetch that parsed to no data (e.g. CO-OPS returning {"error":...} for a station whose
    // sensor is momentarily offline) leaves the payload valid=false. Only overwrite the store on a
    // valid parse, so last-good data is retained and the dial doesn't blank. The transport succeeded,
    // so failCount/reschedule above stay as-is.
    switch (res.endpoint) {
        case FishingEndpoint::Flow:      if (res.gauge.valid) gauge = res.gauge; break;
        case FishingEndpoint::Tides:
            if (res.tide.valid) {
                tide = res.tide;
                if (tide.stationId.isEmpty()) tide.stationId = FirstToken(cfg.noaaStation);
            }
            break;
        case FishingEndpoint::TideCurve:
            // Empty is a valid "no harmonic curve here" (subordinate station) -> keep whatever the
            // parse produced so the screen falls back to hi/lo interpolation.
            tideCurve = res.tideCurve;
            break;
        case FishingEndpoint::WaterTemp:
            if (res.wtemp.valid) {
                wtemp = res.wtemp;
                if (wtemp.stationId.isEmpty()) wtemp.stationId = FirstToken(cfg.noaaStation);
            }
            break;
        case FishingEndpoint::Buoy:
            if (res.buoy.valid) {
                buoy = res.buoy;
                buoy.buoyId = FirstToken(cfg.buoyId);
            }
            break;
        case FishingEndpoint::Weather:   if (res.weather.valid) weather = res.weather; break;
        case FishingEndpoint::Marine:    if (res.marine.valid) marine = res.marine; break;
    }

    if (!firstLogged[f]) {
        firstLogged[f] = true;
        static const char* names[F_COUNT] = {"flow", "tides", "tidecurve", "wtemp", "buoy", "weather", "marine"};
        Serial.printf("[fishing] %s ok\n", names[f]);
    }
}

void FishingFeedClient::Trampoline(void* arg)
{
    static_cast<FishingFeedClient*>(arg)->RunWorker();
}

void FishingFeedClient::RunWorker()
{
    for (;;) {
        FishingFetchRequest* req = nullptr;
        if (xQueueReceive(reqQueue, &req, portMAX_DELAY) != pdTRUE || req == nullptr)
            continue;

        FishingFetchResult* res = new FishingFetchResult();
        res->endpoint = req->endpoint;
        Fetch(http, *req, *res);
        delete req;

        xQueueSend(resQueue, &res, portMAX_DELAY);
    }
}

void FishingFeedClient::Fetch(HttpRequestManager& http,
                              const FishingFetchRequest& req, FishingFetchResult& res)
{
    res.endpoint = req.endpoint;

    if (req.isText) {
        // NDBC fixed-width text: plain GET (the yielding body reader), then column-scan.
        const HttpResult r = http.Get(req.url, req.params, req.headers);
        if (!r.success || r.statusCode < 200 || r.statusCode >= 300) { res.ok = false; return; }
        fishing::ParseNdbcBuoy(r.response, res.buoy);
        res.ok = true;
        return;
    }

    JsonDocument doc;
    const HttpResult r = http.GetJson(req.url, doc, req.params, req.headers);
    if (!r.success || r.statusCode < 200 || r.statusCode >= 300) { res.ok = false; return; }

    JsonObjectConst root = doc.as<JsonObjectConst>();
    switch (req.endpoint) {
        case FishingEndpoint::Flow:      fishing::ParseUsgsFlow(root, res.gauge); break;
        case FishingEndpoint::Tides:     fishing::ParseCoopsTides(root, res.tide); break;
        case FishingEndpoint::TideCurve: fishing::ParseCoopsTideCurve(root, res.tideCurve, TIDECURVE_RETAIN); break;
        case FishingEndpoint::WaterTemp: fishing::ParseCoopsWaterTemp(root, res.wtemp); break;
        case FishingEndpoint::Weather:   fishing::ParseOpenMeteo(root, res.weather, PRESS_HISTORY); break;
        case FishingEndpoint::Marine:    fishing::ParseOpenMeteoMarine(root, res.marine); break;
        default: break;
    }
    res.ok = true; // a successful fetch that parsed to "no data" is still valid
}
