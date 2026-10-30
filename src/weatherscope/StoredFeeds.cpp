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

// The loop's 15 crops, embedded by WxLoopFrames.S (oldest first). Declared at file scope, as above.
#define WX_LOOP_FRAME(n)                                              \
    extern const uint8_t wx_loop_##n##_start[] asm("wx_loop_" #n "_start"); \
    extern const uint8_t wx_loop_##n##_end[] asm("wx_loop_" #n "_end");
WX_LOOP_FRAME(00)
WX_LOOP_FRAME(01)
WX_LOOP_FRAME(02)
WX_LOOP_FRAME(03)
WX_LOOP_FRAME(04)
WX_LOOP_FRAME(05)
WX_LOOP_FRAME(06)
WX_LOOP_FRAME(07)
WX_LOOP_FRAME(08)
WX_LOOP_FRAME(09)
WX_LOOP_FRAME(10)
WX_LOOP_FRAME(11)
WX_LOOP_FRAME(12)
WX_LOOP_FRAME(13)
WX_LOOP_FRAME(14)
#undef WX_LOOP_FRAME

// 3.5a's synthetic clear crop, embedded by WxLoopFrames.S after the loop.
extern const uint8_t wx_synthetic_clear_start[] asm("wx_synthetic_clear_start");
extern const uint8_t wx_synthetic_clear_end[] asm("wx_synthetic_clear_end");

// 3.5b's Bend place, embedded by WxBendData.S: the loop's 15 crops (oldest first), the synthetic clear crop, the
// package. Declared at file scope, as above.
#define WX_BEND_FRAME(n)                                                      \
    extern const uint8_t wx_bend_##n##_start[] asm("wx_bend_" #n "_start"); \
    extern const uint8_t wx_bend_##n##_end[] asm("wx_bend_" #n "_end");
WX_BEND_FRAME(00)
WX_BEND_FRAME(01)
WX_BEND_FRAME(02)
WX_BEND_FRAME(03)
WX_BEND_FRAME(04)
WX_BEND_FRAME(05)
WX_BEND_FRAME(06)
WX_BEND_FRAME(07)
WX_BEND_FRAME(08)
WX_BEND_FRAME(09)
WX_BEND_FRAME(10)
WX_BEND_FRAME(11)
WX_BEND_FRAME(12)
WX_BEND_FRAME(13)
WX_BEND_FRAME(14)
#undef WX_BEND_FRAME
extern const uint8_t wx_bend_clear_start[] asm("wx_bend_clear_start");
extern const uint8_t wx_bend_clear_end[] asm("wx_bend_clear_end");
extern const uint8_t wx_bend_package_start[] asm("wx_bend_package_start");
extern const uint8_t wx_bend_package_end[] asm("wx_bend_package_end");

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

size_t StoredWxFeed::History(CropBytes* out, size_t max) const
{
    static const uint8_t* const kStart[15] = {wx_loop_00_start, wx_loop_01_start, wx_loop_02_start, wx_loop_03_start,
                                              wx_loop_04_start, wx_loop_05_start, wx_loop_06_start, wx_loop_07_start,
                                              wx_loop_08_start, wx_loop_09_start, wx_loop_10_start, wx_loop_11_start,
                                              wx_loop_12_start, wx_loop_13_start, wx_loop_14_start};
    static const uint8_t* const kEnd[15] = {wx_loop_00_end, wx_loop_01_end, wx_loop_02_end, wx_loop_03_end,
                                            wx_loop_04_end, wx_loop_05_end, wx_loop_06_end, wx_loop_07_end,
                                            wx_loop_08_end, wx_loop_09_end, wx_loop_10_end, wx_loop_11_end,
                                            wx_loop_12_end, wx_loop_13_end, wx_loop_14_end};
    size_t n = 0;
    for (; n < 15 && n < max; ++n) {
        out[n].data = kStart[n];
        out[n].size = (size_t)(kEnd[n] - kStart[n]);
    }
    return n;
}

Observation StoredObsFeed::Latest() const
{
    Observation o;
    o.valid = true;
    o.timeUtc = kObsTimeUtc;
    o.presentCount = 0;
    return o;
}

CropBytes StoredSyntheticClear()
{
    CropBytes c;
    c.data = wx_synthetic_clear_start;
    c.size = (size_t)(wx_synthetic_clear_end - wx_synthetic_clear_start);
    return c;
}

CropBytes StoredBendFeed::Latest() const
{
    CropBytes c;   // the newest: the last of the 15
    c.data = wx_bend_14_start;
    c.size = (size_t)(wx_bend_14_end - wx_bend_14_start);
    return c;
}

size_t StoredBendFeed::History(CropBytes* out, size_t max) const
{
    static const uint8_t* const kStart[15] = {wx_bend_00_start, wx_bend_01_start, wx_bend_02_start, wx_bend_03_start,
                                              wx_bend_04_start, wx_bend_05_start, wx_bend_06_start, wx_bend_07_start,
                                              wx_bend_08_start, wx_bend_09_start, wx_bend_10_start, wx_bend_11_start,
                                              wx_bend_12_start, wx_bend_13_start, wx_bend_14_start};
    static const uint8_t* const kEnd[15] = {wx_bend_00_end, wx_bend_01_end, wx_bend_02_end, wx_bend_03_end,
                                            wx_bend_04_end, wx_bend_05_end, wx_bend_06_end, wx_bend_07_end,
                                            wx_bend_08_end, wx_bend_09_end, wx_bend_10_end, wx_bend_11_end,
                                            wx_bend_12_end, wx_bend_13_end, wx_bend_14_end};
    size_t n = 0;
    for (; n < 15 && n < max; ++n) {
        out[n].data = kStart[n];
        out[n].size = (size_t)(kEnd[n] - kStart[n]);
    }
    return n;
}

CropBytes StoredBendClear()
{
    CropBytes c;
    c.data = wx_bend_clear_start;
    c.size = (size_t)(wx_bend_clear_end - wx_bend_clear_start);
    return c;
}

PackageBytes StoredBendPackage()
{
    PackageBytes p;
    p.data = wx_bend_package_start;
    p.size = (size_t)(wx_bend_package_end - wx_bend_package_start);
    return p;
}

AlertList StoredAlertFeed::Active() const
{
    AlertList l;
    l.items = kAlerts;
    l.count = sizeof(kAlerts) / sizeof(kAlerts[0]);
    return l;
}

}  // namespace wx
