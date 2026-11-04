// Blipscope web demo: the firmware's HttpRequestManager (src/HttpRequestManager.h, unchanged), with a body
// that opens no socket. Every request the radar makes is answered here from the fixed scene, exactly as
// the Worker's routes would shape it; anything the scene does not hold is a 404. The radar's own fetch
// and enrich tasks call this, parse what it returns with the firmware's parsers, and hand the results
// back over the firmware's queues.
#include "HttpRequestManager.h"
#include "web_scene.h"
#include <emscripten.h>
#include <ctime>

namespace {
String pathOf(const String& url)
{
    int s = url.indexOf("://");
    int p = url.indexOf('/', s < 0 ? 0 : s + 3);
    String path = p < 0 ? String("/") : url.substring(p);
    int q = path.indexOf('?');
    return q < 0 ? path : path.substring(0, q);
}
HttpResult answer(int status, const std::string& body)
{
    HttpResult r{};
    r.success = status >= 200 && status < 300;
    r.statusCode = status;
    r.response = String(body);
    r.errorMessage = r.success ? String() : String("demo: no such route");
    r.cacheState = "HIT";
    r.upstream = "demo";
    r.bodyBytes = body.size();
    r.requestMs = 1;
    return r;
}
// The route table: what the demo serves.
int route(const String& url, std::string& body)
{
    const String p = pathOf(url);
    if (p.endsWith("/blips")) {
        body = scene::Blips((long)std::time(nullptr), emscripten_get_now() / 1000.0);
        return 200;
    }
    const int e = p.indexOf("/enrich/");
    if (e >= 0) return scene::Enrich(p.substring(e + 8).str(), body) ? 200 : 404;
    return 404;   // /config, /airports, photos, the leaderboard, usage: not in the demo
}
}

String HttpRequestManager::BuildQueryString(const std::vector<std::pair<String, String>>&) const { return String(); }
String HttpRequestManager::ReadBodyYielding(HTTPClient&) { return String(); }

HttpResult HttpRequestManager::GetJsonImpl(const String& url, JsonDocument& doc, const JsonDocument* filter,
                                           const std::vector<std::pair<String, String>>&,
                                           const std::vector<std::pair<String, String>>&)
{
    std::string body;
    const int status = route(url, body);
    HttpResult r = answer(status, status == 200 ? body : std::string());
    if (status == 200) {
        const unsigned long t0 = millis();
        DeserializationError err = filter ? deserializeJson(doc, body, DeserializationOption::Filter(*filter))
                                          : deserializeJson(doc, body);
        r.parseMs = millis() - t0;
        if (err) { r.success = false; r.errorMessage = String("JSON: ") + err.c_str(); }
    }
    r.response = String();   // GetJson hands back the document, not the text
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
HttpResult HttpRequestManager::Get(const String& url, const std::vector<std::pair<String, String>>&,
                                   const std::vector<std::pair<String, String>>&)
{
    std::string body;
    const int status = route(url, body);
    return answer(status, status == 200 ? body : std::string());
}
HttpResult HttpRequestManager::Post(const String&, const String&, const std::vector<std::pair<String, String>>&)
{
    return answer(404, std::string());
}
HttpResult HttpRequestManager::GetSecure(const String&, const char*, const std::vector<std::pair<String, String>>&)
{
    return answer(404, std::string());
}
void HttpRequestManager::ReleaseTlsLocked() {}
