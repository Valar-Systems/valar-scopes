#pragma once
#include <cstdint>
#include "esp_system.h"

// Blipscope web demo: one fixed, LOCALLY ADMINISTERED MAC (02:...), so the demo's device id is the
// same on every page load and can never be a real board's.
typedef enum { ESP_MAC_WIFI_STA = 0, ESP_MAC_WIFI_SOFTAP, ESP_MAC_BT, ESP_MAC_ETH } esp_mac_type_t;
inline esp_err_t esp_read_mac(uint8_t* mac, esp_mac_type_t)
{
    static const uint8_t demo[6] = {0x02, 0x00, 0x00, 0xde, 0x00, 0x01};
    for (int i = 0; i < 6; i++) mac[i] = demo[i];
    return ESP_OK;
}
inline esp_err_t esp_efuse_mac_get_default(uint8_t* mac) { return esp_read_mac(mac, ESP_MAC_WIFI_STA); }
