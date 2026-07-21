#pragma once

#include <WiFiManager.h>
#include <Preferences.h>
#include <esp_wifi.h>

#include "DeviceIdentity.h"
#include "Layout.h"
#include "BootScreen.h"

namespace WiFiManagerHelpers
{
    // Per-device setup hotspot name, e.g. "Blipscope-A1B2C3", so multiple
    // boards in setup mode on the same network can be told apart.
    inline const String& WiFiManagerName() { return DeviceIdentity::Name(); }

    // ---- fast-path join: aim at the last known-good AP, skip the scan ----
    //
    // The retry ladder in ConfigureWiFiManager absorbs mesh band-steering, but it
    // pays in wall clock: every attempt re-scans all channels first, and a failed
    // one burns the whole 15 s timeout, so an unlucky boot took minutes to join.
    // The ledger showed WHY a boot is unlucky -- on one stationary desk, joins
    // landed anywhere from -55 to -80 dBm, i.e. the scan was taking whichever mesh
    // node answered first, and the weak ones are what lose the 4-way handshake
    // (30x reason=204 HANDSHAKE_TIMEOUT in the same ledger).
    //
    // So: remember the channel + BSSID of the last connect that was actually GOOD,
    // and aim straight at that node next boot -- no scan, no band-steering dance,
    // typically a ~1-2 s join. Anything unexpected (no hint yet, device moved, that
    // node gone) falls through to the normal scan/portal path below, and the stale
    // hint is dropped so we never chase a dead node twice.
    namespace detail
    {
        constexpr char     FAST_NS[]      = "wifi-fast";
        constexpr int32_t  FAST_MIN_RSSI  = -70;  // don't pin a node we barely heard
        constexpr uint32_t FAST_JOIN_MS   = 6000; // then give up and scan properly
    }

    inline void ForgetFastAp()
    {
        Preferences p;
        if (!p.begin(detail::FAST_NS, false)) return;
        p.clear();
        p.end();
    }

    // Cache the AP we just joined -- but only when the link was solid. Pinning a
    // marginal node would make the NEXT boot reproduce exactly the slow, handshake-
    // timing-out join this is meant to avoid, so a weak connect clears the hint
    // instead and lets the next boot scan for something better.
    inline void RememberFastAp()
    {
        const int32_t rssi = WiFi.RSSI();
        if (rssi < detail::FAST_MIN_RSSI) {
            Serial.printf("[WiFi] not pinning AP: RSSI %d dBm is below %d\n",
                          (int)rssi, (int)detail::FAST_MIN_RSSI);
            ForgetFastAp();
            return;
        }
        Preferences p;
        if (!p.begin(detail::FAST_NS, false)) return;
        p.putUChar("ch", WiFi.channel());
        p.putBytes("bssid", WiFi.BSSID(), 6);
        p.end();
        Serial.printf("[WiFi] pinned AP ch=%u RSSI=%d dBm for the next boot\n",
                      (unsigned)WiFi.channel(), (int)rssi);
    }

    // true iff we joined using the remembered node.
    inline bool TryFastJoin()
    {
        uint8_t bssid[6] = {};
        uint8_t ch = 0;
        {
            Preferences p;
            if (!p.begin(detail::FAST_NS, true)) return false; // no hint yet (first boot)
            ch = p.getUChar("ch", 0);
            const size_t got = p.getBytes("bssid", bssid, sizeof(bssid));
            p.end();
            if (ch == 0 || got != sizeof(bssid)) return false;
        }

        // Credentials live in the SDK's own store (WiFiManager put them there); read
        // them back rather than keeping a second copy of the password anywhere.
        WiFi.mode(WIFI_STA); // starts the driver so the saved config is readable
        wifi_config_t conf = {};
        if (esp_wifi_get_config(WIFI_IF_STA, &conf) != ESP_OK) return false;
        if (conf.sta.ssid[0] == '\0') return false; // never provisioned -> portal path

        Serial.printf("[WiFi] fast join: ch=%u bssid=%02X:%02X:%02X:%02X:%02X:%02X\n",
                      (unsigned)ch, bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5]);
        WiFi.begin(reinterpret_cast<const char*>(conf.sta.ssid),
                   reinterpret_cast<const char*>(conf.sta.password), ch, bssid);

        const uint32_t start = millis();
        while (millis() - start < detail::FAST_JOIN_MS) {
            if (WiFi.status() == WL_CONNECTED) {
                Serial.printf("[WiFi] fast join OK in %lu ms\n",
                              (unsigned long)(millis() - start));
                return true;
            }
            delay(50); // vTaskDelay: keeps the WiFi/event tasks fed while we wait
        }
        Serial.println("[WiFi] fast join missed; falling back to a full scan");
        WiFi.disconnect(false, false);
        ForgetFastAp(); // that node moved or went away -- don't chase it next boot
        return false;
    }

    static void ConfigureWiFiManager(WiFiManager& wm, LGFX& tft, LGFX_Sprite& backbuffer)
    {
        // DEV level prints the SSID/password the portal actually received, plus the
        // full connect flow -- lets us confirm the portal didn't mangle the credentials.
        wm.setDebugOutput(true, WM_DEBUG_DEV);
        wm.setTitle("Blipscope - Setup WiFi");
        wm.setCustomHeadElement("<style>body{background:#111;color:#00ff00;font-family:monospace;} div:has(> a){background:#00ff00;} a:hover{color:#111;}</style>");

        // unique DHCP/mDNS hostname so the router lists each board distinctly
        wm.setHostname(DeviceIdentity::Name().c_str());

        // Mesh APs (e.g. Google Nest/eero) reject the first few association attempts
        // while they band-steer between nodes. WiFiManager's defaults (1 try, no
        // timeout) bail on the first transient failure and fall back to the portal.
        // Give it several timed attempts so it keeps trying until one sticks, the
        // way a normal client does.
        wm.setConnectRetries(5);
        wm.setConnectTimeout(15); // seconds per attempt; polls through transient reason-2 disconnects

        // log the moment the portal hands new credentials to the radio
        wm.setSaveConfigCallback([]() {
            Serial.println("[WiFi] Portal saved credentials, attempting to connect...");
        });

        wm.setAPCallback([&tft, &backbuffer](WiFiManager* wifiManager) {
            // Composed through the backbuffer so it renders on the SPD2010 (direct per-glyph writes
            // don't); direct on every other SKU. See BootScreen.h.
            DrawCenteredScreen(tft, backbuffer, lgfx::color888(0, 0, 0), lgfx::color888(0, 255, 0),
                               "- SETUP -", "Connect to this WiFi hotspot:", WiFiManagerName().c_str());
            }
        );
    }
}