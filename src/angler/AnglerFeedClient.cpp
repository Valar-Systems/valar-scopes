#include "AnglerFeedClient.h"

using angler::AnglerFetchRequest;
using angler::AnglerFetchResult;
using angler::AnglerEndpoint;

namespace {

// Default poll intervals (ms). Tides change on the astronomical clock (predictions are static once
// fetched); weather + marine update every ~15 min upstream.
constexpr uint32_t TIDES_MS     = 10800000;   // ~3 h: hi/lo predictions are astronomical, slow
constexpr uint32_t TIDECURVE_MS = 10800000;   // ~3 h: the 6-min curve is predicted, changes slowly
constexpr uint32_t WATERTEMP_MS = 1800000;    // ~30 m: NOAA water temp updates ~6-minutely upstream
constexpr uint32_t WEATHER_MS   = 900000;     // ~15 m: Open-Meteo current + pressure history
constexpr uint32_t MARINE_MS    = 1800000;    // ~30 m: waves / sea-surface temp move slowly
constexpr uint32_t BUOY_MS      = 600000;     // ~10 m: NDBC buoys report ~hourly; poll a bit faster

constexpr uint32_t MAX_BACKOFF_MS = 1200000;  // cap exponential backoff at 20 m

constexpr size_t TIDE_RETAIN     = 8;    // a couple of days of hi/lo (screen shows the next few)
constexpr size_t TIDECURVE_RETAIN = 180; // decimated 6-min curve points (bounds JSON + RAM)
constexpr size_t PRESS_HISTORY   = 24;   // last 24 hourly pressure samples for the baro trend/sparkline

const char* USER_AGENT = "Blipscope-Angler/1 (+https://github.com/Valar-Systems/Blipscope)";

} // namespace

void AnglerFeedClient::Begin()
{
    if (taskHandle != nullptr) return;
    reqQueue = xQueueCreate(1, sizeof(AnglerFetchRequest*));
    resQueue = xQueueCreate(1, sizeof(AnglerFetchResult*));
    // Core 0 (Wi-Fi core), priority 1, 12 KB stack -- same shape as the Space/Seismic fetch tasks.
    xTaskCreatePinnedToCore(Trampoline, "angler_fetch", 12288, this, 1, &taskHandle, 0);
}

void AnglerFeedClient::Configure(const Config& newCfg)
{
    cfg = newCfg;

    float sc = cfg.intervalScale;
    if (sc < 1.0f) sc = 1.0f;
    if (sc > 8.0f) sc = 8.0f;

    feeds[F_TIDES].intervalMs     = (uint32_t)(TIDES_MS * sc);
    feeds[F_TIDECURVE].intervalMs = (uint32_t)(TIDECURVE_MS * sc);
    feeds[F_WATERTEMP].intervalMs = (uint32_t)(WATERTEMP_MS * sc);
    feeds[F_WEATHER].intervalMs   = (uint32_t)(WEATHER_MS * sc);
    feeds[F_MARINE].intervalMs    = (uint32_t)(MARINE_MS * sc);
    feeds[F_BUOY].intervalMs      = (uint32_t)(BUOY_MS * sc);

    // Stage the first poll shortly after (re)config, fanned out so they don't hit the TLS client
    // at once.
    const uint32_t now = millis();
    for (int i = 0; i < F_COUNT; ++i) {
        feeds[i].failCount = 0;
        feeds[i].nextDueMs = now + (uint32_t)i * 500 + 400;
        firstLogged[i] = false;
    }
}

void AnglerFeedClient::Poll()
{
    AnglerFetchResult* res = nullptr;
    if (xQueueReceive(resQueue, &res, 0) == pdTRUE && res != nullptr) {
        ApplyResult(*res);
        delete res;
        inFlight = false;
    }
    if (inFlight) return;

    const uint32_t now = millis();
    const int feedIdx = PickDueFeed(now);
    if (feedIdx < 0) return;

    AnglerFetchRequest* req = new AnglerFetchRequest();
    if (!BuildRequest(feedIdx, *req)) {
        delete req;
        feeds[feedIdx].nextDueMs = now + feeds[feedIdx].intervalMs;  // re-arm (no station / no location yet)
        return;
    }
    if (xQueueSend(reqQueue, &req, 0) == pdTRUE) inFlight = true;
    else delete req;
}

int AnglerFeedClient::PickDueFeed(uint32_t now) const
{
    int best = -1;
    uint32_t bestDue = 0;
    for (int i = 0; i < F_COUNT; ++i) {
        if ((int32_t)(now - feeds[i].nextDueMs) >= 0) {                     // due (wrap-safe)
            if (best < 0 || (int32_t)(feeds[i].nextDueMs - bestDue) < 0) {  // most overdue
                best = i;
                bestDue = feeds[i].nextDueMs;
            }
        }
    }
    return best;
}

bool AnglerFeedClient::BuildRequest(int feedIdx, AnglerFetchRequest& req)
{
    req.headers.push_back({"User-Agent", USER_AGENT});
    const char* units = cfg.imperial ? "english" : "metric";
    const String lat = String(cfg.lat, 4);
    const String lon = String(cfg.lon, 4);

    switch (feedIdx) {
        case F_TIDES:
            if (cfg.tideStation.isEmpty()) return false;
            req.endpoint = AnglerEndpoint::Tides;
            req.url = String("https://api.tidesandcurrents.noaa.gov/api/prod/datagetter?product=predictions"
                             "&datum=MLLW&interval=hilo&time_zone=gmt&format=json&range=48&units=") + units +
                      "&station=" + cfg.tideStation;
            return true;
        case F_TIDECURVE:
            if (cfg.tideStation.isEmpty()) return false;
            req.endpoint = AnglerEndpoint::TideCurve;
            // Same predictions endpoint WITHOUT interval=hilo -> the 6-minute curve; range=30 h keeps
            // it small. Harmonic (reference) stations only; subordinate stations error and we fall
            // back to interpolating the hi/lo events.
            req.url = String("https://api.tidesandcurrents.noaa.gov/api/prod/datagetter?product=predictions"
                             "&datum=MLLW&time_zone=gmt&format=json&range=30&units=") + units +
                      "&station=" + cfg.tideStation;
            return true;
        case F_BUOY:
            if (cfg.buoy.isEmpty()) return false;
            req.endpoint = AnglerEndpoint::Buoy;
            req.url = String("https://www.ndbc.noaa.gov/data/realtime2/") + cfg.buoy + ".txt";
            return true;
        case F_WATERTEMP:
            if (cfg.tideStation.isEmpty()) return false;
            req.endpoint = AnglerEndpoint::WaterTemp;
            req.url = String("https://api.tidesandcurrents.noaa.gov/api/prod/datagetter?product=water_temperature"
                             "&date=latest&time_zone=gmt&format=json&units=") + units +
                      "&station=" + cfg.tideStation;
            return true;
        case F_WEATHER:
            if (!cfg.hasLatLon) return false;
            req.endpoint = AnglerEndpoint::Weather;
            req.url = String("https://api.open-meteo.com/v1/forecast?latitude=") + lat + "&longitude=" + lon +
                      "&current=temperature_2m,wind_speed_10m,wind_direction_10m,wind_gusts_10m,"
                      "pressure_msl,cloud_cover,weather_code&hourly=pressure_msl&past_days=1&forecast_days=1";
            if (cfg.imperial) req.url += "&temperature_unit=fahrenheit&wind_speed_unit=mph";
            return true;
        case F_MARINE:
            if (!cfg.hasLatLon) return false;
            req.endpoint = AnglerEndpoint::Marine;
            req.url = String("https://marine-api.open-meteo.com/v1/marine?latitude=") + lat + "&longitude=" + lon +
                      "&current=wave_height,sea_surface_temperature";
            return true;
        default:
            return false;
    }
}

int AnglerFeedClient::FeedForEndpoint(AnglerEndpoint e)
{
    switch (e) {
        case AnglerEndpoint::Tides:     return F_TIDES;
        case AnglerEndpoint::TideCurve: return F_TIDECURVE;
        case AnglerEndpoint::WaterTemp: return F_WATERTEMP;
        case AnglerEndpoint::Weather:   return F_WEATHER;
        case AnglerEndpoint::Marine:    return F_MARINE;
        case AnglerEndpoint::Buoy:      return F_BUOY;
    }
    return F_WEATHER;
}

void AnglerFeedClient::ApplyResult(const AnglerFetchResult& res)
{
    const int f = FeedForEndpoint(res.endpoint);
    const uint32_t now = millis();

    if (!res.ok) {
        feeds[f].failCount++;
        const uint16_t shift = feeds[f].failCount > 5 ? 5 : feeds[f].failCount;
        uint32_t backoff = feeds[f].intervalMs << shift;
        if (backoff > MAX_BACKOFF_MS || backoff < feeds[f].intervalMs) backoff = MAX_BACKOFF_MS;
        feeds[f].nextDueMs = now + backoff;
        return;
    }

    feeds[f].failCount = 0;
    feeds[f].nextDueMs = now + feeds[f].intervalMs;

    switch (res.endpoint) {
        case AnglerEndpoint::Tides:
            tide = res.tide;
            if (tide.events.size() > TIDE_RETAIN) tide.events.resize(TIDE_RETAIN);
            break;
        case AnglerEndpoint::TideCurve:
            tideCurve = res.tideCurve;
            break;
        case AnglerEndpoint::WaterTemp:
            haveWaterTemp = res.haveWaterTemp;
            waterTemp = res.waterTemp;
            break;
        case AnglerEndpoint::Weather:
            weather = res.weather;
            break;
        case AnglerEndpoint::Marine:
            marine = res.marine;
            break;
        case AnglerEndpoint::Buoy:
            buoy = res.buoy;
            break;
    }

    if (!firstLogged[f]) {
        firstLogged[f] = true;
        switch (res.endpoint) {
            case AnglerEndpoint::Tides:
                Serial.printf("[angler] tides ok: %u upcoming hi/lo\n", (unsigned)tide.events.size());
                break;
            case AnglerEndpoint::TideCurve:
                Serial.printf("[angler] tide curve ok: %u points\n", (unsigned)tideCurve.size());
                break;
            case AnglerEndpoint::Buoy:
                Serial.printf("[angler] buoy ok: wtmp=%s%.1fC  wave=%s%.2fm\n",
                              buoy.haveWaterTemp ? "" : "(none) ", buoy.waterTempC,
                              buoy.haveWave ? "" : "(none) ", buoy.waveHeightM);
                break;
            case AnglerEndpoint::WaterTemp:
                if (haveWaterTemp) Serial.printf("[angler] water temp ok: %.1f\n", waterTemp);
                else               Serial.println("[angler] water temp: station has no sensor");
                break;
            case AnglerEndpoint::Weather:
                Serial.printf("[angler] weather ok: %.0f temp, wind %.0f dir %d, %.1f hPa (%u hist)\n",
                              weather.airTemp, weather.windSpeed, weather.windDir, weather.pressureHpa,
                              (unsigned)weather.pressHist.size());
                break;
            case AnglerEndpoint::Marine:
                Serial.printf("[angler] marine ok: wave %s%.2fm  sst %s%.1fC\n",
                              marine.haveWave ? "" : "(none) ", marine.waveHeightM,
                              marine.haveSst ? "" : "(none) ", marine.seaTempC);
                break;
        }
    }
}

void AnglerFeedClient::Trampoline(void* arg)
{
    static_cast<AnglerFeedClient*>(arg)->RunWorker();
}

void AnglerFeedClient::RunWorker()
{
    for (;;) {
        AnglerFetchRequest* req = nullptr;
        if (xQueueReceive(reqQueue, &req, portMAX_DELAY) != pdTRUE || req == nullptr) continue;

        AnglerFetchResult* res = new AnglerFetchResult();
        res->endpoint = req->endpoint;
        Fetch(http, *req, *res);
        delete req;

        xQueueSend(resQueue, &res, portMAX_DELAY);
    }
}

void AnglerFeedClient::Fetch(HttpRequestManager& http,
                             const AnglerFetchRequest& req, AnglerFetchResult& res)
{
    // NDBC is plain text over a Content-Length response: fetch as (yielding) text and column-parse it.
    if (req.endpoint == AnglerEndpoint::Buoy) {
        const HttpResult r = http.Get(req.url, req.params, req.headers);
        if (!r.success || r.statusCode < 200 || r.statusCode >= 300) { res.ok = false; return; }
        res.ok = angler::ParseBuoyText(r.response, res.buoy);
        return;
    }

    JsonDocument doc;
    const HttpResult r = http.GetJson(req.url, doc, req.params, req.headers);
    if (!r.success || r.statusCode < 200 || r.statusCode >= 300) { res.ok = false; return; }

    switch (req.endpoint) {
        case AnglerEndpoint::Tides:
            res.ok = angler::ParseTides(doc.as<JsonObjectConst>(), res.tide, TIDE_RETAIN);
            break;
        case AnglerEndpoint::TideCurve:
            res.ok = angler::ParseTideCurve(doc.as<JsonObjectConst>(), res.tideCurve, TIDECURVE_RETAIN);
            break;
        case AnglerEndpoint::WaterTemp:
            res.ok = angler::ParseWaterTemp(doc.as<JsonObjectConst>(), res.waterTemp, res.haveWaterTemp);
            break;
        case AnglerEndpoint::Weather:
            res.ok = angler::ParseWeather(doc.as<JsonObjectConst>(), res.weather, PRESS_HISTORY);
            break;
        case AnglerEndpoint::Marine:
            res.ok = angler::ParseMarine(doc.as<JsonObjectConst>(), res.marine);
            break;
        case AnglerEndpoint::Buoy:
            break;   // handled above
    }
}
