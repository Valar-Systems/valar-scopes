#pragma once

// Stage 3's implementations of WxFeeds.h: stored data, compiled into the image, with no network.
//
// Only WxFeedFactory.cpp may name these classes (D1, docs/weatherscope-stage3.md A6.2). Everything
// else holds the interface, so swapping in the proxy-backed feeds touches one file.

#include "WxFeeds.h"

namespace wx {

// The KDFW 60 mi crop in test/fixtures/wx/kdfw-60mi.wxc, embedded by the product env's
// board_build.embed_files.
class StoredWxFeed : public WxFeed {
public:
    CropBytes Latest() const override;
};

// One KDFW observation, fetched once at implementation time (A6.3).
class StoredObsFeed : public ObsFeed {
public:
    Observation Latest() const override;
};

// One 2026-10-20 warning, chosen by A6.3's rule.
class StoredAlertFeed : public AlertFeed {
public:
    AlertList Active() const override;
};

}  // namespace wx
