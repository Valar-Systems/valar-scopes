#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFiManager.h>
#include <esp_task_wdt.h>

#include "LGFX.h"
#include "Layout.h"
#include "BootScreen.h"
#include "SplashScreen.h"
#include "Board.h"
#include "BuildIdentity.h"
#include "DeviceIdentity.h"
#include "FactoryReset.h"
#include "ConfigMigration.h"
#include "NtfyTopic.h"
#include "TlsAllocator.h"
#include "HeapHealth.h"
#include "WiFiManagerHelpers.h"
#include "ConfigurationWebServer.h"
#include "HttpRequestManager.h"
#include "OpenSkyAuthTokenHandler.h"
#include "OtaUpdater.h"
// The active app is a compile-time choice: the radar (default), the FEATURE_EAM monitor, the
// FEATURE_SPACE (Spacescope) monitor, the FEATURE_SEISMIC earthquake radar, the FEATURE_BIRDING
// sightings radar, the FEATURE_FISHING (Reelscope) console, the FEATURE_CLAUDESCOPE usage gauge,
// or the FEATURE_SPEED (Speedscope) speed-radar. All expose the same
// Initialise()/Update()/Draw() surface, so loop() drives `appManager` without knowing which it is.
// The radar's sweep/models headers are radar-only.
#if defined(FEATURE_EAM)
#include "eam/EamManager.h"
#elif defined(FEATURE_SPACE)
#include "space/SpaceManager.h"
#elif defined(FEATURE_SEISMIC)
#include "seismic/SeismicManager.h"
#elif defined(FEATURE_BIRDING)
#include "birding/BirdingManager.h"
#elif defined(FEATURE_FISHING)
#include "fishing/FishingManager.h"
#elif defined(FEATURE_CLAUDESCOPE)
#include "claudescope/ClaudescopeManager.h"
#elif defined(FEATURE_SPEED)
#include "speed/SpeedManager.h"
#else
#include "AircraftManager.h"
#include "DrawHelpers.h"
#include "models/Aircraft.h"
#include "models/TrackedAircraft.h"
#endif

#ifdef SOAK_TEST
#include "SoakHarness.h" // realistic-duty 24 h soak
#endif

LGFX tft;
LGFX_Sprite backbuffer(&tft);

WiFiManager wm;
ConfigurationWebServer configServer;
HttpRequestManager http;
OpenSkyAuthTokenHandler authHandler(http);

#if defined(FEATURE_EAM)
EamManager appManager(configServer, authHandler, http, tft);
#elif defined(FEATURE_SPACE)
SpaceManager appManager(configServer, authHandler, http, tft);
#elif defined(FEATURE_SEISMIC)
SeismicManager appManager(configServer, authHandler, http, tft);
#elif defined(FEATURE_BIRDING)
BirdingManager appManager(configServer, authHandler, http, tft);
#elif defined(FEATURE_FISHING)
FishingManager appManager(configServer, authHandler, http, tft);
#elif defined(FEATURE_CLAUDESCOPE)
ClaudescopeManager appManager(configServer, authHandler, http, tft);
#elif defined(FEATURE_SPEED)
SpeedManager appManager(configServer, authHandler, http, tft);
#else
AircraftManager appManager(configServer, authHandler, http, tft);
// The Aviation radar is the #else fallthrough, so "which edition is this?" has no
// positive symbol -- every other edition has one and the radar is defined by the
// absence of all of them. Anything below that needs a RADAR-ONLY member of
// appManager must therefore key on this marker rather than on a
// !defined(A) && !defined(B) && ... chain, which silently rots: a new edition
// added above would satisfy the chain and only fail at the call site, which is
// exactly how this was found (EamManager has no NeedsReverify).
#define BLIPSCOPE_RADAR_EDITION 1
#endif

void setup()
{
  Serial.begin(115200); // non-blocking; the wait for a CDC *host* happens after the splash below

#if ARDUINO_USB_CDC_ON_BOOT
  // Never let a Serial write stall the loop. On native USB-CDC (every SKU but the
  // s3-21, which is on UART0) HWCDC::write BLOCKS when the port is enumerated but
  // nothing is draining it -- a device plugged into a computer with no terminal
  // open. isPlugged() stays true (the SOF watchdog still sees the host), so the
  // driver falls back to a bounded wait of max_consec_timeouts(20) x
  // tx_timeout_ms(100) = UP TO 2 s PER write() CALL. RecordFrameUs' [health]
  // report emits two printfs every 30 s, so the loop froze for ~2-4 s every 30 s:
  // the sweep stopped and touch -- polled once per pass -- went dead, which reads
  // exactly as "slow to open and close cards". Diagnosed 2026-07-31 on the bench
  // s3-128 after its serial capture had been detached for 10 days.
  //
  // 2 ms caps the worst case at ~40 ms (under one frame) while still giving the
  // ring a beat to drain under an attached monitor, so the soak ledger keeps its
  // lines. 0 would be strictly non-blocking but drops bytes the moment the ring
  // fills, and these boards are our measurement instrument.
  // ...and give it room so the bound is rarely reached in the first place. The
  // ring defaults to 256 B, which a boot burst (WiFiManager's DEV-level log)
  // overruns in a few lines -- at a 2 ms timeout the overflow is DROPPED, and it
  // ate a diagnostic line outright, leaving mangled half-merged output in the
  // ledger. 4 KB absorbs the burst, so a host that is draining loses nothing and
  // the timeout only ever bites when nothing is reading at all.
  Serial.setTxBufferSize(4096);
  Serial.setTxTimeoutMs(2);
#endif

  // FIRST LINE OUT, before any subsystem can fail and before anything else can
  // scroll it away: what this binary actually is. See BuildIdentity.h -- a
  // wrong-env flash presents as a hardware or upstream fault, and the only cheap
  // defence is that the device says its own name unprompted.
  BuildIdentity::PrintBanner();
  // ...and which half of the flash it woke up in. Same reasoning as the banner: after an
  // OTA the device is the only thing that can tell you the update stuck rather than being
  // rolled back, and it must say so unprompted.
  LogOtaSlot("boot");

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

  // Board-specific bring-up that has to happen before the display. On SKUs whose panel/touch
  // reset and chip-select hang off an I2C IO expander (the S3-2.1), this drives that expander
  // (and the IMU); on the C3 it's a no-op. See variant::BoardPreInit().
  // BEFORE ANYTHING NETWORKED. mbedTLS frees with whatever allocator is
  // installed at free() time, so this has to be in place before the first TLS
  // context exists or an internal pointer could reach the PSRAM path. Nothing
  // above this line opens a socket.
  tlsalloc::Install();

  variant::BoardPreInit();

  // initialise LGFX + screen. init() returns false if the panel/bus didn't come up (on an
  // RGB panel that means the framebuffer/bus init failed -> nothing scans out).
  const bool panelOk = tft.init();
  tft.invertDisplay(BLIPSCOPE_DISP_INVERT); // per-variant: the GC9A01 boots inverted, the ST7701 doesn't
  // drive the backlight via PWM (configured in LGFX.h) so it's dimmable; full
  // brightness for the boot screen until AircraftManager applies the saved level
  tft.setBrightness(255);

  // The full-frame backbuffer only fits on boards with PSRAM (480x480x8bpp ~= 230 KB); banded
  // SKUs keep it in internal RAM so a TLS handshake still has contiguous heap. setPsram() must
  // precede createSprite().
  // COLOUR DEPTH. 16bpp (RGB565) on PSRAM boards, 8bpp only where the buffer has
  // to live in internal RAM.
  //
  // 8bpp in LovyanGFX is RGB332: 8 levels of red, 8 of green, FOUR of blue --
  // 256 colours. That is close to the worst possible palette for the one thing
  // this display does most, which is show a photograph of an aeroplane against
  // sky: blues posterize, gradients band, and everything takes on a cast. The
  // depth was chosen when the backbuffer had to fit the C3's internal heap; on a
  // PSRAM board it was buying nothing and costing the picture.
  //
  // Measured on the bench s3-128 (src/probe/BlitProbe.cpp) before changing it:
  //   panel push       23.242 -> 25.728 ms   (+11%, NOT +100% -- the SPI wire is
  //                                           already RGB565, so 8bpp was paying
  //                                           a per-frame conversion instead)
  //   240x240 blit      5.369 ->  6.228 ms   (+16%; 16bpp is a straight copy)
  //   full card frame  29.838 -> 33.214 ms   (+11%, against a 60 ms budget)
  //   PSRAM            +115,200 B total      (1.4% of free)
  //   internal heap    unchanged, tlsOk unchanged
  if constexpr (!variant::BANDED_RENDER) {
    backbuffer.setPsram(true);
    backbuffer.setColorDepth(16);
  } else {
    backbuffer.setColorDepth(8);
  }
  void* spriteBuf = backbuffer.createSprite(SCREEN_SIZE, BAND_H);

  // FIRST PIXELS EVER DRAWN. Everything above this point is panel/bus bring-up, so this
  // is the earliest the wordmark can physically appear -- ~100-150 ms after reset, and
  // ahead of the up-to-3 s USB-CDC wait that used to run before the display came up at
  // all (that wait is now below, spent behind the splash instead of a dark screen).
  // Same wordmark in every mode and on every SKU: it is the brand, not a data-source
  // credit. It STAYS on screen from here until the app's first frame -- the boot
  // messages below render as status lines underneath it. See SplashScreen.h.
  DrawSplash(tft, backbuffer);

  // Now wait (up to 3 s) for a USB CDC host to open the port, so a bench capture still
  // catches the boot log below. On a wall wart no host ever appears and this runs in
  // full -- which is exactly why it must come after the splash, not before it.
  while (!Serial && millis() < 3000) { delay(10); }

  Serial.printf("[disp] tft.init=%d %dx%d  backbuffer %s; psram_free=%u heap_free=%u\n",
                panelOk, (int)tft.width(), (int)tft.height(), spriteBuf ? "ok" : "ALLOC FAILED",
                (unsigned)ESP.getFreePsram(), (unsigned)ESP.getFreeHeap());

#ifdef BLIPSCOPE_BRINGUP_DIAG
  // Bring-up only (behind a per-env flag): cycle full-screen colours straight to the panel so we
  // can SEE whether direct draws reach the scanned framebuffer. If the screen flashes
  // red/green/blue/white, the RGB path works and the problem is elsewhere; if it stays black,
  // our pixels aren't reaching the framebuffer the bus scans out. Remove once the panel is up.
  {
    const uint32_t colors[] = { 0xFF0000, 0x00FF00, 0x0000FF, 0xFFFFFF, 0x000000 };
    const char* names[]     = { "RED", "GREEN", "BLUE", "WHITE", "BLACK" };
    for (int pass = 0; pass < 3; ++pass) {
      for (int c = 0; c < 5; ++c) {
        tft.fillScreen(lgfx::color888((colors[c] >> 16) & 0xFF, (colors[c] >> 8) & 0xFF, colors[c] & 0xFF));
        board::DisplayFlush(tft); // push the fill out of cache so the RGB DMA scans it
        Serial.printf("[diag] fillScreen %-5s  tft.init=%d %dx%d psram_free=%u\n",
                      names[c], panelOk, (int)tft.width(), (int)tft.height(), (unsigned)ESP.getFreePsram());
        Serial.flush();
        delay(700);
      }
    }
  }
#endif

  // NOTE: the "Connecting to Wi-Fi..." status is NOT painted here -- it now sits after the
  // touch-to-forget window, immediately before the join actually starts. It used to be here,
  // which was fine while the boot window sampled silently for 1.2 s and this line stayed up
  // underneath it. Once that window grew an on-screen prompt, this paint was overwritten a
  // few milliseconds later: a green flash between the wordmark and TOUCH & HOLD, and for the
  // three seconds that followed the device claimed to be connecting while it was doing
  // nothing of the kind. Status lines say what is happening now, or they are decoration.

#if defined(BLIPSCOPE_PANEL_SPD2010)
  // Critical ordering for the 1.46B: the Wi-Fi radio must NOT come up in the first seconds after
  // power-on or its bring-up glitches the QSPI panel and it never recovers (writes stop landing)
  // until a cold reboot -- a power/clock stabilisation issue, bisected on hardware (WiFi at ~7 s
  // still fails; ~10 s is reliable). Hold the boot screen here so the rails settle before the radio
  // inrush; the radar renders normally afterwards.
  //
  // This hold gets its own status rather than inheriting the connect line it used to sit under:
  // nine seconds is far too long to spend claiming to connect before the join has been asked for.
  DrawSplash(tft, backbuffer, "Starting up...");
  delay(9000);
#endif

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

  WiFiManagerHelpers::ConfigureWiFiManager(wm, tft, backbuffer);

  // Touch-to-forget, checked BEFORE any join attempt so it works even when the
  // saved credentials would hang the boot. Costs 3 s on an ordinary boot; see
  // BootTouchToForget for why the window is that shape (the touch has to ARRIVE
  // after tft.init(), because tft.init() erases one that was already there).
  //
  // STILL A PRESS-AND-HOLD, AND SCHEDULED FOR REMOVAL. The CST816D may report
  // no change interrupt under a static contact, which is what COM119 is
  // currently measuring; this gesture is on the wrong side of that. It stays
  // ONLY because it is presently the sole recovery path when touch works but
  // the network is unreachable, and deleting it before the five-power-cycle
  // failsafe is proven on hardware would leave that gap open. Removal is gated
  // on that evidence, not on the replacement merely being written.
  if (WiFiManagerHelpers::BootTouchToForget(tft, backbuffer)) {
    factoryreset::Perform(factoryreset::Tier::Wifi, [&wm]() { wm.resetSettings(); });
    DrawSplash(tft, backbuffer, "Wi-Fi cleared", "Restarting into setup...");
    delay(1200);
    ESP.restart();
  }

  // NOW say we are connecting, because from the next line on we are. Composed through the
  // backbuffer so it renders on the SPD2010 (which can't take direct per-glyph writes); a
  // no-op-different path on every other SKU. See BootScreen.h. Status renders BENEATH the
  // wordmark, which stays exactly where it was first painted -- so this reads as a line
  // changing under a fixed brand mark, not as the splash being replaced by a different
  // screen. DrawSplash flushes for RGB panels itself.
  DrawSplash(tft, backbuffer, "Connecting to Wi-Fi...");

  // Aim straight at the last known-good AP before doing this the slow way. On a
  // mesh SSID that turns a multi-scan, multi-minute join into a ~1-2 s one; it
  // silently falls through to autoConnect() whenever it can't (first boot, moved
  // device, that node gone). See WiFiManagerHelpers for the full rationale.
  bool connected = WiFiManagerHelpers::TryFastJoin();
  if (!connected) {
    connected = wm.autoConnect(WiFiManagerHelpers::WiFiManagerName().c_str());
    Serial.printf("[WiFi] autoConnect() returned %s\n",
                  connected ? "true (connected)" : "false (portal timed out / not connected)");
    if (!connected) {
      // The portal timed out with nobody using it (see setConfigPortalTimeout).
      // Reboot rather than run on without a network: that retries the SAVED
      // credentials from the top, which is the whole point -- it is what lets a
      // unit that came up before its router heal itself a few minutes later
      // instead of parking in setup mode until a human intervenes.
      Serial.println("[WiFi] no network and nobody at the portal -- restarting to retry saved credentials");
      DrawSplash(tft, backbuffer, "No Wi-Fi", "Retrying...");
      delay(1500);
      ESP.restart();
    }
  }

  // The portal FAILING already restarted, just above. The portal SUCCEEDING has
  // to as well, and that asymmetry is the bug: WiFiManager's captive portal owns
  // port 80, does not release it before ConfigurationWebServer::Initialise() calls
  // server.begin(), and the bind fails with lwIP ERR_USE. AsyncWebServer cannot
  // report that to its caller, so the device runs perfectly while refusing every
  // connection to its own config page until someone power-cycles it.
  //
  // That is the FIRST-RUN path for every customer -- a factory-fresh unit has no
  // saved credentials, so it always arrives here. See PortalProvisioned().
  if (WiFiManagerHelpers::PortalProvisioned()) {
    Serial.println("[WiFi] provisioned via the portal -- restarting so the config server can bind :80");
    DrawSplash(tft, backbuffer, "Wi-Fi saved", "Starting up...");
    delay(1200);
    ESP.restart();
  }

  // Disable Wi-Fi modem-sleep. By default the radio sleeps between beacons and
  // wakes each DTIM to listen, pulsing the supply current at the beacon rate.
  // On this board that periodic load makes the decoupling caps near the ESP32-C3
  // sing -- a faint, steady buzz. Keeping the radio always-on flattens the draw
  // and silences it. The device is USB/mains powered, so the extra ~20-30 mA is
  // a non-issue, and latency/throughput actually improve.
  if (connected) {
    // Remember this AP for the next boot's fast join -- but only if the link was
    // solid (a weak connect clears the hint instead; see RememberFastAp).
    WiFiManagerHelpers::RememberFastAp();

    WiFi.setSleep(WIFI_PS_NONE);

    // Show how to reach the config page so the user doesn't have to remember the device's
    // name later: it lives at http://<name>.local (mDNS), with the IP as a fallback for
    // networks where mDNS doesn't resolve. Held a few seconds before OTA/app startup.
    // Composed through the backbuffer so it renders on the SPD2010 (see BootScreen.h); the
    // host line is also available any time on the radar's Stats screen.
    const String host = DeviceIdentity::Name() + ".local";
    const String ip   = WiFi.localIP().toString();
    DrawSplash(tft, backbuffer, "- CONNECTED -", host.c_str(), ip.c_str());
    delay(4000);
  }

  // start NTP in UTC; the on-screen clock applies the configured offset, and the
  // solar auto-dim works directly in UTC
  configTime(0, 0, "pool.ntp.org");

  // self-update from the latest GitHub release before normal startup; reboots
  // into the new firmware if one is newer than this build
  MaybeUpdateFirmware(tft, backbuffer, http);

  // begin background server for configuration
  configServer.Initialise();

  // One-shot config migrations, BEFORE anything reads settings. Ordering is
  // load-bearing: run this after Initialise() and the app spends its first
  // session on the pre-migration values, which for #238 is exactly the session a
  // customer would be looking at while wondering why the OTA changed nothing.
  configmigration::Apply();

  // A private ntfy topic, generated once on a factory-fresh device (14.1).
  // AFTER Wi-Fi, because esp_random() wants the RF subsystem up for full
  // entropy, and BEFORE the app reads settings, so the first session already
  // has it. Only ever writes a key that has never been written -- an owner
  // who saved the form with the box cleared made a decision.
  ntfytopic::Ensure();

  // initialise the active app (radar or EAM monitor)
  appManager.Initialise();

  // Claim the reserved TLS handshake block (heap fix 4).
  //
  // TAKEN HERE, AT THE END OF setup(), AND THE PLACEMENT IS THE WHOLE TRICK.
  // Earlier would be a bigger, more contiguous heap to carve from -- and would
  // also mean competing with WiFi/TLS/display bring-up, which are the largest
  // allocations this device ever makes. Starving boot to protect a handshake
  // would trade an intermittent blank card for a device that does not come up.
  //
  // By this line every one-time allocation has been made and the heap is as
  // whole as it will ever be again. Everything after this point is the churn
  // that docs/heap-fragmentation-2026-08-17.md measured eroding ~10 KB of
  // contiguous headroom in 12 idle minutes -- so this is the last moment the
  // block is cheap.
  //
  // A failure here is not fatal and not logged as an error: the device simply
  // runs as it does today, and the health line's ball=0 says so.
  heaphealth::ReserveHandshakeBallast();
  Serial.printf("[heap] handshake ballast %s (%u B); free8=%u\n",
                heaphealth::BallastHeld() ? "reserved" : "UNAVAILABLE",
                (unsigned)heaphealth::TLS_HANDSHAKE_BYTES,
                (unsigned)heaphealth::FreeInternal8Bit());

#ifdef SOAK_TEST
  // Arm the human-scale gesture script; the normal bring-up above (WiFi, NTP,
  // config server, real cloud fetching) is exactly what the soak exercises.
  SoakHarness::Setup(appManager);
#endif
}

void loop()
{
#if !defined(FEATURE_EAM) && !defined(FEATURE_SPACE) && !defined(FEATURE_SEISMIC) && !defined(FEATURE_BIRDING) && !defined(FEATURE_FISHING) && !defined(FEATURE_CLAUDESCOPE) && !defined(FEATURE_SPEED)
  // Frame-time instrumentation (radar builds): the whole pass -- update, draw,
  // flush -- is one sample; AircraftManager logs avg/p95 + heap every 30 s and
  // shouts when a budget breaks. u32 micros() wrap-around subtracts correctly.
  const uint32_t frameStartUs = micros();
#endif

  // Perform a requested reset and reboot into the setup portal -- from the
  // config web page, or from the on-screen Stats reset menu (radar builds),
  // which is the path that still works when the device is off the network.
  //
  // BOTH SOURCES ARE CONSUMED, then the larger tier wins. Short-circuiting on
  // the first would leave the other's request latched to fire on the next pass,
  // which after a reboot means a second, unrequested reset.
  factoryreset::Tier tier = configServer.ConsumeResetTier();
#if !defined(FEATURE_EAM) && !defined(FEATURE_SPACE) && !defined(FEATURE_SEISMIC) && !defined(FEATURE_BIRDING) && !defined(FEATURE_FISHING) && !defined(FEATURE_CLAUDESCOPE) && !defined(FEATURE_SPEED)
  tier = factoryreset::Larger(tier, appManager.ConsumeResetTier());
#endif
  if (tier != factoryreset::Tier::None) {
    factoryreset::Perform(tier, [&wm]() { wm.resetSettings(); });
    DrawSplash(tft, backbuffer,
               tier == factoryreset::Tier::Factory ? "Factory reset" : "Wi-Fi cleared",
               "Restarting into setup...");
    delay(1000); // let the HTTP response flush, and let the user read the screen
    ESP.restart();
  }

  // RUNTIME WIFI WATCHDOG. Losing the network after boot recovered nowhere: the
  // IDF's auto-reconnect retries forever, setup() has already returned, and the
  // loop never looked at WiFi.status() -- so a password changed while the device
  // was running left a customer staring at a dead feed with no route forward and
  // no indication anything was wrong beyond "no data".
  //
  // Rebooting converts that into the boot-time case, which now self-heals (portal
  // timeout -> restart -> retry saved credentials) or presents the setup portal.
  //
  // 10 minutes, deliberately not less: a router reboot is 1-3 min and a router
  // firmware update can be ~5, and rebooting a perfectly healthy device over a
  // blip would be a worse bug than the one being fixed. At a 15 s poll cadence
  // this is ~40 consecutive missed polls -- not ambiguous. Only armed once we
  // have actually been connected, so it can never fight the boot path.
  {
    constexpr unsigned long WIFI_DOWN_REBOOT_MS = 10UL * 60UL * 1000UL;
    static unsigned long wifiDownSinceMs = 0;
    static bool everConnected = false;
    if (WiFi.status() == WL_CONNECTED) {
      everConnected = true;
      wifiDownSinceMs = 0;
    } else if (everConnected) {
      if (wifiDownSinceMs == 0) {
        wifiDownSinceMs = millis();
        Serial.println("[WiFi] link lost; watchdog armed (reboot in 10 min if it stays down)");
      } else if (millis() - wifiDownSinceMs >= WIFI_DOWN_REBOOT_MS) {
        Serial.println("[WiFi] down 10 min -- restarting to re-run the join/portal path");
        DrawSplash(tft, backbuffer, "Wi-Fi lost", "Restarting...");
        delay(1500);
        ESP.restart();
      }
    }
  }

  // re-check for firmware updates once a day for always-on devices
  static unsigned long lastOtaCheck = 0;
  if (millis() - lastOtaCheck > 24UL * 60UL * 60UL * 1000UL) {
    lastOtaCheck = millis();
    MaybeUpdateFirmware(tft, backbuffer, http);
  }

  // Apply settings saved via the web UI without rebooting. Done here, on the
  // loop task, so all AircraftManager state changes stay on a single task
  // rather than racing the async web-server callback.
  if (configServer.ConsumeConfigChanged())
    appManager.Initialise();

#if !defined(FEATURE_EAM) && !defined(FEATURE_SPACE) && !defined(FEATURE_SEISMIC) && !defined(FEATURE_BIRDING) && !defined(FEATURE_FISHING) && !defined(FEATURE_CLAUDESCOPE) && !defined(FEATURE_SPEED)
  // Somebody opened the Collection page. Flush a dirty logbook HERE, on the loop
  // task that owns it, so the next read is current -- the page is served from
  // NVS and would otherwise lag the live book by up to the persist debounce.
  // Dirty-only and rate-limited inside MaybePersistForFetch(); this is just the
  // task hand-off.
  if (configServer.ConsumeLogbookFlushRequest())
    appManager.FlushLogbookForFetch();
#endif

  // Publish the credential state across the task boundary, so the config page
  // renders the same bit the radar's banner draws from. A plain store on the
  // loop task, read on async_tcp -- the page must never reach into appManager.
  // Radar-only: the sibling editions authenticate to their own backends (or to
  // nothing) and have no equivalent state, so they do not carry the member.
#ifdef BLIPSCOPE_RADAR_EDITION
  configServer.SetNeedsReverify(appManager.NeedsReverify());
#endif

#ifdef SOAK_TEST
  // burst scheduling + the 60 s stats line + the 24 h gate verdict
  SoakHarness::Tick(appManager);
#endif

  appManager.Update();

  // draw cycle: render the frame one horizontal band at a time into the half-height
  // backbuffer, each shifted into place by a BandCanvas, then pushed to its screen
  // rows. The scene is drawn once per band; the app advances per-frame state (animation
  // tick, trail sampling) only on the first pass so the bands stay in sync.
#if !defined(FEATURE_EAM) && !defined(FEATURE_SPACE) && !defined(FEATURE_SEISMIC) && !defined(FEATURE_BIRDING) && !defined(FEATURE_FISHING) && !defined(FEATURE_CLAUDESCOPE) && !defined(FEATURE_SPEED)
  const bool drawScan = appManager.SweepEnabled() && appManager.IsRadarView()
                        && !appManager.NightClockActive()  // no beam under the night clock face
                        && !appManager.SweepSuppressed();  // and none over data too old to be live

  // The sweep angle is owned by AircraftManager (advanced in Update()), so the
  // drawn beam matches the blip paint-and-fade crossing test exactly. Sampled
  // once here so both render bands derive an identical wedge (no seam).
  const float sweep = appManager.CurrentSweepAngle();
#endif

  for (int bandY = 0; bandY < SCREEN_SIZE; bandY += BAND_H) {
    BandCanvas canvas(backbuffer, bandY);
    const bool firstPass = (bandY == 0);

    canvas.fillScreen(lgfx::color888(0, 0, 0));

#if !defined(FEATURE_EAM) && !defined(FEATURE_SPACE) && !defined(FEATURE_SEISMIC) && !defined(FEATURE_BIRDING) && !defined(FEATURE_FISHING) && !defined(FEATURE_CLAUDESCOPE) && !defined(FEATURE_SPEED)
    if (drawScan)
      DrawRadarSweep(canvas, SCREEN_SIZE_DIV_2 - 1, SCREEN_SIZE_DIV_2 - 1, SCREEN_SIZE_DIV_2, sweep);
#endif

    appManager.Draw(canvas, firstPass);
    backbuffer.pushSprite(0, bandY);
  }
  // RGB SKUs draw into a cached PSRAM framebuffer; write it back so the panel DMA sees the
  // new frame. No-op on SPI SKUs (the pushSprite above already hit the panel directly).
  board::DisplayFlush(tft);

#if !defined(FEATURE_EAM) && !defined(FEATURE_SPACE) && !defined(FEATURE_SEISMIC) && !defined(FEATURE_BIRDING) && !defined(FEATURE_FISHING) && !defined(FEATURE_CLAUDESCOPE) && !defined(FEATURE_SPEED)
  appManager.RecordFrameUs(micros() - frameStartUs);

#ifdef FEATURE_CLOUD_FEED
  // The fleet config raised the firmware floor past this build: run the normal
  // OTA check now rather than waiting out the daily timer. Same code path as
  // the daily check; a same-or-older published release is simply a no-op.
  if (appManager.ConsumeOtaCheckRequest())
    MaybeUpdateFirmware(tft, backbuffer, http);
#endif
#endif
}

