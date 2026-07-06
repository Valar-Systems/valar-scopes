#include "FishingModels.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

namespace fishing {

namespace {

// Days since the Unix epoch for a civil date (Howard Hinnant's algorithm) -- lets us turn a parsed
// UTC wall-clock into an epoch without depending on timegm(), which newlib doesn't reliably provide.
long DaysFromCivil(int y, int m, int d)
{
    y -= m <= 2;
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (153u * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return (long)(era * 146097 + (int)doe - 719468);
}

long MakeEpochUtc(int y, int mo, int d, int h, int mi, int s)
{
    return DaysFromCivil(y, mo, d) * 86400L + (long)h * 3600L + (long)mi * 60L + s;
}

// Parse an ISO-8601-ish timestamp ("YYYY-MM-DDThh:mm:ss", "YYYY-MM-DD hh:mm", with an optional
// trailing 'Z' or +hh:mm/-hh:mm offset) into a UTC epoch. Feeds we don't request in GMT (USGS /iv/)
// carry an explicit offset; the ones we do (CO-OPS with time_zone=gmt, NDBC, Open-Meteo default) have
// none and are already UTC. Returns 0 on a shape we can't read.
long ParseIso8601(const char* s)
{
    if (!s || !s[0]) return 0;
    int Y = 0, Mo = 0, D = 0, h = 0, mi = 0, sec = 0;
    if (sscanf(s, "%d-%d-%dT%d:%d:%d", &Y, &Mo, &D, &h, &mi, &sec) < 5 &&
        sscanf(s, "%d-%d-%d %d:%d:%d", &Y, &Mo, &D, &h, &mi, &sec) < 5)
        return 0;
    long epoch = MakeEpochUtc(Y, Mo, D, h, mi, sec);

    // Timezone suffix, scanned past the date's own '-' separators (position 11+).
    int tzsign = 0, tzh = 0, tzm = 0;
    for (const char* c = s + 11; *c; ++c) {
        if (*c == 'Z') { tzsign = 0; break; }
        if (*c == '+' || *c == '-') {
            tzsign = (*c == '+') ? 1 : -1;
            sscanf(c + 1, "%d:%d", &tzh, &tzm);
            break;
        }
    }
    // The parsed fields are local wall-clock; real UTC = local - offset.
    epoch -= (long)tzsign * ((long)tzh * 3600L + (long)tzm * 60L);
    return epoch;
}

} // namespace

// ------------------------------------------------------------------ freshwater (USGS /iv/)
void ParseUsgsFlow(JsonObjectConst root, RiverGauge& out)
{
    out = RiverGauge();
    if (root.isNull()) return;

    JsonArrayConst series = root["value"]["timeSeries"].as<JsonArrayConst>();
    if (series.isNull()) return;

    bool got = false;
    for (JsonObjectConst ts : series) {
        JsonObjectConst var = ts["variable"].as<JsonObjectConst>();
        const char* code = var["variableCode"][0]["value"] | "";
        const double noData = var["noDataValue"] | -999999.0;

        JsonArrayConst vals = ts["values"][0]["value"].as<JsonArrayConst>();
        if (vals.isNull() || vals.size() == 0) continue;

        JsonObjectConst last = vals[vals.size() - 1].as<JsonObjectConst>();
        const char* vstr = last["value"] | "";
        if (!vstr[0]) continue;
        float v = String(vstr).toFloat();
        if (fabsf(v - (float)noData) < 0.001f) continue; // sentinel = no data

        if (out.siteName.isEmpty()) {
            out.siteName = (const char*)(ts["sourceInfo"]["siteName"] | "");
            out.siteId   = (const char*)(ts["sourceInfo"]["siteCode"][0]["value"] | "");
        }
        const long ep = ParseIso8601(last["dateTime"] | "");
        if (ep > out.timeEpoch) out.timeEpoch = ep;

        if (strcmp(code, "00065") == 0) { out.gaugeHeightFt = v; got = true; }
        else if (strcmp(code, "00060") == 0) {
            out.dischargeCfs = v; got = true;
            // Flow trend from the first vs the last non-sentinel discharge sample in the window.
            float first = NAN;
            for (JsonObjectConst s : vals) {
                const char* sv = s["value"] | "";
                if (!sv[0]) continue;
                const float fv = String(sv).toFloat();
                if (fabsf(fv - (float)noData) < 0.001f) continue;
                first = fv; break;
            }
            if (!isnan(first)) {
                const float delta = v - first;
                const float dead = fmaxf(1.0f, fabsf(first) * 0.02f); // 2% (or 1 CFS) deadband
                out.flowTrend = delta > dead ? +1 : (delta < -dead ? -1 : 0);
            }
        }
        else if (strcmp(code, "00010") == 0) { out.waterTempF = CtoF(v); got = true; }
        else if (strcmp(code, "63680") == 0) { out.turbidityFnu = v; got = true; }
    }
    out.valid = got;
}

// ------------------------------------------------------------- saltwater (NOAA CO-OPS)
void ParseCoopsTides(JsonObjectConst root, TideState& out)
{
    out = TideState();
    if (root.isNull() || !root["error"].isNull()) return; // bad station / param / no sensor

    JsonArrayConst preds = root["predictions"].as<JsonArrayConst>();
    if (preds.isNull()) return;

    for (JsonObjectConst p : preds) {
        TideEvent e;
        e.timeEpoch = ParseIso8601(p["t"] | "");           // requested in GMT -> UTC
        e.heightFt  = String((const char*)(p["v"] | "0")).toFloat();
        const char* ty = p["type"] | "H";
        e.type = (ty[0] == 'L' || ty[0] == 'l') ? 'L' : 'H';
        if (e.timeEpoch > 0) out.events.push_back(e);
    }
    out.valid = !out.events.empty();
}

void ParseCoopsTideCurve(JsonObjectConst root, std::vector<std::pair<long, float>>& out, size_t cap)
{
    out.clear();
    if (root.isNull() || !root["error"].isNull()) return; // subordinate station / no harmonic curve

    JsonArrayConst preds = root["predictions"].as<JsonArrayConst>();
    if (preds.isNull()) return;

    // Decimate to at most `cap` samples so the retained curve stays small.
    const size_t total = preds.size();
    const size_t stride = (cap > 0 && total > cap) ? (total + cap - 1) / cap : 1;
    size_t i = 0;
    for (JsonObjectConst p : preds) {
        if (stride > 1 && (i++ % stride)) continue;
        const long t = ParseIso8601(p["t"] | "");            // requested in GMT -> UTC
        if (t > 0) out.push_back({t, String((const char*)(p["v"] | "0")).toFloat()});
    }
}

void ParseCoopsWaterTemp(JsonObjectConst root, WaterTemp& out)
{
    out = WaterTemp();
    if (root.isNull() || !root["error"].isNull()) return;

    JsonArrayConst data = root["data"].as<JsonArrayConst>();
    if (data.isNull() || data.size() == 0) return;

    JsonObjectConst last = data[data.size() - 1].as<JsonObjectConst>();
    const char* v = last["v"] | "";
    if (!v[0]) return;
    out.tempF = String(v).toFloat(); // units=english -> already Fahrenheit
    out.timeEpoch = ParseIso8601(last["t"] | "");
    out.valid = true;
}

// ------------------------------------------------------------- saltwater (NDBC buoy, text)
void ParseNdbcBuoy(const String& body, BuoyObs& out)
{
    out = BuoyObs();

    // First non-comment line is the most-recent observation (realtime2 is newest-first).
    String line;
    int start = 0;
    const int n = body.length();
    while (start < n) {
        int nl = body.indexOf('\n', start);
        if (nl < 0) nl = n;
        line = body.substring(start, nl);
        line.trim();
        start = nl + 1;
        if (line.length() && line[0] != '#') break;
        line = "";
    }
    if (line.isEmpty()) return;

    // Whitespace-tokenize the fixed-width row.
    std::vector<String> tok;
    { int i = 0; const int L = line.length();
      while (i < L) {
          while (i < L && isspace((unsigned char)line[i])) i++;
          const int s = i;
          while (i < L && !isspace((unsigned char)line[i])) i++;
          if (i > s) tok.push_back(line.substring(s, i));
      } }
    if (tok.size() < 15) return; // need through WTMP (index 14)

    auto num = [&](int idx) -> float {
        if (idx >= (int)tok.size() || tok[idx] == "MM") return NAN;
        return tok[idx].toFloat();
    };

    // #YY MM DD hh mm WDIR WSPD GST WVHT DPD APD MWD PRES ATMP WTMP ...
    out.timeEpoch = MakeEpochUtc(tok[0].toInt(), tok[1].toInt(), tok[2].toInt(),
                                 tok[3].toInt(), tok[4].toInt(), 0);
    const float wvht = num(8), dpd = num(9), pres = num(12), wtmp = num(14);
    if (!isnan(wvht)) out.waveHeightFt    = MtoFt(wvht);
    if (!isnan(dpd))  out.dominantPeriodS = dpd;
    if (!isnan(pres)) out.pressureHpa     = pres;
    if (!isnan(wtmp)) out.waterTempF      = CtoF(wtmp);
    out.valid = !isnan(out.waveHeightFt) || !isnan(out.pressureHpa) || !isnan(out.waterTempF);
}

// ----------------------------------------------------------------- shared (Open-Meteo)
void ParseOpenMeteo(JsonObjectConst root, WeatherObs& out, size_t histCap)
{
    out = WeatherObs();
    if (root.isNull()) return;

    JsonObjectConst cur = root["current"].as<JsonObjectConst>();
    if (!cur.isNull()) {
        out.airTempF    = cur["temperature_2m"]     | NAN;   // display units (F/C per request)
        out.windMph     = cur["wind_speed_10m"]      | NAN;   // display units (mph/kmh per request)
        out.windDirDeg  = cur["wind_direction_10m"]  | -1;
        out.precipIn    = cur["precipitation"]       | NAN;   // display units (in/mm per request)
        // Prefer sea-level pressure (pressure_msl) for the barometer; fall back to surface_pressure.
        out.pressureHpa = cur["pressure_msl"] | (cur["surface_pressure"] | NAN);
        out.timeEpoch   = ParseIso8601(cur["time"]   | "");
    }

    // 24h sea-level pressure history (oldest..newest), bounded by histCap, for the sparkline + rate.
    JsonObjectConst h = root["hourly"].as<JsonObjectConst>();
    if (!h.isNull()) {
        JsonArrayConst ts = h["time"].as<JsonArrayConst>();
        JsonArrayConst pp = h["pressure_msl"].isNull() ? h["surface_pressure"].as<JsonArrayConst>()
                                                       : h["pressure_msl"].as<JsonArrayConst>();
        if (!ts.isNull() && !pp.isNull()) {
            const size_t n = ts.size() < pp.size() ? ts.size() : pp.size();
            const size_t start = (histCap > 0 && n > histCap) ? n - histCap : 0;
            for (size_t i = start; i < n; ++i) {
                const long e = ParseIso8601(ts[i] | "");
                const float pv = pp[i] | NAN;
                if (e > 0 && !isnan(pv) && pv > 0) out.pressHist.push_back({e, pv});
            }
        }
    }

    // Short barometric trend arrow from first vs last of the retained series (the manager computes the
    // richer ~6h rate for the alert; this drives the on-screen glyph).
    if (out.pressHist.size() >= 2) {
        const float d = out.pressHist.back().second - out.pressHist.front().second;
        out.pressureTrend = d > 0.6f ? +1 : (d < -0.6f ? -1 : 0); // ~0.6 hPa deadband
    }
    out.valid = !isnan(out.airTempF) || !isnan(out.pressureHpa);
}

void ParseOpenMeteoMarine(JsonObjectConst root, MarineObs& out)
{
    out = MarineObs();
    if (root.isNull()) return;

    JsonObjectConst cur = root["current"].as<JsonObjectConst>();
    if (!cur.isNull()) {
        if (!cur["wave_height"].isNull())             { out.waveHeightM = cur["wave_height"] | NAN;             out.haveWave = !isnan(out.waveHeightM); }
        if (!cur["sea_surface_temperature"].isNull()) { out.seaTempC    = cur["sea_surface_temperature"] | NAN; out.haveSst  = !isnan(out.seaTempC); }
    }
    if (!out.haveWave && !out.haveSst) { // fall back to the first hourly sample
        JsonObjectConst h = root["hourly"].as<JsonObjectConst>();
        if (!h.isNull()) {
            JsonArrayConst w = h["wave_height"].as<JsonArrayConst>();
            JsonArrayConst s = h["sea_surface_temperature"].as<JsonArrayConst>();
            if (!w.isNull() && w.size() > 0 && !w[0].isNull()) { out.waveHeightM = w[0] | NAN; out.haveWave = !isnan(out.waveHeightM); }
            if (!s.isNull() && s.size() > 0 && !s[0].isNull()) { out.seaTempC    = s[0] | NAN; out.haveSst  = !isnan(out.seaTempC); }
        }
    }
    out.valid = true; // valid even if both absent (inland) -> the screen just skips the marine line
}

// ----------------------------------------------------------------------------- geo helpers
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

} // namespace fishing
