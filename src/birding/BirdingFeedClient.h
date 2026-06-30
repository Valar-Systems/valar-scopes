#pragma once

#include <Arduino.h>
#include <vector>

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include "HttpRequestManager.h"
#include "BirdingModels.h"

// Polls the eBird API and holds the bounded, retained results the screens read.
//
// Architecture mirrors SpaceFeedClient / SeismicFeedClient (and AircraftManager's fetch task): ONE
// background worker does the blocking TLS GET + JSON parse (sharing HttpRequestManager's single TLS
// client), and hands a parsed result back to the loop via a depth-1 queue. ALL scheduling, backoff,
// and the result store live on the loop task (Poll()), so the store is only ever touched by one task.
// One fetch is in flight at a time; each endpoint has its own interval and exponential backoff.
//
// Three endpoints on the Cornell Lab eBird API 2.0, all requiring the user's OWN free token (sent as
// the X-eBirdApiToken header; nothing is polled until a token is set): recent *notable* sightings
// nearby, all recent sightings nearby, and nearby hotspots. No backend is baked in.
class BirdingFeedClient {
public:
    explicit BirdingFeedClient(HttpRequestManager& httpManager) : http(httpManager) {}

    struct Config {
        String apiKey;             // eBird X-eBirdApiToken; empty = poll nothing
        bool hasLatLon = false;
        double lat = 0.0;
        double lon = 0.0;
        int radiusKm = 25;         // eBird `dist` (max 50)
        int backDays = 7;          // eBird `back` (max 30)
        int maxResults = 80;       // eBird `maxResults` cap
        float intervalScale = 1.0f;
    };

    void Begin();
    void Configure(const Config& cfg);
    void Poll();

    // ---- result store (loop-task read) ----
    const std::vector<birding::Sighting>& Notable() const { return notable; }
    const std::vector<birding::Sighting>& Recent() const { return recent; }
    const std::vector<birding::Hotspot>& Hotspots() const { return hotspots; }
    bool HasAny() const { return !notable.empty() || !recent.empty() || !hotspots.empty(); }

private:
    enum FeedIdx : uint8_t { F_NOTABLE, F_RECENT, F_HOTSPOTS, F_COUNT };
    struct Feed {
        uint32_t intervalMs = 0;
        uint32_t nextDueMs = 0;
        uint16_t failCount = 0;
    };
    Feed feeds[F_COUNT];

    HttpRequestManager& http;
    Config cfg;

    std::vector<birding::Sighting> notable;
    std::vector<birding::Sighting> recent;
    std::vector<birding::Hotspot> hotspots;

    bool firstLogged[F_COUNT] = {};

    TaskHandle_t taskHandle = nullptr;
    QueueHandle_t reqQueue = nullptr;
    QueueHandle_t resQueue = nullptr;
    bool inFlight = false;

    static void Trampoline(void* arg);
    void RunWorker();
    static void Fetch(HttpRequestManager& http,
                      const birding::BirdFetchRequest& req, birding::BirdFetchResult& res);

    int PickDueFeed(uint32_t now) const;
    bool BuildRequest(int feedIdx, birding::BirdFetchRequest& req) const;
    void ApplyResult(const birding::BirdFetchResult& res);
    static int FeedForEndpoint(birding::BirdEndpoint e);
};
