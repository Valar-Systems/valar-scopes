// Blipscope web renderer: the firmware's HttpRequestManager (src/HttpRequestManager.h, unchanged) with a body
// that fetches from the PAGE'S OWN ORIGIN (docs/web-renderer.md).
//
// The firmware builds every URL itself (CloudFeed::BlipsUrl / EnrichUrl and its query); this file only decides
// where a request may go:
//   - under the configured cloud base, the paths web_route.h forwards (/blips, /enrich/, and /airports only when the
//     page sets "airports-remote") are re-rooted onto the page's apiBase (default "": the same origin) and fetched
//     with NO headers -- the firmware's X-Blip-* set never leaves the page;
//   - everything else (/config, /airports when not asked for, photos, any other host) is answered here with 404 and
//     no request;
//   - every Post() is answered here with 404 and no request (the leaderboard, ntfy, reports).
// A fetch runs in the browser while the calling task's fiber yields frame by frame (web_rtos.cpp); 15 s without
// an answer reads as status 0, the way a dead socket reads on the device.
#include "HttpRequestManager.h"
#include "web_route.h"     // which requests are forwarded (docs/web-kiosk-airfield.md)
#include <freertos/task.h>
#include <emscripten.h>
#include <emscripten/stack.h>
#include <string>

// ---- the page's half (render.js may install Module.blipscopeFetch; the default is a same-origin fetch) ----
EM_JS(int, js_fetch_start, (const char* url), {
    const u = UTF8ToString(url);
    Module._webFetches = Module._webFetches || new Map();
    Module._webFetchId = (Module._webFetchId || 0) + 1;
    const id = Module._webFetchId;
    const rec = { done: false, status: 0, body: new Uint8Array(0) };
    Module._webFetches.set(id, rec);
    const run = Module.blipscopeFetch || ((x) => fetch(x, { credentials: "omit", cache: "no-store" })
        .then(async (r) => ({ status: r.status, body: new Uint8Array(await r.arrayBuffer()) })));
    Promise.resolve().then(() => run(u)).then((r) => {
        rec.status = r.status | 0;
        rec.body = r.body instanceof Uint8Array ? r.body : new TextEncoder().encode(String(r.body || ""));
        rec.done = true;
    }, () => { rec.status = 0; rec.done = true; });
    return id;
});
EM_JS(int, js_fetch_poll, (int id), {
    const rec = Module._webFetches && Module._webFetches.get(id);
    return !rec ? 0 : (rec.done ? rec.status : -1);
});
EM_JS(int, js_fetch_len, (int id), {
    const rec = Module._webFetches.get(id);
    return rec ? rec.body.length : 0;
});
EM_JS(void, js_fetch_take, (int id, char* dst, int len), {
    const rec = Module._webFetches.get(id);
    if (rec) HEAPU8.set(rec.body.subarray(0, len), dst);
    Module._webFetches.delete(id);
});
EM_JS(void, js_fetch_drop, (int id), { if (Module._webFetches) Module._webFetches.delete(id); });

namespace web {
String g_cloudBase;                 // web_config.cpp: the firmware's "cloud-url", which only this file reads
bool g_airportsRemote = false;      // web_config.cpp: the page's "airports-remote", off unless exactly "true"
std::string g_apiBase;              // the page's apiBase ("" = the page's own origin)
struct NetStats { uint32_t blips = 0, enrich = 0, local404 = 0, posts = 0, timeouts = 0, fromLoop = 0; } g_net;
}

namespace {
constexpr unsigned long FETCH_TIMEOUT_MS = 15000;

// HttpRequestManager::BuildQueryString (src/HttpRequestManager.cpp:170), TRANSCRIBED: the real file needs the
// ESP32 HTTP client and cannot compile here. "?k=v&k=v", nothing encoded -- the device sends exactly this.
String Query(const std::vector<std::pair<String, String>>& params)
{
    if (params.empty()) return "";
    String q = "?";
    bool first = true;
    for (const auto& [k, v] : params) {
        if (!first) q += "&";
        q += k + "=" + v;
        first = false;
    }
    return q;
}

// The same-origin URL a request may go to, or "" when it must be answered here.
std::string Route(const String& url, const std::vector<std::pair<String, String>>& params, bool& isBlips)
{
    isBlips = false;
    if (web::g_cloudBase.isEmpty() || !url.startsWith(web::g_cloudBase)) return "";
    const String path = url.substring(web::g_cloudBase.length());
    if (!webroute::Forwarded(path.str(), web::g_airportsRemote)) return "";
    isBlips = webroute::IsBlips(path.str());
    return web::g_apiBase + (path + Query(params)).str();
}

HttpResult Answer(int status, std::string body)
{
    HttpResult r{};
    r.success = status >= 200 && status < 300;
    r.statusCode = status;
    r.response = String(body);
    r.errorMessage = r.success ? String() : (status == 0 ? String("web: no answer") : String("web: HTTP ") + status);
    r.bodyBytes = body.size();
    return r;
}

// Fetch on the page's origin; yields the calling task's fiber until the answer arrives.
int Fetch(const std::string& target, std::string& body)
{
    if (xTaskGetCurrentTaskHandle() == nullptr) {   // the frame loop cannot wait; the firmware never asks it to
        web::g_net.fromLoop++;
        return 0;
    }
    const int id = js_fetch_start(target.c_str());
    const unsigned long t0 = millis();
    int st;
    while ((st = js_fetch_poll(id)) < 0) {
        if (millis() - t0 > FETCH_TIMEOUT_MS) { js_fetch_drop(id); web::g_net.timeouts++; return 0; }
        vTaskDelay(10);
    }
    const int n = js_fetch_len(id);
    body.assign((size_t)n, '\0');
    js_fetch_take(id, n ? &body[0] : nullptr, n);
    return st;
}
}

String HttpRequestManager::BuildQueryString(const std::vector<std::pair<String, String>>& params) const { return Query(params); }
String HttpRequestManager::ReadBodyYielding(HTTPClient&) { return String(); }

HttpResult HttpRequestManager::GetJsonImpl(const String& url, JsonDocument& doc, const JsonDocument* filter,
                                           const std::vector<std::pair<String, String>>& params,
                                           const std::vector<std::pair<String, String>>&)
{
    bool isBlips = false;
    const std::string target = Route(url, params, isBlips);
    if (target.empty()) { web::g_net.local404++; return Answer(404, ""); }
    (isBlips ? web::g_net.blips : web::g_net.enrich)++;
    const unsigned long t0 = millis();
    std::string body;
    const int status = Fetch(target, body);
    HttpResult r = Answer(status, std::string());
    r.bodyBytes = body.size();
    r.requestMs = millis() - t0;
    if (r.success) {
        const unsigned long p0 = millis();
        DeserializationError err = filter ? deserializeJson(doc, body, DeserializationOption::Filter(*filter))
                                          : deserializeJson(doc, body);
        r.parseMs = millis() - p0;
        if (err) { r.success = false; r.errorMessage = String("JSON: ") + err.c_str(); }
    }
    return r;
}

HttpResult HttpRequestManager::GetJson(const String& url, JsonDocument& doc,
                                       const std::vector<std::pair<String, String>>& params,
                                       const std::vector<std::pair<String, String>>& headers)
{
    return GetJsonImpl(url, doc, nullptr, params, headers);
}
HttpResult HttpRequestManager::GetJson(const String& url, JsonDocument& doc, const JsonDocument& filter,
                                       const std::vector<std::pair<String, String>>& params,
                                       const std::vector<std::pair<String, String>>& headers)
{
    return GetJsonImpl(url, doc, &filter, params, headers);
}
// Get() is the photo path (and nothing else on the cloud build): photos are not part of the web renderer.
HttpResult HttpRequestManager::Get(const String&, const std::vector<std::pair<String, String>>&,
                                   const std::vector<std::pair<String, String>>&)
{
    web::g_net.local404++;
    return Answer(404, "");
}
// GetBytes() is the weather layer's path (WxPolicy), asked only with wx-layer on: the website does not draw weather
// (docs/wx-device.md, (A)). If it is ever asked, it answers as a dead socket -- status 0, nothing read, no request.
HttpResult HttpRequestManager::GetBytes(const String&, const std::vector<std::pair<String, String>>&,
                                        const std::vector<std::pair<String, String>>&, uint8_t*, size_t, size_t& len,
                                        String& etag, String& maxAge)
{
    len = 0;
    etag = String();
    maxAge = String();
    return Answer(0, "");
}
HttpResult HttpRequestManager::Post(const String&, const String&, const std::vector<std::pair<String, String>>&)
{
    web::g_net.posts++;
    return Answer(404, "");
}
HttpResult HttpRequestManager::GetSecure(const String&, const char*, const std::vector<std::pair<String, String>>&)
{
    web::g_net.local404++;
    return Answer(404, "");
}
void HttpRequestManager::ReleaseTlsLocked() {}

// A test probe (docs/web-renderer.md, R6): one GET through the same routing, from the page. An off-base or
// not-allowed URL is answered here (404) without a request; an allowed one cannot wait on the page's own stack
// and reads status 0 (it is counted as fromLoop).
extern "C" EMSCRIPTEN_KEEPALIVE int web_http_probe(const char* url)
{
    HttpRequestManager h;
    JsonDocument doc;
    return h.GetJson(String(url ? url : ""), doc).statusCode;
}

extern "C" EMSCRIPTEN_KEEPALIVE const uint32_t* web_net_stats()
{
    static uint32_t s[6];
    s[0] = web::g_net.blips; s[1] = web::g_net.enrich; s[2] = web::g_net.local404;
    s[3] = web::g_net.posts; s[4] = web::g_net.timeouts; s[5] = web::g_net.fromLoop;
    return s;
}
