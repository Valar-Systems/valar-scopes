// Blipscope P4 4.0 (bench) -- the board's bring-up hooks (docs/p4-40-bringup.md).
//
// Every env compiles src/ with +<*>, so this TU is in every build. It is wrapped whole in the variant
// test and compiles to an empty object everywhere else: no S3 image gains a byte from it (row C5).
//
// variant::BoardPreInit() runs in setup() after Serial.begin() and before tft.init(). It prints, and
// changes nothing:
//   [p4] ...       chip model, silicon revision, cores, CPU clock, flash header size, PSRAM size
//                  (bench rows B0 and B1 read it);
//   [p4] identity  base=<esp_err name> known=<0|1> label=<XX:XX|none> wifi_sta=<esp_err name>: the
//                  identity read (ruling Q-ID (a), DeviceIdentity.h) by result and short label only,
//                  never the full MAC, which may not appear in any record (rows B-ID and B-IDr);
//   [hosted] ...   once, at the first Wi-Fi STA or AP start: the ESP-Hosted host and slave versions
//                  and the C6's target name, with a verdict (row W5). It hangs off a Wi-Fi event so
//                  main.cpp is not touched; by then the Arduino core has brought the link up and
//                  cached the slave's version (esp32-hal-hosted.c, hostedInit -> hostedHasUpdate).
#if defined(BLIPSCOPE_VARIANT_P4_40)

#include <Arduino.h>
#include <WiFi.h>
#include <esp_chip_info.h>
#include <esp_mac.h>

#include "variants/Variant.h"
#include "DeviceIdentity.h"

#if defined(CONFIG_ESP_WIFI_REMOTE_ENABLED)
#include "esp32-hal-hosted.h"
#endif

namespace {

void PrintBoardFacts()
{
    esp_chip_info_t info;
    esp_chip_info(&info);
    // ESP-IDF 5.x: revision is major * 100 + minor (v1.3 -> 103).
    Serial.printf("[p4] chip=%s rev=v%u.%u cores=%u cpu=%luMHz flash=%luB psram=%luB\n",
                  ESP.getChipModel(), (unsigned)(info.revision / 100), (unsigned)(info.revision % 100),
                  (unsigned)info.cores, (unsigned long)getCpuFrequencyMhz(),
                  (unsigned long)ESP.getFlashChipSize(), (unsigned long)ESP.getPsramSize());

    // Ruling Q-ID (a): the identity is the base MAC through DeviceIdentity::ReadMac, refused when it
    // cannot be read or reads all zero. The line carries the read's RESULT by name and the short
    // label (the MAC's last two bytes, the form the repo names boards by) -- never more of the MAC.
    // wifi_sta= is section 2's finding, read on the glass: the call DeviceIdentity used to make.
    uint8_t scratch[8] = {0};   // read into and discarded; never printed
    const esp_err_t base = esp_read_mac(scratch, ESP_MAC_BASE);
    const esp_err_t sta = esp_read_mac(scratch, ESP_MAC_WIFI_STA);
    uint8_t mac[6] = {0};
    char label[6] = "none";
    if (DeviceIdentity::ReadMac(mac))
        snprintf(label, sizeof(label), "%02X:%02X", mac[4], mac[5]);
    Serial.printf("[p4] identity: base=%s known=%d label=%s wifi_sta=%s%s\n",
                  esp_err_to_name(base), DeviceIdentity::Known() ? 1 : 0, label, esp_err_to_name(sta),
#if defined(BLIPSCOPE_P4_ID_PLANT_REFUSE)
                  "  ** PLANT: identity refused on purpose (row B-IDr) **"
#else
                  ""
#endif
    );
}

#if defined(CONFIG_ESP_WIFI_REMOTE_ENABLED)
void PrintHostedOnce()
{
    static bool printed = false;
    if (printed) return;
    printed = true;

    uint32_t hMaj = 0, hMin = 0, hPat = 0, sMaj = 0, sMin = 0, sPat = 0;
    hostedGetHostVersion(&hMaj, &hMin, &hPat);
    hostedGetSlaveVersion(&sMaj, &sMin, &sPat);
    const uint32_t host = (hMaj << 16) | (hMin << 8) | hPat;
    const uint32_t slave = (sMaj << 16) | (sMin << 8) | sPat;
    // "unread" is not a verdict: the slave's version RPC failed, and row W5 stops on it.
    const char* verdict = slave == 0 ? "unread" : host == slave ? "equal" : host > slave ? "slave older" : "slave newer";
    Serial.printf("[hosted] host=%lu.%lu.%lu slave=%lu.%lu.%lu target=%s -> %s\n",
                  (unsigned long)hMaj, (unsigned long)hMin, (unsigned long)hPat,
                  (unsigned long)sMaj, (unsigned long)sMin, (unsigned long)sPat,
                  hostedGetSlaveTargetName(), verdict);
}
#endif

} // namespace

void variant::BoardPreInit()
{
    PrintBoardFacts();
#if defined(CONFIG_ESP_WIFI_REMOTE_ENABLED)
    WiFi.onEvent([](arduino_event_id_t, arduino_event_info_t) { PrintHostedOnce(); }, ARDUINO_EVENT_WIFI_STA_START);
    WiFi.onEvent([](arduino_event_id_t, arduino_event_info_t) { PrintHostedOnce(); }, ARDUINO_EVENT_WIFI_AP_START);
#else
    Serial.println("[hosted] not built: CONFIG_ESP_WIFI_REMOTE_ENABLED is unset in this image");
#endif
}

#endif // BLIPSCOPE_VARIANT_P4_40
