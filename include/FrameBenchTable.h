#pragma once
// THE FRAME BENCH's (a) TABLE (docs/frame-time-bench.md: H1, H2 and "C1's bench"): fixed_traffic.py's aircraft(home, n,
// t), b530faf4, in C++11 and double precision, for the /blips wire. PURE (no Arduino), so test/host/tbl_host.py compiles
// this header and compares it with the Python, both homes, n = 60, at t in {0, 1794441386, 1794441386 + 86,400}.
// THE ONE PINNED ROUNDING: the wire's integers are lround (half away from zero); tbl_host.py mirrors it by name.
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

namespace fbtable {

constexpr double kMiKm = 1.609344;
constexpr double kKmPerDegLat = 111.32;
constexpr double kPi = 3.14159265358979323846;
constexpr int kCategoryA3 = 4;   // LocalCategoryToOpenSky("A3"): A1 -> 2 .. A7 -> 8 (src/models/Aircraft.cpp)

struct Row {
    char hex[8];       // "fb%04x"
    char flight[10];   // "FB%d": the cloud wire carries callsigns trimmed
    double lat, lon;
    int altFt;
    double gs, track;
};

inline double Frac(double x) { return x - floor(x); }

inline Row At(double lat0, double lon0, int i, double t)
{
    const double cos0 = cos(lat0 * kPi / 180.0);
    const double rMi = 1.0 + 7.0 * Frac(i * 0.618034);
    const double gs = 180.0 + 270.0 * Frac(i * 0.754877);
    const int alt = (int)(3000 + 35000 * Frac(i * 0.569840));
    const double sign = (i % 2 == 0) ? 1.0 : -1.0;
    const double phase = 2.0 * kPi * Frac(i * 0.414214);
    const double omega = sign * (gs * 1.852 / 3600.0) / (rMi * kMiKm);   // rad/s: v / r
    const double a = phase + omega * t;
    const double dn = rMi * kMiKm * cos(a), de = rMi * kMiKm * sin(a);   // km north, km east
    Row r;
    snprintf(r.hex, sizeof r.hex, "fb%04x", i);
    snprintf(r.flight, sizeof r.flight, "FB%d", i);
    r.lat = lat0 + dn / kKmPerDegLat;
    r.lon = lon0 + de / (kKmPerDegLat * cos0);
    const double vn = -sin(a) * omega, ve = cos(a) * omega;   // the tangent; heading from north, clockwise
    r.track = fmod(atan2(ve, vn) * 180.0 / kPi + 360.0, 360.0);
    r.altFt = alt;
    r.gs = gs;
    return r;
}

// The wire's integers: lat and lon x 1e4, ground speed and track, each by lround.
inline long Q4(double deg) { return lround(deg * 1e4); }

// One row's quantized wire text, the unit of TBL's CRC: "hex,flight,lat4,lon4,alt,gs,track\n".
inline int Line(const Row& r, char* out, size_t cap)
{
    return snprintf(out, cap, "%s,%s,%ld,%ld,%d,%ld,%ld\n", r.hex, r.flight, Q4(r.lat), Q4(r.lon), r.altFt,
                    lround(r.gs), lround(r.track));
}

// CRC-32 (IEEE, zlib's), bitwise.
inline uint32_t Crc32(uint32_t c, const char* p, size_t n)
{
    c = ~c;
    for (size_t i = 0; i < n; ++i) {
        c ^= (uint8_t)p[i];
        for (int k = 0; k < 8; ++k) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    return ~c;
}

// TBL's sum: the CRC of n rows' wire text at time t.
inline uint32_t Sum(double lat0, double lon0, int n, double t)
{
    uint32_t c = 0;
    char line[96];
    for (int i = 0; i < n; ++i) {
        const int k = Line(At(lat0, lon0, i, t), line, sizeof line);
        c = Crc32(c, line, (size_t)k);
    }
    return c;
}

}  // namespace fbtable
