#pragma once

#include <ESPAsyncWebServer.h>

#include "FactoryReset.h"

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

    // Same crossing, opposite trigger: raised when /logbook.json is READ, so the
    // loop task can flush a dirty logbook and the NEXT read is current. The page
    // is served from NVS (JsonStream reads the store, not the live maps), so
    // without this a long-running device's Collection could sit up to ten
    // minutes behind what the radar had already logged.
    volatile bool logbookFlushRequested = false;

    // Raised when the Reset WiFi button is used; loop() forgets the credentials
    // and restarts on the main task (WiFi/restart work off the async callback).
    /// The largest reset the web page has asked for, not yet performed.
    ///
    /// A TIER RATHER THAN A BOOL because there are two of them now, and because
    /// the async web task must not do NVS writes -- it records the request and
    /// the loop task performs it. Stored as the underlying integer so it stays
    /// trivially `volatile`-safe across the two tasks.
    volatile uint8_t resetTierRequested = 0;

    // PUBLISHED BY THE LOOP, READ BY THE PAGE -- the reverse direction of
    // configChanged above, and for the same reason: the page renders on the
    // async_tcp task and must never reach into AircraftManager, which the loop
    // task owns. One bool crossing the boundary is the whole interface.
    //
    // Same bit the radar's banner draws from, not a second evaluation of the same
    // idea, so the screen and the page cannot disagree about whether this board
    // needs attention -- which is exactly the kind of drift a customer reports as
    // "it says one thing here and another there" and nobody can reproduce.
    volatile bool needsReverify = false;

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

    /**
     * Set when /logbook.json is fetched; consumed on the LOOP task.
     *
     * The handler runs on async_tcp and the logbook is written by the loop task,
     * so the flush cannot happen inline -- crossing that boundary is the race the
     * NVS-backed page exists to avoid in the first place. A flag is the whole
     * mechanism: the fetch asks, the loop decides (dirty-only, rate-limited).
     */
    bool ConsumeLogbookFlushRequest();

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
    /// The largest reset requested since the last call, and clears it.
    /// Called from the loop task; main.cpp performs and reboots.
    factoryreset::Tier ConsumeResetTier();

    /// Record a request. Safe from the async web task -- it only sets a flag.
    void RequestReset(factoryreset::Tier tier);

    // Publish the loop task's credential state for the page to render. Called
    // every loop; a plain store, no consume -- this is a LEVEL, not an event, and
    // it must be able to go back down the moment a re-verify succeeds.
    void SetNeedsReverify(bool v) { needsReverify = v; }
};