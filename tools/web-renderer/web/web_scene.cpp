// NOT BUILT. The spike's compiled-in scene (dc81ceb), kept for the record: the web renderer has no data of its
// own, and build.sh does not list this file. site/scene.js is the scene now, on the page's side of the fetch.
//
// Blipscope web renderer: the fixed scene. SYNTHETIC and deterministic -- no recorded position, no real
// flight. Each aircraft flies a straight line across a 42 km disc centred on Bend at constant speed
// (two climb or descend toward Redmond), leaves, stays away long enough for the radar's retention to
// drop it (35 s, past the 30 s ceiling of the grace window), and comes back on the same line: the scene
// loops forever and the picture never freezes. Hex codes sit in the US civil block, clear of every
// military and non-ICAO range, so the radar enriches each one through /enrich like a real contact.
// Callsigns, types, operators and routes are illustrative, not records; registrations are left empty.
#include "web_scene.h"
#include <cmath>
#include <cstdio>
#include <string>

namespace {
struct Plane {
    const char *hex, *cs, *type, *typeName, *op, *from, *to;
    int altFt, gsKt, hdg, vrateFpm, cat;   // cat: the OpenSky emitter category the wire carries
    double crossKm;                        // signed distance of the track from the centre
    double phaseS;                         // where in its cycle the aircraft is at t = 0
};
// clang-format off
const Plane PLANES[] = {
    {"a4f2c1", "ASA321",  "B739", "Boeing 737-900",     "Alaska Airlines",   "SEA", "LAX", 36000, 462, 168,     0, 4,  12.0,   0},
    {"a6b3d2", "SWA2177", "B38M", "Boeing 737 MAX 8",   "Southwest Airlines","PDX", "LAS", 38000, 450, 152,     0, 4,  -8.0, 140},
    {"a1c4e5", "QXE2061", "DH8D", "Dash 8-400",         "Horizon Air",       "SEA", "RDM", 12500, 265, 162, -1100, 3,  21.0,  60},
    {"a3d5f7", "SKW5412", "E75L", "Embraer 175",        "SkyWest Airlines",  "DEN", "RDM", 15000, 300, 284, -1400, 3,  17.0, 210},
    {"a8e6a9", "UAL1734", "A320", "Airbus A320",        "United Airlines",   "SFO", "SEA", 35000, 446, 348,     0, 4, -25.0,  30},
    {"a2f7b4", "DAL1520", "A321", "Airbus A321",        "Delta Air Lines",   "SLC", "PDX", 37000, 468, 301,     0, 4,   5.0, 260},
    {"a5a8c6", "FDX1288", "B763", "Boeing 767-300F",    "FedEx",             "MEM", "PDX", 33000, 474, 292,     0, 6, -15.0, 110},
    {"a7b9d8", "AAL2490", "A21N", "Airbus A321neo",     "American Airlines", "PHX", "SEA", 39000, 455, 336,     0, 4,  30.0, 320},
    {"a9c1e2", "N731DM",  "C172", "Cessna 172",         "",                  "",    "",     7500, 108,  75,     0, 2,   3.0, 400},
    {"a0d2f3", "MED12",   "EC35", "Airbus H135",        "",                  "",    "",     5800, 122, 210,     0, 8,  -4.0, 150},
    {"a3e4a5", "N412PC",  "PC12", "Pilatus PC-12",      "",                  "",    "",    17500, 268, 120,     0, 2, -18.0, 500},
    {"a6f5b7", "EJA512",  "C68A", "Cessna Citation Latitude", "NetJets",     "SUN", "BFI", 41000, 430, 322,     0, 4,  24.0,  80},
    {"a8a6c9", "UPS2915", "B748", "Boeing 747-8F",      "UPS Airlines",      "ANC", "ONT", 40000, 488, 148,     0, 6, -33.0, 190},
    {"a1b7d1", "N522SR",  "SR22", "Cirrus SR22",        "",                  "",    "",     9500, 172, 255,     0, 2,  10.0, 700},
    {"a4c8e3", "QXE2190", "DH8D", "Dash 8-400",         "Horizon Air",       "RDM", "SEA",  6500, 230, 338,  1800, 3,  23.0, 300},
    {"a7d9f5", "SKW3307", "CRJ9", "Bombardier CRJ900",  "SkyWest Airlines",  "SLC", "SEA", 34000, 438, 318,     0, 4, -20.0, 360},
    {"a9e1a7", "FFT1503", "A20N", "Airbus A320neo",     "Frontier Airlines", "DEN", "PDX", 36000, 452, 296,     0, 4,  -1.0, 430},
    {"a2f2b9", "N181CP",  "P28A", "Piper Cherokee",     "",                  "",    "",     6900, 104, 340,     0, 2, -11.0, 900},
    {"a5a3c2", "ASA602",  "B39M", "Boeing 737 MAX 9",   "Alaska Airlines",   "SFO", "PAE", 35000, 458, 345,     0, 4,  35.0, 240},
    {"a8b4d4", "N35KB",   "BE35", "Beechcraft Bonanza", "",                  "",    "",     9000, 165, 190,     0, 2,  27.0, 600},
    {"a0c5e6", "SWA1394", "B737", "Boeing 737-700",     "Southwest Airlines","BOI", "OAK", 34000, 441, 238,     0, 4, -29.0, 170},
};
// clang-format on
constexpr int N = sizeof(PLANES) / sizeof(PLANES[0]);
constexpr double DISC_KM = 42.0;     // a little past the radar's 40 km so contacts enter from the rim
constexpr double ABSENT_S = 35.0;    // off the disc this long each cycle: past the radar's 30 s retention
constexpr double KT_TO_KMS = 1.852 / 3600.0;
constexpr double KM_PER_DEG = 111.32;

struct Fix { bool on; double lat, lon; int altFt; };

Fix FixAt(const Plane& p, double t)
{
    const double v = p.gsKt * KT_TO_KMS;                                   // km/s
    const double half = std::sqrt(std::max(1.0, DISC_KM * DISC_KM - p.crossKm * p.crossKm));
    const double cycleS = (2 * half) / v + ABSENT_S;
    const double ts = std::fmod(t + p.phaseS, cycleS);
    if (ts > (2 * half) / v) return {false, 0, 0, 0};                     // away this part of the cycle
    const double s = ts * v - half;                                        // along-track, km from abeam
    const double h = p.hdg * M_PI / 180.0;
    const double ex = std::sin(h), ny = std::cos(h);                       // track direction (east, north)
    const double x = -ny * p.crossKm + ex * s, y = ex * p.crossKm + ny * s;
    const double lat = scene::CENTRE_LAT + y / KM_PER_DEG;
    const double lon = scene::CENTRE_LON + x / (KM_PER_DEG * std::cos(scene::CENTRE_LAT * M_PI / 180.0));
    int alt = p.altFt + (int)(p.vrateFpm * (ts / 60.0));
    if (alt < 5000) alt = 5000;
    if (alt > 22000 && p.vrateFpm > 0) alt = 22000;
    return {true, lat, lon, alt};
}
}

std::string scene::Blips(long nowEpoch, double nowSec)
{
    std::string out = "{\"v\":1,\"t\":" + std::to_string(nowEpoch) + ",\"a\":[";
    bool first = true;
    for (const Plane& p : PLANES) {
        const Fix f = FixAt(p, nowSec);
        if (!f.on) continue;
        char row[200];
        std::snprintf(row, sizeof row, "%s[\"%s\",\"%s\",%ld,%ld,%d,%d,%d,%d,%d,1]", first ? "" : ",", p.hex, p.cs,
                      std::lround(f.lat * 1e4), std::lround(f.lon * 1e4), f.altFt, p.gsKt, p.hdg, p.vrateFpm, p.cat);
        out += row;
        first = false;
    }
    return out + "]}";
}

bool scene::Enrich(const std::string& hex, std::string& out)
{
    for (const Plane& p : PLANES) {
        if (hex != p.hex) continue;
        out = std::string("{\"v\":1,\"r\":\"\",\"t\":\"") + p.type + "\",\"tn\":\"" + p.typeName + "\",\"op\":\"" + p.op +
              "\",\"o\":\"" + p.from + "\",\"d\":\"" + p.to + "\",\"p\":null,\"pk\":\"\"}";
        return true;
    }
    return false;
}
