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

class WxFeed {
public:
    virtual ~WxFeed() = default;
    // The newest crop, or an empty one when there is none yet. The past hour's history joins this
    // interface in step 3.3, with the loop's frames.
    virtual CropBytes Latest() const = 0;
};

// The nearest station's latest observation, reduced to what "here now" reads (screens doc 1c):
// its present weather and its time. Nothing else is kept.
struct Observation {
    static constexpr size_t kMaxPresent = 4;
    bool valid = false;
    int64_t timeUtc = 0;                    // the observation's timestamp, Unix seconds
    uint8_t presentCount = 0;               // NWS presentWeather entries; 0 = none reported
    const char* present[kMaxPresent] = {};  // each entry's raw METAR string, e.g. "-RA"
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

}  // namespace wx
