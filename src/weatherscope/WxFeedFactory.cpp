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
    static const StoredBendFeed bend;
    static const StoredForecastFeed forecast;
    static const StoredDayObsFeed dayObs;
    static const StoredDayAlertFeed dayAlerts;
    return Feeds{radar,  obs, alerts, /*stored=*/true, StoredSyntheticClear(), &bend, StoredBendClear(),
                 StoredBendPackage(), &forecast, &dayObs, &dayAlerts};
}

}  // namespace wx
