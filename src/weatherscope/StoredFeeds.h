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
    // The loop's 15 SeamlessHSR crops (test/fixtures/wx/loop/), embedded by WxLoopFrames.S, oldest first.
    size_t History(CropBytes* out, size_t max) const override;
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

// 3.5a's SYNTHETIC all-dry crop for the stored Clear scenario (Q15): not a feed and not weather, so it is reached
// only through Feeds::clear, which only the stored factory fills.
CropBytes StoredSyntheticClear();

// 3.5b's stored Bend place (A10.4, A12.53), embedded by WxBendData.S: its loop's 15 SeamlessHSR crops
// (test/fixtures/wx/bend-loop/, oldest first), its SYNTHETIC all-dry crop (Q33) and its basemap package
// (test/fixtures/wx/basemap/bend.bmpk). Reached only through Feeds' bend members, which only the stored factory fills.
class StoredBendFeed : public WxFeed {
public:
    CropBytes Latest() const override;
    size_t History(CropBytes* out, size_t max) const override;
};
CropBytes StoredBendClear();
PackageBytes StoredBendPackage();

}  // namespace wx
