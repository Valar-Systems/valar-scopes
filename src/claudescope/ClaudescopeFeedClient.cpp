#include "ClaudescopeFeedClient.h"

using claudescope::ClaudeFetchRequest;
using claudescope::ClaudeFetchResult;

namespace {

// A polite UA so a picky sidecar (or a reverse proxy in front of it) can identify the device. No
// key, nothing identifying the user.
const char* USER_AGENT = "Blipscope-Claudescope/1 (+https://github.com/Valar-Systems/Blipscope)";

// Turn the configured sidecar base into the usage endpoint URL. If the user pasted the full JSON
// URL (ends in .json) it's used as-is; otherwise we append the sidecar's well-known path.
String UsageUrl(const String& base)
{
    String u = base;
    u.trim();
    if (u.isEmpty()) return String();
    if (u.endsWith(".json")) return u;
    while (u.endsWith("/")) u.remove(u.length() - 1);
    u += "/usage.json";
    return u;
}

} // namespace

void ClaudescopeFeedClient::Begin()
{
    if (taskHandle != nullptr) return; // survives config reloads

    reqQueue = xQueueCreate(1, sizeof(ClaudeFetchRequest*));
    resQueue = xQueueCreate(1, sizeof(ClaudeFetchResult*));

    // Pinned to core 0 (the Wi-Fi core) at priority 1, like the radar/space fetch tasks, so the
    // blocking network work never competes with the loop/render task. 12 KB stack like every
    // other feed worker: cl-base-url may be an https reverse proxy, and the mbedTLS handshake
    // runs on this task's stack (10 KB was 2 KB under the established TLS minimum).
    xTaskCreatePinnedToCore(Trampoline, "claude_fetch", 12288, this, 1, &taskHandle, 0);
}

void ClaudescopeFeedClient::Configure(const Config& newCfg)
{
    cfg = newCfg;

    float sc = cfg.intervalScale;
    if (sc < 1.0f) sc = 1.0f;
    if (sc > 8.0f) sc = 8.0f;
    intervalMs = (uint32_t)(USAGE_MS * sc);

    failCount = 0;
    firstLogged = false;         // re-confirm the feed after a (re)configure
    nextDueMs = millis() + 500;  // first poll shortly after (re)config
}

void ClaudescopeFeedClient::Poll()
{
    // 1. apply a ready result (frees its heap) and clear the in-flight gate.
    ClaudeFetchResult* res = nullptr;
    if (xQueueReceive(resQueue, &res, 0) == pdTRUE && res != nullptr) {
        ApplyResult(*res);
        delete res;
        inFlight = false;
    }
    if (inFlight) return; // one fetch at a time over the shared client

    // 2. dispatch when due.
    const uint32_t now = millis();
    if ((int32_t)(now - nextDueMs) < 0) return; // signed compare handles millis() wrap

    ClaudeFetchRequest* req = new ClaudeFetchRequest();
    if (!BuildRequest(*req)) {
        delete req;
        nextDueMs = now + intervalMs; // e.g. no sidecar configured yet -- re-arm, don't spin
        return;
    }

    if (xQueueSend(reqQueue, &req, 0) == pdTRUE) {
        inFlight = true;
    } else {
        delete req; // queue unexpectedly full; try again next Poll()
    }
}

bool ClaudescopeFeedClient::BuildRequest(ClaudeFetchRequest& req) const
{
    const String url = UsageUrl(cfg.baseUrl);
    if (url.isEmpty()) return false; // nothing configured -> nothing polled

    req.endpoint = claudescope::ClaudeEndpoint::Usage;
    req.url = url;
    req.headers.push_back({"User-Agent", USER_AGENT});
    return true;
}

void ClaudescopeFeedClient::ApplyResult(const ClaudeFetchResult& res)
{
    const uint32_t now = millis();

    if (!res.ok) {
        // Exponential backoff; keep the previously-stored snapshot so the gauges don't blank.
        failCount++;
        const uint16_t shift = failCount > 5 ? 5 : failCount;
        uint32_t backoff = intervalMs << shift;
        if (backoff > MAX_BACKOFF_MS || backoff < intervalMs) backoff = MAX_BACKOFF_MS;
        if (backoff < intervalMs) backoff = intervalMs; // never poll a FAILING feed faster than a healthy one
        nextDueMs = now + backoff;
        return;
    }

    failCount = 0;
    nextDueMs = now + intervalMs;
    usage = res.usage;

    if (!firstLogged) {
        firstLogged = true;
        Serial.printf("[claude] usage ok: plan=%s session=%.0f%% week=%.0f%% models=%u\n",
                      usage.planName.c_str(),
                      usage.session.valid ? usage.session.pct : -1.0f,
                      usage.weekAll.valid ? usage.weekAll.pct : -1.0f,
                      (unsigned)usage.weekModels.size());
    }
}

void ClaudescopeFeedClient::Trampoline(void* arg)
{
    static_cast<ClaudescopeFeedClient*>(arg)->RunWorker();
}

void ClaudescopeFeedClient::RunWorker()
{
    for (;;) {
        ClaudeFetchRequest* req = nullptr;
        if (xQueueReceive(reqQueue, &req, portMAX_DELAY) != pdTRUE || req == nullptr)
            continue;

        ClaudeFetchResult* res = new ClaudeFetchResult();
        res->endpoint = req->endpoint;
        Fetch(http, *req, *res);
        delete req;

        xQueueSend(resQueue, &res, portMAX_DELAY);
    }
}

void ClaudescopeFeedClient::Fetch(HttpRequestManager& http,
                                  const ClaudeFetchRequest& req, ClaudeFetchResult& res)
{
    JsonDocument doc;
    const HttpResult r = http.GetJson(req.url, doc, req.params, req.headers);
    if (!r.success || r.statusCode < 200 || r.statusCode >= 300) {
        res.ok = false;
        return;
    }
    claudescope::ParseUsage(doc.as<JsonObjectConst>(), res.usage);
    res.ok = res.usage.valid; // a 200 with no recognizable window is a failed poll (keep last-good)
}
