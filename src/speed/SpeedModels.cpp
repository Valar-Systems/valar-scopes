#include "SpeedModels.h"

namespace speed {

void ParseDeviceState(JsonObjectConst root, DeviceState& out)
{
    out = DeviceState();
    if (root.isNull()) return;

    // live
    out.signal      = root["signal"]    | 0L;
    out.freeHeap    = root["heap"]       | 0L;
    out.freePsram   = root["psram"]      | 0L;
    out.uptime      = (const char*)(root["uptime"]      | "");
    out.rssi        = (const char*)(root["rssi"]        | "n/a");
    out.ip          = (const char*)(root["ip"]          | "n/a");
    out.lastUpload  = root["lastUpload"] | 0;
    out.resetReason = (const char*)(root["resetReason"] | "");
    out.bootCount   = root["bootCount"]  | 0;
    out.fwVersion   = (const char*)(root["fwVersion"]   | "");
    out.otaStatus   = (const char*)(root["otaStatus"]   | "");
    out.claim       = (const char*)(root["claim"]       | "");
    out.streamActive = root["streamActive"] | false;

    // settings
    out.isKph       = root["isKph"]      | false;
    out.powerSaver  = root["powerSaver"] | false;
    out.minSpeed    = root["minSpeed"]   | 0;
    out.photoSpeed  = root["photoSpeed"] | 0;
    out.minSignal   = root["minSignal"]  | 0;
    out.photoSignal = root["photoSignal"] | 0;
    out.psFront     = root["psFront"]    | 0;
    out.psRear      = root["psRear"]     | 0;
    out.ssid        = (const char*)(root["ssid"] | "");

    // A response we could read at all counts as a live device (even a fresh boot with no passes).
    out.valid = true;
}

void ParseEvents(JsonObjectConst root, EventList& out, long nowUtc)
{
    out = EventList();
    if (root.isNull()) return;

    out.kph = root["kph"] | false;

    JsonArrayConst arr = root["events"].as<JsonArrayConst>();
    if (!arr.isNull()) {
        for (JsonObjectConst e : arr) {
            if (out.events.size() >= 32) break; // comfortably above the camera's ring; bounds a hostile proxy reply
            SpeedRecord r;
            r.speed  = e["speed"]  | 0;
            r.ageSec = e["ageSec"] | 0;
            r.mag    = e["mag"]    | 0L;
            r.dir    = e["dir"]    | 0;
            // Turn the device-relative age into an absolute time using our own NTP clock, so
            // "today" windows and "ago" labels stay stable across polls (the device has no RTC).
            r.epoch  = (nowUtc > 1600000000L && r.ageSec >= 0) ? (nowUtc - r.ageSec) : 0;
            out.events.push_back(r);
        }
    }
    // A well-formed reply (even with an empty ring) is valid -- it proves the events endpoint exists.
    out.valid = true;
}

} // namespace speed
