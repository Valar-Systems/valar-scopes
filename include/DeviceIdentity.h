#pragma once

#include <Arduino.h>
#include <cstring>
#include <esp_mac.h>
#include <mbedtls/sha256.h>

// Salt for the leaderboard id hash below. A per-build constant (overridable via
// a build flag) so the public id can't be brute-forced back to a MAC without it.
#ifndef LEADERBOARD_SALT
#  define LEADERBOARD_SALT "blipscope-lb-v1"
#endif

// The product name for the compiled Edition, used as the device's network-name prefix below. Each
// Edition (selected by the same -DFEATURE_* flag main.cpp dispatches the app on) advertises itself
// under its own product name, so a device shows up on the network as e.g. "speedscope-a1b2c3.local"
// instead of always "blipscope-...". An env may pre-define DEVICE_PRODUCT_NAME to override this
// mapping without editing here. The radar (no FEATURE_* flag) is the original Blipscope.
#ifndef DEVICE_PRODUCT_NAME
#  if defined(FEATURE_EAM)
#    define DEVICE_PRODUCT_NAME "Missileer"
#  elif defined(FEATURE_SPACE)
#    define DEVICE_PRODUCT_NAME "Orbitscope"
#  elif defined(FEATURE_SEISMIC)
#    define DEVICE_PRODUCT_NAME "Quakescope"
#  elif defined(FEATURE_BIRDING)
#    define DEVICE_PRODUCT_NAME "Quillscope"
#  elif defined(FEATURE_FISHING)
#    define DEVICE_PRODUCT_NAME "Reelscope"
#  elif defined(FEATURE_SPEED)
#    define DEVICE_PRODUCT_NAME "Speedscope"
#  elif defined(FEATURE_CLAUDESCOPE)
#    define DEVICE_PRODUCT_NAME "Claudescope"
#  else
#    define DEVICE_PRODUCT_NAME "Blipscope"
#  endif
#endif

namespace DeviceIdentity
{
    // Stable, per-device name derived from the factory WiFi MAC, e.g. "Speedscope-A1B2C3" -- the
    // compiled Edition's product name (DEVICE_PRODUCT_NAME) plus the last 3 octets of the MAC in hex,
    // so every board on the same network gets a unique, product-appropriate name. This one string is
    // the mDNS hostname, the DHCP hostname (the router's device list), and the WiFi setup-AP SSID.
    //
    // Reads the MAC straight from efuse (esp_read_mac), which works even before WiFi is started --
    // unlike WiFi.macAddress(), which returns zeros until the STA netif exists. Computed once and cached.
    inline const String& Name()
    {
        static const String name = []() {
            uint8_t mac[6] = {0};
            esp_read_mac(mac, ESP_MAC_WIFI_STA);
            char suffix[7];
            snprintf(suffix, sizeof(suffix), "%02X%02X%02X", mac[3], mac[4], mac[5]);
            return String(DEVICE_PRODUCT_NAME "-") + suffix;
        }();
        return name;
    }

    // The bare product name for the compiled Edition (no MAC suffix), for UI/branding.
    inline const char* Product() { return DEVICE_PRODUCT_NAME; }

    // Opaque, stable leaderboard identity: the first 8 bytes of SHA-256(MAC ||
    // salt), as 16 lowercase hex chars. Derived from the FULL 6-octet MAC (not
    // just the visible 3-octet name suffix) and salted, so the raw MAC never
    // leaves the device and the public id can't be reversed to it. Computed
    // once and cached. Matches the server's id shape (/^[0-9a-f]{8,32}$/).
    inline const String& LeaderboardId()
    {
        static const String id = []() {
            uint8_t mac[6] = {0};
            esp_read_mac(mac, ESP_MAC_WIFI_STA);
            const char* salt = LEADERBOARD_SALT;
            size_t slen = strlen(salt);
            if (slen > 64) slen = 64;
            uint8_t in[6 + 64];
            memcpy(in, mac, 6);
            memcpy(in + 6, salt, slen);
            uint8_t out[32];
            mbedtls_sha256(in, 6 + slen, out, 0); // is224 = 0 -> SHA-256
            char hex[17];
            for (int i = 0; i < 8; i++)
                snprintf(hex + i * 2, 3, "%02x", out[i]);
            return String(hex);
        }();
        return id;
    }
}
