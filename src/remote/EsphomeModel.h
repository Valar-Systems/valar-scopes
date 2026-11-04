#pragma once

/* ===========================================================================
 * ONE ESPHOME COVER DEVICE, AS ITS EVENT STREAM DESCRIBES IT (docs/remote-edition.md section 5).
 *
 * Fed (type, data) events from sse::Framer and the link's connect/disconnect edges and clock; produces the
 * snapshot F1's policy reads (coverpolicy::Device) and the one URL a command goes to. ArduinoJson only (the
 * firmware's own copy, so the host test grades the library that ships), no socket, task or Arduino core.
 *
 *   LIVE needs PROOF THE CONNECT DUMP HAS FINISHED. ESPHome drops bare state updates while its dump is
 *   still iterating (web_server.cpp, deferrable_send_state), so the dump's own cover entry can be stale and
 *   the first bare event -- no "domain" field -- is the proof. Until then the device is Unknown, but the
 *   dump's values still land in the snapshot, so a STOP decision uses the freshest operation there is.
 *
 *   VALUES ARE VALIDATED, NEVER COERCED. A position outside 0..1 or an operation that is not IDLE/OPENING/
 *   CLOSING rejects the event whole; the previous values stand. Clamping 1.2 to 1.0, or reading "WOBBLING" as
 *   IDLE, would turn a message we do not understand into a plausible state -- the most expensive exchange
 *   in CLAUDE.md.
 *
 *   OBSTRUCTION (Glasscalibur, ruled 2026-10-30) is an increase in StallGuard Trips or Vibration Stall Trips
 *   over the baseline taken at each connect, so trips while the remote was not listening never fire.
 *   TAMPER is Tamper Detected ON or State TAMPERED; acknowledging it lasts until it clears and returns.
 * ======================================================================== */

#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>

#include <ArduinoJson.h>

#include "CoverPolicy.h"

namespace esphome {

constexpr uint32_t SILENCE_MS = 25000;   // pings come every 10 s; two missed and a margin is Offline
constexpr size_t   NAME_MAX = 64;

struct Counter {
    bool     based = false;
    uint32_t base = 0;
};

struct Model {
    // ---- the connection ----
    bool     connected = false;
    bool     coverSeen = false;   // this connection delivered the cover's state
    bool     bareSeen = false;    // ...and a bare state event: the dump has finished
    bool     unsupported = false; // a second, different cover: no commands, never Live
    uint32_t lastHeardMs = 0;
    // ---- the cover's identity ----
    char     coverName[NAME_MAX] = {0};   // the entity name (name_id or the 2026.8 id), or ""
    char     objectId[NAME_MAX] = {0};    // the legacy object id, or ""
    // ---- its state ----
    coverpolicy::Op op = coverpolicy::Op::Idle;
    coverpolicy::Op lastDirection = coverpolicy::Op::Idle;
    uint16_t positionPermille = 0;
    bool     positionKnown = false;
    bool     motionSeen = false;
    uint32_t lastMotionMs = 0;
    // ---- alarms ----
    bool     tamperBinary = false, tamperState = false;
    bool     tamperWas = false, tamperAcked = false;
    bool     obstructionPending = false;
    Counter  stallGuard, vibration;
    // ---- instruments ----
    uint32_t rejects = 0;   // events refused for an out-of-range or unknown value
    uint32_t parses = 0;    // events that were JSON-parsed at all
};

inline void OnConnect(Model& m, uint32_t now)
{
    m.connected = true;
    m.coverSeen = false;
    m.bareSeen = false;
    m.unsupported = false;
    m.coverName[0] = m.objectId[0] = '\0';
    m.lastHeardMs = now;
    m.stallGuard.based = false;   // re-baselined: trips while we were not listening never fire
    m.vibration.based = false;
}

inline void OnDisconnect(Model& m) { m.connected = false; }

inline bool Silent(const Model& m, uint32_t now) { return now - m.lastHeardMs >= SILENCE_MS; }

inline coverpolicy::Conn ConnOf(const Model& m, uint32_t now)
{
    if (!m.connected || Silent(m, now)) return coverpolicy::Conn::Offline;
    if (m.unsupported || !m.coverSeen || !m.bareSeen) return coverpolicy::Conn::Unknown;
    return coverpolicy::Conn::Live;
}

inline bool TamperPending(const Model& m) { return (m.tamperBinary || m.tamperState) && !m.tamperAcked; }

inline coverpolicy::Device Snapshot(const Model& m, uint32_t now)
{
    coverpolicy::Device d;
    d.conn = ConnOf(m, now);
    d.op = m.op;
    d.lastDirection = m.lastDirection;
    d.positionPermille = m.positionPermille;
    d.motionSeen = m.motionSeen;
    d.lastMotionMs = m.lastMotionMs;
    d.alarmPending = TamperPending(m) || m.obstructionPending;
    return d;
}

/// The person acknowledged the takeover: every pending alarm on this device.
inline void Ack(Model& m)
{
    if (m.tamperBinary || m.tamperState) m.tamperAcked = true;
    m.obstructionPending = false;
}

// ----------------------------------------------------------------------------- reading events

/// Is this event about something the model reads? Answered on the raw text, so the accelerometer, the logs
/// and every other entity pass through without a JSON parse. Both the name form (2026.6.5's name_id, the
/// 2026.8 id) and the legacy object-id form are recognised.
inline bool Relevant(const std::string& data)
{
    static const char* const keys[] = {
        "\"cover/", "\"cover-",
        "\"text_sensor/State\"", "\"text_sensor-state\"",
        "\"binary_sensor/Tamper Detected\"", "\"binary_sensor-tamper_detected\"",
        "\"sensor/StallGuard Trips\"", "\"sensor-stallguard_trips\"",
        "\"sensor/Vibration Stall Trips\"", "\"sensor-vibration_stall_trips\"",
    };
    for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); ++i)
        if (data.find(keys[i]) != std::string::npos) return true;
    return false;
}

inline void Copy(char* dst, const char* src, size_t n)
{
    std::strncpy(dst, src, NAME_MAX - 1);
    dst[NAME_MAX - 1] = '\0';
    if (n < NAME_MAX) dst[n] = '\0';
}

inline void StampMotion(Model& m, uint32_t now)
{
    m.motionSeen = true;
    m.lastMotionMs = now;
}

inline void ApplyCover(Model& m, JsonObjectConst o, const char* name, size_t nameLen, bool byName, uint32_t now)
{
    // Identity first: the first cover is the device's; a different second one makes it unsupported. So does a
    // name too long to hold whole -- a truncated name is a different URL, never a shorter spelling of this one.
    if (nameLen >= NAME_MAX) { m.unsupported = true; return; }
    char key[NAME_MAX];
    Copy(key, name, nameLen);
    if (!m.coverName[0] && !m.objectId[0]) {
        if (byName) Copy(m.coverName, name, nameLen);
        else Copy(m.objectId, name, nameLen);
    } else if (std::strcmp(byName ? m.coverName : m.objectId, key) != 0) {
        m.unsupported = true;
        return;
    }
    // Then the values, validated whole before any is applied.
    const char* opStr = o["current_operation"] | (const char*)nullptr;
    coverpolicy::Op op;
    if (!opStr) { ++m.rejects; return; }
    if (std::strcmp(opStr, "IDLE") == 0) op = coverpolicy::Op::Idle;
    else if (std::strcmp(opStr, "OPENING") == 0) op = coverpolicy::Op::Opening;
    else if (std::strcmp(opStr, "CLOSING") == 0) op = coverpolicy::Op::Closing;
    else { ++m.rejects; return; }
    bool hasPos = false;
    uint16_t permille = m.positionPermille;
    if (!o["position"].isNull()) {
        if (!o["position"].is<float>()) { ++m.rejects; return; }
        const float pos = o["position"].as<float>();
        if (!(pos >= 0.0f && pos <= 1.0f)) { ++m.rejects; return; }
        hasPos = true;
        permille = (uint16_t)std::lround(pos * 1000.0f);
    }
    if (op != coverpolicy::Op::Idle) {
        m.lastDirection = op;
        StampMotion(m, now);
    }
    if (hasPos && (!m.positionKnown || permille != m.positionPermille)) {
        if (m.positionKnown) StampMotion(m, now);
        m.positionPermille = permille;
        m.positionKnown = true;
    }
    m.op = op;
    m.coverSeen = true;
}

inline void ApplyCounter(Model& m, Counter& c, JsonObjectConst o)
{
    if (!o["value"].is<float>()) { ++m.rejects; return; }
    const float v = o["value"].as<float>();
    if (!(v >= 0.0f) || v != std::floor(v)) { ++m.rejects; return; }
    const uint32_t n = (uint32_t)v;
    if (c.based && n > c.base) m.obstructionPending = true;
    c.base = n;          // an increase, or a reset button's decrease: either way the new baseline
    c.based = true;
}

inline void Retamper(Model& m)
{
    const bool tamper = m.tamperBinary || m.tamperState;
    if (tamper && !m.tamperWas) m.tamperAcked = false;   // a new episode is pending again
    m.tamperWas = tamper;
}

/// One SSE event. `now` stamps liveness and motion.
inline void OnEvent(Model& m, const std::string& type, const std::string& data, uint32_t now)
{
    m.lastHeardMs = now;   // any event at all, a ping included, is the device talking
    if (type != "state") return;
    if (data.find("\"domain\":") == std::string::npos) m.bareSeen = true;   // the dump has finished
    if (!Relevant(data)) return;

    JsonDocument doc;
    ++m.parses;
    if (deserializeJson(doc, data)) { ++m.rejects; return; }
    JsonObjectConst o = doc.as<JsonObjectConst>();
    const char* key = o["name_id"] | (const char*)nullptr;
    if (!key) key = o["id"] | (const char*)nullptr;
    if (!key) return;

    // name form "<domain>/<name>" (2026.6.5's name_id; 2026.8's id), else legacy "<domain>-<object_id>"
    const char* slash = std::strchr(key, '/');
    const char* dash = std::strchr(key, '-');
    const char* sep = slash ? slash : dash;
    if (!sep) return;
    const std::string domain(key, (size_t)(sep - key));
    const char* rest = sep + 1;
    const bool byName = slash != nullptr;

    if (domain == "cover") {
        ApplyCover(m, o, rest, std::strlen(rest), byName, now);
    } else if (domain == "text_sensor" && (byName ? std::strcmp(rest, "State") == 0 : std::strcmp(rest, "state") == 0)) {
        const char* v = o["value"] | "";
        m.tamperState = std::strcmp(v, "TAMPERED") == 0;
        Retamper(m);
    } else if (domain == "binary_sensor"
               && (byName ? std::strcmp(rest, "Tamper Detected") == 0 : std::strcmp(rest, "tamper_detected") == 0)) {
        m.tamperBinary = o["value"] | false;
        Retamper(m);
    } else if (domain == "sensor") {
        if (byName ? std::strcmp(rest, "StallGuard Trips") == 0 : std::strcmp(rest, "stallguard_trips") == 0)
            ApplyCounter(m, m.stallGuard, o);
        else if (byName ? std::strcmp(rest, "Vibration Stall Trips") == 0 : std::strcmp(rest, "vibration_stall_trips") == 0)
            ApplyCounter(m, m.vibration, o);
    }
}

// ----------------------------------------------------------------------------- the one URL

/// The path a command goes to: "/cover/<name>/<action>", the name percent-encoded; the legacy object id
/// when the firmware sends no name. False when there is no cover, the device is unsupported, or the intent
/// is not a motion command.
inline bool CommandPath(const Model& m, coverpolicy::Intent i, char* out, size_t cap)
{
    const char* action = i == coverpolicy::Intent::Open ? "open"
                       : i == coverpolicy::Intent::Close ? "close"
                       : i == coverpolicy::Intent::Stop ? "stop" : nullptr;
    if (!action || m.unsupported || (!m.coverName[0] && !m.objectId[0])) return false;
    std::string p = "/cover/";
    const char* src = m.coverName[0] ? m.coverName : m.objectId;
    static const char* const hex = "0123456789ABCDEF";
    for (const char* c = src; *c; ++c) {
        const unsigned char u = (unsigned char)*c;
        if ((u >= 'A' && u <= 'Z') || (u >= 'a' && u <= 'z') || (u >= '0' && u <= '9') || u == '-' || u == '.' || u == '_' || u == '~') {
            p.push_back((char)u);
        } else {
            p.push_back('%');
            p.push_back(hex[u >> 4]);
            p.push_back(hex[u & 15]);
        }
    }
    p += "/";
    p += action;
    if (p.size() + 1 > cap) return false;
    std::memcpy(out, p.c_str(), p.size() + 1);
    return true;
}

} // namespace esphome
