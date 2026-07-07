#pragma once

#include <Arduino.h>
#include <vector>
#include <utility>
#include <ArduinoJson.h>

// Typed models for the Speedscope edition data plus the parsers that build them off the wire, and
// the request/result envelopes the background poller hands across the task boundary. Speedscope ties
// into a MiniSpeedCam (minispeedcam.com) over the LAN, reading its two keyless local endpoints:
//   - GET /api/state  -> device health + settings + live radar proximity (`signal`)
//   - GET /api/events -> a ring of recent vehicle passes (speed + age + direction)
// The /api/events endpoint is added by the MiniSpeedCam r1.1 firmware (see its events.h). All fields
// are best-effort; the screens degrade gracefully when a poll hasn't landed. Speeds stay in the
// device's configured unit (the DeviceState.isKph / EventList.kph flags say which).
namespace speed {

// -------------------------------------------------------------------- GET /api/state
// A snapshot of the camera's health, its current settings, and the live radar proximity magnitude.
// Mirrors the keys emitted by the MiniSpeedCam config portal's portalBuildState().
struct DeviceState {
    bool valid = false;
    unsigned long fetchedAtMs = 0; // millis() of the last good fetch (freshness / "ago")

    // live
    long   signal = 0;             // g_last_peak_mag -- radar proximity/motion proxy (0 = clear)
    long   freeHeap = 0;
    long   freePsram = 0;
    String uptime;                 // "12h 03m 04s"
    String rssi;                   // "-54" (dBm) or "n/a"
    String ip;                     // "192.168.x.y" or "n/a"
    int    lastUpload = 0;         // HTTP status of the camera's last cloud upload (0 = none yet)
    String resetReason;
    int    bootCount = 0;
    String fwVersion;              // "ESP n / STM m"
    String otaStatus;
    String claim;                  // "Linked to your account" or a claim code
    bool   streamActive = false;

    // settings
    bool isKph = false;
    bool powerSaver = false;
    int  minSpeed = 0, photoSpeed = 0;
    int  minSignal = 0, photoSignal = 0, psFront = 0, psRear = 0;
    String ssid;
};

// -------------------------------------------------------------------- GET /api/events
// One recent detection. The device has no RTC, so it reports `ageSec` (device-relative); we stamp an
// absolute `epoch` at parse time from the local NTP clock so stats windows survive the next poll.
struct SpeedRecord {
    int  speed = 0;   // run max speed, device units
    int  ageSec = 0;  // seconds since the pass (device clock)
    long mag = 0;     // peak FFT magnitude (proximity/confidence proxy)
    int  dir = 0;     // 0 unknown, 1 approaching, 2 receding
    long epoch = 0;   // absolute epoch (Blipscope clock), 0 before NTP
};

struct EventList {
    bool valid = false;
    bool kph = false;                    // unit for these speeds
    std::vector<SpeedRecord> events;     // newest first
    unsigned long fetchedAtMs = 0;
};

// --------------------------------------------------------------- poller request / result
enum class SpeedEndpoint : uint8_t { State, Events };

struct SpeedFetchRequest {
    SpeedEndpoint endpoint = SpeedEndpoint::State;
    String url;
    std::vector<std::pair<String, String>> params;
    std::vector<std::pair<String, String>> headers;
};

struct SpeedFetchResult {
    SpeedEndpoint endpoint = SpeedEndpoint::State;
    bool ok = false;             // transport-level success (2xx + parsed)
    DeviceState state;
    EventList events;
};

// -------------------------------------------------------------------------------- parsers
// Read the MiniSpeedCam JSON (or a normalized aggregator shape) into `out`, tolerating missing fields.
void ParseDeviceState(JsonObjectConst root, DeviceState& out);
// nowUtc stamps each event's absolute epoch from ageSec (0 if NTP hasn't synced yet).
void ParseEvents(JsonObjectConst root, EventList& out, long nowUtc);

} // namespace speed
