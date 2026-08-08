#pragma once

#include <ESPAsyncWebServer.h>

class ConfigurationWebServer {
private:
    AsyncWebServer server;
    // NVS access uses a function-local Preferences per call site, never a shared
    // member: the wrapper is not thread-safe, and the handlers here run on the
    // async_tcp task while GetStoredString runs on the loop task. A shared object
    // lets one task's end() close the other's live handle mid-read/mid-save.

    // Set on the web-server task when settings are saved, consumed on the main
    // loop task. The save handler can't safely touch AircraftManager directly
    // (different FreeRTOS task), so it raises this flag and lets loop() reload.
    volatile bool configChanged = false;

    // Raised when the Reset WiFi button is used; loop() forgets the credentials
    // and restarts on the main task (WiFi/restart work off the async callback).
    volatile bool wifiResetRequested = false;

    // Which port we asked for, kept so the liveness check can look for OUR listener
    // rather than assuming 80.
    const uint16_t listenPort;

    // Result of the post-begin() liveness check. Kept rather than only printed: a
    // symptom this invisible should be answerable at any moment, not only from a
    // capture that happened to be attached at boot.
    bool listening = false;

public:
    ConfigurationWebServer() : server(80), listenPort(80) {}
    ConfigurationWebServer(int port) : server(port), listenPort((uint16_t)port) {}

    void Initialise();

    // Did the config server actually claim its port this boot?
    //
    // AsyncWebServer::begin() returns void. When it cannot take the listen socket it
    // emits one library-level line and carries on, so a device whose config page
    // refuses every connection is byte-for-byte identical, in OUR output, to a healthy
    // one -- which is why #166 shipped into every first-run unit unannounced.
    //
    // This does NOT try to repair anything. The known cause is handled at the source
    // (WiFiManagerHelpers restarts after portal provisioning); this exists so the NEXT
    // cause costs one line in a capture instead of weeks of "works on my bench".
    [[nodiscard]] bool IsListening() const { return listening; }
    [[nodiscard]] const String GetStoredString(const char* key);

    // Returns true at most once per save, clearing the flag. Lets the main loop
    // reload settings in-place instead of rebooting the device.
    bool ConsumeConfigChanged();

    // Returns true once after the Reset WiFi button is used.
    bool ConsumeWifiReset();
};