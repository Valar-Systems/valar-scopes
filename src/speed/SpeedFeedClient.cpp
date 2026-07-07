#include "SpeedFeedClient.h"

#include <time.h>

using speed::SpeedFetchRequest;
using speed::SpeedFetchResult;
using speed::SpeedEndpoint;

namespace {

// Poll intervals (ms). The camera's own config page polls at ~1 Hz only while open; a persistent
// desk companion is gentler. Events polls a touch faster so a new pass reaches the alert path
// promptly; state carries the live proximity + health.
constexpr uint32_t STATE_MS  = 2500;
constexpr uint32_t EVENTS_MS = 3000;

// Keep the ceiling low: a down camera should be flagged offline within a handful of seconds, not
// minutes, so don't let backoff stretch the state poll out far.
constexpr uint32_t MAX_BACKOFF_MS = 20000;

const char* USER_AGENT = "Blipscope-Speedscope/1 (+https://github.com/Valar-Systems/Blipscope)";

} // namespace

void SpeedFeedClient::Begin()
{
    if (taskHandle != nullptr) return; // survives config reloads

    reqQueue = xQueueCreate(1, sizeof(SpeedFetchRequest*));
    resQueue = xQueueCreate(1, sizeof(SpeedFetchResult*));

    // Pinned to core 0 (the Wi-Fi core) at priority 1, like the radar/Fishing fetch tasks, so the
    // blocking network work never competes with the loop/render task. 12 KB stack covers the GET +
    // JSON decode.
    xTaskCreatePinnedToCore(Trampoline, "speed_fetch", 12288, this, 1, &taskHandle, 0);
}

void SpeedFeedClient::Configure(const Config& newCfg)
{
    cfg = newCfg;

    float sc = cfg.intervalScale;
    if (sc < 1.0f) sc = 1.0f;
    if (sc > 8.0f) sc = 8.0f;

    feeds[F_STATE].intervalMs  = (uint32_t)(STATE_MS * sc);
    feeds[F_EVENTS].intervalMs = (uint32_t)(EVENTS_MS * sc);

    // Stage the first poll of each endpoint shortly after (re)config, fanned out so they don't hit
    // the single client at once.
    const uint32_t now = millis();
    for (int i = 0; i < F_COUNT; ++i) {
        feeds[i].failCount = 0;
        feeds[i].nextDueMs = now + (uint32_t)i * 400 + 300;
        firstLogged[i] = false;
    }
}

void SpeedFeedClient::Poll()
{
    // 1. apply a ready result (frees its heap) and clear the in-flight gate.
    SpeedFetchResult* res = nullptr;
    if (xQueueReceive(resQueue, &res, 0) == pdTRUE && res != nullptr) {
        ApplyResult(*res);
        delete res;
        inFlight = false;
    }
    if (inFlight) return; // one fetch at a time over the shared client

    // 2. dispatch the next due endpoint.
    const uint32_t now = millis();
    const int feedIdx = PickDueFeed(now);
    if (feedIdx < 0) return;

    SpeedFetchRequest* req = new SpeedFetchRequest();
    if (!BuildRequest(feedIdx, *req)) {
        // no camera host configured -> re-arm and move on (nothing opened).
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

int SpeedFeedClient::PickDueFeed(uint32_t now) const
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

bool SpeedFeedClient::BuildRequest(int feedIdx, SpeedFetchRequest& req) const
{
    if (cfg.origin.isEmpty()) return false; // nothing to poll until the camera host is set

    String base = cfg.origin;
    while (base.endsWith("/")) base.remove(base.length() - 1);

    req.headers.push_back({"User-Agent", USER_AGENT});

    switch (feedIdx) {
        case F_STATE:
            req.endpoint = SpeedEndpoint::State;
            req.url = base + "/api/state";
            return true;
        case F_EVENTS:
            req.endpoint = SpeedEndpoint::Events;
            req.url = base + "/api/events";
            return true;
        default:
            return false;
    }
}

int SpeedFeedClient::FeedForEndpoint(SpeedEndpoint e)
{
    switch (e) {
        case SpeedEndpoint::State:  return F_STATE;
        case SpeedEndpoint::Events: return F_EVENTS;
    }
    return F_STATE;
}

void SpeedFeedClient::ApplyResult(const SpeedFetchResult& res)
{
    const int f = FeedForEndpoint(res.endpoint);
    const uint32_t now = millis();

    if (!res.ok) {
        // Exponential backoff; keep the previously-stored data so the screen doesn't blank. (A failed
        // state poll is exactly the "camera offline" signal MsSinceStateOk() reports to the manager.)
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

    switch (res.endpoint) {
        case SpeedEndpoint::State:
            if (res.state.valid) {
                state = res.state;
                state.fetchedAtMs = now;
                everStateOk = true;
                lastStateOkMs = now;
            }
            break;
        case SpeedEndpoint::Events:
            if (res.events.valid) {
                events = res.events;
                events.fetchedAtMs = now;
            }
            break;
    }

    if (!firstLogged[f]) {
        firstLogged[f] = true;
        static const char* names[F_COUNT] = {"state", "events"};
        Serial.printf("[speed] %s ok\n", names[f]);
    }
}

void SpeedFeedClient::Trampoline(void* arg)
{
    static_cast<SpeedFeedClient*>(arg)->RunWorker();
}

void SpeedFeedClient::RunWorker()
{
    for (;;) {
        SpeedFetchRequest* req = nullptr;
        if (xQueueReceive(reqQueue, &req, portMAX_DELAY) != pdTRUE || req == nullptr)
            continue;

        SpeedFetchResult* res = new SpeedFetchResult();
        res->endpoint = req->endpoint;
        Fetch(http, *req, *res);
        delete req;

        xQueueSend(resQueue, &res, portMAX_DELAY);
    }
}

void SpeedFeedClient::Fetch(HttpRequestManager& http,
                            const SpeedFetchRequest& req, SpeedFetchResult& res)
{
    res.endpoint = req.endpoint;

    JsonDocument doc;
    const HttpResult r = http.GetJson(req.url, doc, req.params, req.headers);
    if (!r.success || r.statusCode < 200 || r.statusCode >= 300) { res.ok = false; return; }

    JsonObjectConst root = doc.as<JsonObjectConst>();
    switch (req.endpoint) {
        case SpeedEndpoint::State:  speed::ParseDeviceState(root, res.state); break;
        case SpeedEndpoint::Events: speed::ParseEvents(root, res.events, (long)time(nullptr)); break;
    }
    res.ok = true;
}
