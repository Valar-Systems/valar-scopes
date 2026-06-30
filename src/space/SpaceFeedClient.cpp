#include "SpaceFeedClient.h"

#include <time.h>

using space::SpaceFetchRequest;
using space::SpaceFetchResult;

namespace {

// Default poll intervals (ms).
constexpr uint32_t ISS_MS        = 5000;      // ~5 s: a smoothly gliding blip (well under wheretheiss' ~1 req/s)
constexpr uint32_t LAUNCH_MS     = 1200000;   // ~20 m: editorial, slow-moving; the T-minus clock ticks locally
constexpr uint32_t KP_MS         = 720000;    // ~12 m: SWPC Kp is 3-hourly with a recent estimate
constexpr uint32_t DSN_MS        = 30000;     // ~30 s: which dish talks to whom changes slowly
constexpr uint32_t DEEPSPACE_MS  = 120000;    // ~2 m per target (round-robin); probes move slowly
constexpr uint32_t FLARE_MS      = 90000;     // ~90 s: GOES X-ray updates ~1 min; flares evolve fast
constexpr uint32_t HUMANS_MS     = 3600000;   // ~1 h: the crew roster changes rarely

constexpr uint32_t MAX_BACKOFF_MS = 600000; // cap exponential backoff at 10 m

constexpr size_t LAUNCH_RETAIN = 5;       // a few upcoming launches (the screen shows the next)
constexpr size_t KP_HISTORY     = 24;     // recent Kp samples kept for the gauge sparkline
constexpr size_t DSN_LINK_CAP   = 12;     // bound the parsed active-link list

// Deep-space probes fetched round-robin from JPL Horizons (one per DeepSpace poll). cmd is the
// Horizons body code; CENTER=500@399 yields range from Earth. Verified -31 (Voyager 1) live.
struct DeepTarget { const char* name; const char* cmd; };
const DeepTarget DEEP_TARGETS[] = {
    {"Voyager 1", "-31"}, {"Voyager 2", "-32"}, {"New Horizons", "-98"},
    {"JWST", "-170"}, {"Parker Solar Probe", "-96"},
};
constexpr int DEEP_N = (int)(sizeof(DEEP_TARGETS) / sizeof(DEEP_TARGETS[0]));

// A polite UA: some public APIs reject default/empty agents. No key, nothing identifying the user.
const char* USER_AGENT = "Blipscope-Spacescope/1 (+https://github.com/Valar-Systems/Blipscope)";

} // namespace

void SpaceFeedClient::Begin()
{
    if (taskHandle != nullptr) return; // survives config reloads

    // Seed the round-robin deep-space target list with friendly names (distances fill in as polled).
    deepTargets.resize(DEEP_N);
    for (int i = 0; i < DEEP_N; ++i) { deepTargets[i].name = DEEP_TARGETS[i].name; deepTargets[i].valid = false; }

    reqQueue = xQueueCreate(1, sizeof(SpaceFetchRequest*));
    resQueue = xQueueCreate(1, sizeof(SpaceFetchResult*));

    // Pinned to core 0 (the Wi-Fi core) at priority 1, like the radar/EAM fetch tasks, so the
    // blocking TLS work never competes with the loop/render task. 12 KB stack covers a TLS
    // handshake + JSON decode.
    xTaskCreatePinnedToCore(Trampoline, "space_fetch", 12288, this, 1, &taskHandle, 0);
}

void SpaceFeedClient::Configure(const Config& newCfg)
{
    cfg = newCfg;

    float sc = cfg.intervalScale;
    if (sc < 1.0f) sc = 1.0f;
    if (sc > 8.0f) sc = 8.0f;

    feeds[F_ISS].intervalMs       = (uint32_t)(ISS_MS * sc);
    feeds[F_LAUNCH].intervalMs    = (uint32_t)(LAUNCH_MS * sc);
    feeds[F_KP].intervalMs        = (uint32_t)(KP_MS * sc);
    feeds[F_DSN].intervalMs       = (uint32_t)(DSN_MS * sc);
    feeds[F_DEEPSPACE].intervalMs = (uint32_t)(DEEPSPACE_MS * sc);
    feeds[F_FLARE].intervalMs     = (uint32_t)(FLARE_MS * sc);
    feeds[F_HUMANS].intervalMs    = (uint32_t)(HUMANS_MS * sc);

    // Stage the first poll of each endpoint shortly after (re)config, fanned out by ~400 ms so
    // they don't all hit the single TLS client at once.
    const uint32_t now = millis();
    for (int i = 0; i < F_COUNT; ++i) {
        feeds[i].failCount = 0;
        feeds[i].nextDueMs = now + (uint32_t)i * 400 + 300;
        firstLogged[i] = false; // re-confirm each feed after a (re)configure
    }
}

void SpaceFeedClient::Poll()
{
    // 1. apply a ready result (frees its heap) and clear the in-flight gate.
    SpaceFetchResult* res = nullptr;
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

    SpaceFetchRequest* req = new SpaceFetchRequest();
    if (!BuildRequest(feedIdx, *req)) {
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

int SpaceFeedClient::PickDueFeed(uint32_t now) const
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

bool SpaceFeedClient::BuildRequest(int feedIdx, SpaceFetchRequest& req)
{
    req.headers.push_back({"User-Agent", USER_AGENT});
    switch (feedIdx) {
        case F_ISS:
            req.endpoint = space::SpaceEndpoint::Iss;
            req.url = "https://api.wheretheiss.at/v1/satellites/25544";
            return true;
        case F_LAUNCH:
            req.endpoint = space::SpaceEndpoint::Launch;
            req.url = "https://fdo.rocketlaunch.live/json/launches/next/5";
            return true;
        case F_KP:
            req.endpoint = space::SpaceEndpoint::Kp;
            req.url = "https://services.swpc.noaa.gov/products/noaa-planetary-k-index.json";
            return true;
        case F_DSN:
            req.endpoint = space::SpaceEndpoint::Dsn;
            req.url = "https://eyes.nasa.gov/dsn/data/dsn.xml";
            return true;
        case F_FLARE:
            req.endpoint = space::SpaceEndpoint::Flare;
            req.url = "https://services.swpc.noaa.gov/json/goes/primary/xrays-6-hour.json";
            return true;
        case F_HUMANS:
            req.endpoint = space::SpaceEndpoint::Humans;
            // corquaid GitHub-Pages mirror: fresh + reliable HTTPS (open-notify is stale/flaky).
            req.url = "https://corquaid.github.io/international-space-station-APIs/JSON/people-in-space.json";
            return true;
        case F_DEEPSPACE: {
            // Needs NTP for the START/STOP date window; skip (re-arm) until the clock is set.
            const time_t now = time(nullptr);
            if (now < 1600000000) return false;
            const time_t tmrw = now + 86400;
            struct tm a, b;
            gmtime_r(&now, &a);
            gmtime_r(&tmrw, &b);
            char start[12], stop[12];
            strftime(start, sizeof(start), "%Y-%m-%d", &a);
            strftime(stop, sizeof(stop), "%Y-%m-%d", &b);

            const DeepTarget& t = DEEP_TARGETS[deepIdx];
            req.endpoint = space::SpaceEndpoint::DeepSpace;
            req.targetIdx = deepIdx;
            // Pre-encoded Horizons query (%27=' %40=@); STEP_SIZE='1' = one interval, no spaces in
            // the request line. format=json so the existing GetJson de-chunker handles the body.
            String u = "https://ssd.jpl.nasa.gov/api/horizons.api?format=json";
            u += "&OBJ_DATA=%27NO%27&MAKE_EPHEM=%27YES%27&EPHEM_TYPE=%27OBSERVER%27";
            u += "&CENTER=%27500%40399%27&QUANTITIES=%2720%27&STEP_SIZE=%271%27";
            u += "&COMMAND=%27"; u += t.cmd; u += "%27";
            u += "&START_TIME=%27"; u += start; u += "%27";
            u += "&STOP_TIME=%27"; u += stop; u += "%27";
            req.url = u;

            deepIdx = (deepIdx + 1) % DEEP_N; // advance round-robin for the next poll
            return true;
        }
        default:
            return false;
    }
}

int SpaceFeedClient::FeedForEndpoint(space::SpaceEndpoint e)
{
    switch (e) {
        case space::SpaceEndpoint::Iss:       return F_ISS;
        case space::SpaceEndpoint::Launch:    return F_LAUNCH;
        case space::SpaceEndpoint::Kp:        return F_KP;
        case space::SpaceEndpoint::Dsn:       return F_DSN;
        case space::SpaceEndpoint::DeepSpace: return F_DEEPSPACE;
        case space::SpaceEndpoint::Flare:     return F_FLARE;
        case space::SpaceEndpoint::Humans:    return F_HUMANS;
    }
    return F_ISS;
}

void SpaceFeedClient::ApplyResult(const SpaceFetchResult& res)
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
        case space::SpaceEndpoint::Iss:
            iss = res.iss;
            break;
        case space::SpaceEndpoint::Launch:
            launches = res.launches;
            if (launches.size() > LAUNCH_RETAIN) launches.resize(LAUNCH_RETAIN);
            break;
        case space::SpaceEndpoint::Kp:
            wx = res.wx;
            break;
        case space::SpaceEndpoint::Dsn:
            dsn = res.dsn;
            break;
        case space::SpaceEndpoint::DeepSpace:
            if (res.targetIdx >= 0 && res.targetIdx < (int)deepTargets.size()) {
                const String nm = deepTargets[res.targetIdx].name; // preserve the friendly name
                deepTargets[res.targetIdx] = res.deepTarget;
                deepTargets[res.targetIdx].name = nm;
            }
            break;
        case space::SpaceEndpoint::Flare:
            flare = res.flare;
            break;
        case space::SpaceEndpoint::Humans:
            crew = res.crew;
            break;
    }

    // One-line confirmation the first time each feed lands (handy for field/serial diagnostics).
    if (!firstLogged[f]) {
        firstLogged[f] = true;
        switch (res.endpoint) {
            case space::SpaceEndpoint::Iss:
                Serial.printf("[space] ISS ok: lat=%.1f lon=%.1f alt=%.0fkm %s\n",
                              iss.lat, iss.lon, iss.altKm, iss.sunlit ? "sunlit" : "eclipsed");
                break;
            case space::SpaceEndpoint::Launch:
                Serial.printf("[space] launch ok: %u upcoming; next=%s / %s\n",
                              (unsigned)launches.size(),
                              launches.empty() ? "-" : launches.front().provider.c_str(),
                              launches.empty() ? "-" : launches.front().mission.c_str());
                break;
            case space::SpaceEndpoint::Kp:
                Serial.printf("[space] Kp ok: %.2f (%u samples)\n", wx.kp, (unsigned)wx.history.size());
                break;
            case space::SpaceEndpoint::Dsn:
                Serial.printf("[space] DSN ok: %u active links\n", (unsigned)dsn.links.size());
                break;
            case space::SpaceEndpoint::DeepSpace:
                if (res.targetIdx >= 0 && res.targetIdx < (int)deepTargets.size())
                    Serial.printf("[space] deepspace ok: %s %.2f AU\n",
                                  deepTargets[res.targetIdx].name.c_str(),
                                  deepTargets[res.targetIdx].distanceAu);
                break;
            case space::SpaceEndpoint::Flare:
                Serial.printf("[space] flare ok: %s (%.1e W/m2)\n",
                              space::XrayClass(flare.fluxWm2).c_str(), flare.fluxWm2);
                break;
            case space::SpaceEndpoint::Humans:
                Serial.printf("[space] humans ok: %d in space\n", crew.number);
                break;
        }
    }
}

void SpaceFeedClient::Trampoline(void* arg)
{
    static_cast<SpaceFeedClient*>(arg)->RunWorker();
}

void SpaceFeedClient::RunWorker()
{
    for (;;) {
        SpaceFetchRequest* req = nullptr;
        if (xQueueReceive(reqQueue, &req, portMAX_DELAY) != pdTRUE || req == nullptr)
            continue;

        SpaceFetchResult* res = new SpaceFetchResult();
        res->endpoint = req->endpoint;
        res->targetIdx = req->targetIdx;
        Fetch(http, *req, *res);
        delete req;

        xQueueSend(resQueue, &res, portMAX_DELAY);
    }
}

void SpaceFeedClient::Fetch(HttpRequestManager& http,
                            const SpaceFetchRequest& req, SpaceFetchResult& res)
{
    // DSN is XML over a Content-Length response: fetch as (yielding) text and scan it.
    if (req.endpoint == space::SpaceEndpoint::Dsn) {
        const HttpResult r = http.Get(req.url, req.params, req.headers);
        if (!r.success || r.statusCode < 200 || r.statusCode >= 300) { res.ok = false; return; }
        space::ParseDsn(r.response, res.dsn, DSN_LINK_CAP);
        res.ok = true; // a successful fetch is valid even with zero active links
        return;
    }

    JsonDocument doc;
    const HttpResult r = http.GetJson(req.url, doc, req.params, req.headers);
    if (!r.success || r.statusCode < 200 || r.statusCode >= 300) {
        res.ok = false;
        return;
    }

    switch (req.endpoint) {
        case space::SpaceEndpoint::Iss:
            res.ok = space::ParseIss(doc.as<JsonObjectConst>(), res.iss);
            break;
        case space::SpaceEndpoint::Launch:
            // The endpoint's root is an object with a "result" array. Empty = valid "no upcoming".
            space::ParseLaunches(doc.as<JsonObjectConst>(), res.launches, LAUNCH_RETAIN);
            res.ok = true;
            break;
        case space::SpaceEndpoint::Kp:
            // SWPC serves a bare JSON array (of {time_tag, Kp, ...} objects), not an object root.
            res.ok = space::ParseKp(doc.as<JsonArrayConst>(), res.wx, KP_HISTORY);
            break;
        case space::SpaceEndpoint::DeepSpace: {
            // Horizons format=json wraps the OBSERVER table text in "result"; scan its $$SOE line.
            const char* result = doc["result"] | "";
            double au = 0, dd = 0;
            if (space::ParseHorizonsRange(String(result), au, dd)) {
                res.deepTarget.distanceAu = au;
                res.deepTarget.speedKms = dd < 0 ? -dd : dd;
                res.deepTarget.receding = dd >= 0;
                res.deepTarget.valid = true;
                res.ok = true;
            } else {
                res.ok = false;
            }
            break;
        }
        case space::SpaceEndpoint::Flare:
            // SWPC GOES X-ray: a bare JSON array of {time_tag, flux, energy} objects.
            res.ok = space::ParseFlare(doc.as<JsonArrayConst>(), res.flare);
            break;
        case space::SpaceEndpoint::Humans:
            res.ok = space::ParseCrew(doc.as<JsonObjectConst>(), res.crew, 16);
            break;
        case space::SpaceEndpoint::Dsn:
            break; // handled above
    }
}
