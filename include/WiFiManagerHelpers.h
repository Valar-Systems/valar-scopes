#pragma once

#include <WiFiManager.h>

#include "DeviceIdentity.h"
#include "Layout.h"

namespace WiFiManagerHelpers
{
    // Per-device setup hotspot name, e.g. "Blipscope-A1B2C3", so multiple
    // boards in setup mode on the same network can be told apart.
    inline const String& WiFiManagerName() { return DeviceIdentity::Name(); }

    static void ConfigureWiFiManager(WiFiManager& wm, LGFX& tft)
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

        wm.setAPCallback([&tft](WiFiManager* wifiManager) {
            tft.fillScreen(lgfx::color888(0, 0, 0));
            tft.setTextColor(lgfx::color888(0, 255, 0));

            const int lineHeight = tft.fontHeight() + 10;
            const int screenSize = SCREEN_SIZE;
            tft.drawCenterString("- SETUP -", screenSize / 2, screenSize / 2 - lineHeight);
            tft.drawCentreString("Connect to this WiFi hotspot:", screenSize / 2, screenSize / 2);
            tft.drawCenterString(WiFiManagerName(), screenSize / 2, screenSize / 2 + lineHeight);
            }
        );
    }
}