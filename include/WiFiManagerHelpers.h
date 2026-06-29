#pragma once

#include <WiFiManager.h>

#include "DeviceIdentity.h"
#include "Layout.h"
#include "BootScreen.h"

namespace WiFiManagerHelpers
{
    // Per-device setup hotspot name, e.g. "Blipscope-A1B2C3", so multiple
    // boards in setup mode on the same network can be told apart.
    inline const String& WiFiManagerName() { return DeviceIdentity::Name(); }

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