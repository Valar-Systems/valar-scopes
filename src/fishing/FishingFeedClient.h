#pragma once

#include <Arduino.h>

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include "HttpRequestManager.h"
#include "FishingModels.h"

// Polls the fishing-conditions feeds and holds the bounded, retained results the screens read.
//
// Architecture mirrors SeismicFeedClient / BirdingFeedClient: ONE background worker does the blocking
// TLS GET + parse (sharing HttpRequestManager's single TLS client) and hands a parsed result back to
// the loop via a depth-1 queue. ALL scheduling, backoff, and the result store live on the loop task
// (Poll()), so the store is only ever touched by one task -- the worker never holds a pointer into it.
// One fetch is in flight at a time; each endpoint has its own interval + exponential backoff (last-good
// data is retained across failures so a dial never blanks).
//
// FIVE endpoints spanning both water types, each individually gated so a fresh-only or salt-only user
// never opens TLS to the other family's APIs (BuildRequest returns false and re-arms):
//   - Flow      (FRESH)  USGS Water Services /iv/          -- gauge height, discharge, water temp, turbidity
//   - Tides     (SALT)   NOAA CO-OPS predictions (hilo)    -- next highs/lows
//   - WaterTemp (SALT)   NOAA CO-OPS water_temperature     -- separate product; many stations lack it
//   - Buoy      (SALT)   NDBC realtime2 (fixed-width text) -- wave height, dominant period, pressure
//   - Weather   (SHARED) Open-Meteo (keyless)              -- air temp, wind, precip, barometric trend
// Sun/moon/solunar are computed on-device (Solunar.cpp), not fetched. The optional `baseUrl` (a future
// valar-fishing-feed aggregator) is carried but unused while empty; the client hits the public APIs
// directly.
class FishingFeedClient {
public:
    explicit FishingFeedClient(HttpRequestManager& httpManager) : http(httpManager) {}

    enum WaterType : uint8_t { FRESH = 0, SALT = 1, BOTH = 2 };

    // Loop-task config snapshot, applied by Configure().
    struct Config {
        String baseUrl;                 // optional aggregator; empty = direct public APIs
        uint8_t waterType = BOTH;       // gates which endpoint family fires
        bool   hasLatLon = false;
        double lat = 0.0;
        double lon = 0.0;
        String usgsSite;                // primary USGS site number (freshwater)
        String noaaStation;             // primary CO-OPS station id (saltwater)
        String buoyId;                  // primary NDBC buoy id (saltwater)
        float  intervalScale = 1.0f;    // global poll-interval multiplier (>=1 relaxes all pollers)
    };

    void Begin();                       // spawn the worker task + queues (once; idempotent)
    void Configure(const Config& cfg);  // loop: apply config, restage the schedule
    void Poll();                        // loop: apply a ready result, then dispatch the next due fetch

    // ---- result store (loop-task read) ----
    const fishing::RiverGauge& Flow()    const { return gauge; }
    const fishing::TideState&  Tide()    const { return tide; }
    const fishing::WaterTemp&  SeaTemp() const { return wtemp; }
    const fishing::BuoyObs&    Buoy()    const { return buoy; }
    const fishing::WeatherObs& Weather() const { return weather; }
    bool HasAny() const {
        return gauge.valid || tide.valid || wtemp.valid || buoy.valid || weather.valid;
    }

private:
    enum FeedIdx : uint8_t { F_FLOW, F_TIDES, F_WTEMP, F_BUOY, F_WEATHER, F_COUNT };
    struct Feed {
        uint32_t intervalMs = 0;
        uint32_t nextDueMs = 0;
        uint16_t failCount = 0;
    };
    Feed feeds[F_COUNT];

    HttpRequestManager& http;
    Config cfg;

    // result store
    fishing::RiverGauge gauge;
    fishing::TideState  tide;
    fishing::WaterTemp  wtemp;
    fishing::BuoyObs    buoy;
    fishing::WeatherObs weather;

    bool firstLogged[F_COUNT] = {};

    // worker task
    TaskHandle_t taskHandle = nullptr;
    QueueHandle_t reqQueue = nullptr;  // loop -> worker: FishingFetchRequest*
    QueueHandle_t resQueue = nullptr;  // worker -> loop: FishingFetchResult*
    bool inFlight = false;

    static void Trampoline(void* arg);
    void RunWorker();
    static void Fetch(HttpRequestManager& http,
                      const fishing::FishingFetchRequest& req, fishing::FishingFetchResult& res);

    int  PickDueFeed(uint32_t now) const;
    bool BuildRequest(int feedIdx, fishing::FishingFetchRequest& req) const;
    void ApplyResult(const fishing::FishingFetchResult& res);
    static int FeedForEndpoint(fishing::FishingEndpoint e);
    static String FirstToken(const String& s);   // first comma/space-separated id from a config field
};
