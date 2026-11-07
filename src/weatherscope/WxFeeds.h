#pragma once

// Weatherscope's three data interfaces (docs/weatherscope-stage3.md, section 2).
//
// The edition reads weather ONLY through these. Which implementation stands behind each one is
// decided in exactly one file, WxFeedFactory.cpp (D1): Stage 3 serves stored data, and the
// proxy-backed implementations replace it there and nowhere else. The device never talks to
// NOAA or NWS itself (D2).
//
// Plain C++ with no Arduino types, so the screen logic built on these is host-tested against
// fakes (test/host/test_wx_feeds.cpp).

#include <stddef.h>
#include <stdint.h>

namespace wx {

// One WXC1 crop: the bytes the shared encoder wrote. include/WxCrop.h validates and decodes them.
struct CropBytes {
    const uint8_t* data = nullptr;
    size_t size = 0;
    bool Empty() const { return data == nullptr || size == 0; }
};

// A basemap package's bytes (BMPK; include/BasemapPackage.h validates them). Not a crop: 3.5b's stored Bend package
// (A10.4, A12.53) is reached through the factory as these.
struct PackageBytes {
    const uint8_t* data = nullptr;
    size_t size = 0;
    bool Empty() const { return data == nullptr || size == 0; }
};

class WxFeed {
public:
    virtual ~WxFeed() = default;
    // The newest crop, or an empty one when there is none yet.
    virtual CropBytes Latest() const = 0;
    // The past hour's frames for the loop, oldest first (step 3.3): writes at most `max` and returns how many.
    // A feed with no history returns 0, so a fake that predates the loop still builds.
    virtual size_t History(CropBytes* out, size_t max) const
    {
        (void)out;
        (void)max;
        return 0;
    }
};

// The nearest station's latest observation, reduced to what the screens read (screens doc 1c): its present weather,
// its time, and its temperature for "54° · NO RAIN" (3.5a, A9.3), and for the conditions screen (3.5c, A12.130) its wind,
// gust and humidity. Each has a flag, because NWS sends nulls. Nothing else is kept.
struct Observation {
    static constexpr size_t kMaxPresent = 4;
    bool valid = false;
    int64_t timeUtc = 0;                    // the observation's timestamp, Unix seconds
    uint8_t presentCount = 0;               // NWS presentWeather entries; 0 = none reported
    const char* present[kMaxPresent] = {};  // each entry's raw METAR string, e.g. "-RA"
    bool hasTempF = false;                  // NWS reports a null temperature at times: then no figure is shown
    double tempF = 0;
    bool hasWind = false;
    int16_t windMph = 0;
    bool hasWindDir = false;
    int16_t windDirDeg = 0;
    bool hasGust = false;
    int16_t gustMph = 0;
    bool hasHumidity = false;
    int16_t humidityPct = 0;
};

class ObsFeed {
public:
    virtual ~ObsFeed() = default;
    virtual Observation Latest() const = 0;
};

// One warning, reduced to what step 3.2 keeps. The polygon and the bottom-line fields join in 3.3,
// with the screen that draws them. Whether a warning has ended (screens doc 3c) is the screen
// logic's call, not the feed's: a feed reports what it holds.
struct Alert {
    const char* event = nullptr;            // "Tornado Warning", ...
    const char* headline = nullptr;
    int64_t expiresUtc = 0;                 // Unix seconds
    int64_t startUtc = 0;                   // onset, else effective (3.5c: the alerts screen orders and shows it)
};

struct AlertList {
    const Alert* items = nullptr;
    size_t count = 0;
};

class AlertFeed {
public:
    virtual ~AlertFeed() = default;
    virtual AlertList Active() const = 0;
};

// The forecast (3.5c, A12.130): the next 24 hours and the day-part periods, reduced to what the screens read. Each value
// NWS may send as null has a flag. Everything is integers, as the record is.
struct Hour {
    int64_t startUtc;
    int16_t tempF;
    uint8_t pop;                            // chance of precipitation, %
    bool hasTemp;
    bool hasPop;
};

struct Period {
    const char* name;                       // "Tuesday Night"; a holiday's name never reaches the screen
    int64_t startUtc;
    bool isDaytime;
    int16_t tempF;
    bool hasTemp;
    uint8_t pop;
    bool hasPop;
    const char* shortForecast;              // cut to 24 characters: free text, so a prefix is a true prefix
};

struct Forecast {
    int64_t issuedUtc = 0;                  // the screens' "now" in stored mode, as the map takes its frame's time
    int offsetMin = 0;                      // the place's local offset, fixed per stored case
    const Hour* hours = nullptr;
    size_t nHours = 0;
    const Period* periods = nullptr;
    size_t nPeriods = 0;
};

class ForecastFeed {
public:
    virtual ~ForecastFeed() = default;
    virtual Forecast Latest() const = 0;
};

}  // namespace wx
