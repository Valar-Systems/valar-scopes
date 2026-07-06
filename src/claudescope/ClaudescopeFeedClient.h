#pragma once

#include <Arduino.h>

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include "HttpRequestManager.h"
#include "ClaudescopeModels.h"

// Polls the claudescope-sidecar and holds the last-good usage snapshot the screens read.
//
// Architecture mirrors SpaceFeedClient / SeismicFeedClient: ONE background worker does the blocking
// GET + parse (sharing HttpRequestManager's single HTTP/TLS client) and hands a parsed result back
// to the loop via a depth-1 queue. ALL scheduling, backoff, and the result store live on the loop
// task (Poll()), so the store is only ever touched by one task -- the worker never holds a pointer
// into it. One fetch at a time; exponential backoff on failure (last-good data is retained so the
// gauges never blank on a transient sidecar hiccup).
//
// There is exactly one endpoint (the sidecar's normalized /usage.json). It is gated on `baseUrl`
// being set: with no sidecar configured, BuildRequest returns false and re-arms, so an unconfigured
// device never opens a socket. The sidecar lives on the user's LAN and holds the Claude OAuth token;
// the device only ever sees pre-chewed JSON, never a credential.
class ClaudescopeFeedClient {
public:
    explicit ClaudescopeFeedClient(HttpRequestManager& httpManager) : http(httpManager) {}

    // Loop-task config snapshot, applied by Configure().
    struct Config {
        String baseUrl;              // the sidecar's address; empty = nothing polled
        float  intervalScale = 1.0f; // poll-interval multiplier (>=1 relaxes the poll)
    };

    void Begin();                    // spawn the worker task + queues (once; idempotent)
    void Configure(const Config& cfg); // loop: apply config, restage the schedule
    void Poll();                     // loop: apply a ready result, then dispatch the next due fetch

    // ---- result store (loop-task read) ----
    const claudescope::UsageState& Usage() const { return usage; }
    bool HasData() const { return usage.valid; }

private:
    static constexpr uint32_t USAGE_MS       = 30000;  // ~30 s: the sidecar caches; matches the panel cadence
    static constexpr uint32_t MAX_BACKOFF_MS = 600000; // cap exponential backoff at 10 m

    HttpRequestManager& http;
    Config cfg;

    // schedule (single feed)
    uint32_t intervalMs = USAGE_MS;
    uint32_t nextDueMs  = 0;
    uint16_t failCount  = 0;
    bool     firstLogged = false;

    // result store
    claudescope::UsageState usage;

    // worker task
    TaskHandle_t taskHandle = nullptr;
    QueueHandle_t reqQueue = nullptr;  // loop -> worker: ClaudeFetchRequest*
    QueueHandle_t resQueue = nullptr;  // worker -> loop: ClaudeFetchResult*
    bool inFlight = false;

    static void Trampoline(void* arg);
    void RunWorker();
    static void Fetch(HttpRequestManager& http,
                      const claudescope::ClaudeFetchRequest& req, claudescope::ClaudeFetchResult& res);

    bool BuildRequest(claudescope::ClaudeFetchRequest& req) const;
    void ApplyResult(const claudescope::ClaudeFetchResult& res);
};
