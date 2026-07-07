#include "BirdingFeedClient.h"

using birding::BirdFetchRequest;
using birding::BirdFetchResult;

namespace {

// Default poll intervals (ms). eBird data updates as birders submit checklists -- minutes is plenty,
// and a gentle cadence respects a free public service (and any per-token rate limit).
constexpr uint32_t NOTABLE_MS  = 300000;   // ~5 m: the headline rare-sightings feed
constexpr uint32_t RECENT_MS   = 360000;   // ~6 m: all recent sightings (big-day count + radar)
constexpr uint32_t HOTSPOTS_MS = 1800000;  // ~30 m: nearby hotspots barely change

constexpr uint32_t MAX_BACKOFF_MS = 600000;

constexpr size_t NOTABLE_RETAIN  = 40;
constexpr size_t RECENT_RETAIN   = 80;
constexpr size_t HOTSPOTS_RETAIN = 20;

const char* USER_AGENT = "Blipscope-Birding/1 (+https://github.com/Valar-Systems/valar-scopes)";
const char* EBIRD = "https://api.ebird.org/v2";

int ClampInt(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

} // namespace

void BirdingFeedClient::Begin()
{
    if (taskHandle != nullptr) return;
    reqQueue = xQueueCreate(1, sizeof(BirdFetchRequest*));
    resQueue = xQueueCreate(1, sizeof(BirdFetchResult*));
    xTaskCreatePinnedToCore(Trampoline, "bird_fetch", 12288, this, 1, &taskHandle, 0);
}

void BirdingFeedClient::Configure(const Config& newCfg)
{
    cfg = newCfg;

    float sc = cfg.intervalScale;
    if (sc < 1.0f) sc = 1.0f;
    if (sc > 8.0f) sc = 8.0f;

    feeds[F_NOTABLE].intervalMs  = (uint32_t)(NOTABLE_MS * sc);
    feeds[F_RECENT].intervalMs   = (uint32_t)(RECENT_MS * sc);
    feeds[F_HOTSPOTS].intervalMs = (uint32_t)(HOTSPOTS_MS * sc);

    const uint32_t now = millis();
    for (int i = 0; i < F_COUNT; ++i) {
        feeds[i].failCount = 0;
        feeds[i].nextDueMs = now + (uint32_t)i * 500 + 300;
        firstLogged[i] = false;
    }
}

void BirdingFeedClient::Poll()
{
    BirdFetchResult* res = nullptr;
    if (xQueueReceive(resQueue, &res, 0) == pdTRUE && res != nullptr) {
        ApplyResult(*res);
        delete res;
        inFlight = false;
    }
    if (inFlight) return;

    const uint32_t now = millis();
    const int feedIdx = PickDueFeed(now);
    if (feedIdx < 0) return;

    BirdFetchRequest* req = new BirdFetchRequest();
    if (!BuildRequest(feedIdx, *req)) {
        delete req;
        feeds[feedIdx].nextDueMs = now + feeds[feedIdx].intervalMs;
        return;
    }

    if (xQueueSend(reqQueue, &req, 0) == pdTRUE) {
        inFlight = true;
    } else {
        delete req;
    }
}

int BirdingFeedClient::PickDueFeed(uint32_t now) const
{
    int best = -1;
    uint32_t bestDue = 0;
    for (int i = 0; i < F_COUNT; ++i) {
        if ((int32_t)(now - feeds[i].nextDueMs) >= 0) {
            if (best < 0 || (int32_t)(feeds[i].nextDueMs - bestDue) < 0) {
                best = i;
                bestDue = feeds[i].nextDueMs;
            }
        }
    }
    return best;
}

bool BirdingFeedClient::BuildRequest(int feedIdx, BirdFetchRequest& req) const
{
    if (cfg.apiKey.isEmpty() || !cfg.hasLatLon) return false; // need a key + a location to query

    req.headers.push_back({"User-Agent", USER_AGENT});
    req.headers.push_back({"X-eBirdApiToken", cfg.apiKey});

    const String lat = String(cfg.lat, 4);
    const String lng = String(cfg.lon, 4);
    const int dist = ClampInt(cfg.radiusKm, 1, 50);
    const int back = ClampInt(cfg.backDays, 1, 30);
    const int maxr = ClampInt(cfg.maxResults, 1, 200);
    const String geo = "?lat=" + lat + "&lng=" + lng + "&dist=" + String(dist) + "&back=" + String(back);

    switch (feedIdx) {
        case F_NOTABLE:
            req.endpoint = birding::BirdEndpoint::Notable;
            req.url = String(EBIRD) + "/data/obs/geo/recent/notable" + geo +
                      "&maxResults=" + String(maxr) + "&detail=simple";
            return true;
        case F_RECENT:
            req.endpoint = birding::BirdEndpoint::Recent;
            req.url = String(EBIRD) + "/data/obs/geo/recent" + geo + "&maxResults=" + String(maxr);
            return true;
        case F_HOTSPOTS:
            req.endpoint = birding::BirdEndpoint::Hotspots;
            req.url = String(EBIRD) + "/ref/hotspot/geo?lat=" + lat + "&lng=" + lng +
                      "&dist=" + String(dist) + "&fmt=json";
            return true;
        default:
            return false;
    }
}

int BirdingFeedClient::FeedForEndpoint(birding::BirdEndpoint e)
{
    switch (e) {
        case birding::BirdEndpoint::Notable:  return F_NOTABLE;
        case birding::BirdEndpoint::Recent:   return F_RECENT;
        case birding::BirdEndpoint::Hotspots: return F_HOTSPOTS;
    }
    return F_NOTABLE;
}

void BirdingFeedClient::ApplyResult(const BirdFetchResult& res)
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
    fetched[f] = true;

    switch (res.endpoint) {
        case birding::BirdEndpoint::Notable:  notable = res.sightings; break;
        case birding::BirdEndpoint::Recent:   recent = res.sightings; break;
        case birding::BirdEndpoint::Hotspots: hotspots = res.hotspots; break;
    }

    if (!firstLogged[f]) {
        firstLogged[f] = true;
        switch (res.endpoint) {
            case birding::BirdEndpoint::Notable:
                Serial.printf("[birding] notable ok: %u sightings%s\n", (unsigned)notable.size(),
                              notable.empty() ? "" : (String(" (") + notable.front().comName + ")").c_str());
                break;
            case birding::BirdEndpoint::Recent:
                Serial.printf("[birding] recent ok: %u sightings\n", (unsigned)recent.size());
                break;
            case birding::BirdEndpoint::Hotspots:
                Serial.printf("[birding] hotspots ok: %u\n", (unsigned)hotspots.size());
                break;
        }
    }
}

void BirdingFeedClient::Trampoline(void* arg)
{
    static_cast<BirdingFeedClient*>(arg)->RunWorker();
}

void BirdingFeedClient::RunWorker()
{
    for (;;) {
        BirdFetchRequest* req = nullptr;
        if (xQueueReceive(reqQueue, &req, portMAX_DELAY) != pdTRUE || req == nullptr)
            continue;

        BirdFetchResult* res = new BirdFetchResult();
        res->endpoint = req->endpoint;
        Fetch(http, *req, *res);
        delete req;

        xQueueSend(resQueue, &res, portMAX_DELAY);
    }
}

void BirdingFeedClient::Fetch(HttpRequestManager& http,
                              const BirdFetchRequest& req, BirdFetchResult& res)
{
    JsonDocument doc;
    const HttpResult r = http.GetJson(req.url, doc, req.params, req.headers);
    if (!r.success || r.statusCode < 200 || r.statusCode >= 300) {
        res.ok = false;
        return;
    }
    JsonArrayConst arr = doc.as<JsonArrayConst>();
    switch (req.endpoint) {
        case birding::BirdEndpoint::Notable:
            birding::ParseObs(arr, res.sightings, NOTABLE_RETAIN, true);
            break;
        case birding::BirdEndpoint::Recent:
            birding::ParseObs(arr, res.sightings, RECENT_RETAIN, false);
            break;
        case birding::BirdEndpoint::Hotspots:
            birding::ParseHotspots(arr, res.hotspots, HOTSPOTS_RETAIN);
            break;
    }
    res.ok = true; // a successful fetch with zero results is valid "no data"
}
