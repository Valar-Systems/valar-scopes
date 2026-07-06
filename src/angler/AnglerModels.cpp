#include "AnglerModels.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace angler {

namespace {

// Days from 1970-01-01 to the given civil (proleptic Gregorian) date. Howard Hinnant's algorithm;
// avoids timegm() (not reliably present in this newlib) and is TZ-independent + reentrant.
long DaysFromCivil(int y, unsigned m, unsigned d)
{
    y -= m <= 2;
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return (long)era * 146097 + (long)doe - 719468;
}

} // namespace

long IsoToEpoch(const char* s)
{
    if (!s || !s[0]) return 0;
    int Y = 0, Mo = 0, D = 0, h = 0, m = 0, sec = 0;
    if (sscanf(s, "%d-%d-%d", &Y, &Mo, &D) != 3) return 0;
    if (Mo < 1 || Mo > 12 || D < 1 || D > 31) return 0;
    long day = DaysFromCivil(Y, (unsigned)Mo, (unsigned)D);
    if (strlen(s) >= 16) {
        const char* t = s + 10;                    // index 10 is the date/time separator
        if (*t == 'T' || *t == ' ') {
            ++t;
            if (sscanf(t, "%d:%d:%d", &h, &m, &sec) < 2) { h = m = sec = 0; }
        }
    }
    return day * 86400L + h * 3600L + m * 60L + sec;
}

bool ParseTides(JsonObjectConst root, TideData& out, size_t cap)
{
    out.valid = false;
    out.events.clear();
    JsonArrayConst arr = root["predictions"].as<JsonArrayConst>();
    if (arr.isNull()) return false;   // {"error":..} / {"message":..} / 504 gateway -> fail
    for (JsonObjectConst e : arr) {
        if (out.events.size() >= cap) break;
        TideEvent ev;
        ev.t = (time_t)IsoToEpoch(e["t"] | "");
        ev.height = atof(e["v"] | "0");
        const char* ty = e["type"] | "";
        ev.high = (ty[0] == 'H');
        if (ev.t > 0) out.events.push_back(ev);
    }
    out.valid = true;   // an empty upcoming list is still a valid fetch
    return true;
}

bool ParseTideCurve(JsonObjectConst root, std::vector<std::pair<time_t, float>>& out, size_t cap)
{
    out.clear();
    JsonArrayConst arr = root["predictions"].as<JsonArrayConst>();
    if (arr.isNull()) return false;
    const size_t total = arr.size();
    const size_t stride = (cap > 0 && total > cap) ? (total + cap - 1) / cap : 1;   // decimate to <= cap
    size_t i = 0;
    for (JsonObjectConst e : arr) {
        if (stride > 1 && (i++ % stride)) continue;
        const long t = IsoToEpoch(e["t"] | "");
        if (t > 0) out.push_back({ (time_t)t, (float)atof(e["v"] | "0") });
    }
    return !out.empty();
}

bool ParseBuoyText(const String& txt, BuoyData& out)
{
    out = BuoyData();
    const int n = txt.length();
    int i = 0;
    while (i < n) {
        int eol = txt.indexOf('\n', i);
        if (eol < 0) eol = n;
        int s = i;
        while (s < eol && (txt[s] == ' ' || txt[s] == '\t' || txt[s] == '\r')) ++s;
        if (s < eol && txt[s] != '#') {
            // Tokenise the first (newest) data row on whitespace: cols 8=WVHT, 9=DPD, 14=WTMP.
            String tok[20]; int nt = 0;
            int p = s;
            while (p < eol && nt < 20) {
                while (p < eol && (txt[p] == ' ' || txt[p] == '\t' || txt[p] == '\r')) ++p;
                int q = p;
                while (q < eol && txt[q] != ' ' && txt[q] != '\t' && txt[q] != '\r') ++q;
                if (q > p) tok[nt++] = txt.substring(p, q);
                p = q;
            }
            auto fld = [&](int idx) -> const String* { return (idx < nt && tok[idx] != "MM") ? &tok[idx] : nullptr; };
            if (const String* w = fld(8))  { out.waveHeightM = w->toFloat(); out.haveWave = true;
                                             if (const String* d = fld(9)) out.wavePeriodS = d->toFloat(); }
            if (const String* t = fld(14)) { out.waterTempC = t->toFloat(); out.haveWaterTemp = true; }
            out.valid = true;
            return true;
        }
        i = eol + 1;
    }
    return false;   // no data row found
}

bool ParseWaterTemp(JsonObjectConst root, float& tempOut, bool& have)
{
    // Most tide stations have no temperature sensor and answer with {"error":{"message":"No data
    // ... not offered at this station"}} -- a PERMANENT "no temp here", not a transient failure.
    // Treat any well-formed reply (data present or an error payload) as a valid empty result so the
    // poller doesn't back off and churn on a station that will never report temperature.
    have = false;
    JsonArrayConst d = root["data"].as<JsonArrayConst>();
    if (!d.isNull() && d.size() > 0) {
        const char* v = d[0]["v"] | "";
        if (v[0]) { tempOut = atof(v); have = true; }
    }
    return true;
}

bool ParseWeather(JsonObjectConst root, WeatherData& out, size_t histCap)
{
    JsonObjectConst cur = root["current"].as<JsonObjectConst>();
    if (cur.isNull()) return false;

    out.airTemp     = cur["temperature_2m"]     | 0.0f;
    out.windSpeed   = cur["wind_speed_10m"]     | 0.0f;
    out.windGust    = cur["wind_gusts_10m"]     | 0.0f;
    out.windDir     = cur["wind_direction_10m"] | 0;
    out.pressureHpa = cur["pressure_msl"]       | 0.0f;
    out.cloud       = cur["cloud_cover"]        | 0;
    out.weatherCode = cur["weather_code"]       | -1;
    out.currentEpoch = IsoToEpoch(cur["time"] | "");

    out.pressHist.clear();
    JsonObjectConst h = root["hourly"].as<JsonObjectConst>();
    if (!h.isNull()) {
        JsonArrayConst t = h["time"].as<JsonArrayConst>();
        JsonArrayConst p = h["pressure_msl"].as<JsonArrayConst>();
        if (!t.isNull() && !p.isNull()) {
            const size_t n = t.size() < p.size() ? t.size() : p.size();
            const size_t start = n > histCap ? n - histCap : 0;
            for (size_t i = start; i < n; ++i) {
                const long e = IsoToEpoch(t[i] | "");
                const float pv = p[i] | 0.0f;
                if (e > 0 && pv > 0) out.pressHist.push_back({ (time_t)e, pv });
            }
        }
    }
    out.valid = true;
    return true;
}

bool ParseMarine(JsonObjectConst root, MarineData& out)
{
    JsonObjectConst cur = root["current"].as<JsonObjectConst>();
    if (!cur.isNull()) {
        if (!cur["wave_height"].isNull())             { out.waveHeightM = cur["wave_height"] | 0.0f;             out.haveWave = true; }
        if (!cur["sea_surface_temperature"].isNull()) { out.seaTempC    = cur["sea_surface_temperature"] | 0.0f; out.haveSst  = true; }
    }
    if (!out.haveWave && !out.haveSst) {   // fall back to the first hourly sample
        JsonObjectConst h = root["hourly"].as<JsonObjectConst>();
        if (!h.isNull()) {
            JsonArrayConst w = h["wave_height"].as<JsonArrayConst>();
            JsonArrayConst s = h["sea_surface_temperature"].as<JsonArrayConst>();
            if (!w.isNull() && w.size() > 0 && !w[0].isNull()) { out.waveHeightM = w[0] | 0.0f; out.haveWave = true; }
            if (!s.isNull() && s.size() > 0 && !s[0].isNull()) { out.seaTempC    = s[0] | 0.0f; out.haveSst  = true; }
        }
    }
    out.valid = true;   // valid even if both absent (inland) -> the Water screen just skips
    return true;
}

} // namespace angler
