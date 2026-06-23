#pragma once

#include <Arduino.h>
#include <esp_mac.h>

namespace DeviceIdentity
{
    // Stable, per-device name derived from the factory WiFi MAC,
    // e.g. "Blipscope-A1B2C3". The suffix is the last 3 octets of the
    // MAC in hex, so every board on the same network gets a unique name.
    //
    // Reads the MAC straight from efuse (esp_read_mac), which works even
    // before WiFi is started -- unlike WiFi.macAddress(), which returns
    // zeros until the STA netif exists. Computed once on first use and cached.
    inline const String& Name()
    {
        static const String name = []() {
            uint8_t mac[6] = {0};
            esp_read_mac(mac, ESP_MAC_WIFI_STA);
            char suffix[7];
            snprintf(suffix, sizeof(suffix), "%02X%02X%02X", mac[3], mac[4], mac[5]);
            return String("Blipscope-") + suffix;
        }();
        return name;
    }
}
