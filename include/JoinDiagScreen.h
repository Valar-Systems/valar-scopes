#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#include "JoinDiag.h"
#include "LGFX.h"
#include "Layout.h"   // ChordWidthPx: this is a ROUND disc

/* ===========================================================================
 * WI-FI JOIN DIAGNOSTICS -- the glass half. BENCH BUILD ONLY.
 *
 * WHERE THIS DRAWS FROM, AND WHY IT IS NOT loop().
 *
 * The instruction was record-in-the-callback, render-from-the-loop, and the
 * reason behind it is right: a blocking SPI blit from the WiFi event task can
 * perturb the timing being measured. That rule is kept -- nothing here is
 * called from WiFi.onEvent.
 *
 * But loop() is the wrong renderer for THIS failure, and not by preference:
 * `wm.autoConnect()` BLOCKS inside setup() for the whole join attempt, and on
 * failure the device reboots without ever reaching loop(). A loop-based
 * renderer would draw nothing at all on precisely the path being diagnosed.
 *
 * So the render point is WiFiManager's AP callback -- which fires on the MAIN
 * task, not the event task, at exactly the moment the join has given up and the
 * portal opens. It satisfies the actual constraint (no SPI from the event
 * callback) and it lands in a 180 s window where the screen sits still, which
 * is what "survives being photographed" requires.
 *
 * SCREEN COVERAGE. Written for the 240x240 s3-128 with direct tft writes. That
 * is the only board this build is for; a panel that cannot take direct per-glyph
 * writes (SPD2010) would need the backbuffer path, and this build does not
 * target one.
 *
 * THE CYCLE COUNTER IS THE ONLY PERSISTENT STATE, and it exists because RAM
 * does not survive the reboot-retry. Without it every cycle would show ~2
 * minutes elapsed and Daniel could not tell a twelve-minute run from a
 * thirty-second one -- which is how this whole investigation started. One
 * uint16 in its own namespace, written once per boot.
 * ======================================================================== */

namespace joindiag {

constexpr const char* NVS_NS = "jdiag";

/** Cached so the renderer never re-reads NVS: bumped once at boot, read often. */
inline uint16_t& CycleRef() { static uint16_t c = 0; return c; }
inline uint16_t CycleCount() { return CycleRef(); }

/** Bump and return the boot/cycle counter. Called once, early. */
inline uint16_t BumpCycle()
{
    Preferences p;
    if (!p.begin(NVS_NS, false)) return 0;
    uint16_t n = p.getUShort("cycle", 0);
    if (n < 0xFFFE) ++n;
    p.putUShort("cycle", n);
    p.end();
    CycleRef() = n;
    return n;
}

inline void ResetCycle()
{
    Preferences p;
    if (p.begin(NVS_NS, false)) { p.putUShort("cycle", 0); p.end(); }
}

/**
 * Scan for the target SSID and record EVERY BSSID that answers for it.
 *
 * Not the best one: on a mesh, which nodes the board can hear and how loudly is
 * the finding. With encryption ruled out (eero reports WPA2 on both bands,
 * 2026-09-12), whether the SSID appears on a 2.4 GHz channel AT ALL is the
 * discriminating datum.
 *
 * Runs from the AP callback, once per boot, on the main task. It is a
 * diagnostic scan that happens AFTER the join has already failed -- it does not
 * touch the join path's own scanning.
 */
inline void ScanTarget()
{
    State& s = Get();
    if (s.scanDone) return;
    s.scanDone = true;
    s.scanAtMs = millis();

    const int n = WiFi.scanNetworks(false /*async*/, true /*show hidden*/);
    if (n <= 0) return;

    for (int i = 0; i < n && s.bssCount < MAX_BSSIDS; ++i) {
        if (s.ssid[0] && WiFi.SSID(i) != String(s.ssid)) continue;
        s.ssidSeen = true;
        const uint8_t ch = (uint8_t)WiFi.channel(i);
        if (Is24(ch)) s.seen24 = true; else s.seen5 = true;

        Bss& b = s.bss[s.bssCount++];
        const uint8_t* m = WiFi.BSSID(i);
        if (m) memcpy(b.mac, m, 6);
        b.rssi = (int8_t)WiFi.RSSI(i);
        b.channel = ch;
        b.enc = (uint8_t)WiFi.encryptionType(i);
    }
    WiFi.scanDelete();
}

/**
 * Draw the whole diagnostic. Main task only.
 *
 * THIS IS A ROUND 240x240 DISC, NOT A SQUARE PANEL. The first version of this
 * drew top-left at (6, 6) and the corners fell off the glass -- the top rows
 * were unreadable, which on a diagnostic whose entire job is to be photographed
 * is a total failure, not a cosmetic one.
 *
 * Every row is therefore CENTRED and clipped to the disc's chord at that height
 * (Layout.h's ChordWidthPx, the same helper the rest of the UI uses), and the
 * vertical range is held to the band where the chord is wide enough to carry
 * text. A row that will not fit is shortened rather than drawn off the edge.
 */
inline void DrawRow(LGFX& tft, const String& text, int y, uint32_t colour)
{
    if (y < 18 || y > 216) return;             // outside the usable band
    const int lineH = tft.fontHeight();
    const int avail = ChordWidthPx(y, lineH);  // usable width at THIS height
    String t = text;
    // Trim to fit rather than overflow the curve. The last character is dropped
    // repeatedly because proportional glyph widths make arithmetic unreliable.
    while (t.length() > 0 && tft.textWidth(t) > avail) t.remove(t.length() - 1);
    tft.setTextColor(colour);
    tft.setTextDatum(textdatum_t::top_center);
    tft.drawString(t, SCREEN_SIZE / 2, y);
}

inline void Draw(LGFX& tft, uint16_t cycle)
{
    const State& s = Get();
    const uint32_t up = millis() / 1000;

    tft.fillScreen(lgfx::color888(0, 0, 0));
    tft.setTextSize(1);
    const int H = 11;
    int y = 22;   // start below the top of the disc, where the chord is usable

    // TWO LINES, EACH SHORT ENOUGH TO FIT THE TOP CHORD BY CONSTRUCTION.
    // One long line got silently trimmed to "** WIFI DIAG -- NOT S", which is
    // the one row on this screen that must never be ambiguous -- a truncated
    // warning reads as a different warning. The chord at y=26 carries about 21
    // characters; both of these are shorter than that with room to spare.
    DrawRow(tft, "** WIFI DIAG **", y, lgfx::color888(255, 200, 0)); y += H;
    DrawRow(tft, "NOT SHIPPING", y, lgfx::color888(255, 200, 0)); y += H + 2;
    DrawRow(tft, String("cyc ") + cycle + "  up " + (up / 60) + "m" + (up % 60) +
                 "s  try " + s.attempts, y, lgfx::color888(0, 255, 0)); y += H;
    DrawRow(tft, String("SSID ") + (s.ssid[0] ? s.ssid : "(not set)"), y,
            lgfx::color888(0, 200, 0)); y += H;
    DrawRow(tft, String("assoc ") + (s.everAssociated ? "Y" : "n") +
                 "  ip " + (s.everGotIp ? "Y" : "n") +
                 "  drops " + s.total, y, lgfx::color888(0, 200, 0)); y += H + 2;

    // ---- reason codes, newest first ----------------------------------------
    if (s.total == 0) {
        DrawRow(tft, "no disconnects yet", y, lgfx::color888(120, 120, 120)); y += H;
    } else {
        for (int i = 0; i < 3; ++i) {
            const Event* e = Nth(i);
            if (!e) break;
            DrawRow(tft, String("#") + e->attempt + " r=" + e->reason + " " + e->name,
                    y, lgfx::color888(255, 80, 80));
            y += H;
        }
    }
    y += 2;

    // ---- the scan ----------------------------------------------------------
    if (!s.scanDone) { DrawRow(tft, "scan pending", y, lgfx::color888(120, 120, 120)); return; }
    if (!s.ssidSeen) {
        DrawRow(tft, "SSID NOT FOUND IN SCAN", y, lgfx::color888(255, 80, 80)); y += H;
        DrawRow(tft, "(radio cannot see it)", y, lgfx::color888(255, 80, 80));
        return;
    }
    if (s.seen5 && !s.seen24) {
        DrawRow(tft, "ONLY ON 5GHz -- board is", y, lgfx::color888(255, 200, 0)); y += H;
        DrawRow(tft, "2.4GHz ONLY. THAT IS IT.", y, lgfx::color888(255, 200, 0)); y += H + 2;
    } else if (s.seen24) {
        DrawRow(tft, String("on 2.4GHz") + (s.seen5 ? " and 5GHz" : ""), y,
                lgfx::color888(0, 255, 0)); y += H + 2;
    }

    // WHOSE BSSIDs THESE ARE. With no target SSID the scan cannot filter, so the
    // list is every network in range -- and a reader who has just seen "SSID"
    // above will otherwise take them for that network's nodes.
    if (!s.ssid[0]) {
        DrawRow(tft, "-- all nets in range --", y, lgfx::color888(150, 150, 0));
        y += H;
    }
    for (int i = 0; i < s.bssCount; ++i) {
        const Bss& b = s.bss[i];
        char mac[8];
        snprintf(mac, sizeof(mac), "%02X%02X", b.mac[4], b.mac[5]);
        DrawRow(tft, String("..") + mac + " c" + b.channel +
                     (Is24(b.channel) ? "/2.4 " : "/5 ") + b.rssi + "dB",
                y, lgfx::color888(0, 200, 0));
        y += H;
        if (y > 205) break;
    }
}

/** One-line serial echo of the same facts, for when a cable IS available. */
inline void LogSerial(uint16_t cycle)
{
    const State& s = Get();
    Serial.printf("[jdiag] ** WIFI DIAG BUILD ** cycle=%u up=%lus ssid=%s attempts=%u "
                  "assoc=%d ip=%d drops=%u\n",
                  cycle, (unsigned long)(millis() / 1000),
                  s.ssid[0] ? s.ssid : "(none)", s.attempts,
                  (int)s.everAssociated, (int)s.everGotIp, s.total);
    for (int i = 0; i < MAX_EVENTS; ++i) {
        const Event* e = Nth(i);
        if (!e) break;
        Serial.printf("[jdiag]   #%u reason=%u (%s) at %lums\n",
                      e->attempt, e->reason, e->name, (unsigned long)e->atMs);
    }
    Serial.printf("[jdiag]   scan: seen=%d 2.4GHz=%d 5GHz=%d nodes=%u\n",
                  (int)s.ssidSeen, (int)s.seen24, (int)s.seen5, s.bssCount);
    for (int i = 0; i < s.bssCount; ++i) {
        const Bss& b = s.bss[i];
        Serial.printf("[jdiag]   bss %02X:%02X:%02X:%02X:%02X:%02X ch=%u band=%s rssi=%d enc=%s\n",
                      b.mac[0], b.mac[1], b.mac[2], b.mac[3], b.mac[4], b.mac[5],
                      b.channel, Is24(b.channel) ? "2.4" : "5", b.rssi, AuthName(b.enc));
    }
}

} // namespace joindiag
