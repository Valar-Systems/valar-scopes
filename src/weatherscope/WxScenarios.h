// Weatherscope's four demonstration scenarios (docs/weatherscope-stage3.md A8.10, A8.15), built at run time from a
// loop frame -- no coordinate is stored here. The location is the crop's centre; the clock is the frame's epoch.
//
//   Dry          a frame whose nearest echo is beyond 15 mi; no observation, no warning
//   Approaching  a frame with an echo within 15 mi; no observation, no warning
//   HereNow      the approaching frame + a SYNTHETIC observation "-RA", 5 min before the frame's clock
//   Warning      the approaching frame + a SYNTHETIC Severe Thunderstorm Warning: a square +-0.25 deg around the
//                centre; expires at the clock + 45 min on the -05:00 clock; hail "1.00", wind "60 MPH"; motion from
//                240 deg at 30 kt; the storm at the nearest echo cell's centre
//   Stale        the approaching frame read 31 min after its own time (Q15): the clock moves on, the frame does not
//   Clear        a SYNTHETIC all-dry crop (test/fixtures/wx/loop/synthetic-clear.wxc, the newest frame's header) and a
//                SYNTHETIC observation at 54 F with no present weather, 5 min before the clock (Q15)
//
// The synthetic parts are labelled synthetic in the scenario and are never presented as real weather.
// Pure C++11 over WxLogic.h, host-testable (test/host/test_wx_scenarios.cpp).

#pragma once

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "WxCrop.h"
#include "WxLogic.h"

namespace wxscen {

using wxlogic::LatLon;

enum class Kind : uint8_t { Dry, Approaching, HereNow, Warning, Stale, Clear };

constexpr int64_t kObsLeadS = 300;          // the observation, this long before the frame's clock
constexpr int64_t kExpiresAfterS = 45 * 60; // the warning expires this long after it
constexpr int kOffsetMin = -5 * 60;         // CDT on 2024-05-21: the clock NWS would write the expiry on
constexpr int kBendOffsetMin = -7 * 60;     // PDT on 2024-06-02: 3.5b's Bend scenarios' clock (A10.4)
constexpr double kHalfDeg = 0.25;           // the warning's square, around the centre
constexpr int kFromDeg = 240;               // storm motion: from this direction ...
constexpr int kSpeedKt = 30;                // ... at this speed
constexpr int64_t kStaleLateS = 31 * 60;    // Stale: the clock this long after the frame (Q15)
constexpr double kClearTempF = 54.0;        // Clear: the synthetic observation's temperature (Q15)

// The crop's centre, as RK and ring_ref.py take it: the middle of the cell grid.
inline LatLon Centre(const wxcrop::Header& h)
{
    return LatLon{h.latNwMicro / 1e6 - (h.rows - 1) / 2.0 * (h.cellLatMicro / 1e6),
                  h.lonNwMicro / 1e6 + (h.cols - 1) / 2.0 * (h.cellLonMicro / 1e6)};
}

// The centre of the nearest echo cell (code 1-13): NearestEchoMi's scan, row-major, the first minimum wins.
inline bool NearestEchoCell(const wxcrop::Header& h, const uint8_t* codes, LatLon here, LatLon* out)
{
    const double latNw = h.latNwMicro / 1e6, lonNw = h.lonNwMicro / 1e6;
    const double cellLat = h.cellLatMicro / 1e6, cellLon = h.cellLonMicro / 1e6;
    const double kx = wxlogic::kKmPerDeg * cos(here.lat * (wxlogic::kPi / 180.0)), ky = wxlogic::kKmPerDeg;
    double best = -1.0;
    for (uint32_t r = 0; r < h.rows; ++r) {
        const double dy = (latNw - r * cellLat - here.lat) * ky;
        for (uint32_t c = 0; c < h.cols; ++c) {
            if (!wxcrop::IsEcho(codes[r * (uint32_t)h.cols + c])) continue;
            const double dx = (lonNw + c * cellLon - here.lon) * kx;
            const double d2 = dx * dx + dy * dy;
            if (best < 0 || d2 < best) { best = d2; *out = LatLon{latNw - r * cellLat, lonNw + c * cellLon}; }
        }
    }
    return best >= 0;
}

// "2024-05-21T17:09:00-05:00" for a UTC instant read on a clock offMin minutes from UTC; "...Z" when offMin is 0.
inline void IsoOnClock(int64_t utc, int offMin, char* out, size_t cap)
{
    const int64_t t = utc + (int64_t)offMin * 60;
    int64_t days = t >= 0 ? t / 86400 : (t - 86399) / 86400;
    const int64_t sec = t - days * 86400;
    days += 719468;                                              // civil from days (H. Hinnant)
    const int64_t era = (days >= 0 ? days : days - 146096) / 146097;
    const unsigned doe = (unsigned)(days - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    const unsigned d = doy - (153 * mp + 2) / 5 + 1, m = mp < 10 ? mp + 3 : mp - 9;
    const long y = (long)(yoe + era * 400 + (m <= 2));
    const int hh = (int)(sec / 3600), mm = (int)(sec / 60 % 60), ss = (int)(sec % 60);
    // Formatted into a local the compiler can bound (A8.23: g++ 13's -Wformat-truncation sees up to 60 bytes from
    // these int64-derived fields), then copied at most cap - 1 bytes. Every real date is 25 characters.
    char buf[64];
    if (offMin == 0) {
        snprintf(buf, sizeof buf, "%04ld-%02u-%02uT%02d:%02d:%02dZ", y, m, d, hh, mm, ss);
    } else {
        const int a = offMin < 0 ? -offMin : offMin;
        snprintf(buf, sizeof buf, "%04ld-%02u-%02uT%02d:%02d:%02d%c%02d:%02d", y, m, d, hh, mm, ss,
                 offMin < 0 ? '-' : '+', a / 60, a % 60);
    }
    if (cap == 0) return;
    size_t n = strlen(buf);
    if (n > cap - 1) n = cap - 1;
    memcpy(out, buf, n);
    out[n] = '\0';
}

// One scenario. The warning points into this object's own buffers, so it is built in place and never copied.
struct Scenario {
    Kind kind = Kind::Dry;
    bool synthetic = false;            // true when anything was made up (HereNow, Warning, Stale, Clear)
    int64_t nowUtc = 0;
    int64_t frameUtc = 0;              // the frame's own time; the clock is later than it only in Stale
    bool hasTemp = false;              // the observation's temperature (Clear's, synthetic)
    double tempF = 0;
    LatLon here{0, 0};
    bool approaching = false;
    const char* obs[1] = {nullptr};
    size_t obsN = 0;
    bool obsValid = false;
    int64_t obsUtc = 0;
    wxlogic::Warn warn;
    size_t warnN = 0;
    LatLon polygon[5];
    char expiresIso[32] = {};
    char motion[112] = {};

    Scenario() = default;
    Scenario(const Scenario&) = delete;
    Scenario& operator=(const Scenario&) = delete;

    wxlogic::Precip HereNowPrecip() const { return wxlogic::HereNow(obs, obsN, obsValid, obsUtc, nowUtc); }
    wxlogic::Choice Choose() const { return wxlogic::Choose(&warn, warnN, here, nowUtc, HereNowPrecip(), approaching); }
};

// Builds `kind` from a decoded frame. False only for a Warning on a frame with no echo (no storm to place).
inline bool Build(Kind kind, const wxcrop::Header& h, const uint8_t* codes, Scenario* s)
{
    s->kind = kind;
    s->synthetic = kind != Kind::Dry && kind != Kind::Approaching;
    s->frameUtc = h.frameEpoch;
    s->nowUtc = h.frameEpoch + (kind == Kind::Stale ? kStaleLateS : 0);
    s->here = Centre(h);
    s->approaching = wxlogic::EchoWithin(h, codes, s->here);
    s->obsN = 0;
    s->obsValid = false;
    s->hasTemp = false;
    s->tempF = 0;
    s->warnN = 0;
    if (kind == Kind::Clear) {
        s->obsValid = true;                                       // SYNTHETIC: no present weather, 54 F
        s->obsUtc = s->nowUtc - kObsLeadS;
        s->hasTemp = true;
        s->tempF = kClearTempF;
    }
    if (kind == Kind::HereNow) {
        s->obs[0] = "-RA";                                        // SYNTHETIC
        s->obsN = 1;
        s->obsValid = true;
        s->obsUtc = s->nowUtc - kObsLeadS;
    }
    if (kind != Kind::Warning) return true;
    LatLon storm{0, 0};
    if (!NearestEchoCell(h, codes, s->here, &storm)) return false;
    const LatLon c = s->here;
    const double s0 = c.lat - kHalfDeg, n0 = c.lat + kHalfDeg, w0 = c.lon - kHalfDeg, e0 = c.lon + kHalfDeg;
    s->polygon[0] = LatLon{s0, w0};
    s->polygon[1] = LatLon{s0, e0};
    s->polygon[2] = LatLon{n0, e0};
    s->polygon[3] = LatLon{n0, w0};
    s->polygon[4] = s->polygon[0];
    wxlogic::Warn w;                                              // SYNTHETIC
    w.event = "Severe Thunderstorm Warning";
    w.messageType = "Alert";
    w.expiresUtc = s->nowUtc + kExpiresAfterS;
    IsoOnClock(w.expiresUtc, kOffsetMin, s->expiresIso, sizeof(s->expiresIso));
    w.expiresIso = s->expiresIso;
    w.polygon = s->polygon;
    w.polygonN = 5;
    char at[32];
    IsoOnClock(s->nowUtc, kOffsetMin, at, sizeof(at));
    snprintf(s->motion, sizeof(s->motion), "%s...storm...%dDEG...%dKT...%.6f,%.6f", at, kFromDeg, kSpeedKt, storm.lat,
             storm.lon);
    w.motion = s->motion;
    w.maxHailSize = "1.00";
    w.maxWindGust = "60 MPH";
    s->warn = w;
    s->warnN = 1;
    return true;
}

}  // namespace wxscen
