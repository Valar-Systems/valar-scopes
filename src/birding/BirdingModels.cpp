#include "BirdingModels.h"

#include <math.h>

namespace birding {

void ParseObs(JsonArrayConst root, std::vector<Sighting>& out, size_t cap, bool notable)
{
    out.clear();
    if (root.isNull()) return;

    for (JsonObjectConst o : root) {
        if (out.size() >= cap) break;
        Sighting s;
        s.comName = (const char*)(o["comName"] | "");
        if (s.comName.isEmpty()) continue; // nothing to show without a name
        s.speciesCode = (const char*)(o["speciesCode"] | "");
        s.sciName = (const char*)(o["sciName"] | "");
        s.locId = (const char*)(o["locId"] | "");
        s.locName = (const char*)(o["locName"] | "");
        s.obsDt = (const char*)(o["obsDt"] | "");
        s.howMany = o["howMany"] | 0;       // absent for "X" (present, uncounted)
        s.lat = o["lat"] | 0.0;
        s.lon = o["lng"] | 0.0;             // eBird names longitude "lng"
        s.notable = notable;
        out.push_back(s);
    }
}

void ParseHotspots(JsonArrayConst root, std::vector<Hotspot>& out, size_t cap)
{
    out.clear();
    if (root.isNull()) return;

    for (JsonObjectConst o : root) {
        if (out.size() >= cap) break;
        Hotspot h;
        h.name = (const char*)(o["locName"] | "");
        if (h.name.isEmpty()) continue;
        h.locId = (const char*)(o["locId"] | "");
        h.lat = o["lat"] | 0.0;
        h.lon = o["lng"] | 0.0;
        h.numSpecies = o["numSpeciesAllTime"] | 0;
        out.push_back(h);
    }
}

double DistanceKm(double lat1, double lon1, double lat2, double lon2)
{
    constexpr double R = 6371.0;
    const double dLat = (lat2 - lat1) * M_PI / 180.0;
    const double dLon = (lon2 - lon1) * M_PI / 180.0;
    const double a = sin(dLat / 2) * sin(dLat / 2) +
                     cos(lat1 * M_PI / 180.0) * cos(lat2 * M_PI / 180.0) *
                     sin(dLon / 2) * sin(dLon / 2);
    return 2.0 * R * atan2(sqrt(a), sqrt(1 - a));
}

double BearingDeg(double lat1, double lon1, double lat2, double lon2)
{
    const double phi1 = lat1 * M_PI / 180.0, phi2 = lat2 * M_PI / 180.0;
    const double dLon = (lon2 - lon1) * M_PI / 180.0;
    const double y = sin(dLon) * cos(phi2);
    const double x = cos(phi1) * sin(phi2) - sin(phi1) * cos(phi2) * cos(dLon);
    double brg = atan2(y, x) * 180.0 / M_PI;
    if (brg < 0) brg += 360.0;
    return brg;
}

} // namespace birding
