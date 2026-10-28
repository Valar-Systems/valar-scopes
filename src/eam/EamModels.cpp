#include "EamModels.h"

#include <cstdio>
#include <cstring>
#include <math.h>

namespace eam {

namespace {

// Split a Skyking/EAM text into phonetic groups when the backend didn't pre-split it.
void SplitGroups(const String& text, std::vector<String>& out)
{
    int start = 0;
    const int n = (int)text.length();
    for (int i = 0; i <= n; ++i) {
        const bool sep = (i == n) || text[i] == ' ' || text[i] == '\t' || text[i] == '\n';
        if (sep) {
            if (i > start) out.push_back(text.substring(start, i));
            start = i + 1;
        }
    }
}

MsgType ParseType(const String& t)
{
    if (t == "skyking") return MsgType::Skyking;
    if (t == "eam") return MsgType::Eam;
    return MsgType::Unknown;
}

// The four E-4B "Nightwatch" ICAO24 addresses, cited (from #409; docs/missileer-e4b-table.md). The OpenSky
// command-post watch has no type field, so these are tagged E-4B by address. They are also the default seed for
// the OpenSky watchlist (see EamFeedClient); the user can edit the list in web config.
//
// The USAF fact sheet gives the inventory as four
// (https://www.af.mil/About-Us/Fact-Sheets/Display/Article/104503/e-4b/). A USAF serial is the registration
// ADS-B databases carry for the aircraft.
//
//   hex     serial   the serial is an E-4B
//   ADFEB3  73-1676  "73-1676 (E-4A/B)"     http://www.joebaugher.com/usaf_serials/1973.html
//   ADFEB4  73-1677  "73-1677 (E-4A/B)"     http://www.joebaugher.com/usaf_serials/1973.html
//   ADFEB5  74-0787  "74-0787 Boeing E-4A ... 1980: Converted to E-4B"
//                                           http://www.joebaugher.com/usaf_serials/1974.html
//   ADFEB6  75-0125  "75-0125 Boeing E-4B"  http://www.joebaugher.com/usaf_serials/1975.html
//
// The hex belongs to the serial in three registries, which agree on all four:
//   - tar1090-db, db/AD.js at commit 17679e257733ad808a417cf4a5d52f3b00238887, keys "FEB3".."FEB6";
//   - adsbdb, by serial: https://api.adsbdb.com/v0/aircraft/73-1676 -> "mode_s":"ADFEB3", "E-4B"
//     (and 73-1677, 74-0787, 75-0125 -> ADFEB4, ADFEB5, ADFEB6);
//   - hexdb.io, by hex: https://hexdb.io/api/v1/aircraft/ADFEB3 -> "73-1676", "E-4B"
//     (and ADFEB4..ADFEB6 -> 73-1677, 74-0787, 75-0125).
//
// NOT THE OLD TABLE. Until this change the table held four placeholder addresses, ae0451..ae0454. They are four
// US Navy C-2A Greyhounds, not E-4Bs: adsbdb (https://api.adsbdb.com/v0/aircraft/AE0451 ... AE0454), hexdb.io and
// tar1090-db (A.js keys "E0451".."E0454") all say so. scripts/check_e4b_table.py keeps them out.
const char* const kE4bHexes[] = { "adfeb3", "adfeb4", "adfeb5", "adfeb6" };

} // namespace

bool ParseMsg(JsonObjectConst o, Msg& out)
{
    if (o.isNull()) return false;
    if (!o["id"].is<const char*>() || !o["type"].is<const char*>() || !o["text"].is<const char*>())
        return false;

    out.id = o["id"].as<String>();
    out.type = ParseType(o["type"].as<String>());
    out.text = o["text"].as<String>();

    out.groups.clear();
    if (o["groups"].is<JsonArrayConst>()) {
        for (JsonVariantConst g : o["groups"].as<JsonArrayConst>())
            out.groups.push_back(g.as<String>());
    }
    if (out.groups.empty() && out.text.length())
        SplitGroups(out.text, out.groups);

    out.charCount  = o["char_count"].is<int>()  ? o["char_count"].as<int>()  : (int)out.text.length();
    out.groupCount = o["group_count"].is<int>() ? o["group_count"].as<int>() : (int)out.groups.size();
    out.heardAt = o["heard_at"].as<String>();
    out.heardAtEpoch = Iso8601ToEpoch(out.heardAt);
    out.frequencyKhz = o["frequency_khz"].is<int>() ? o["frequency_khz"].as<int>() : 0;
    out.callsign = o["callsign"].as<String>();
    out.codeword = o["codeword"].as<String>();
    out.malformed = o["malformed"].is<bool>() ? o["malformed"].as<bool>() : false;
    out.partial = o["partial"].is<bool>() ? o["partial"].as<bool>() : false;
    return true;
}

bool ParseMessages(JsonObjectConst root, std::vector<Msg>& out, size_t cap)
{
    out.clear();
    if (!root["messages"].is<JsonArrayConst>()) return false; // wrong shape: do not clear retained data
    for (JsonObjectConst m : root["messages"].as<JsonArrayConst>()) {
        if (out.size() >= cap) break;
        Msg msg;
        if (ParseMsg(m, msg)) out.push_back(std::move(msg));
    }
    return true;
}

bool ParseTempo(JsonObjectConst root, Tempo& out)
{
    if (root.isNull()) return false;
    out.countToday     = root["count_today"].is<int>()        ? root["count_today"].as<int>() : 0;
    out.baselineMedian = root["baseline_median"].as<float>();
    out.ratio          = root["ratio"].as<float>();
    out.level          = root["level"].is<const char*>() ? root["level"].as<String>() : String("normal");
    out.windowDays     = root["window_days"].is<int>() ? root["window_days"].as<int>() : 0;
    out.valid = true;
    return true;
}

bool ParseStats(JsonObjectConst root, Stats& out)
{
    if (root.isNull()) return false;
    out.countToday      = root["count_today"].is<int>() ? root["count_today"].as<int>() : 0;
    out.longest         = root["longest"].is<int>() ? root["longest"].as<int>() : 0;
    out.busiestHourUtc  = root["busiest_hour_utc"].is<int>() ? root["busiest_hour_utc"].as<int>() : -1;

    // by_freq: today's count per HFGCS frequency (+ a khz:0 "other" bucket the device drops).
    out.byFreq.clear();
    if (root["by_freq"].is<JsonArrayConst>()) {
        for (JsonObjectConst f : root["by_freq"].as<JsonArrayConst>()) {
            if (out.byFreq.size() >= 12) break;            // bound a hostile/huge list
            FreqCount fc;
            fc.khz = f["khz"].is<int>() ? f["khz"].as<int>() : 0;
            fc.count = f["count"].is<int>() ? f["count"].as<int>() : 0;
            if (fc.khz) out.byFreq.push_back(fc);          // skip the "other" bucket on the strip
        }
    }

    // by_hour: 24 {hour_utc,count} buckets for the current UTC day.
    for (int i = 0; i < 24; ++i) out.byHour[i] = 0;
    out.hasByHour = false;
    if (root["by_hour"].is<JsonArrayConst>()) {
        for (JsonObjectConst h : root["by_hour"].as<JsonArrayConst>()) {
            const int hr = h["hour_utc"].is<int>() ? h["hour_utc"].as<int>() : -1;
            if (hr >= 0 && hr < 24) out.byHour[hr] = h["count"].is<int>() ? h["count"].as<int>() : 0;
        }
        out.hasByHour = true;
    }

    out.longestQuietMin = root["longest_quiet_min"].is<int>() ? root["longest_quiet_min"].as<int>() : -1;
    out.valid = true;
    return true;
}

bool ParseCodewords(JsonObjectConst root, std::vector<Codeword>& out, int& windowDays, size_t cap)
{
    out.clear();
    windowDays = root["window_days"].is<int>() ? root["window_days"].as<int>() : 0;
    if (!root["recent"].is<JsonArrayConst>()) return false; // wrong shape: do not clear retained data
    for (JsonObjectConst c : root["recent"].as<JsonArrayConst>()) {
        if (out.size() >= cap) break;
        Codeword cw;
        cw.codeword = c["codeword"].as<String>();
        if (cw.codeword.isEmpty()) continue;
        cw.lastSeen = c["last_seen"].as<String>();
        cw.count = c["count"].is<int>() ? c["count"].as<int>() : 0;
        out.push_back(std::move(cw));
    }
    return true;
}

bool ParsePropagation(JsonObjectConst root, Propagation& out)
{
    if (root.isNull()) return false;
    out.updatedAt = root["updated_at"].as<String>();
    out.sfi      = root["sfi"].as<int>();
    out.aIndex   = root["a_index"].as<int>();
    out.kIndex   = root["k_index"].as<int>();
    out.sunspots = root["sunspots"].as<int>();
    out.source   = root["source"].as<String>();

    out.bands.clear();
    if (root["bands"].is<JsonArrayConst>()) {
        for (JsonObjectConst b : root["bands"].as<JsonArrayConst>()) {
            if (out.bands.size() >= 12) break; // HF has a fixed handful of bands; bound a hostile reply
            PropBand pb;
            pb.band = b["band"].as<String>();
            pb.day = b["day"].as<String>();
            pb.night = b["night"].as<String>();
            out.bands.push_back(std::move(pb));
        }
    }

    JsonObjectConst hf = root["hfgcs"];
    if (!hf.isNull()) {
        out.freqsKhz.clear();
        if (hf["freqs_khz"].is<JsonArrayConst>())
            for (JsonVariantConst f : hf["freqs_khz"].as<JsonArrayConst>()) {
                if (out.freqsKhz.size() >= 12) break; // bound a hostile reply
                out.freqsKhz.push_back(f.as<int>());
            }
        out.suggestedKhz = hf["suggested_khz"].is<int>() ? hf["suggested_khz"].as<int>() : 0;
        out.suggestedReason = hf["suggested_reason"].as<String>();
    }

    // Optional space-weather block (also served raw at /spaceweather). .valid gates the banner.
    JsonObjectConst sw = root["space_weather"];
    if (!sw.isNull()) {
        out.space.valid     = true;
        out.space.kp        = sw["kp"].is<int>() ? sw["kp"].as<int>() : -1;       // number | null
        out.space.rScale    = sw["r_scale"].is<int>() ? sw["r_scale"].as<int>() : -1; // 0..5 | null
        out.space.gScale    = sw["g_scale"].is<int>() ? sw["g_scale"].as<int>() : -1; // 0..5 | null
        out.space.xrayClass = sw["xray_class"].as<String>();
        out.space.hfDegraded = sw["hf_degraded"].is<bool>() ? sw["hf_degraded"].as<bool>() : false;
        out.space.note      = sw["note"].as<String>();
    }
    out.valid = true;
    return true;
}

bool ParseLaunches(JsonObjectConst root, std::vector<Launch>& out, size_t cap)
{
    out.clear();
    if (!root["next"].is<JsonArrayConst>()) return false; // wrong shape: do not clear retained data
    for (JsonObjectConst l : root["next"].as<JsonArrayConst>()) {
        if (out.size() >= cap) break;
        Launch lp;
        lp.designation = l["designation"].as<String>();
        lp.windowStart = l["window_start"].as<String>();
        lp.windowEnd = l["window_end"].as<String>();
        lp.windowStartEpoch = Iso8601ToEpoch(lp.windowStart);
        lp.site = l["site"].as<String>();
        lp.source = l["source"].as<String>();
        out.push_back(std::move(lp));
    }
    return true;
}

bool ParseAbncpBackend(JsonObjectConst root, Abncp& out)
{
    if (root.isNull()) return false;
    out.airborne = root["airborne"].is<bool>() ? root["airborne"].as<bool>() : false;
    out.checkedAt = root["checked_at"].as<String>();
    out.aircraft.clear();
    if (root["aircraft"].is<JsonArrayConst>()) {
        for (JsonObjectConst a : root["aircraft"].as<JsonArrayConst>()) {
            if (out.aircraft.size() >= 8) break; // a handful of command posts at most; bound a hostile reply
            AbncpAircraft ac;
            // NULL-CHECKED: the backend sends type null for an aircraft whose
            // upstream row has no type (valar-eam-feed #68). as<String>() would
            // turn that null into the TEXT "null" (ArduinoJson 7 serializes a
            // non-string); `| ""` keeps it empty = unknown.
            ac.type = a["type"] | "";
            ac.callsign = a["callsign"].as<String>();
            ac.hex = a["hex"].as<String>();
            ac.heardAt = a["heard_at"].as<String>();
            if (a["lat"].is<float>() && a["lon"].is<float>()) {
                ac.hasPos = true;
                ac.lat = a["lat"].as<double>();
                ac.lon = a["lon"].as<double>();
            }
            out.aircraft.push_back(std::move(ac));
        }
    }
    out.valid = true;
    return true;
}

bool ParseOpenSkyStates(JsonObjectConst root, Abncp& out)
{
    // OpenSky /states/all: { time, states: [ [icao24, callsign, origin, time_position,
    // last_contact, longitude, latitude, baro_altitude, on_ground, ...], ... ] }. We watch a
    // hex list, so any returned state is a hit; on_ground == false means airborne.
    if (root.isNull()) return false;
    out.aircraft.clear();
    out.airborne = false;
    if (root["states"].is<JsonArrayConst>()) {
        for (JsonArrayConst s : root["states"].as<JsonArrayConst>()) {
            if (out.aircraft.size() >= 8) break; // the watchlist is a handful of hexes; bound a hostile/huge reply
            if (s.size() < 9) continue;
            AbncpAircraft ac;
            ac.hex = s[0].as<String>();
            ac.callsign = s[1].as<String>();
            ac.callsign.trim();
            if (!s[5].isNull() && !s[6].isNull()) {
                ac.hasPos = true;
                ac.lon = s[5].as<double>();
                ac.lat = s[6].as<double>();
            }
            const bool onGround = s[8].is<bool>() ? s[8].as<bool>() : false;
            ac.type = ClassifyAbncp(ac.hex, ac.callsign);
            if (!onGround) out.airborne = true;
            out.aircraft.push_back(std::move(ac));
        }
    }
    out.valid = true;
    return true;
}

bool ParseMilAir(JsonObjectConst root, MilAir& out, size_t cap)
{
    if (root.isNull()) return false;
    out.aircraft.clear();
    out.source = root["source"].as<String>();
    if (root["aircraft"].is<JsonArrayConst>()) {
        for (JsonObjectConst a : root["aircraft"].as<JsonArrayConst>()) {
            if (out.aircraft.size() >= cap) break;
            MilAircraft ac;
            // NULL-CHECKED, as in ParseAbncpBackend: type and category are null for
            // an aircraft of unknown type, and as<String>() would render "null".
            ac.type = a["type"] | "";
            ac.callsign = a["callsign"].as<String>();
            ac.hex = a["hex"].as<String>();
            ac.heardAt = a["heard_at"].as<String>();
            ac.category = a["category"] | "";
            if (a["lat"].is<float>() && a["lon"].is<float>()) {
                ac.hasPos = true;
                ac.lat = a["lat"].as<double>();
                ac.lon = a["lon"].as<double>();
            }
            out.aircraft.push_back(std::move(ac));
        }
    }
    // Trust an explicit count if given (the list may be truncated by the backend or by `cap`).
    out.count = root["count"].is<int>() ? root["count"].as<int>() : (int)out.aircraft.size();
    out.valid = true;
    return true;
}

String ClassifyAbncp(const String& hex, const String& callsign)
{
    String h = hex;
    h.toLowerCase();
    for (const char* e4b : kE4bHexes)
        if (h == e4b) return "E-4B";
    // E-6B Mercury callsigns are not fixed; leave unknown unless the caller knows better.
    (void)callsign;
    return "";
}

std::vector<String> DefaultAbncpWatch()
{
    std::vector<String> out;
    for (const char* h : kE4bHexes) out.push_back(String(h));
    return out;
}

long Iso8601ToEpoch(const String& s)
{
    if (s.length() < 19) return 0;
    int Y, Mo, D, H, Mi, S;
    if (sscanf(s.c_str(), "%d-%d-%dT%d:%d:%d", &Y, &Mo, &D, &H, &Mi, &S) != 6 &&
        sscanf(s.c_str(), "%d-%d-%d %d:%d:%d", &Y, &Mo, &D, &H, &Mi, &S) != 6)
        return 0;
    if (Mo < 1 || Mo > 12 || D < 1 || D > 31) return 0;

    // days_from_civil (Howard Hinnant): days since 1970-01-01 for a proleptic Gregorian date.
    int y = Y - (Mo <= 2 ? 1 : 0);
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (153 * (unsigned)(Mo + (Mo > 2 ? -3 : 9)) + 2) / 5 + (unsigned)D - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    const long days = (long)era * 146097 + (long)doe - 719468;
    return ((days * 24L + H) * 60L + Mi) * 60L + S;
}

} // namespace eam
