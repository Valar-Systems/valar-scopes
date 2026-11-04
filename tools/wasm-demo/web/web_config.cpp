// Blipscope web demo: the firmware's ConfigurationWebServer (src/ConfigurationWebServer.h, unchanged) with a
// body that serves no page. The radar reads its settings through GetStoredString; the demo answers from
// the map below and returns "" for every other key, so the firmware's own default applies -- the same
// thing a factory-fresh device does. Change a value here and rebuild to see the radar with it.
#include "ConfigurationWebServer.h"
#include "web_scene.h"
#include <map>
#include <string>

namespace {
const std::map<std::string, std::string>& settings()
{
    static const std::map<std::string, std::string> m = {
        {"latitude", "44.0582"},           // Bend, Oregon (city centre)
        {"longitude", "-121.3153"},
        {"radius", "40"},
        {"radius-unit", "km"},
        {"data-source", "cloud"},          // the Blipscope Cloud path: /blips + /enrich, answered by the scene
        {"cloud-url", "https://demo.invalid"},
        {"cloud-key", "demo"},
        {"tz-offset", "-8"},               // PST
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
