#pragma once
#include <stdint.h>
#include <string.h>

/* ===========================================================================
 * WI-FI JOIN DIAGNOSTICS -- the RAM half.
 *
 * WHY (2026-09-12). A board that joins at home would not join an eero 6+ at
 * another house: the portal took the password, then it sat on "Connecting to
 * Wi-Fi..." forever. The reason code that would identify this is ALREADY
 * printed by WiFi.onEvent -- but only to serial, and the failure is at somebody
 * else's house with no laptop. So the screen has to carry it.
 *
 * THIS FILE IS DELIBERATELY DISPLAY-AGNOSTIC AND ALLOCATION-FREE. It is the
 * prototype for the v12 persistent join-failure store: the same struct will
 * back the NVS record and the config-portal view. Keeping it a plain POD with
 * fixed buffers is what lets it graduate rather than be rewritten -- no String,
 * no heap, nothing that cares whether it is being rendered, persisted or
 * served.
 *
 * THE ONE RULE THAT IS NOT NEGOTIABLE: RECORD IN THE CALLBACK, RENDER FROM THE
 * LOOP. WiFi.onEvent runs on the WiFi event task. A blocking SPI blit from
 * there can perturb the very timing being measured, and an instrument that
 * causes the failure it is measuring is worse than no instrument. Record()
 * touches nothing but this struct; the loop reads it and draws.
 *
 * BOUNDED BY CONSTRUCTION. A fixed ring, fixed strings, no growth. The v12
 * version inherits that bound, which is the same rule the logbook follows so it
 * cannot starve the config namespace sharing the partition.
 * ======================================================================== */

namespace joindiag {

constexpr int MAX_EVENTS = 5;   // last N disconnects, oldest overwritten
constexpr int MAX_BSSIDS = 6;   // per scan: every node heard for the SSID
constexpr int SSID_LEN   = 33;
constexpr int NAME_LEN   = 28;  // WiFi.disconnectReasonName() longest + slack

/** One disconnect, as the event handler saw it. */
struct Event {
    uint8_t  reason = 0;
    char     name[NAME_LEN] = {0};
    uint32_t atMs = 0;          // millis() at the event
    uint16_t attempt = 0;       // which join attempt this boot
    bool     used = false;
};

/** One BSSID heard for the target SSID. Every node, not the best one -- on a
 *  mesh, WHICH nodes the board can hear and how loudly IS the finding. */
struct Bss {
    uint8_t mac[6] = {0};
    int8_t  rssi = 0;
    uint8_t channel = 0;
    uint8_t enc = 0;            // wifi_auth_mode_t, as the radio reports it
};

struct State {
    // ---- identity of the attempt ------------------------------------------
    char     ssid[SSID_LEN] = {0};
    uint16_t attempts = 0;
    uint32_t bootMs = 0;

    // ---- what happened ------------------------------------------------------
    Event    events[MAX_EVENTS];
    uint8_t  next = 0;          // ring cursor
    uint16_t total = 0;         // disconnects this boot (may exceed MAX_EVENTS)
    bool     everAssociated = false;
    bool     everGotIp = false;

    // ---- what the radio can hear -------------------------------------------
    Bss      bss[MAX_BSSIDS];
    uint8_t  bssCount = 0;
    bool     scanDone = false;
    bool     ssidSeen = false;      // found at all, on any band
    bool     seen24 = false;        // found on a 2.4 GHz channel (1..14)
    bool     seen5 = false;         // found on a 5/6 GHz channel
    uint32_t scanAtMs = 0;
};

inline State& Get() { static State s; return s; }

/** Copy a C string into a fixed buffer, always terminated. */
inline void SetStr(char* dst, int cap, const char* src)
{
    if (!dst || cap <= 0) return;
    if (!src) { dst[0] = 0; return; }
    int i = 0;
    for (; i < cap - 1 && src[i]; ++i) dst[i] = src[i];
    dst[i] = 0;
}

/** Called FROM THE EVENT CALLBACK. Must stay allocation-free and non-blocking. */
inline void RecordDisconnect(uint8_t reason, const char* name, uint32_t nowMs)
{
    State& s = Get();
    Event& e = s.events[s.next];
    e.reason = reason;
    SetStr(e.name, NAME_LEN, name);
    e.atMs = nowMs;
    e.attempt = ++s.attempts;
    e.used = true;
    s.next = (uint8_t)((s.next + 1) % MAX_EVENTS);
    if (s.total < 0xFFFF) ++s.total;
}

inline void RecordAssociated(const char* ssid)
{
    State& s = Get();
    s.everAssociated = true;
    if (ssid && ssid[0]) SetStr(s.ssid, SSID_LEN, ssid);
}

inline void RecordGotIp() { Get().everGotIp = true; }

inline void RecordTargetSsid(const char* ssid)
{
    if (ssid && ssid[0]) SetStr(Get().ssid, SSID_LEN, ssid);
}

/** 2.4 GHz is channels 1..14; anything above is 5/6 GHz. */
inline bool Is24(uint8_t ch) { return ch >= 1 && ch <= 14; }

/** Short label for a wifi_auth_mode_t without dragging the enum header in. */
inline const char* AuthName(uint8_t enc)
{
    switch (enc) {
        case 0: return "OPEN";
        case 1: return "WEP";
        case 2: return "WPA-PSK";
        case 3: return "WPA2-PSK";
        case 4: return "WPA/WPA2";
        case 5: return "ENTERPRISE";
        case 6: return "WPA3-PSK";
        case 7: return "WPA2/WPA3";
        case 8: return "WAPI";
        default: return "?";
    }
}

/** Newest-first walk over the ring. Returns nullptr past the end. */
inline const Event* Nth(int i)
{
    const State& s = Get();
    if (i < 0 || i >= MAX_EVENTS) return nullptr;
    int idx = (int)s.next - 1 - i;
    while (idx < 0) idx += MAX_EVENTS;
    const Event& e = s.events[idx];
    return e.used ? &e : nullptr;
}

} // namespace joindiag
