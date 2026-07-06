#include "FishingManager.h"

#include <math.h>
#include <time.h>

#include "Layout.h"
#include "DeviceIdentity.h"

// The Fishing edition (Reelscope) dials. Each draws one full frame into the band canvas in absolute
// screen coordinates. Colours come from the palette scaled by GlowFactor() so night-dim fades
// everything uniformly. Screens degrade gracefully (show "--") when a field hasn't landed.

namespace {

String FitWidth(BandCanvas& c, String s, int maxW)
{
    if (c.textWidth(s) <= maxW) return s;
    while (s.length() > 1 && c.textWidth(s + "...") > maxW) s.remove(s.length() - 1);
    return s + "...";
}

const char* CompassPoint(double brg)
{
    static const char* pts[] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
    int i = (int)lround(brg / 45.0) & 7;
    return pts[i];
}

const char* MoonPhaseName(float p) // p 0..1 (0/1 new, .5 full)
{
    if (p < 0.03f || p > 0.97f) return "New";
    if (p < 0.22f) return "Waxing Crescent";
    if (p < 0.28f) return "First Quarter";
    if (p < 0.47f) return "Waxing Gibbous";
    if (p < 0.53f) return "Full";
    if (p < 0.72f) return "Waning Gibbous";
    if (p < 0.78f) return "Last Quarter";
    return "Waning Crescent";
}

} // namespace

// --------------------------------------------------------------------------------- splash
void FishingManager::DrawSplash(BandCanvas& c)
{
    const float gf = GlowFactor();
    const uint32_t fg    = fishing::ScaleColor(palette.fg, gf);
    const uint32_t dim   = fishing::ScaleColor(palette.dim, gf);
    const uint32_t accent= fishing::ScaleColor(palette.accent, gf);

    c.setTextSize(4);
    CenterText(c, "Reelscope", SCREEN_SIZE_DIV_2 - 44, fg);
    c.setTextSize(1);
    CenterText(c, "fishing conditions", SCREEN_SIZE_DIV_2 - 6, dim);

    // Context-sensitive setup hint.
    String hint;
    if (!hasLatLon) hint = "set your location in web config";
    else if (waterType != FishingFeedClient::SALT && usgsSite.isEmpty() &&
             waterType != FishingFeedClient::FRESH && noaaStation.isEmpty() && buoyId.isEmpty())
        hint = "add a USGS site or NOAA station";
    else if (waterType == FishingFeedClient::FRESH && usgsSite.isEmpty()) hint = "add a USGS site number";
    else if (waterType == FishingFeedClient::SALT && noaaStation.isEmpty() && buoyId.isEmpty())
        hint = "add a NOAA station or NDBC buoy";
    else hint = "reeling in conditions...";

    c.setTextSize(1);
    CenterText(c, hint, SCREEN_SIZE_DIV_2 + 30, accent);

    String host = "http://" + DeviceIdentity::Name() + ".local";
    host.toLowerCase();
    CenterText(c, host, SCREEN_SIZE - 36, dim);
}

// --------------------------------------------------------------------------------- clock
void FishingManager::DrawClock(BandCanvas& c)
{
    const float gf = GlowFactor();
    const uint32_t fg  = fishing::ScaleColor(palette.fg, gf);
    const uint32_t dim = fishing::ScaleColor(palette.dim, gf);

    const time_t utc = time(nullptr);
    if (utc < 1600000000) {
        c.setTextSize(2);
        CenterText(c, "syncing time...", SCREEN_SIZE_DIV_2, dim);
        return;
    }
    time_t local = utc + tzOffsetSec;
    struct tm t; gmtime_r(&local, &t);

    char hhmm[8];
    snprintf(hhmm, sizeof(hhmm), "%02d:%02d", t.tm_hour, t.tm_min);
    c.setTextSize(7);
    CenterText(c, hhmm, SCREEN_SIZE_DIV_2 - 30, fg);

    static const char* days[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    static const char* mon[]  = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                 "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    char date[24];
    snprintf(date, sizeof(date), "%s %s %d", days[t.tm_wday & 7], mon[t.tm_mon % 12], t.tm_mday);
    c.setTextSize(2);
    CenterText(c, date, SCREEN_SIZE_DIV_2 + 40, dim);
}

// --------------------------------------------------------------------------------- tide
void FishingManager::DrawTide(BandCanvas& c)
{
    const float gf = GlowFactor();
    const uint32_t fg    = fishing::ScaleColor(palette.fg, gf);
    const uint32_t dim   = fishing::ScaleColor(palette.dim, gf);
    const uint32_t faint = fishing::ScaleColor(palette.faint, gf);
    const uint32_t accent= fishing::ScaleColor(palette.accent, gf);

    c.setTextSize(2);
    CenterText(c, "TIDE", 26, fg);
    c.setTextSize(1);
    if (feed.Tide().stationId.length())
        CenterText(c, "stn " + feed.Tide().stationId, 52, faint);

    const time_t now = time(nullptr);
    const fishing::TideState& td = feed.Tide();
    const fishing::TideEvent* next = nullptr;
    for (const fishing::TideEvent& e : td.events)
        if ((long)now < e.timeEpoch) { next = &e; break; }

    if (!next) {
        c.setTextSize(2);
        CenterText(c, "no upcoming tide", SCREEN_SIZE_DIV_2, dim);
        return;
    }

    // Headline: RISING/FALLING toward the next extreme + a countdown.
    const bool rising = (next->type == 'H');
    c.setTextSize(2);
    CenterText(c, rising ? "RISING" : "FALLING", 72, rising ? accent : palette.warn);
    char line[48];
    snprintf(line, sizeof(line), "next %s in %s", next->type == 'H' ? "high" : "low",
             FormatCountdown((long)next->timeEpoch - (long)now).c_str());
    c.setTextSize(2);
    CenterText(c, line, 96, fg);
    char meta[48];
    snprintf(meta, sizeof(meta), "%.1f %s  @ %s", next->heightFt, WaveUnit(), FormatClock(next->timeEpoch, tzOffsetSec).c_str());
    c.setTextSize(1);
    CenterText(c, meta, 120, dim);

    // Tide curve: prefer the real 6-minute prediction curve; else cosine-interpolate the hi/lo events.
    const std::vector<std::pair<long, float>>& curve = feed.TideCurve();
    const bool haveCurve = !curve.empty();
    const int L = 34, R = SCREEN_SIZE - 34, yTop = 150, yBot = SCREEN_SIZE - 48;

    long t0, t1; float minH, maxH;
    if (haveCurve) {
        t0 = curve.front().first; t1 = curve.back().first;
        minH = maxH = curve.front().second;
        for (const auto& p : curve) { minH = min(minH, p.second); maxH = max(maxH, p.second); }
    } else {
        t0 = td.events.front().timeEpoch; t1 = td.events.back().timeEpoch;
        minH = maxH = td.events.front().heightFt;
        for (const fishing::TideEvent& e : td.events) { minH = min(minH, e.heightFt); maxH = max(maxH, e.heightFt); }
    }
    if (maxH - minH < 0.1f) maxH = minH + 0.1f;

    if (t1 > t0) {
        auto yOf = [&](float hh) { return yBot - (int)lround((hh - minH) / (maxH - minH) * (yBot - yTop)); };
        if (haveCurve) {
            int px = L, py = yOf(curve.front().second);
            for (size_t i = 1; i < curve.size(); ++i) {
                const int x = L + (int)((long)(R - L) * (curve[i].first - t0) / (t1 - t0));
                const int y = yOf(curve[i].second);
                c.drawLine(px, py, x, y, fg);
                px = x; py = y;
            }
        } else {
            auto hAt = [&](long t) -> float {
                if (t <= td.events.front().timeEpoch) return td.events.front().heightFt;
                for (size_t i = 0; i + 1 < td.events.size(); ++i) {
                    const fishing::TideEvent& a = td.events[i];
                    const fishing::TideEvent& b = td.events[i + 1];
                    if (t >= a.timeEpoch && t <= b.timeEpoch) {
                        const float ph = (float)(t - a.timeEpoch) / (float)(b.timeEpoch - a.timeEpoch);
                        return a.heightFt + (b.heightFt - a.heightFt) * (1.0f - cosf(ph * (float)M_PI)) * 0.5f;
                    }
                }
                return td.events.back().heightFt;
            };
            int px = L, py = yOf(hAt(t0));
            for (int x = L + 3; x <= R; x += 3) {
                const long t = t0 + (long)((long long)(t1 - t0) * (x - L) / (R - L));
                const int y = yOf(hAt(t));
                c.drawLine(px, py, x, y, fg);
                px = x; py = y;
            }
        }
        // hi/lo markers (from the extremes feed) + "now" line.
        for (const fishing::TideEvent& e : td.events) {
            if (e.timeEpoch < t0 || e.timeEpoch > t1) continue;
            const int x = L + (int)((long)(R - L) * (e.timeEpoch - t0) / (t1 - t0));
            c.fillCircle(x, yOf(e.heightFt), 3, e.type == 'H' ? accent : dim);
        }
        if ((long)now >= t0 && (long)now <= t1) {
            const int x = L + (int)((long)(R - L) * ((long)now - t0) / (t1 - t0));
            c.drawFastVLine(x, yTop - 6, (yBot - yTop) + 12, faint);
        }
    }

    c.setTextSize(1);
    CenterText(c, "tap for schedule", SCREEN_SIZE - 22, faint);
}

// --------------------------------------------------------------------------------- flow
void FishingManager::DrawFlow(BandCanvas& c)
{
    const float gf = GlowFactor();
    const uint32_t fg    = fishing::ScaleColor(palette.fg, gf);
    const uint32_t dim   = fishing::ScaleColor(palette.dim, gf);
    const uint32_t faint = fishing::ScaleColor(palette.faint, gf);

    const fishing::RiverGauge& g = feed.Flow();

    c.setTextSize(2);
    CenterText(c, "RIVER FLOW", 26, fg);

    // Big discharge (CFS) with a trend arrow.
    char cfs[16];
    if (!isnan(g.dischargeCfs)) snprintf(cfs, sizeof(cfs), "%.0f", g.dischargeCfs);
    else                       snprintf(cfs, sizeof(cfs), "--");
    c.setTextSize(6);
    CenterText(c, cfs, SCREEN_SIZE_DIV_2 - 40, fg);
    c.setTextSize(2);
    CenterText(c, "CFS", SCREEN_SIZE_DIV_2 + 20, dim);

    if (!isnan(g.dischargeCfs)) {
        c.setTextSize(3);
        c.setTextColor(fishing::TrendColor(palette, g.flowTrend));
        c.drawString(fishing::TrendGlyph(g.flowTrend), SCREEN_SIZE_DIV_2 + 90, SCREEN_SIZE_DIV_2 - 30);
    }

    // Secondary chips: gauge height, water temp, turbidity.
    int y = SCREEN_SIZE_DIV_2 + 56;
    char chip[40];
    if (!isnan(g.gaugeHeightFt)) {
        snprintf(chip, sizeof(chip), "gauge %.2f ft", g.gaugeHeightFt);
        c.setTextSize(2); CenterText(c, chip, y, fg); y += 26;
    }
    String sub;
    if (!isnan(g.waterTempF)) { char t[20]; snprintf(t, sizeof(t), "%.0fF", g.waterTempF); sub += t; }
    if (!isnan(g.turbidityFnu)) { char t[24]; snprintf(t, sizeof(t), "  %.0f FNU", g.turbidityFnu); sub += t; }
    if (sub.length()) { c.setTextSize(1); CenterText(c, sub, y, dim); }

    if (g.siteName.length()) {
        c.setTextSize(1);
        CenterText(c, FitWidth(c, g.siteName, SCREEN_SIZE - 40), SCREEN_SIZE - 28, faint);
    }
}

// --------------------------------------------------------------------------------- temp
void FishingManager::DrawTemp(BandCanvas& c)
{
    const float gf = GlowFactor();
    const uint32_t fg    = fishing::ScaleColor(palette.fg, gf);
    const uint32_t dim   = fishing::ScaleColor(palette.dim, gf);
    const uint32_t faint = fishing::ScaleColor(palette.faint, gf);

    c.setTextSize(2);
    CenterText(c, "WATER TEMP", 26, fg);

    float tF; const char* src = "";
    const bool haveT = BestWaterTemp(tF, src);

    // Waves (buoy real, else Open-Meteo Marine model) share this screen.
    float waveDisp = 0, periodS = 0; const char* wSrc = "";
    const bool haveW = BestWaves(waveDisp, periodS, wSrc);

    if (!haveT && !haveW) {
        c.setTextSize(2);
        CenterText(c, "no reading", SCREEN_SIZE_DIV_2, dim);
        return;
    }

    if (haveT) {
        char big[12];
        snprintf(big, sizeof(big), "%.0f", tF);
        c.setTextSize(8);
        CenterText(c, big, SCREEN_SIZE_DIV_2 - 40, fg);
        c.setTextSize(2);
        CenterText(c, String("\xF7") + TempUnit(), SCREEN_SIZE_DIV_2 + 44, dim); // degree-ish glyph + unit
    } else {
        c.setTextSize(2);
        CenterText(c, "water temp n/a", SCREEN_SIZE_DIV_2 - 10, dim);
    }

    // Active-feeding band indicator (thresholds in the configured display units).
    if (haveT && (!isnan(tempLoF) || !isnan(tempHiF))) {
        const bool inBand = (isnan(tempLoF) || tF >= tempLoF) && (isnan(tempHiF) || tF <= tempHiF);
        const char* label = inBand ? "IN THE STRIKE ZONE" : (!isnan(tempLoF) && tF < tempLoF ? "below band" : "above band");
        c.setTextSize(2);
        CenterText(c, label, SCREEN_SIZE_DIV_2 + 78, inBand ? fishing::ScaleColor(palette.good, gf) : palette.warn);
    } else if (haveW) {
        char wv[36];
        if (periodS > 0) snprintf(wv, sizeof(wv), "waves %.1f %s @ %.0fs", waveDisp, WaveUnit(), periodS);
        else             snprintf(wv, sizeof(wv), "waves %.1f %s", waveDisp, WaveUnit());
        c.setTextSize(2);
        CenterText(c, wv, SCREEN_SIZE_DIV_2 + 78, fg);
    }

    c.setTextSize(1);
    String footer = haveT ? String("source: ") + src : String();
    if (haveW) footer += (footer.length() ? "   waves: " : "waves: ") + String(wSrc);
    CenterText(c, footer, SCREEN_SIZE - 28, faint);
}

// --------------------------------------------------------------------------------- solunar
void FishingManager::DrawSolunar(BandCanvas& c)
{
    const float gf = GlowFactor();
    const uint32_t fg    = fishing::ScaleColor(palette.fg, gf);
    const uint32_t dim   = fishing::ScaleColor(palette.dim, gf);
    const uint32_t faint = fishing::ScaleColor(palette.faint, gf);

    c.setTextSize(2);
    CenterText(c, "SOLUNAR", 26, fg);

    // Day rating as filled dots (0..4).
    const int dotY = 58, gap = 18, n = 4;
    int x = SCREEN_SIZE_DIV_2 - (n - 1) * gap / 2;
    for (int i = 0; i < n; ++i) {
        const bool on = i < today.dayRating;
        c.fillCircle(x, dotY, on ? 5 : 3, on ? fishing::ScaleColor(palette.good, gf) : faint);
        x += gap;
    }

    // 24 h band with the day's windows.
    const int barX = 24, barW = SCREEN_SIZE - 48, barY = SCREEN_SIZE_DIV_2, barH = 22;
    c.drawRect(barX, barY, barW, barH, faint);

    const time_t now = time(nullptr);
    const long dayStart = (long)now - (((long)now + tzOffsetSec) % 86400); // UTC epoch of local 00:00
    auto xForEpoch = [&](long ep) {
        double f = (double)(ep - dayStart) / 86400.0;
        if (f < 0) f = 0;
        if (f > 1) f = 1;
        return barX + (int)lround(f * barW);
    };

    const fishing::SolunarWindow* nextW = nullptr;
    for (const fishing::SolunarWindow& w : today.windows) {
        const int x0 = xForEpoch(w.startEpoch), x1 = xForEpoch(w.endEpoch);
        const uint32_t col = fishing::WindowColor(palette, w.major);
        c.fillRect(x0, barY + (w.major ? 2 : 7), (x1 - x0 > 2 ? x1 - x0 : 2), (w.major ? barH - 4 : barH - 14),
                   fishing::ScaleColor(col, gf));
        if (!nextW && w.startEpoch > (long)now) nextW = &w;
    }
    // "now" marker.
    const int nx = xForEpoch((long)now);
    c.drawFastVLine(nx, barY - 6, barH + 12, fishing::ScaleColor(palette.accent, gf));

    // Next window countdown.
    String line = "no more windows today";
    if (nextW) {
        line = String(nextW->major ? "major" : "minor") + " in " +
               FormatCountdown(nextW->startEpoch - (long)now);
    }
    c.setTextSize(2);
    CenterText(c, line, barY + 44, fg);
    c.setTextSize(1);
    CenterText(c, "major = strong bite", SCREEN_SIZE - 30, dim);
}

// --------------------------------------------------------------------------------- weather
void FishingManager::DrawWeather(BandCanvas& c)
{
    const float gf = GlowFactor();
    const uint32_t fg    = fishing::ScaleColor(palette.fg, gf);
    const uint32_t dim   = fishing::ScaleColor(palette.dim, gf);
    const uint32_t faint = fishing::ScaleColor(palette.faint, gf);

    const uint32_t accent = fishing::ScaleColor(palette.accent, gf);
    const fishing::WeatherObs& w = feed.Weather();

    c.setTextSize(2);
    CenterText(c, "WEATHER", 26, fg);

    // Barometric pressure is the fishing-relevant headline (unit-aware: inHg vs hPa).
    if (!isnan(w.pressureHpa)) {
        char p[16];
        if (imperial) snprintf(p, sizeof(p), "%.2f", PressDisp(w.pressureHpa));
        else          snprintf(p, sizeof(p), "%.0f", PressDisp(w.pressureHpa));
        c.setTextSize(imperial ? 5 : 6);
        CenterText(c, p, 58, fg);
        c.setTextSize(1);
        CenterText(c, PressUnit(), 108, dim);
        c.setTextSize(3);
        c.setTextColor(fishing::TrendColor(palette, w.pressureTrend));
        c.drawString(fishing::TrendGlyph(w.pressureTrend), SCREEN_SIZE_DIV_2 + 78, 62);
    }

    // 24h barometer: rate (hPa/h) from ~6h ago + a "front coming" hint on a fall.
    const BaroTrend bt = ComputeBaro();
    int y = 132;
    if (bt.valid) {
        const bool fall = bt.rateHpaPerH <= -0.2f, rise = bt.rateHpaPerH >= 0.2f;
        const uint32_t tc = fall ? accent : (rise ? fg : dim); // falling glass = the good bite
        char r[44];
        const char* word = fall ? "FALLING" : (rise ? "RISING" : "STEADY");
        if (imperial) snprintf(r, sizeof(r), "%s  %+.2f inHg/h", word, bt.rateHpaPerH * 0.02953f);
        else          snprintf(r, sizeof(r), "%s  %+.1f hPa/h", word, bt.rateHpaPerH);
        c.setTextSize(1); CenterText(c, r, y, tc); y += 16;
        CenterText(c, fall ? "front coming - fish feeding" : (rise ? "bite may slow" : "stable"), y, dim); y += 18;

        // 24h sparkline of sea-level pressure.
        if (w.pressHist.size() >= 2) {
            float mn = w.pressHist[0].second, mx = mn;
            for (const auto& s : w.pressHist) { mn = min(mn, s.second); mx = max(mx, s.second); }
            if (mx - mn < 0.5f) mx = mn + 0.5f;
            const int L = 44, Rr = SCREEN_SIZE - 44, yb = y + 28, yt = y;
            const int n = (int)w.pressHist.size();
            int px = L, py = yb - (int)lround((w.pressHist[0].second - mn) / (mx - mn) * (yb - yt));
            for (int i = 1; i < n; ++i) {
                const int x = L + (Rr - L) * i / (n - 1);
                const int yy = yb - (int)lround((w.pressHist[i].second - mn) / (mx - mn) * (yb - yt));
                c.drawLine(px, py, x, yy, faint);
                px = x; py = yy;
            }
            y = yb + 6;
        }
    }

    // Wind (unit-aware) + air temp on one line each.
    char line[48];
    if (!isnan(w.windMph)) {
        snprintf(line, sizeof(line), "wind %.0f %s %s", w.windMph, WindUnit(),
                 w.windDirDeg >= 0 ? CompassPoint(w.windDirDeg) : "");
        c.setTextSize(1); CenterText(c, line, y, fg); y += 16;
    }
    String sub;
    if (!isnan(w.airTempF)) { char t[16]; snprintf(t, sizeof(t), "air %.0f%s", w.airTempF, TempUnit()); sub += t; }
    if (!isnan(w.precipIn) && w.precipIn > 0) { char t[24]; snprintf(t, sizeof(t), "  rain %.2f %s", w.precipIn, imperial ? "in" : "mm"); sub += t; }
    if (sub.length()) { c.setTextSize(1); CenterText(c, sub, y, dim); }

    // Swell footer: real buoy > modeled marine.
    float waveDisp = 0, periodS = 0; const char* wSrc = "";
    if (BestWaves(waveDisp, periodS, wSrc)) {
        char sw[44];
        if (periodS > 0) snprintf(sw, sizeof(sw), "swell %.1f %s @ %.0fs (%s)", waveDisp, WaveUnit(), periodS, wSrc);
        else             snprintf(sw, sizeof(sw), "swell %.1f %s (%s)", waveDisp, WaveUnit(), wSrc);
        c.setTextSize(1); CenterText(c, sw, SCREEN_SIZE - 22, faint);
    }
}

// --------------------------------------------------------------------------------- moon
void FishingManager::DrawMoon(BandCanvas& c)
{
    const float gf = GlowFactor();
    const uint32_t fg    = fishing::ScaleColor(palette.fg, gf);
    const uint32_t dim   = fishing::ScaleColor(palette.dim, gf);
    const uint32_t faint = fishing::ScaleColor(palette.faint, gf);

    c.setTextSize(2);
    CenterText(c, "MOON", 26, fg);

    const int cx = SCREEN_SIZE_DIV_2, cy = SCREEN_SIZE_DIV_2 - 10, r = 46;
    // Disc: faint outline, lit fraction as a filled circle scaled by illumination.
    c.drawCircle(cx, cy, r, faint);
    const uint8_t lit = (uint8_t)(60 + today.moonIllum * 195);
    c.fillCircle(cx, cy, (int)(r * (0.35f + 0.65f * today.moonIllum)),
                 fishing::ScaleColor(lgfx::color888(lit, lit, lit), gf));

    char pct[16];
    snprintf(pct, sizeof(pct), "%.0f%% lit", today.moonIllum * 100.0f);
    c.setTextSize(2);
    CenterText(c, pct, cy + r + 14, fg);
    c.setTextSize(1);
    CenterText(c, MoonPhaseName(today.moonPhase), cy + r + 40, dim);

    if (today.moonriseEpoch > 0 || today.moonsetEpoch > 0) {
        char line[48];
        snprintf(line, sizeof(line), "rise %s   set %s",
                 today.moonriseEpoch > 0 ? FormatClock(today.moonriseEpoch, tzOffsetSec).c_str() : "--:--",
                 today.moonsetEpoch  > 0 ? FormatClock(today.moonsetEpoch,  tzOffsetSec).c_str() : "--:--");
        c.setTextSize(1);
        CenterText(c, line, SCREEN_SIZE - 30, faint);
    }
}

// --------------------------------------------------------------------------------- catch log
void FishingManager::DrawCatchLog(BandCanvas& c)
{
    const float gf = GlowFactor();
    const uint32_t fg     = fishing::ScaleColor(palette.fg, gf);
    const uint32_t dim    = fishing::ScaleColor(palette.dim, gf);
    const uint32_t faint  = fishing::ScaleColor(palette.faint, gf);
    const uint32_t accent = fishing::ScaleColor(palette.accent, gf);
    const uint32_t good   = fishing::ScaleColor(palette.good, gf);

    const time_t now = time(nullptr);
    c.setTextSize(2);
    CenterText(c, "CATCH LOG", 26, fg);

    if (!logbook.Any()) {
        c.setTextSize(2); CenterText(c, "no catches yet", SCREEN_SIZE_DIV_2 - 20, dim);
        c.setTextSize(1); CenterText(c, "tap to log your first catch", SCREEN_SIZE_DIV_2 + 14, accent);
        return;
    }

    c.setTextSize(1); CenterText(c, "TOTAL", 58, dim);
    char big[8]; snprintf(big, sizeof(big), "%u", (unsigned)logbook.Total());
    c.setTextSize(7); CenterText(c, big, 76, fg);

    char line[48];
    snprintf(line, sizeof(line), "today %u   best day %u", (unsigned)logbook.TodayCount(now), (unsigned)logbook.Best());
    c.setTextSize(1); CenterText(c, line, SCREEN_SIZE_DIV_2 + 34, dim);
    snprintf(line, sizeof(line), "%d%% during bite windows", logbook.BitePercent());
    CenterText(c, line, SCREEN_SIZE_DIV_2 + 52, good);
    snprintf(line, sizeof(line), "streak %u d   last %s", (unsigned)logbook.CurrentStreak(now),
             logbook.LastCatch() > 0 ? FormatClock(logbook.LastCatch(), tzOffsetSec).c_str() : "--:--");
    CenterText(c, line, SCREEN_SIZE_DIV_2 + 70, faint);

    c.setTextSize(1);
    CenterText(c, "tap to log a catch", SCREEN_SIZE - 28, dim);
}

// --------------------------------------------------------------------------------- detail card
void FishingManager::DrawDetailCard(BandCanvas& c)
{
    const float gf = GlowFactor();
    const uint32_t fg    = fishing::ScaleColor(palette.fg, gf);
    const uint32_t dim   = fishing::ScaleColor(palette.dim, gf);
    const uint32_t faint = fishing::ScaleColor(palette.faint, gf);
    const uint32_t accent= fishing::ScaleColor(palette.accent, gf);

    const int m = 18;
    c.fillRoundRect(m, m, SCREEN_SIZE - 2 * m, SCREEN_SIZE - 2 * m, 16, palette.bg);
    c.drawRoundRect(m, m, SCREEN_SIZE - 2 * m, SCREEN_SIZE - 2 * m, 16, accent);

    int y = 48;
    auto row = [&](const String& s, uint32_t col) { c.setTextSize(2); CenterText(c, s, y, col); y += 30; };

    const time_t now = time(nullptr);
    char buf[64];

    switch (detailFor) {
        case Screen::Tide: {
            c.setTextSize(2); CenterText(c, "TIDE SCHEDULE", y, fg); y += 40;
            int shown = 0;
            for (const fishing::TideEvent& e : feed.Tide().events) {
                if ((long)e.timeEpoch < (long)now) continue;
                snprintf(buf, sizeof(buf), "%s  %s  %.1f %s", e.type == 'H' ? "HIGH" : "LOW ",
                         FormatClock(e.timeEpoch, tzOffsetSec).c_str(), e.heightFt, WaveUnit());
                row(buf, e.type == 'H' ? accent : dim);
                if (++shown >= 5) break;
            }
            if (!shown) row("no upcoming events", dim);
            break;
        }
        case Screen::Flow: {
            const fishing::RiverGauge& g = feed.Flow();
            c.setTextSize(1); CenterText(c, FitWidth(c, g.siteName.length() ? g.siteName : ("site " + g.siteId), SCREEN_SIZE - 60), y, dim); y += 30;
            if (!isnan(g.dischargeCfs)) { snprintf(buf, sizeof(buf), "%.0f CFS  %s", g.dischargeCfs, fishing::TrendGlyph(g.flowTrend)); row(buf, fg); }
            if (!isnan(g.gaugeHeightFt)) { snprintf(buf, sizeof(buf), "gauge %.2f ft", g.gaugeHeightFt); row(buf, fg); }
            if (!isnan(g.waterTempF))   { snprintf(buf, sizeof(buf), "water %.0f%s", imperial ? g.waterTempF : (g.waterTempF - 32.0f) / 1.8f, TempUnit()); row(buf, fg); }
            if (!isnan(g.turbidityFnu)) { snprintf(buf, sizeof(buf), "turbidity %.0f FNU", g.turbidityFnu); row(buf, fg); }
            snprintf(buf, sizeof(buf), "updated %s ago", FormatAgo(g.timeEpoch).c_str()); row(buf, faint);
            break;
        }
        case Screen::Weather: {
            const fishing::WeatherObs& w = feed.Weather();
            if (!isnan(w.pressureHpa)) {
                if (imperial) snprintf(buf, sizeof(buf), "pressure %.2f inHg %s", PressDisp(w.pressureHpa), fishing::TrendGlyph(w.pressureTrend));
                else          snprintf(buf, sizeof(buf), "pressure %.0f hPa %s", w.pressureHpa, fishing::TrendGlyph(w.pressureTrend));
                row(buf, fg);
            }
            const BaroTrend bt = ComputeBaro();
            if (bt.valid) {
                if (imperial) snprintf(buf, sizeof(buf), "24h %+.2f inHg/h", bt.rateHpaPerH * 0.02953f);
                else          snprintf(buf, sizeof(buf), "24h %+.1f hPa/h", bt.rateHpaPerH);
                row(buf, dim);
            }
            if (!isnan(w.windMph))  { snprintf(buf, sizeof(buf), "wind %.0f %s %s", w.windMph, WindUnit(), w.windDirDeg >= 0 ? CompassPoint(w.windDirDeg) : ""); row(buf, fg); }
            if (!isnan(w.airTempF)) { snprintf(buf, sizeof(buf), "air %.0f%s", w.airTempF, TempUnit()); row(buf, fg); }
            if (!isnan(w.precipIn)) { snprintf(buf, sizeof(buf), "precip %.2f %s", w.precipIn, imperial ? "in" : "mm"); row(buf, fg); }
            break;
        }
        default: {
            // Temp / Solunar / Moon: a compact readout.
            float tF; const char* src;
            if (detailFor == Screen::Temp && BestWaterTemp(tF, src)) {
                snprintf(buf, sizeof(buf), "%.0f%s", tF, TempUnit()); row(buf, fg);
                row(String("source: ") + src, dim);
            } else if (detailFor == Screen::Solunar && today.valid) {
                snprintf(buf, sizeof(buf), "day rating %d/4", today.dayRating); row(buf, fg);
                for (const fishing::SolunarWindow& w : today.windows) {
                    if (w.endEpoch < (long)now) continue;
                    snprintf(buf, sizeof(buf), "%s %s-%s", w.major ? "MAJ" : "min",
                             FormatClock(w.startEpoch, tzOffsetSec).c_str(),
                             FormatClock(w.endEpoch, tzOffsetSec).c_str());
                    row(buf, w.major ? accent : dim);
                }
            } else if (detailFor == Screen::Moon && today.valid) {
                snprintf(buf, sizeof(buf), "%.0f%% %s", today.moonIllum * 100.0f, MoonPhaseName(today.moonPhase)); row(buf, fg);
            }
            break;
        }
    }

    c.setTextSize(1);
    CenterText(c, "tap to close", SCREEN_SIZE - 40, faint);
}

// --------------------------------------------------------------------------------- chrome
void FishingManager::DrawScreenDots(BandCanvas& c, const std::vector<Screen>& rot) const
{
    const int n = (int)rot.size();
    if (n <= 1) return;
    const int gap = 10;
    const int totalW = (n - 1) * gap;
    int x = SCREEN_SIZE_DIV_2 - totalW / 2;
    const int y = SCREEN_SIZE - 12;
    for (int i = 0; i < n; ++i) {
        const bool active = (rot[i] == current);
        c.fillCircle(x, y, active ? 2 : 1, active ? palette.fg : palette.faint);
        x += gap;
    }
}
