// Blipscope web renderer: the firmware's ConfigurationWebServer (src/ConfigurationWebServer.h, unchanged) with a
// body that serves no page. The radar reads every setting through GetStoredString; this answers from a map the
// PAGE fills before web_start() (render.js: mount(canvas, {lat, lon, radius, unit, ...})). Every key the page
// does not set returns "", so the firmware's own default applies, as on a factory-fresh device -- except the
// fixed web values below, which stub what a website has no use for (docs/web-renderer.md, "Stubbed").
#include "ConfigurationWebServer.h"
#include "CloudFeed.h"
#include <emscripten.h>
#include "web_route.h"
#include <map>
#include <string>

namespace web {
extern String g_cloudBase;
extern bool g_airportsRemote;
extern std::string g_apiBase;
}

namespace {
std::map<std::string, std::string>& settings()
{
    static std::map<std::string, std::string> m = {
        // The cloud path is the data path; its base is a placeholder that web_http.cpp re-roots onto the page's
        // own origin. The key is a placeholder too: web_http.cpp sends no header, so no key ever leaves.
        {"data-source", "cloud"},
        {"cloud-url", "https://web.invalid"},
        {"cloud-key", "web"},
        {"logbook", "false"},        // its storage would be memory for the page's life: off unless the page asks
        {"lb-enabled", "false"},
        {"mqtt", "false"},
        {"ntfy-topic", ""},
        {"wx-layer", "false"},       // in writing: the firmware reads ABSENT as ON, and this renderer has no pixels
                                     // for the layer (docs/wx-device.md, (A)). check_wx_off.mjs refuses its removal.
    };
    return m;
}
}

void ConfigurationWebServer::Initialise() { listening = false; }
bool ConfigurationWebServer::ConsumeLogbookFlushRequest() { return false; }
const String ConfigurationWebServer::GetStoredString(const char* key)
{
    auto it = settings().find(key ? key : "");
    return it == settings().end() ? String() : String(it->second);
}
bool ConfigurationWebServer::HasStoredKey(const char* key) { return settings().count(key ? key : "") > 0; }
bool ConfigurationWebServer::ConsumeConfigChanged() { return false; }
factoryreset::Tier ConfigurationWebServer::ConsumeResetTier() { return factoryreset::Tier::None; }
void ConfigurationWebServer::RequestReset(factoryreset::Tier) {}

// The base the firmware will build its URLs on, by the firmware's own rule (CloudFeed::NormalizeBaseUrl).
void web_config_commit()
{
    web::g_cloudBase = CloudFeed::NormalizeBaseUrl(String(settings()["cloud-url"]));
    // The venue's own field (docs/web-kiosk-airfield.md): /airports goes to the page's apiBase only when asked.
    web::g_airportsRemote = webroute::AirportsRemoteOn(settings()["airports-remote"]);
}

extern "C" {
// Before web_start(): one setting, as the config page would store it ("latitude" "44.0582", "radius-unit" "km", ...).
EMSCRIPTEN_KEEPALIVE void web_config_set(const char* key, const char* value)
{
    if (key) settings()[key] = value ? value : "";
}
// Where same-origin requests are rooted ("" = the page's own origin; "/radar" for a sub-path).
EMSCRIPTEN_KEEPALIVE void web_set_api_base(const char* base) { web::g_apiBase = base ? base : ""; }
}
