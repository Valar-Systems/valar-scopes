#pragma once

#include <Arduino.h>
#include <memory>
#include <vector>

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include "HttpRequestManager.h"
#include "OpenSkyAuthTokenHandler.h"
#include "EamModels.h"
#include "AbncpProvider.h"

// Polls the valar-eam-feed endpoints and the command-post watch, and holds the bounded, retained
// results the screens read.
//
// Architecture mirrors AircraftManager's fetch task: ONE background worker does the blocking
// TLS GET + JSON parse (sharing HttpRequestManager's single TLS client -- the C3 hasn't heap for
// a second), and hands a parsed result back to the loop via a depth-1 queue. ALL scheduling,
// backoff, dedupe, retention, and the result store live on the loop task (Poll()), so the store
// is only ever touched by one task -- the worker never holds a pointer into it. One fetch is in
// flight at a time; each endpoint has its own interval and exponential backoff on failure.
class EamFeedClient {
public:
    EamFeedClient(HttpRequestManager& httpManager, OpenSkyAuthTokenHandler& auth)
        : http(httpManager), authHandler(auth) {}

    // Source for the command-post watch (mirrors the web-config dropdown).
    enum class AbncpSource : uint8_t { Backend, OpenSky };

    // Loop-task config snapshot, applied by Configure().
    struct Config {
        String baseUrl;                  // valar-eam-feed base (no trailing slash needed)
        bool hasLatLon = false;
        double lat = 0.0;
        double lon = 0.0;
        AbncpSource abncpSource = AbncpSource::Backend;
        String openskyId;                // only used when abncpSource == OpenSky
        String openskySecret;
        std::vector<String> abncpWatch;  // ICAO24 hexes for the OpenSky watch
        float intervalScale = 1.0f;      // global poll-interval multiplier (>=1 relaxes all pollers)
    };

    void Begin();                  // spawn the worker task + queues (once; idempotent)
    void Configure(const Config& cfg); // loop: apply config, rebuild ABNCP provider, restage schedule
    void Poll();                   // loop: apply a ready result, then dispatch the next due fetch

    // ---- result store (loop-task read) ----
    const std::vector<eam::Msg>& Latest() const { return latest; }
    const std::vector<eam::Msg>& Skykings() const { return skykings; }
    const eam::Tempo& Tempo() const { return tempo; }
    const std::vector<eam::Codeword>& Codewords() const { return codewords; }
    int CodewordWindowDays() const { return codewordWindowDays; }
    const eam::Propagation& Propagation() const { return propagation; }
    const std::vector<eam::Launch>& Launches() const { return launches; }
    const eam::Abncp& Abncp() const { return abncp; }

    // Non-null while the selected ABNCP source can't poll (e.g. OpenSky creds blank).
    const char* AbncpInertReason() const { return abncpProvider ? abncpProvider->InertReason() : nullptr; }

    // Edge: true once after a genuinely new top-of-feed EAM arrives (drives the "NEW" pulse and,
    // later, the ntfy alert). Consuming clears it.
    bool ConsumeNewLatest();

private:
    // One scheduled endpoint. ABNCP's interval/endpoint come from the provider.
    enum FeedIdx : uint8_t { F_LATEST, F_SKYKINGS, F_TEMPO, F_CODEWORDS, F_PROPAGATION, F_ICBM, F_ABNCP, F_COUNT };
    struct Feed {
        uint32_t intervalMs = 0;
        uint32_t nextDueMs = 0;
        uint16_t failCount = 0;
    };
    Feed feeds[F_COUNT];

    HttpRequestManager& http;
    OpenSkyAuthTokenHandler& authHandler;

    Config cfg;
    std::unique_ptr<eam::AbncpProvider> abncpProvider;

    // result store
    std::vector<eam::Msg> latest;
    std::vector<eam::Msg> skykings;
    eam::Tempo tempo;
    std::vector<eam::Codeword> codewords;
    int codewordWindowDays = 0;
    eam::Propagation propagation;
    std::vector<eam::Launch> launches;
    eam::Abncp abncp;

    String lastTopId;          // top-of-feed EAM id last seen, for new-arrival detection
    bool newLatestEdge = false;

    // worker task
    TaskHandle_t taskHandle = nullptr;
    QueueHandle_t reqQueue = nullptr;  // loop -> worker: EamFetchRequest*
    QueueHandle_t resQueue = nullptr;  // worker -> loop: EamFetchResult*
    bool inFlight = false;

    static void Trampoline(void* arg);
    void RunWorker();                                  // worker task body
    static void Fetch(HttpRequestManager& http, OpenSkyAuthTokenHandler& auth,
                      const eam::EamFetchRequest& req, eam::EamFetchResult& res); // worker: GET + parse

    int PickDueFeed(uint32_t now) const;               // most-overdue ready feed, or -1
    bool BuildRequest(int feedIdx, eam::EamFetchRequest& req) const; // loop: build URL/params
    void ApplyResult(const eam::EamFetchResult& res);  // loop: store + reschedule
    void MergeLatest(std::vector<eam::Msg>& incoming);
    static int FeedForEndpoint(eam::EamEndpoint e);
};
