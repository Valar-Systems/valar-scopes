#include "SpaceModels.h"

#include <stdlib.h>
#include <string.h>

namespace {

// Days from the civil (proleptic Gregorian) date to 1970-01-01 (Howard Hinnant's algorithm).
// Avoids timegm(), which isn't reliably present in this newlib; correct for all UTC dates.
long DaysFromCivil(int y, unsigned m, unsigned d)
{
    y -= m <= 2;
    const long era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097L + (long)doe - 719468;
}

// Extract an XML attribute value: name="value", searched within s[from..to). "" if absent.
String XmlAttr(const String& s, int from, int to, const char* name)
{
    String key = String(name) + "=\"";
    const int k = s.indexOf(key, from);
    if (k < 0 || k >= to) return "";
    const int v = k + (int)key.length();
    const int e = s.indexOf('"', v);
    if (e < 0 || e > to) return "";
    return s.substring(v, e);
}

} // namespace

namespace space {

bool ParseIss(JsonObjectConst root, IssState& out)
{
    if (root.isNull() || root["latitude"].isNull() || root["longitude"].isNull())
        return false;

    out.lat = root["latitude"] | 0.0;
    out.lon = root["longitude"] | 0.0;
    out.altKm = root["altitude"] | 0.0;
    out.velocityKmh = root["velocity"] | 0.0;
    out.footprintKm = root["footprint"] | 0.0;
    out.timestamp = (long)(root["timestamp"] | 0);
    const char* vis = root["visibility"] | "";
    out.sunlit = (strcmp(vis, "daylight") == 0);
    out.valid = true;
    return true;
}

void ParseLaunches(JsonObjectConst root, std::vector<Launch>& out, size_t cap)
{
    out.clear();
    JsonArrayConst arr = root["result"].as<JsonArrayConst>();
    if (arr.isNull()) return;

    for (JsonObjectConst o : arr) {
        if (out.size() >= cap) break;
        Launch L;

        // RocketLaunch.Live names read "Vehicle | Mission"; keep the mission half when present.
        String name = (const char*)(o["name"] | "");
        const int bar = name.indexOf('|');
        if (bar >= 0) { L.mission = name.substring(bar + 1); L.mission.trim(); }
        else          { L.mission = name; }

        L.provider = (const char*)(o["provider"]["name"] | "");
        L.vehicle = (const char*)(o["vehicle"]["name"] | "");

        const String padName = (const char*)(o["pad"]["name"] | "");
        const String loc = (const char*)(o["pad"]["location"]["name"] | "");
        L.pad = padName;
        if (loc.length()) L.pad += (padName.length() ? ", " : "") + loc;

        // Best launch instant: t0 (precise) -> win_open (precise) -> sort_date (coarse unix).
        const String t0 = (const char*)(o["t0"] | "");
        const String winOpen = (const char*)(o["win_open"] | "");
        if (t0.length()) { L.t0Epoch = Iso8601ToEpoch(t0); L.precise = L.t0Epoch > 0; }
        if (!L.precise && winOpen.length()) { L.t0Epoch = Iso8601ToEpoch(winOpen); L.precise = L.t0Epoch > 0; }
        if (L.t0Epoch <= 0) {
            const long sd = String((const char*)(o["sort_date"] | "")).toInt();
            if (sd > 0) { L.t0Epoch = sd; L.precise = false; }
        }
        L.dateStr = (const char*)(o["date_str"] | "");

        out.push_back(L);
    }
}

bool ParseKp(JsonArrayConst root, SpaceWx& out, size_t historyCap)
{
    if (root.isNull()) return false;

    // SWPC's noaa-planetary-k-index.json is an array of objects {time_tag, Kp, a_running,
    // station_count}, newest last, with Kp a JSON number (e.g. 3.33). Verified against the live
    // feed 2026-06-30.
    std::vector<float> vals;
    String lastTime;
    for (JsonObjectConst row : root) {
        if (row.isNull() || row["Kp"].isNull()) continue;
        vals.push_back(row["Kp"] | 0.0f);
        lastTime = (const char*)(row["time_tag"] | "");
    }
    if (vals.empty()) return false;

    out.kp = vals.back();
    out.timeTagEpoch = Iso8601ToEpoch(lastTime);
    const size_t start = vals.size() > historyCap ? vals.size() - historyCap : 0;
    out.history.assign(vals.begin() + start, vals.end());
    out.valid = true;
    return true;
}

void ParseDsn(const String& xml, DsnState& out, size_t cap)
{
    out.links.clear();
    out.valid = false;
    if (xml.isEmpty()) return;

    const int n = (int)xml.length();
    int d = 0;
    // Walk each <dish ...> ... </dish> block; collect its active up/down signals naming a craft.
    while ((d = xml.indexOf("<dish ", d)) >= 0 && out.links.size() < cap) {
        int dEnd = xml.indexOf("</dish>", d);
        if (dEnd < 0) dEnd = n;
        const int dishTagEnd = xml.indexOf('>', d);
        const String dishName = XmlAttr(xml, d, dishTagEnd, "name");

        int s = dishTagEnd;
        while (s < dEnd && out.links.size() < cap) {
            const int up = xml.indexOf("<upSignal", s);
            const int dn = xml.indexOf("<downSignal", s);
            int sig = -1; bool isUp = false;
            if (up >= 0 && up < dEnd && (dn < 0 || up < dn)) { sig = up; isUp = true; }
            else if (dn >= 0 && dn < dEnd) { sig = dn; isUp = false; }
            if (sig < 0) break;

            const int sigEnd = xml.indexOf('>', sig);
            if (sigEnd < 0 || sigEnd > dEnd) break;
            const String craft = XmlAttr(xml, sig, sigEnd, "spacecraft");
            if (XmlAttr(xml, sig, sigEnd, "active") == "true" && craft.length() && craft != "none") {
                DsnLink L;
                L.dish = dishName;
                L.spacecraft = craft;
                L.band = XmlAttr(xml, sig, sigEnd, "band");
                L.dataRateBps = XmlAttr(xml, sig, sigEnd, "dataRate").toDouble();
                L.up = isUp;
                out.links.push_back(L);
            }
            s = sigEnd + 1;
        }
        d = dEnd + 1;
    }
    out.valid = true; // a successful fetch+parse, even if no links are currently active
}

bool ParseHorizonsRange(const String& result, double& deltaAu, double& deldotKms)
{
    const int soe = result.indexOf("$$SOE");
    if (soe < 0) return false;
    const int nl = result.indexOf('\n', soe);
    if (nl < 0) return false;
    int lineEnd = result.indexOf('\n', nl + 1);
    if (lineEnd < 0) lineEnd = (int)result.length();

    String line = result.substring(nl + 1, lineEnd);
    line.trim();
    if (line.isEmpty() || line.startsWith("$$EOE")) return false;

    // Columns: "<date> <hh:mm> <delta> <deldot>" -- the last two whitespace tokens are delta, deldot.
    int sp = line.lastIndexOf(' ');
    if (sp < 0) return false;
    deldotKms = line.substring(sp + 1).toDouble();
    line = line.substring(0, sp);
    line.trim();
    sp = line.lastIndexOf(' ');
    if (sp < 0) return false;
    deltaAu = line.substring(sp + 1).toDouble();
    return deltaAu > 0;
}

bool ParseFlare(JsonArrayConst root, Flare& out)
{
    if (root.isNull()) return false;
    // SWPC GOES xrays-6-hour.json: array of {time_tag, satellite, flux, energy}. Use the long band
    // (0.1-0.8nm) -- the band the NOAA flare class is defined on. Newest sample is last.
    float latest = -1, peak = 0;
    String t;
    for (JsonObjectConst o : root) {
        const char* e = o["energy"] | "";
        if (strcmp(e, "0.1-0.8nm") != 0) continue;
        const float fx = o["flux"] | 0.0f;
        if (fx <= 0) continue;
        latest = fx;
        t = (const char*)(o["time_tag"] | "");
        if (fx > peak) peak = fx;
    }
    if (latest < 0) return false;
    out.fluxWm2 = latest;
    out.peakFluxWm2 = peak;
    out.timeEpoch = Iso8601ToEpoch(t);
    out.valid = true;
    return true;
}

bool ParseCrew(JsonObjectConst root, Crew& out, size_t cap)
{
    out.people.clear();
    out.valid = false;
    if (root.isNull()) return false;
    // corquaid people-in-space mirror: {number, people:[{name, iss(bool), spacecraft, agency, ...}]}.
    // iss flags the station (true=ISS, false=Tiangong -- the only two crewed stations today).
    out.number = root["number"] | 0;
    JsonArrayConst arr = root["people"].as<JsonArrayConst>();
    if (!arr.isNull()) {
        for (JsonObjectConst p : arr) {
            if (out.people.size() >= cap) break;
            const bool iss = p["iss"] | true;
            out.people.push_back({ iss ? String("ISS") : String("Tiangong"),
                                   String((const char*)(p["name"] | "")) });
        }
    }
    if (out.number <= 0) out.number = (int)out.people.size();
    out.valid = true;
    return true;
}

String XrayClass(float f)
{
    char letter; float div;
    if (f >= 1e-4f)      { letter = 'X'; div = 1e-4f; }
    else if (f >= 1e-5f) { letter = 'M'; div = 1e-5f; }
    else if (f >= 1e-6f) { letter = 'C'; div = 1e-6f; }
    else if (f >= 1e-7f) { letter = 'B'; div = 1e-7f; }
    else                 { letter = 'A'; div = 1e-8f; }
    char b[8];
    snprintf(b, sizeof(b), "%c%.1f", letter, f > 0 ? f / div : 0.0f);
    return String(b);
}

long Iso8601ToEpoch(const String& s)
{
    if (s.length() < 16) return 0; // need at least "YYYY-MM-DDThh:mm"
    int Y = 0, Mo = 0, D = 0, h = 0, m = 0, sec = 0;
    if (sscanf(s.c_str(), "%d-%d-%d", &Y, &Mo, &D) != 3) return 0;
    if (Mo < 1 || Mo > 12 || D < 1 || D > 31) return 0;

    const char* t = s.c_str() + 10;        // index 10 is the date/time separator ('T' or ' ')
    if (*t == 'T' || *t == ' ') ++t; else return 0;
    if (sscanf(t, "%d:%d:%d", &h, &m, &sec) < 2) return 0; // seconds optional

    return DaysFromCivil(Y, (unsigned)Mo, (unsigned)D) * 86400L + h * 3600L + m * 60L + sec;
}

} // namespace space
