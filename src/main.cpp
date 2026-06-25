#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFiManager.h>
#include <esp_task_wdt.h>

#include "LGFX.h"
#include "DeviceIdentity.h"
#include "WiFiManagerHelpers.h"
#include "ConfigurationWebServer.h"
#include "HttpRequestManager.h"
#include "OpenSkyAuthTokenHandler.h"
#include "AircraftManager.h"
#include "OtaUpdater.h"
#include "DrawHelpers.h"
#include "models/Aircraft.h"
#include "models/TrackedAircraft.h"

constexpr int SCREEN_SIZE = 240;
constexpr int SCREEN_SIZE_DIV_2 = (SCREEN_SIZE / 2);

// Render the screen in horizontal bands instead of one full-screen sprite. A full
// 240x240x8-bit backbuffer is ~56 KB; at half height it's ~28 KB, freeing the
// contiguous heap a TLS handshake needs on the single-core C3. Must divide SCREEN_SIZE.
constexpr int BAND_H = SCREEN_SIZE / 2; // 2 bands of 240x120

LGFX tft;
LGFX_Sprite backbuffer(&tft);

WiFiManager wm;
ConfigurationWebServer configServer;
HttpRequestManager http;
OpenSkyAuthTokenHandler authHandler(http);

AircraftManager aircraftManager(configServer, authHandler, http, tft);

void setup()
{
  Serial.begin(115200);
  while (!Serial && millis() < 3000) { delay(10); } // wait up to 3s for the USB CDC host to open the port

  // Give the Task Watchdog headroom over a single synchronous network call. The OpenSky
  // and adsbdb fetches run TLS handshakes that take the lwIP core lock and don't yield;
  // on the single-core C3 that can keep the watchdog-fed async_tcp service task from
  // running for several seconds. The IDF default TWDT is 5 s -- the same order as a slow
  // handshake -- so a legitimately slow (but still progressing) fetch tripped it and
  // rebooted the board (async_tcp / osky_fetch watchdog abort). 10 s clears the worst-case
  // connect (bounded to 3 s in HttpRequestManager) plus margin, while a genuine hang still
  // reboots. Reconfigure, not init: IDF startup already armed the TWDT.
  esp_task_wdt_config_t wdtConfig = {
    .timeout_ms = 10000,
    .idle_core_mask = (1 << 0), // CPU0 idle task -- matches the single-core Arduino default
    .trigger_panic = true,      // still reboot on a real hang
  };
  esp_task_wdt_reconfigure(&wdtConfig);

  // initialise LGFX + screen
  tft.init();
  tft.invertDisplay(true);
  // drive the backlight via PWM (configured in LGFX.h) so it's dimmable; full
  // brightness for the boot screen until AircraftManager applies the saved level
  tft.setBrightness(255);

  backbuffer.setColorDepth(8);
  backbuffer.createSprite(SCREEN_SIZE, BAND_H);

  // establish WiFi connection
  tft.fillScreen(lgfx::color888(0, 0, 0));
  tft.setTextColor(lgfx::color888(0, 255, 0));
  tft.drawCentreString("Connecting to WiFi...", SCREEN_SIZE / 2, SCREEN_SIZE / 2);

  // Log every WiFi radio event so we can see exactly where a join fails.
  // These fire on the WiFi event task even while autoConnect() blocks below.
  WiFi.onEvent([](arduino_event_id_t event, arduino_event_info_t info) {
    switch (event) {
      case ARDUINO_EVENT_WIFI_STA_START:
        // Re-apply the hostname the instant the STA interface starts -- before the DHCP
        // request goes out -- so it's sent as DHCP option 12 and the router registers it.
        // That's what lets Angry IP Scanner (and the router's device list) resolve the
        // name; WiFiManager sets it too, but its mode-cycling applies it after DHCP, too
        // late. This mirrors MiniSpeedCam's working mode->setHostname->begin ordering.
        WiFi.setHostname(DeviceIdentity::Name().c_str());
        // Full TX power (chip default) for best range. Both setters require a started STA,
        // which is exactly what this event signals.
        WiFi.setTxPower(WIFI_POWER_19_5dBm);
        Serial.printf("[WiFi] STA started; hostname=%s, TX 19.5dBm\n", DeviceIdentity::Name().c_str());
        break;
      case ARDUINO_EVENT_WIFI_STA_CONNECTED:
        Serial.printf("[WiFi] Associated with \"%s\", waiting for IP...\n", WiFi.SSID().c_str());
        break;
      case ARDUINO_EVENT_WIFI_STA_GOT_IP:
        Serial.printf("[WiFi] CONNECTED  IP=%s  RSSI=%d dBm\n",
                      WiFi.localIP().toString().c_str(), WiFi.RSSI());
        break;
      case ARDUINO_EVENT_WIFI_STA_DISCONNECTED: {
        const auto reason = (wifi_err_reason_t)info.wifi_sta_disconnected.reason;
        Serial.printf("[WiFi] DISCONNECTED  reason=%d (%s)\n",
                      reason, WiFi.disconnectReasonName(reason));
        if (reason == WIFI_REASON_NO_AP_FOUND)
          Serial.println("       SSID not found: check spelling/range. The ESP32-C3 is 2.4GHz-only and cannot see 5GHz networks.");
        else if (reason == WIFI_REASON_AUTH_FAIL || reason == WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT ||
                 reason == WIFI_REASON_AUTH_EXPIRE)
          Serial.println("       Auth/key mismatch: most likely a wrong password.");
        else if (reason == WIFI_REASON_HANDSHAKE_TIMEOUT)
          Serial.println("       Handshake frames lost (not a key mismatch): RF/power instability or weak signal. Try lower TX power / better supply.");
        break;
      }
      default:
        break;
    }
  });

  WiFiManagerHelpers::ConfigureWiFiManager(wm, tft);

  const bool connected = wm.autoConnect(WiFiManagerHelpers::WiFiManagerName().c_str());
  Serial.printf("[WiFi] autoConnect() returned %s\n",
                connected ? "true (connected)" : "false (portal timed out / not connected)");

  // Disable Wi-Fi modem-sleep. By default the radio sleeps between beacons and
  // wakes each DTIM to listen, pulsing the supply current at the beacon rate.
  // On this board that periodic load makes the decoupling caps near the ESP32-C3
  // sing -- a faint, steady buzz. Keeping the radio always-on flattens the draw
  // and silences it. The device is USB/mains powered, so the extra ~20-30 mA is
  // a non-issue, and latency/throughput actually improve.
  if (connected)
    WiFi.setSleep(WIFI_PS_NONE);

  // start NTP in UTC; the on-screen clock applies the configured offset, and the
  // solar auto-dim works directly in UTC
  configTime(0, 0, "pool.ntp.org");

  // self-update from the latest GitHub release before normal startup; reboots
  // into the new firmware if one is newer than this build
  MaybeUpdateFirmware(tft);

  // begin background server for configuration
  configServer.Initialise();

  // initialise aircraft manager
  aircraftManager.Initialise();
}

void loop()
{
  // Forget WiFi credentials and reboot into the setup portal when requested.
  if (configServer.ConsumeWifiReset()) {
    wm.resetSettings();
    delay(200); // let the HTTP response flush before the reboot
    ESP.restart();
  }

  // re-check for firmware updates once a day for always-on devices
  static unsigned long lastOtaCheck = 0;
  if (millis() - lastOtaCheck > 24UL * 60UL * 60UL * 1000UL) {
    lastOtaCheck = millis();
    MaybeUpdateFirmware(tft);
  }

  // Apply settings saved via the web UI without rebooting. Done here, on the
  // loop task, so all AircraftManager state changes stay on a single task
  // rather than racing the async web-server callback.
  if (configServer.ConsumeConfigChanged())
    aircraftManager.Initialise();

  aircraftManager.Update();

  // draw cycle: render the frame one horizontal band at a time into the half-height
  // backbuffer, each shifted into place by a BandCanvas, then pushed to its screen
  // rows. The scene is drawn once per band; AircraftManager advances per-frame state
  // (animation tick, trail sampling) only on the first pass so the bands stay in sync.
  String renderScanlines = configServer.GetStoredString("scanline");
  const bool drawScan = (renderScanlines.isEmpty() || renderScanlines == "true") && aircraftManager.IsRadarView();

  // compute the sweep endpoints once so both bands draw the identical line (no seam)
  const float sweep = millis() / 3000.0f;
  const int sweepX = SCREEN_SIZE_DIV_2 - 1 + (std::cos(sweep) * SCREEN_SIZE_DIV_2);
  const int sweepY = SCREEN_SIZE_DIV_2 - 1 + (std::sin(sweep) * SCREEN_SIZE_DIV_2);

  for (int bandY = 0; bandY < SCREEN_SIZE; bandY += BAND_H) {
    BandCanvas canvas(backbuffer, bandY);
    const bool firstPass = (bandY == 0);

    canvas.fillScreen(lgfx::color888(0, 0, 0));

    if (drawScan)
      DrawScanLines(canvas, SCREEN_SIZE_DIV_2 - 1, SCREEN_SIZE_DIV_2 - 1, sweepX, sweepY, 20, 128, 5);

    aircraftManager.Draw(canvas, firstPass);
    backbuffer.pushSprite(0, bandY);
  }
}

