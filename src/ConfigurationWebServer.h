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

    // Whether server.begin() actually took port 80 this boot. AsyncWebServer
    // cannot tell us, so Initialise() probes for it -- see the comment there.
    // False means the config page is unreachable until the device is restarted.
    bool serverListening = false;

public:
    ConfigurationWebServer() : server(80) {}
    ConfigurationWebServer(int port) : server(port) {}

    void Initialise();
    [[nodiscard]] const String GetStoredString(const char* key);

    // False means the config page is UNREACHABLE for the rest of this boot --
    // AsyncWebServer::begin() failed to take the port and has no way to say so.
    // Exposed rather than left as a boot-time print because a symptom this
    // invisible should be answerable at any moment, not only from a capture that
    // happened to be attached at startup.
    [[nodiscard]] bool IsListening() const { return serverListening; }

    // Returns true at most once per save, clearing the flag. Lets the main loop
    // reload settings in-place instead of rebooting the device.
    bool ConsumeConfigChanged();

    // Returns true once after the Reset WiFi button is used.
    bool ConsumeWifiReset();
};