#pragma once
/* Minimal host stand-in for the ESP32 Arduino Preferences (NVS) class, so a
 * header that persists through it -- include/UsageStore.h -- can be exercised on
 * the host. In-memory, per process, shared across instances like NVS is across
 * handles. Only the calls UsageStore makes exist; anything else fails to compile,
 * which is the point: a test cannot quietly depend on NVS behaviour this does not
 * model. */

#include <cstdint>
#include <map>
#include <string>

class Preferences
{
  public:
    bool begin(const char* ns, bool /*readOnly*/ = false) { ns_ = ns ? ns : ""; return true; }
    void end() {}
    uint32_t getUInt(const char* key, uint32_t def = 0) const
    {
        const auto it = store().find(ns_ + "/" + key);
        return it == store().end() ? def : it->second;
    }
    size_t putUInt(const char* key, uint32_t v) { store()[ns_ + "/" + key] = v; return sizeof(v); }

  private:
    static std::map<std::string, uint32_t>& store() { static std::map<std::string, uint32_t> m; return m; }
    std::string ns_;
};
