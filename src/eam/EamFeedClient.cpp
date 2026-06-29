#include "EamFeedClient.h"

#include <algorithm>

// The fetch request/result envelopes live in namespace eam; bring them in for this unit so the
// worker/scheduler code below reads cleanly.
using eam::EamFetchRequest;
using eam::EamFetchResult;

namespace {

// Default poll intervals (ms). Per the backend contract; a global override can be added later.
constexpr uint32_t LATEST_MS      = 30000;     // ~30 s
constexpr uint32_t SKYKINGS_MS    = 60000;     // ~60 s (unspecified; conservative)
constexpr uint32_t TEMPO_MS       = 300000;    // ~5 m
constexpr uint32_t CODEWORDS_MS   = 300000;    // ~5 m (unspecified; matches tempo)
constexpr uint32_t PROPAGATION_MS = 1800000;   // ~30 m
constexpr uint32_t ICBM_MS        = 21600000;  // ~6 h

constexpr uint32_t MAX_BACKOFF_MS = 600000;    // cap exponential backoff at 10 m

// Bounded retention so a huge/hostile response can't exhaust the C3's heap.
constexpr size_t LATEST_RETAIN   = 50;
constexpr size_t SKYKINGS_RETAIN = 20;
constexpr size_t CODEWORD_RETAIN = 50;
constexpr size_t LAUNCH_RETAIN   = 10;

String StripTrailingSlash(String s)
{
    while (s.endsWith("/")) s.remove(s.length() - 1);
    return s;
}

} // namespace

void EamFeedClient::Begin()
{
    if (taskHandle != nullptr) return; // survives config reloads

    reqQueue = xQueueCreate(1, sizeof(EamFetchRequest*));
    resQueue = xQueueCreate(1, sizeof(EamFetchResult*));

    // Pinned to core 0 (the Wi-Fi core) and priority 1, exactly like AircraftManager's fetch task,
    // so the blocking TLS work never competes with the loop/render task. 12 KB stack covers a TLS
    // handshake + JSON decode (+ the OpenSky token exchange).
    xTaskCreatePinnedToCore(Trampoline, "eam_fetch", 12288, this, 1, &taskHandle, 0);
}

void EamFeedClient::Configure(const Config& newCfg)
{
    cfg = newCfg;

    // (Re)build the command-post provider from the selected source. Never any baked-in key:
    // the OpenSky provider is constructed with the user's own (possibly blank) credentials and
    // goes inert on its own when they're blank.
    if (cfg.abncpSource == AbncpSource::OpenSky)
        abncpProvider.reset(new eam::OpenSkyAbncpProvider(cfg.openskyId, cfg.openskySecret, cfg.abncpWatch));
    else
        abncpProvider.reset(new eam::BackendAbncpProvider(StripTrailingSlash(cfg.baseUrl)));

    // Global interval scale (>=1 relaxes every poller; clamped so it can never go below 1x and
    // breach OpenSky's 60 s floor for the ABNCP watch).
    float sc = cfg.intervalScale;
    if (sc < 1.0f) sc = 1.0f;
    if (sc > 8.0f) sc = 8.0f;

    feeds[F_LATEST].intervalMs      = (uint32_t)(LATEST_MS * sc);
    feeds[F_SKYKINGS].intervalMs    = (uint32_t)(SKYKINGS_MS * sc);
    feeds[F_TEMPO].intervalMs       = (uint32_t)(TEMPO_MS * sc);
    feeds[F_CODEWORDS].intervalMs   = (uint32_t)(CODEWORDS_MS * sc);
    feeds[F_PROPAGATION].intervalMs = (uint32_t)(PROPAGATION_MS * sc);
    feeds[F_ICBM].intervalMs        = (uint32_t)(ICBM_MS * sc);
    feeds[F_ABNCP].intervalMs       = (uint32_t)(abncpProvider->IntervalMs() * sc);

    // Stage the first poll of each endpoint shortly after (re)config, fanned out by ~400 ms so
    // they don't all hit the single TLS client at once.
    const uint32_t now = millis();
    for (int i = 0; i < F_COUNT; ++i) {
        feeds[i].failCount = 0;
        feeds[i].nextDueMs = now + (uint32_t)i * 400 + 300;
    }
}

void EamFeedClient::Poll()
{
    // 1. apply a ready result (frees its heap) and clear the in-flight gate.
    EamFetchResult* res = nullptr;
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

    EamFetchRequest* req = new EamFetchRequest();
    if (!BuildRequest(feedIdx, *req)) {
        // Provider inert (e.g. OpenSky creds blank): make no call, just re-arm the timer.
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

int EamFeedClient::PickDueFeed(uint32_t now) const
{
    int best = -1;
    uint32_t bestDue = 0;
    for (int i = 0; i < F_COUNT; ++i) {
        // millis() wraps after ~49 days; (now - nextDue) as signed handles the wrap correctly.
        if ((int32_t)(now - feeds[i].nextDueMs) >= 0) {
            if (best < 0 || (int32_t)(feeds[i].nextDueMs - bestDue) < 0) {
                best = i;
                bestDue = feeds[i].nextDueMs;
            }
        }
    }
    return best;
}

bool EamFeedClient::BuildRequest(int feedIdx, EamFetchRequest& req) const
{
    const String base = StripTrailingSlash(cfg.baseUrl);
    switch (feedIdx) {
        case F_LATEST:
            req.endpoint = eam::EamEndpoint::Latest;
            req.url = base + "/eam/latest";
            req.params.push_back({"limit", "20"});
            return true;
        case F_SKYKINGS:
            req.endpoint = eam::EamEndpoint::Skykings;
            req.url = base + "/eam/skykings";
            req.params.push_back({"limit", "20"});
            return true;
        case F_TEMPO:
            req.endpoint = eam::EamEndpoint::Tempo;
            req.url = base + "/eam/tempo";
            return true;
        case F_CODEWORDS:
            req.endpoint = eam::EamEndpoint::Codewords;
            req.url = base + "/eam/codewords";
            return true;
        case F_PROPAGATION:
            req.endpoint = eam::EamEndpoint::Propagation;
            req.url = base + "/propagation";
            if (cfg.hasLatLon) {
                req.params.push_back({"lat", String(cfg.lat, 4)});
                req.params.push_back({"lon", String(cfg.lon, 4)});
            }
            return true;
        case F_ICBM:
            req.endpoint = eam::EamEndpoint::Icbm;
            req.url = base + "/launches/icbm";
            return true;
        case F_ABNCP:
            return abncpProvider && abncpProvider->BuildRequest(req);
        default:
            return false;
    }
}

int EamFeedClient::FeedForEndpoint(eam::EamEndpoint e)
{
    switch (e) {
        case eam::EamEndpoint::Latest:       return F_LATEST;
        case eam::EamEndpoint::Skykings:     return F_SKYKINGS;
        case eam::EamEndpoint::Tempo:        return F_TEMPO;
        case eam::EamEndpoint::Codewords:    return F_CODEWORDS;
        case eam::EamEndpoint::Propagation:  return F_PROPAGATION;
        case eam::EamEndpoint::Icbm:         return F_ICBM;
        case eam::EamEndpoint::Abncp:        return F_ABNCP;
        case eam::EamEndpoint::AbncpOpenSky: return F_ABNCP;
    }
    return F_LATEST;
}

void EamFeedClient::ApplyResult(const EamFetchResult& res)
{
    const int f = FeedForEndpoint(res.endpoint);
    const uint32_t now = millis();

    if (!res.ok) {
        // Exponential backoff; keep the previously-stored data so the screen doesn't blank.
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
        case eam::EamEndpoint::Latest: {
            std::vector<eam::Msg> incoming = res.messages;
            MergeLatest(incoming);
            break;
        }
        case eam::EamEndpoint::Skykings:
            skykings = res.messages;
            if (skykings.size() > SKYKINGS_RETAIN) skykings.resize(SKYKINGS_RETAIN);
            break;
        case eam::EamEndpoint::Tempo:
            tempo = res.tempo;
            break;
        case eam::EamEndpoint::Codewords:
            codewords = res.codewords;
            if (codewords.size() > CODEWORD_RETAIN) codewords.resize(CODEWORD_RETAIN);
            codewordWindowDays = res.codewordWindowDays;
            break;
        case eam::EamEndpoint::Propagation:
            propagation = res.propagation;
            break;
        case eam::EamEndpoint::Icbm:
            launches = res.launches;
            if (launches.size() > LAUNCH_RETAIN) launches.resize(LAUNCH_RETAIN);
            break;
        case eam::EamEndpoint::Abncp:
        case eam::EamEndpoint::AbncpOpenSky:
            abncp = res.abncp;
            break;
    }
}

void EamFeedClient::MergeLatest(std::vector<eam::Msg>& incoming)
{
    // The endpoint returns newest-first. Retain up to LATEST_RETAIN by prepending genuinely new
    // ids and keeping older ones that have since scrolled off the 20-message window.
    std::vector<eam::Msg> merged = incoming;
    for (const eam::Msg& old : latest) {
        if (merged.size() >= LATEST_RETAIN) break;
        bool present = false;
        for (const eam::Msg& m : incoming)
            if (m.id == old.id) { present = true; break; }
        if (!present) merged.push_back(old);
    }
    if (merged.size() > LATEST_RETAIN) merged.resize(LATEST_RETAIN);

    // New-arrival edge: a different top id than last time, and we'd seen a feed before.
    if (!merged.empty()) {
        if (!lastTopId.isEmpty() && merged.front().id != lastTopId)
            newLatestEdge = true;
        lastTopId = merged.front().id;
    }

    latest = std::move(merged);
}

bool EamFeedClient::ConsumeNewLatest()
{
    if (!newLatestEdge) return false;
    newLatestEdge = false;
    return true;
}

void EamFeedClient::Trampoline(void* arg)
{
    static_cast<EamFeedClient*>(arg)->RunWorker();
}

void EamFeedClient::RunWorker()
{
    for (;;) {
        EamFetchRequest* req = nullptr;
        if (xQueueReceive(reqQueue, &req, portMAX_DELAY) != pdTRUE || req == nullptr)
            continue;

        EamFetchResult* res = new EamFetchResult();
        res->endpoint = req->endpoint;
        Fetch(http, authHandler, *req, *res);
        delete req;

        xQueueSend(resQueue, &res, portMAX_DELAY);
    }
}

void EamFeedClient::Fetch(HttpRequestManager& http, OpenSkyAuthTokenHandler& auth,
                          const EamFetchRequest& req, EamFetchResult& res)
{
    std::vector<std::pair<String, String>> headers = req.headers;

    // OpenSky command-post watch: exchange the user's own credentials for a bearer token here,
    // off-loop. Never falls back to any shared key; an empty token means we make no states call.
    if (req.needsOpenSkyToken) {
        const String token = auth.GetValidToken(req.openskyId, req.openskySecret);
        if (token.isEmpty()) {
            res.ok = false;
            return;
        }
        headers.push_back({"Authorization", "Bearer " + token});
    }

    JsonDocument doc;
    const HttpResult r = http.GetJson(req.url, doc, req.params, headers);
    if (!r.success || r.statusCode < 200 || r.statusCode >= 300) {
        res.ok = false;
        return;
    }

    JsonObjectConst root = doc.as<JsonObjectConst>();
    switch (req.endpoint) {
        case eam::EamEndpoint::Latest:
            eam::ParseMessages(root, res.messages, 50);
            break;
        case eam::EamEndpoint::Skykings:
            eam::ParseMessages(root, res.messages, 20);
            break;
        case eam::EamEndpoint::Tempo:
            eam::ParseTempo(root, res.tempo);
            break;
        case eam::EamEndpoint::Codewords:
            eam::ParseCodewords(root, res.codewords, res.codewordWindowDays, 50);
            break;
        case eam::EamEndpoint::Propagation:
            eam::ParsePropagation(root, res.propagation);
            break;
        case eam::EamEndpoint::Icbm:
            eam::ParseLaunches(root, res.launches, 10);
            break;
        case eam::EamEndpoint::Abncp:
            eam::ParseAbncpBackend(root, res.abncp);
            break;
        case eam::EamEndpoint::AbncpOpenSky:
            eam::ParseOpenSkyStates(root, res.abncp);
            break;
    }
    res.ok = true;
}
