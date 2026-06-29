#include "EamModels.h"

#include <cstdio>

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

// PLACEHOLDER E-4B "Nightwatch" ICAO24 hexes. The OpenSky command-post watch has no type field,
// so we tag these as E-4B. VERIFY against a live tracker (adsb.lol / ADS-B Exchange) before
// relying on them -- the fleet's hex assignments change. These are also the default seed for the
// OpenSky watchlist (see EamFeedClient); the user can edit the list in web config.
const char* const kE4bHexes[] = { "ae0451", "ae0452", "ae0453", "ae0454" };

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
    return true;
}

void ParseMessages(JsonObjectConst root, std::vector<Msg>& out, size_t cap)
{
    out.clear();
    if (!root["messages"].is<JsonArrayConst>()) return;
    for (JsonObjectConst m : root["messages"].as<JsonArrayConst>()) {
        if (out.size() >= cap) break;
        Msg msg;
        if (ParseMsg(m, msg)) out.push_back(std::move(msg));
    }
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

void ParseCodewords(JsonObjectConst root, std::vector<Codeword>& out, int& windowDays, size_t cap)
{
    out.clear();
    windowDays = root["window_days"].is<int>() ? root["window_days"].as<int>() : 0;
    if (!root["recent"].is<JsonArrayConst>()) return;
    for (JsonObjectConst c : root["recent"].as<JsonArrayConst>()) {
        if (out.size() >= cap) break;
        Codeword cw;
        cw.codeword = c["codeword"].as<String>();
        if (cw.codeword.isEmpty()) continue;
        cw.lastSeen = c["last_seen"].as<String>();
        cw.count = c["count"].is<int>() ? c["count"].as<int>() : 0;
        out.push_back(std::move(cw));
    }
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
            for (JsonVariantConst f : hf["freqs_khz"].as<JsonArrayConst>())
                out.freqsKhz.push_back(f.as<int>());
        out.suggestedKhz = hf["suggested_khz"].is<int>() ? hf["suggested_khz"].as<int>() : 0;
        out.suggestedReason = hf["suggested_reason"].as<String>();
    }
    out.valid = true;
    return true;
}

void ParseLaunches(JsonObjectConst root, std::vector<Launch>& out, size_t cap)
{
    out.clear();
    if (!root["next"].is<JsonArrayConst>()) return;
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
}

bool ParseAbncpBackend(JsonObjectConst root, Abncp& out)
{
    if (root.isNull()) return false;
    out.airborne = root["airborne"].is<bool>() ? root["airborne"].as<bool>() : false;
    out.checkedAt = root["checked_at"].as<String>();
    out.aircraft.clear();
    if (root["aircraft"].is<JsonArrayConst>()) {
        for (JsonObjectConst a : root["aircraft"].as<JsonArrayConst>()) {
            AbncpAircraft ac;
            ac.type = a["type"].as<String>();
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
