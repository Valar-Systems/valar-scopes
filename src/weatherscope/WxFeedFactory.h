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
};

Feeds MakeFeeds();

}  // namespace wx
