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
    const fishing::TideEvent* next = nullptr;
    for (const fishing::TideEvent& e : feed.Tide().events)
        if ((long)now < e.timeEpoch) { next = &e; break; }

    if (!next) {
        c.setTextSize(2);
        CenterText(c, "no upcoming tide", SCREEN_SIZE_DIV_2, dim);
        return;
    }

    // If the next event is a HIGH we're rising toward it; a LOW means falling.
    const bool rising = (next->type == 'H');
    c.setTextSize(3);
    CenterText(c, rising ? "RISING" : "FALLING", SCREEN_SIZE_DIV_2 - 46, rising ? accent : palette.warn);

    char line[48];
    snprintf(line, sizeof(line), "next %s in %s", next->type == 'H' ? "high" : "low",
             FormatCountdown((long)next->timeEpoch - (long)now).c_str());
    c.setTextSize(2);
    CenterText(c, line, SCREEN_SIZE_DIV_2 + 4, fg);

    char meta[48];
    snprintf(meta, sizeof(meta), "%.1f ft  @ %s", next->heightFt, FormatClock(next->timeEpoch, tzOffsetSec).c_str());
    c.setTextSize(1);
    CenterText(c, meta, SCREEN_SIZE_DIV_2 + 40, dim);

    c.setTextSize(1);
    CenterText(c, "tap for schedule", SCREEN_SIZE - 30, faint);
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
    if (!BestWaterTempF(tF, src)) {
        c.setTextSize(2);
        CenterText(c, "no reading", SCREEN_SIZE_DIV_2, dim);
        return;
    }

    char big[12];
    snprintf(big, sizeof(big), "%.0f", tF);
    c.setTextSize(8);
    CenterText(c, big, SCREEN_SIZE_DIV_2 - 40, fg);
    c.setTextSize(2);
    CenterText(c, "\xF7""F", SCREEN_SIZE_DIV_2 + 44, dim); // degree-ish glyph + F

    // Active-feeding band indicator.
    if (!isnan(tempLoF) || !isnan(tempHiF)) {
        const bool inBand = (isnan(tempLoF) || tF >= tempLoF) && (isnan(tempHiF) || tF <= tempHiF);
        const char* label = inBand ? "IN THE STRIKE ZONE" : (!isnan(tempLoF) && tF < tempLoF ? "below band" : "above band");
        c.setTextSize(2);
        CenterText(c, label, SCREEN_SIZE_DIV_2 + 78, inBand ? fishing::ScaleColor(palette.good, gf) : palette.warn);
    }

    c.setTextSize(1);
    CenterText(c, String("source: ") + src, SCREEN_SIZE - 28, faint);
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

    const fishing::WeatherObs& w = feed.Weather();

    c.setTextSize(2);
    CenterText(c, "WEATHER", 26, fg);

    // Barometric pressure + trend is the fishing-relevant headline.
    if (!isnan(w.pressureHpa)) {
        char p[16]; snprintf(p, sizeof(p), "%.0f", w.pressureHpa);
        c.setTextSize(6);
        CenterText(c, p, SCREEN_SIZE_DIV_2 - 46, fg);
        c.setTextSize(1);
        CenterText(c, "hPa", SCREEN_SIZE_DIV_2 + 12, dim);
        c.setTextSize(3);
        c.setTextColor(fishing::TrendColor(palette, w.pressureTrend));
        c.drawString(fishing::TrendGlyph(w.pressureTrend), SCREEN_SIZE_DIV_2 + 70, SCREEN_SIZE_DIV_2 - 36);
    }

    int y = SCREEN_SIZE_DIV_2 + 30;
    char line[48];
    if (!isnan(w.windMph)) {
        snprintf(line, sizeof(line), "wind %.0f mph %s", w.windMph,
                 w.windDirDeg >= 0 ? CompassPoint(w.windDirDeg) : "");
        c.setTextSize(2); CenterText(c, line, y, fg); y += 26;
    }
    String sub;
    if (!isnan(w.airTempF)) { char t[16]; snprintf(t, sizeof(t), "air %.0fF", w.airTempF); sub += t; }
    if (!isnan(w.precipIn) && w.precipIn > 0) { char t[20]; snprintf(t, sizeof(t), "  rain %.2f in", w.precipIn); sub += t; }
    if (sub.length()) { c.setTextSize(1); CenterText(c, sub, y, dim); y += 20; }

    // Offshore swell, when a buoy is configured.
    const fishing::BuoyObs& b = feed.Buoy();
    if (b.valid && !isnan(b.waveHeightFt)) {
        char sw[40];
        if (!isnan(b.dominantPeriodS)) snprintf(sw, sizeof(sw), "swell %.1f ft @ %.0fs", b.waveHeightFt, b.dominantPeriodS);
        else                           snprintf(sw, sizeof(sw), "swell %.1f ft", b.waveHeightFt);
        c.setTextSize(1); CenterText(c, sw, SCREEN_SIZE - 28, faint);
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
                snprintf(buf, sizeof(buf), "%s  %s  %.1f ft", e.type == 'H' ? "HIGH" : "LOW ",
                         FormatClock(e.timeEpoch, tzOffsetSec).c_str(), e.heightFt);
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
            if (!isnan(g.waterTempF))   { snprintf(buf, sizeof(buf), "water %.0f F", g.waterTempF); row(buf, fg); }
            if (!isnan(g.turbidityFnu)) { snprintf(buf, sizeof(buf), "turbidity %.0f FNU", g.turbidityFnu); row(buf, fg); }
            snprintf(buf, sizeof(buf), "updated %s ago", FormatAgo(g.timeEpoch).c_str()); row(buf, faint);
            break;
        }
        case Screen::Weather: {
            const fishing::WeatherObs& w = feed.Weather();
            if (!isnan(w.pressureHpa)) { snprintf(buf, sizeof(buf), "pressure %.0f hPa %s", w.pressureHpa, fishing::TrendGlyph(w.pressureTrend)); row(buf, fg); }
            if (!isnan(w.windMph))     { snprintf(buf, sizeof(buf), "wind %.0f mph %s", w.windMph, w.windDirDeg >= 0 ? CompassPoint(w.windDirDeg) : ""); row(buf, fg); }
            if (!isnan(w.airTempF))    { snprintf(buf, sizeof(buf), "air %.0f F", w.airTempF); row(buf, fg); }
            if (!isnan(w.precipIn))    { snprintf(buf, sizeof(buf), "precip %.2f in", w.precipIn); row(buf, fg); }
            break;
        }
        default: {
            // Temp / Solunar / Moon: a compact readout.
            float tF; const char* src;
            if (detailFor == Screen::Temp && BestWaterTempF(tF, src)) {
                snprintf(buf, sizeof(buf), "%.0f F", tF); row(buf, fg);
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
