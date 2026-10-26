#include "WxFeedFactory.h"
#include "StoredFeeds.h"

namespace wx {

// Stage 3: stored data behind all three interfaces. Function-local statics, so the feeds exist
// from the first call and are shared by every caller.
Feeds MakeFeeds()
{
    static const StoredWxFeed radar;
    static const StoredObsFeed obs;
    static const StoredAlertFeed alerts;
    return Feeds{radar, obs, alerts, /*stored=*/true, StoredSyntheticClear()};
}

}  // namespace wx
