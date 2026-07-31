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

    // ---- boot-time hold-to-forget: the FALLBACK recovery path ----
    //
    // The on-screen Stats reset is the primary route and covers the normal stuck
    // case (wrong credentials: the device still boots and renders, the radar just
    // has no data). This covers the states where that UI does not exist yet --
    // the config portal itself, where setup() has not returned, and any future
    // wedge that never reaches the main loop. It is the touchscreen equivalent of
    // "hold the button while powering on", for an enclosure with no exposed button.
    //
    // Shape: we only need to notice a finger that is ALREADY down at power-on
    // (that is what the instruction tells the customer to do), so the detect
    // window is short -- 1.2 s, the only cost added to an ordinary boot. Once a
    // finger is seen we ask for a further 3 s of continuous contact, with
    // on-screen feedback the whole time, so it can never fire silently and
    // releasing is always a cancel.
    //
    // Returns true if the user held it through. Caller does the resetSettings().
    inline bool BootHoldToForget(LGFX& tft, LGFX_Sprite& backbuffer)
    {
        constexpr uint32_t DETECT_MS = 1200; // watch for an already-present finger
        constexpr uint32_t HOLD_MS   = 3000; // then require this much unbroken contact

        // Is the touch controller actually up this early? tft.init() brings it up
        // with the panel, but "the driver exists" and "the chip answers" are
        // different claims, and this whole path is worthless if it silently never
        // sees a finger. Logged every boot so a regression (or a batch with a
        // different touch IC -- see INCOMING-INSPECTION.md) shows up as a line in
        // the ledger rather than as a recovery route that quietly stopped working.
        int32_t x = 0, y = 0;
        const bool haveDriver = (tft.touch() != nullptr);
        uint32_t polls = 0;
        bool sawTouch = false;

        const uint32_t detectUntil = millis() + DETECT_MS;
        while ((int32_t)(millis() - detectUntil) < 0) {
            ++polls;
            if (tft.getTouch(&x, &y)) { sawTouch = true; break; }
            delay(20);
        }
        Serial.printf("[wifi-reset] boot touch window: driver=%d polls=%lu touched=%d\n",
                      (int)haveDriver, (unsigned long)polls, (int)sawTouch);

        if (!sawTouch || !tft.getTouch(&x, &y))
            return false; // nothing held -- the overwhelmingly common path

        Serial.println("[wifi-reset] boot touch detected; hold to confirm");
        const uint32_t holdStart = millis();
        uint32_t lastShown = UINT32_MAX;
        while (tft.getTouch(&x, &y)) {
            const uint32_t held = millis() - holdStart;
            if (held >= HOLD_MS) {
                DrawCenteredScreen(tft, backbuffer, lgfx::color888(0, 0, 0),
                                   lgfx::color888(255, 176, 0),
                                   "WIFI RESET", "Forgetting network...", "Release now");
                delay(1200);
                Serial.println("[wifi-reset] hold completed -- clearing credentials");
                return true;
            }
            // Count down out loud. Silent detection would be worse than none: the
            // customer needs to know it is working AND that letting go cancels.
            const uint32_t left = (HOLD_MS - held + 999) / 1000;
            if (left != lastShown) {
                lastShown = left;
                DrawCenteredScreen(tft, backbuffer, lgfx::color888(0, 0, 0),
                                   lgfx::color888(255, 176, 0), "KEEP HOLDING",
                                   ("Reset WiFi in " + String((int)left) + "...").c_str(),
                                   "Release to cancel");
            }
            delay(30);
        }
        Serial.println("[wifi-reset] released early -- cancelled");
        DrawCenteredScreen(tft, backbuffer, lgfx::color888(0, 0, 0), lgfx::color888(0, 255, 0),
                           "CANCELLED", "WiFi settings kept", "");
        delay(900);
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

        // THE PORTAL MUST GIVE UP -- BUT ONLY WHEN THERE IS SOMETHING TO RETRY.
        //
        // Default is 0 = block in the portal forever, which loses a device to the
        // commonest household event there is: a power cut. The board is up in ~10 s,
        // a router takes 1-3 min, so the join fails, the portal opens, and the unit
        // sits in setup mode indefinitely -- long after the network came back, with
        // the customer having done nothing wrong. Timing out and rebooting retries
        // the saved credentials, which is the self-heal.
        //
        // On a NEVER-PROVISIONED board there are no saved credentials, so a reboot
        // retries nothing -- all it does is drop the setup hotspot every 3 minutes
        // while the customer is still scanning for it on their phone. So the timeout
        // is armed only when credentials exist. (Out-of-box first setup is exactly
        // this case, and it is the one moment the portal must be rock steady.)
        //
        // setAPClientCheck is the other half: the timeout is suspended while anyone
        // is connected to the setup hotspot (WiFiManager checks
        // WiFi_softap_num_stations()), so a customer typing their password is never
        // cut off mid-setup even on the timed path.
        if (wm.getWiFiIsSaved()) {
            wm.setConfigPortalTimeout(180);
            wm.setAPClientCheck(true);
            Serial.println("[WiFi] credentials saved: portal will time out after 180 s and retry them");
        } else {
            wm.setConfigPortalTimeout(0); // explicit: never drop the hotspot during first setup
            Serial.println("[WiFi] no saved credentials: portal stays up indefinitely for first setup");
        }

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