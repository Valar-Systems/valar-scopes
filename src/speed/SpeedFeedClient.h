#pragma once

#include <Arduino.h>

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include "HttpRequestManager.h"
#include "SpeedModels.h"

// Polls a MiniSpeedCam's two local endpoints and holds the retained results the screens read.
//
// Architecture mirrors FishingFeedClient / SeismicFeedClient: ONE background worker does the
// blocking GET + parse (sharing HttpRequestManager's single client) and hands a parsed result back
// to the loop via a depth-1 queue. ALL scheduling, backoff, and the result store live on the loop
// task (Poll()), so the store is only ever touched by one task. One fetch is in flight at a time;
// each endpoint has its own interval + backoff (last-good data is retained across failures so a
// screen never blanks).
//
// TWO endpoints, both plain HTTP on the LAN (the camera serves no TLS):
//   - State  GET <origin>/api/state   -- device health + settings + live proximity `signal`
//   - Events GET <origin>/api/events  -- recent passes (speed/age/direction)
// `origin` is resolved by the manager (mDNS name -> IP, or a bare IP / an aggregator base URL) and
// handed in via Configure(); the client just appends the paths. An empty origin gates both feeds off
// (nothing polled until the camera host is configured).
class SpeedFeedClient {
public:
    explicit SpeedFeedClient(HttpRequestManager& httpManager) : http(httpManager) {}

    // Loop-task config snapshot, applied by Configure().
    struct Config {
        String origin;               // e.g. "http://192.168.1.50" -- empty = nothing to poll
        float  intervalScale = 1.0f; // global poll-interval multiplier (>=1 relaxes both pollers)
    };

    void Begin();                        // spawn the worker task + queues (once; idempotent)
    void Configure(const Config& cfg);   // loop: apply config, restage the schedule
    void Poll();                         // loop: apply a ready result, then dispatch the next due fetch

    // ---- result store (loop-task read) ----
    const speed::DeviceState& State()  const { return state; }
    const speed::EventList&   Events() const { return events; }
    bool HasState()  const { return state.valid; }
    bool HasEvents() const { return events.valid; }
    bool HasAny()    const { return state.valid || events.valid; }

    // Freshness of the /api/state poll, for online/offline detection in the manager.
    bool          EverPolledState() const { return everStateOk; }
    unsigned long MsSinceStateOk()  const { return everStateOk ? (millis() - lastStateOkMs) : 0xFFFFFFFFUL; }

private:
    enum FeedIdx : uint8_t { F_STATE, F_EVENTS, F_COUNT };
    struct Feed {
        uint32_t intervalMs = 0;
        uint32_t nextDueMs = 0;
        uint16_t failCount = 0;
    };
    Feed feeds[F_COUNT];

    HttpRequestManager& http;
    Config cfg;

    // result store
    speed::DeviceState state;
    speed::EventList   events;

    bool everStateOk = false;
    unsigned long lastStateOkMs = 0;
    bool firstLogged[F_COUNT] = {};

    // worker task
    TaskHandle_t taskHandle = nullptr;
    QueueHandle_t reqQueue = nullptr;  // loop -> worker: SpeedFetchRequest*
    QueueHandle_t resQueue = nullptr;  // worker -> loop: SpeedFetchResult*
    bool inFlight = false;

    static void Trampoline(void* arg);
    void RunWorker();
    static void Fetch(HttpRequestManager& http,
                      const speed::SpeedFetchRequest& req, speed::SpeedFetchResult& res);

    int  PickDueFeed(uint32_t now) const;
    bool BuildRequest(int feedIdx, speed::SpeedFetchRequest& req) const;
    void ApplyResult(const speed::SpeedFetchResult& res);
    static int FeedForEndpoint(speed::SpeedEndpoint e);
};
