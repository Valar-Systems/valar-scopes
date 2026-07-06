#pragma once

#include <Arduino.h>

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include "HttpRequestManager.h"
#include "AnglerModels.h"

// Polls the Angler edition's Stage-2 live sources and holds the retained results the screens read.
//
// Architecture mirrors SpaceFeedClient / SeismicFeedClient: ONE background worker does the blocking
// TLS GET + JSON parse (sharing HttpRequestManager's single TLS client) and hands a parsed result
// back to the loop via a depth-1 queue. ALL scheduling, backoff, and the result store live on the
// loop task (Poll()); the worker never holds a pointer into the store. One fetch in flight at a time;
// each endpoint has its own interval + exponential backoff (last-good data retained across failures).
//
// Four keyless endpoints, no key and no backend:
//   Tides / WaterTemp -- NOAA CO-OPS datagetter (needs a tide-station id; US), fetched in the user's
//                        chosen units. Only polled when a station is configured.
//   Weather           -- Open-Meteo forecast (current wind/temp/pressure + hourly pressure history
//                        for the barometer trend). Worldwide. Needs a location.
//   Marine            -- Open-Meteo Marine (wave height + sea-surface temperature). Worldwide.
class AnglerFeedClient {
public:
    explicit AnglerFeedClient(HttpRequestManager& httpManager) : http(httpManager) {}

    struct Config {
        bool hasLatLon = false;
        double lat = 0.0;
        double lon = 0.0;
        String tideStation;         // NOAA CO-OPS station id; empty = no tide/water-temp/curve polling
        String buoy;                // NDBC buoy id; empty = no buoy polling
        bool imperial = true;       // ft/degF/mph vs m/degC/(km/h)
        float intervalScale = 1.0f; // global poll-interval multiplier (>=1 relaxes all pollers)
    };

    void Begin();                       // spawn the worker task + queues (once; idempotent)
    void Configure(const Config& cfg);  // loop: apply config, restage the schedule
    void Poll();                        // loop: apply a ready result, then dispatch the next due fetch

    // ---- result store (loop-task read) ----
    const angler::TideData& Tide() const { return tide; }
    const std::vector<std::pair<time_t, float>>& TideCurve() const { return tideCurve; }
    const angler::WeatherData& Weather() const { return weather; }
    const angler::MarineData& Marine() const { return marine; }
    const angler::BuoyData& Buoy() const { return buoy; }
    bool HaveWaterTemp() const { return haveWaterTemp; }
    float WaterTemp() const { return waterTemp; }
    bool HasAny() const { return tide.valid || weather.valid || marine.valid || buoy.valid; }

private:
    enum FeedIdx : uint8_t { F_TIDES, F_TIDECURVE, F_WATERTEMP, F_WEATHER, F_MARINE, F_BUOY, F_COUNT };
    struct Feed {
        uint32_t intervalMs = 0;
        uint32_t nextDueMs = 0;
        uint16_t failCount = 0;
    };
    Feed feeds[F_COUNT];

    HttpRequestManager& http;
    Config cfg;

    // result store
    angler::TideData tide;
    std::vector<std::pair<time_t, float>> tideCurve;
    angler::WeatherData weather;
    angler::MarineData marine;
    angler::BuoyData buoy;
    bool haveWaterTemp = false;
    float waterTemp = 0;

    bool firstLogged[F_COUNT] = {};

    // worker task
    TaskHandle_t taskHandle = nullptr;
    QueueHandle_t reqQueue = nullptr;   // loop -> worker: AnglerFetchRequest*
    QueueHandle_t resQueue = nullptr;   // worker -> loop: AnglerFetchResult*
    bool inFlight = false;

    static void Trampoline(void* arg);
    void RunWorker();
    static void Fetch(HttpRequestManager& http,
                      const angler::AnglerFetchRequest& req, angler::AnglerFetchResult& res);

    int PickDueFeed(uint32_t now) const;
    bool BuildRequest(int feedIdx, angler::AnglerFetchRequest& req);
    void ApplyResult(const angler::AnglerFetchResult& res);
    static int FeedForEndpoint(angler::AnglerEndpoint e);
};
