#include "StoredFeeds.h"

// Stage 3's stored data (docs/weatherscope-stage3.md section 2, A6.1 and A6.3).
//
// The observation and the alert were fetched ONCE, by the Stage 3 session at implementation time,
// never by a device, and trimmed to the fields kept below. Nothing else from either document is
// stored: no coordinates, polygons, area names, ids, URLs or sender addresses. That keeps the
// image clear of every host fragment D2 looks for.

// The crop, embedded from test/fixtures/wx/kdfw-60mi.wxc: the same symbols the bench uses, declared at
// file scope (inside an unnamed namespace an extern would get internal linkage and never resolve).
extern const uint8_t wx_kdfw_crop_start[] asm("_binary_test_fixtures_wx_kdfw_60mi_wxc_start");
extern const uint8_t wx_kdfw_crop_end[] asm("_binary_test_fixtures_wx_kdfw_60mi_wxc_end");

namespace wx {

namespace {

// KDFW's /observations/latest, fetched 2026-10-21. Its timestamp is 2026-10-21T21:55:00Z, and it
// reported no present weather (the station read "Clear").
constexpr int64_t kObsTimeUtc = 1792619700;

// The 2026-10-20 warning chosen by A6.3's rule: of the types in the screens doc's 3d order
// (Tornado, Snow Squall, Severe Thunderstorm, Flash Flood Warning), the first with any alert sent
// on 2026-10-20 UTC, and within it the earliest sent. That was a Tornado Warning Update, sent
// 2026-10-20T00:02:00Z, expiring 2026-10-20T00:15:00Z.
const Alert kAlerts[] = {
    {"Tornado Warning",
     "Tornado Warning issued October 19 at 6:02PM MDT until October 19 at 6:15PM MDT by NWS El Paso Tx/Santa Teresa NM",
     1792455300},
};

}  // namespace

CropBytes StoredWxFeed::Latest() const
{
    CropBytes c;
    c.data = wx_kdfw_crop_start;
    c.size = (size_t)(wx_kdfw_crop_end - wx_kdfw_crop_start);
    return c;
}

Observation StoredObsFeed::Latest() const
{
    Observation o;
    o.valid = true;
    o.timeUtc = kObsTimeUtc;
    o.presentCount = 0;
    return o;
}

AlertList StoredAlertFeed::Active() const
{
    AlertList l;
    l.items = kAlerts;
    l.count = sizeof(kAlerts) / sizeof(kAlerts[0]);
    return l;
}

}  // namespace wx
