#pragma once

// The one place that decides which implementation stands behind each of WxFeeds.h's interfaces
// (D1, docs/weatherscope-stage3.md section 2). Callers get interfaces only; replacing the stored
// data with the proxy-backed feeds changes WxFeedFactory.cpp and nothing else.

#include "WxFeeds.h"

namespace wx {

struct Feeds {
    const WxFeed& radar;
    const ObsFeed& obs;
    const AlertFeed& alerts;
    // True only for the stored data. Stored mode cycles made-up scenarios (a synthetic warning among them), so
    // nothing that cycles may run unless this is set -- and a customer unit must never reach it (A8.19's release
    // gate). Set here, by the factory, and nowhere else.
    bool stored;
    // Stored mode's SYNTHETIC all-dry crop for its Clear scenario (Q15, A9.10). Empty unless `stored`: like the
    // cycle, it must be unreachable on a customer unit.
    CropBytes clear;
};

Feeds MakeFeeds();

}  // namespace wx
