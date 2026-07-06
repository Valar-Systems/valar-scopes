#include "AnglerManager.h"

#include <math.h>
#include <stdio.h>
#include <time.h>

#include "Layout.h"
#include "astro/Astro.h"

// The Angler edition screens. Each draws one full frame into the band canvas in absolute screen
// coordinates (the S3 renders a single full-height band). Colours come from the palette scaled by
// GlowFactor(); the warm gold accent marks the sun and an active/major bite window.

namespace {

constexpr long NTP_FLOOR = 1600000000;

String FitWidth(BandCanvas& c, String s, int maxW)
{
    if (c.textWidth(s) <= maxW) return s;
    while (s.length() > 1 && c.textWidth(s + "...") > maxW) s.remove(s.length() - 1);
    return s + "...";
}

// "H:MM" for >= 1h, else "Mm". Used for bite-window / tide countdowns.
String FormatDur(long secs)
{
    if (secs < 0) secs = 0;
    const int h = secs / 3600, m = (secs % 3600) / 60;
    char b[16];
    if (h > 0) snprintf(b, sizeof(b), "%d:%02d", h, m);
    else       snprintf(b, sizeof(b), "%dm", m);
    return String(b);
}

// A short label for a WMO weather code (Open-Meteo's weather_code).
const char* WmoText(int c)
{
    switch (c) {
        case 0:  return "Clear";
        case 1:  return "Mainly clear";
        case 2:  return "Partly cloudy";
        case 3:  return "Overcast";
        case 45: case 48: return "Fog";
        case 51: case 53: case 55: return "Drizzle";
        case 56: case 57: return "Freezing drizzle";
        case 61: case 63: case 65: return "Rain";
        case 66: case 67: return "Freezing rain";
        case 71: case 73: case 75: case 77: return "Snow";
        case 80: case 81: case 82: return "Showers";
        case 85: case 86: return "Snow showers";
        case 95: return "Thunderstorm";
        case 96: case 99: return "Thunderstorm, hail";
        default: return "--";
    }
}

} // namespace

// --------------------------------------------------------------------------------- bite forecast (hero)
void AnglerManager::DrawBite(BandCanvas& c)
{
    const float gf = GlowFactor();
    const uint32_t fg     = angler::ScaleColor(palette.fg, gf);
    const uint32_t dim    = angler::ScaleColor(palette.dim, gf);
    const uint32_t faint  = angler::ScaleColor(palette.faint, gf);
    const uint32_t accent = angler::ScaleColor(palette.accent, gf);

    const int cx = SCREEN_SIZE_DIV_2, cy = SCREEN_SIZE_DIV_2;
    const int R = BiteRingRadius();
    const time_t now = time(nullptr);

    c.setTextSize(1);
    CenterText(c, "BITE FORECAST", 16, dim);

    // Daytime arc (sunrise -> sunset), a hair brighter, so the lit part of the 24h dial reads.
    if (today.sunrise && today.sunset && today.sunset > today.sunrise) {
        for (time_t t = today.sunrise; t <= today.sunset; t += 300) {
            const auto p = RingXY(LocalSod(t), R);
            c.fillCircle(p.first, p.second, 1, faint);
        }
    }
    // Base ring + quarter ticks with tiny hour labels.
    c.drawCircle(cx, cy, R, faint);
    for (int q = 0; q < 4; ++q) {
        const auto o = RingXY((long)q * 21600, R + 7);
        const auto i = RingXY((long)q * 21600, R - 7);
        c.drawLine(i.first, i.second, o.first, o.second, dim);
    }
    c.setTextColor(faint);
    { const auto p = RingXY(0, R + 18);      c.drawString("12a", p.first - c.textWidth("12a") / 2, p.second - 4); }
    { const auto p = RingXY(21600, R + 22);  c.drawString("6a",  p.first - c.textWidth("6a") / 2,  p.second - 4); }
    { const auto p = RingXY(43200, R + 18);  c.drawString("12p", p.first - c.textWidth("12p") / 2, p.second - 4); }
    { const auto p = RingXY(64800, R + 22);  c.drawString("6p",  p.first - c.textWidth("6p") / 2,  p.second - 4); }

    // Sunrise / sunset markers.
    if (today.sunrise) { const auto p = RingXY(LocalSod(today.sunrise), R); c.fillCircle(p.first, p.second, 3, accent); }
    if (today.sunset)  { const auto p = RingXY(LocalSod(today.sunset),  R); c.fillCircle(p.first, p.second, 3, accent); }

    // Period arcs: majors thick gold, minors thinner teal. Drawn along the ring from start to end.
    for (int i = 0; i < today.count; ++i) {
        const angler::Period& p = today.periods[i];
        const bool major = p.major();
        const uint32_t col = major ? accent : fg;
        for (time_t t = p.start; t <= p.end; t += 180) {
            const auto q = RingXY(LocalSod(t), R);
            c.fillCircle(q.first, q.second, major ? 4 : 3, col);
        }
        const auto ctr = PeriodMarkerXY(p);           // tap target marker
        c.fillCircle(ctr.first, ctr.second, major ? 3 : 2, palette.bg);
        c.drawCircle(ctr.first, ctr.second, major ? 6 : 4, col);
    }

    // "Now" hand.
    if (now > NTP_FLOOR) {
        const auto p = RingXY(LocalSod(now), R - 12);
        c.drawLine(cx, cy, p.first, p.second, accent);
        c.fillCircle(p.first, p.second, 3, accent);
    }
    c.fillCircle(cx, cy, 3, dim);

    // Centre hub: the headline countdown + the day rating.
    angler::Period act, nxt;
    const bool active = (now > NTP_FLOOR) && ActiveNow(now, act);
    const bool haveNext = (now > NTP_FLOOR) && NextPeriod(now, nxt, false);

    c.setTextSize(1);
    CenterText(c, active ? "FEEDING NOW" : "NEXT BITE", cy - 54, dim);
    c.setTextSize(3);
    if (active)        CenterText(c, FormatDur(act.end - now) + " left", cy - 40, accent);
    else if (haveNext) CenterText(c, "in " + FormatDur(nxt.start - now), cy - 40, fg);
    else               CenterText(c, "--", cy - 40, dim);

    c.setTextSize(1);
    const angler::Period& hub = active ? act : nxt;
    if (active || haveNext) {
        String k = String(angler::PeriodShort(hub.kind)) + " - " + angler::PeriodLabel(hub.kind) +
                   " " + LocalHM(hub.center);
        CenterText(c, k, cy - 2, dim);
    }

    // Rating stars + label.
    const int stars = angler::RatingStars(today.rating);
    const int sx0 = cx - (4 * 16) / 2 + 8;
    for (int i = 0; i < 4; ++i) {
        const bool on = i < stars;
        c.fillCircle(sx0 + i * 16, cy + 26, 4, on ? accent : palette.bg);
        c.drawCircle(sx0 + i * 16, cy + 26, 4, on ? accent : faint);
    }
    c.setTextColor(dim);
    CenterText(c, String(angler::RatingLabel(today.rating)) + " day", cy + 44, dim);
}

// --------------------------------------------------------------------------------- moon
void AnglerManager::DrawMoon(BandCanvas& c)
{
    const float gf = GlowFactor();
    const uint32_t fg    = angler::ScaleColor(palette.fg, gf);
    const uint32_t dim   = angler::ScaleColor(palette.dim, gf);
    const uint32_t faint = angler::ScaleColor(palette.faint, gf);
    const uint32_t lit   = angler::ScaleColor(lgfx::color888(226, 232, 214), gf);

    const int cx = SCREEN_SIZE_DIV_2;
    const time_t now = time(nullptr);

    c.setTextSize(1);
    CenterText(c, "MOON", 20, dim);

    double ra, dec, f = today.moonIllum;
    bool waxing = true;
    if (now > NTP_FLOOR) {
        double il0, il1, r2, d2;
        space::astro::MoonRaDec(now, ra, dec, il0);
        space::astro::MoonRaDec(now + 3600, r2, d2, il1);
        f = il0;
        waxing = il1 >= il0;
    }

    // Phase disk: dark globe, then paint the lit fraction. Terminator x per scanline = xw*(1-2f).
    const int mcx = cx, mcy = 132, rr = 74;
    c.fillCircle(mcx, mcy, rr, angler::ScaleColor(palette.faint, gf * 0.7f));
    for (int dy = -rr; dy <= rr; ++dy) {
        const double xw = sqrt((double)rr * rr - (double)dy * dy);
        const double xt = xw * (1.0 - 2.0 * f);
        int x1, x2;
        if (waxing) { x1 = mcx + (int)lround(xt); x2 = mcx + (int)lround(xw); }
        else        { x1 = mcx - (int)lround(xw); x2 = mcx - (int)lround(xt); }
        if (x2 > x1) c.drawFastHLine(x1, mcy + dy, x2 - x1, lit);
    }
    c.drawCircle(mcx, mcy, rr, faint);

    c.setTextSize(2);
    CenterText(c, angler::MoonPhaseName(f, waxing), 226, fg);
    char pct[16];
    snprintf(pct, sizeof(pct), "%.0f%% lit", f * 100.0);
    c.setTextSize(1);
    CenterText(c, pct, 252, dim);

    // Rise / transit / set for the day.
    int y = 286;
    auto row = [&](const char* lbl, time_t t) {
        String s = String(lbl) + "  " + LocalHM(t);
        CenterText(c, s, y, t ? fg : faint);
        y += 22;
    };
    row("Rise  ", today.moonrise);
    row("High  ", today.moonTransit);
    row("Set   ", today.moonset);
}

// --------------------------------------------------------------------------------- sun
void AnglerManager::DrawSun(BandCanvas& c)
{
    const float gf = GlowFactor();
    const uint32_t fg     = angler::ScaleColor(palette.fg, gf);
    const uint32_t dim    = angler::ScaleColor(palette.dim, gf);
    const uint32_t faint  = angler::ScaleColor(palette.faint, gf);
    const uint32_t accent = angler::ScaleColor(palette.accent, gf);

    const int cx = SCREEN_SIZE_DIV_2;
    const time_t now = time(nullptr);

    c.setTextSize(1);
    CenterText(c, "SUN", 20, dim);

    const int left = 60, right = SCREEN_SIZE - 60, horizon = SCREEN_SIZE_DIV_2 + 10;
    const int width = right - left, arcH = 96;
    c.drawFastHLine(left - 10, horizon, width + 20, faint);

    // Day arc (sunrise -> sunset) as a dotted dome above the horizon.
    for (int i = 0; i <= 60; ++i) {
        const double u = i / 60.0;
        const int x = left + (int)lround(u * width);
        const int yy = horizon - (int)lround(sin(u * M_PI) * arcH);
        c.fillCircle(x, yy, 1, faint);
    }

    // Sun marker: fraction of daylight elapsed along the arc; below the horizon at night.
    if (today.sunrise && today.sunset && today.sunset > today.sunrise && now > NTP_FLOOR) {
        double u = (double)(now - today.sunrise) / (double)(today.sunset - today.sunrise);
        if (u >= 0.0 && u <= 1.0) {
            const int x = left + (int)lround(u * width);
            const int yy = horizon - (int)lround(sin(u * M_PI) * arcH);
            c.fillCircle(x, yy, 8, accent);
        } else {
            const int x = (u < 0.0) ? left : right;
            c.fillCircle(x, horizon + 16, 6, dim);
        }
    }

    c.setTextSize(2);
    c.setTextColor(accent);
    c.drawString(LocalHM(today.sunrise), left - 14, horizon + 16);
    { const String s = LocalHM(today.sunset); c.drawString(s, right + 14 - c.textWidth(s), horizon + 16); }
    c.setTextSize(1);
    c.setTextColor(dim);
    c.drawString("rise", left - 14, horizon + 40);
    c.drawString("set", right + 14 - c.textWidth("set"), horizon + 40);

    if (today.sunrise && today.sunset && today.sunset > today.sunrise) {
        const long dl = today.sunset - today.sunrise;
        char b[24];
        snprintf(b, sizeof(b), "%ldh %02ldm daylight", dl / 3600, (dl % 3600) / 60);
        c.setTextSize(1);
        CenterText(c, b, SCREEN_SIZE - 52, fg);
        CenterText(c, "golden hour near rise & set", SCREEN_SIZE - 32, faint);
    }
}

// --------------------------------------------------------------------------------- tides
void AnglerManager::DrawTides(BandCanvas& c)
{
    const float gf = GlowFactor();
    const uint32_t fg     = angler::ScaleColor(palette.fg, gf);
    const uint32_t dim    = angler::ScaleColor(palette.dim, gf);
    const uint32_t faint  = angler::ScaleColor(palette.faint, gf);
    const uint32_t accent = angler::ScaleColor(palette.accent, gf);

    const angler::TideData& td = feed.Tide();
    const time_t now = time(nullptr);

    c.setTextSize(1);
    CenterText(c, "TIDES", 18, dim);

    if (td.events.empty()) { c.setTextSize(2); CenterText(c, "no tide data", SCREEN_SIZE_DIV_2, dim); return; }

    // Headline: countdown to the next extreme + its height.
    const angler::TideEvent* next = nullptr;
    for (const angler::TideEvent& e : td.events) if (e.t > now) { next = &e; break; }
    if (next) {
        c.setTextSize(1);
        CenterText(c, next->high ? "NEXT HIGH" : "NEXT LOW", 42, dim);
        c.setTextSize(3);
        CenterText(c, "in " + FormatDur(next->t - now), 60, next->high ? accent : fg);
        char h[28];
        snprintf(h, sizeof(h), "%.1f %s  at %s", next->height, WaveUnit(), LocalHM(next->t).c_str());
        c.setTextSize(1);
        CenterText(c, h, 98, dim);
    }

    // Tide curve: prefer the real 6-minute prediction curve; else cosine-interpolate the hi/lo events.
    const std::vector<std::pair<time_t, float>>& curve = feed.TideCurve();
    const bool haveCurve = !curve.empty();
    const int L = 34, R = SCREEN_SIZE - 34, yTop = 130, yBot = 250;

    time_t t0, t1;
    float minH, maxH;
    if (haveCurve) {
        t0 = curve.front().first; t1 = curve.back().first;
        minH = curve.front().second; maxH = minH;
        for (const auto& p : curve) { minH = min(minH, p.second); maxH = max(maxH, p.second); }
    } else {
        t0 = td.events.front().t; t1 = td.events.back().t;
        minH = td.events.front().height; maxH = minH;
        for (const angler::TideEvent& e : td.events) { minH = min(minH, e.height); maxH = max(maxH, e.height); }
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
            auto hAt = [&](time_t t) -> float {
                if (t <= td.events.front().t) return td.events.front().height;
                for (size_t i = 0; i + 1 < td.events.size(); ++i) {
                    const angler::TideEvent& a = td.events[i];
                    const angler::TideEvent& b = td.events[i + 1];
                    if (t >= a.t && t <= b.t) {
                        const float ph = (float)(t - a.t) / (float)(b.t - a.t);
                        return a.height + (b.height - a.height) * (1.0f - cosf(ph * (float)M_PI)) * 0.5f;
                    }
                }
                return td.events.back().height;
            };
            int px = L, py = yOf(hAt(t0));
            for (int x = L + 3; x <= R; x += 3) {
                const time_t t = t0 + (time_t)((long)(t1 - t0) * (x - L) / (R - L));
                const int y = yOf(hAt(t));
                c.drawLine(px, py, x, y, fg);
                px = x; py = y;
            }
        }
        // hi/lo markers (from the extremes feed) + "now"
        for (const angler::TideEvent& e : td.events) {
            if (e.t < t0 || e.t > t1) continue;
            const int x = L + (int)((long)(R - L) * (e.t - t0) / (t1 - t0));
            c.fillCircle(x, yOf(e.height), 3, e.high ? accent : dim);
        }
        if (now >= t0 && now <= t1) {
            const int x = L + (int)((long)(R - L) * (now - t0) / (t1 - t0));
            c.drawFastVLine(x, yTop - 6, (yBot - yTop) + 12, faint);
        }
    }

    if (feed.HaveWaterTemp()) {
        char w[24];
        snprintf(w, sizeof(w), "Water %.0f%s", feed.WaterTemp(), TempUnit());
        c.setTextSize(2);
        CenterText(c, w, SCREEN_SIZE - 52, accent);
    }
}

// --------------------------------------------------------------------------------- barometer
void AnglerManager::DrawBarometer(BandCanvas& c)
{
    const float gf = GlowFactor();
    const uint32_t fg     = angler::ScaleColor(palette.fg, gf);
    const uint32_t dim    = angler::ScaleColor(palette.dim, gf);
    const uint32_t faint  = angler::ScaleColor(palette.faint, gf);
    const uint32_t accent = angler::ScaleColor(palette.accent, gf);

    const angler::WeatherData& w = feed.Weather();
    const int cx = SCREEN_SIZE_DIV_2, cy = SCREEN_SIZE_DIV_2;

    c.setTextSize(1);
    CenterText(c, "BAROMETER", 18, dim);
    if (!w.valid) { c.setTextSize(2); CenterText(c, "no data", cy, dim); return; }

    char b[16];
    if (imperial) snprintf(b, sizeof(b), "%.2f", PressDisp(w.pressureHpa));
    else          snprintf(b, sizeof(b), "%.0f", PressDisp(w.pressureHpa));
    c.setTextSize(6); CenterText(c, b, cy - 74, fg);
    c.setTextSize(2); CenterText(c, PressUnit(), cy - 20, dim);

    const BaroTrend bt = ComputeBaro();
    if (bt.valid) {
        const bool fall = bt.rateHpaPerH <= -0.2f, rise = bt.rateHpaPerH >= 0.2f;
        const uint32_t tc = fall ? accent : rise ? fg : dim;   // falling glass = gold (the good bite)
        const int ax = cx, ay = cy + 22;
        if (fall)      c.fillTriangle(ax - 9, ay - 9, ax + 9, ay - 9, ax, ay + 9, tc);
        else if (rise) c.fillTriangle(ax - 9, ay + 9, ax + 9, ay + 9, ax, ay - 9, tc);
        else           c.fillRect(ax - 9, ay - 2, 18, 4, tc);

        char r[40];
        const char* word = fall ? "FALLING" : rise ? "RISING" : "STEADY";
        if (imperial) snprintf(r, sizeof(r), "%s  %+.2f inHg/h", word, bt.rateHpaPerH * 0.02953f);
        else          snprintf(r, sizeof(r), "%s  %+.1f hPa/h", word, bt.rateHpaPerH);
        c.setTextSize(1); CenterText(c, r, cy + 42, tc);
        CenterText(c, fall ? "fish feeding ahead of a front" : rise ? "bite may slow" : "stable",
                   cy + 60, dim);

        // 24h sparkline of sea-level pressure.
        if (w.pressHist.size() >= 2) {
            float mn = w.pressHist[0].second, mx = mn;
            for (const auto& s : w.pressHist) { mn = min(mn, s.second); mx = max(mx, s.second); }
            if (mx - mn < 0.5f) mx = mn + 0.5f;
            const int L = 40, Rr = SCREEN_SIZE - 40, yb = SCREEN_SIZE - 34, yt = SCREEN_SIZE - 70;
            const int n = (int)w.pressHist.size();
            int px = L, py = yb - (int)lround((w.pressHist[0].second - mn) / (mx - mn) * (yb - yt));
            for (int i = 1; i < n; ++i) {
                const int x = L + (Rr - L) * i / (n - 1);
                const int y = yb - (int)lround((w.pressHist[i].second - mn) / (mx - mn) * (yb - yt));
                c.drawLine(px, py, x, y, faint);
                px = x; py = y;
            }
        }
    }
}

// --------------------------------------------------------------------------------- wind
void AnglerManager::DrawWind(BandCanvas& c)
{
    const float gf = GlowFactor();
    const uint32_t fg     = angler::ScaleColor(palette.fg, gf);
    const uint32_t dim    = angler::ScaleColor(palette.dim, gf);
    const uint32_t faint  = angler::ScaleColor(palette.faint, gf);
    const uint32_t accent = angler::ScaleColor(palette.accent, gf);

    const angler::WeatherData& w = feed.Weather();
    const int cx = SCREEN_SIZE_DIV_2, cy = SCREEN_SIZE_DIV_2 - 6;

    c.setTextSize(1);
    CenterText(c, "WIND", 18, dim);
    if (!w.valid) { c.setTextSize(2); CenterText(c, "no data", SCREEN_SIZE_DIV_2, dim); return; }

    const int Rr = SCREEN_SIZE_DIV_2 - 70;
    c.drawCircle(cx, cy, Rr, faint);
    c.setTextColor(faint);
    CenterText(c, "N", cy - Rr - 12, faint);
    c.drawString("E", cx + Rr + 4, cy - 4);
    c.drawString("W", cx - Rr - 12, cy - 4);
    CenterText(c, "S", cy + Rr + 4, faint);

    // Arrow flies WITH the wind: tail at the FROM bearing, head at the opposite side.
    const double af = w.windDir * M_PI / 180.0;
    const double at = (w.windDir + 180) * M_PI / 180.0;
    const int fx = cx + (int)lround(Rr * sin(af)), fy = cy - (int)lround(Rr * cos(af));
    const int hx = cx + (int)lround(Rr * sin(at)), hy = cy - (int)lround(Rr * cos(at));
    c.drawLine(fx, fy, hx, hy, accent);
    double dx = hx - fx, dy = hy - fy; const double len = sqrt(dx * dx + dy * dy);
    if (len > 1) {
        dx /= len; dy /= len;
        const double pxv = -dy, pyv = dx;
        c.fillTriangle(hx, hy,
                       hx - (int)(14 * dx + 7 * pxv), hy - (int)(14 * dy + 7 * pyv),
                       hx - (int)(14 * dx - 7 * pxv), hy - (int)(14 * dy - 7 * pyv), accent);
    }

    c.setTextSize(5); CenterText(c, String((int)lround(w.windSpeed)), cy - 22, fg);
    c.setTextSize(1); CenterText(c, WindUnit(), cy + 20, dim);

    char foot[40];
    snprintf(foot, sizeof(foot), "gust %d  from %s", (int)lround(w.windGust), CompassPoint(w.windDir));
    c.setTextSize(1); CenterText(c, foot, SCREEN_SIZE - 54, dim);
    char foot2[40];
    snprintf(foot2, sizeof(foot2), "%.0f%s  %s", w.airTemp, TempUnit(), WmoText(w.weatherCode));
    CenterText(c, foot2, SCREEN_SIZE - 34, faint);
}

// --------------------------------------------------------------------------------- water
void AnglerManager::DrawWater(BandCanvas& c)
{
    const float gf = GlowFactor();
    const uint32_t fg     = angler::ScaleColor(palette.fg, gf);
    const uint32_t dim    = angler::ScaleColor(palette.dim, gf);
    const uint32_t faint  = angler::ScaleColor(palette.faint, gf);
    const uint32_t accent = angler::ScaleColor(palette.accent, gf);

    const angler::MarineData& m = feed.Marine();
    const angler::BuoyData& bu = feed.Buoy();
    c.setTextSize(1);
    CenterText(c, "WATER", 18, dim);

    // Water temp priority: NOAA station gauge > real NDBC buoy > modeled sea-surface temp.
    bool haveT = false; float temp = 0; const char* tSrc = "";
    if (feed.HaveWaterTemp())              { temp = feed.WaterTemp();           haveT = true; tSrc = "NOAA station"; }
    else if (bu.valid && bu.haveWaterTemp) { temp = SeaTempDisp(bu.waterTempC); haveT = true; tSrc = "NDBC buoy"; }
    else if (m.valid && m.haveSst)         { temp = SeaTempDisp(m.seaTempC);    haveT = true; tSrc = "model"; }

    // Waves priority: real buoy observation (with dominant period) > modeled.
    bool haveW = false; float wave = 0, period = 0; const char* wSrc = "";
    if (bu.valid && bu.haveWave)    { wave = WaveDisp(bu.waveHeightM); period = bu.wavePeriodS; haveW = true; wSrc = "buoy"; }
    else if (m.valid && m.haveWave) { wave = WaveDisp(m.waveHeightM);  haveW = true; wSrc = "model"; }

    if (!haveT && !haveW) { c.setTextSize(2); CenterText(c, "no water data", SCREEN_SIZE_DIV_2, dim); return; }

    if (haveT) {
        char b[12]; snprintf(b, sizeof(b), "%.0f", temp);
        c.setTextSize(7); CenterText(c, b, 92, accent);
        c.setTextSize(2); CenterText(c, String("water ") + TempUnit(), 166, dim);
    } else {
        c.setTextSize(2); CenterText(c, "water temp n/a", 120, dim);
    }

    if (haveW) {
        char wv[32];
        if (period > 0) snprintf(wv, sizeof(wv), "waves %.1f %s  @ %.0fs", wave, WaveUnit(), period);
        else            snprintf(wv, sizeof(wv), "waves %.1f %s", wave, WaveUnit());
        c.setTextSize(2); CenterText(c, wv, 222, fg);
    }
    c.setTextSize(1);
    String src = haveT ? String("temp: ") + tSrc : String();
    if (haveW) src += (src.length() ? "   waves: " : "waves: ") + String(wSrc);
    CenterText(c, src, SCREEN_SIZE - 34, faint);
}

// --------------------------------------------------------------------------------- catch log
void AnglerManager::DrawCatchLog(BandCanvas& c)
{
    const float gf = GlowFactor();
    const uint32_t fg     = angler::ScaleColor(palette.fg, gf);
    const uint32_t dim    = angler::ScaleColor(palette.dim, gf);
    const uint32_t faint  = angler::ScaleColor(palette.faint, gf);
    const uint32_t accent = angler::ScaleColor(palette.accent, gf);

    const time_t now = time(nullptr);
    c.setTextSize(1);
    CenterText(c, "CATCH LOG", 18, dim);

    if (!logbook.Any()) {
        c.setTextSize(2); CenterText(c, "no catches yet", SCREEN_SIZE_DIV_2 - 20, dim);
        c.setTextSize(1); CenterText(c, "tap to log your first catch", SCREEN_SIZE_DIV_2 + 12, accent);
        return;
    }

    char big[8]; snprintf(big, sizeof(big), "%u", (unsigned)logbook.Total());
    c.setTextSize(1); CenterText(c, "TOTAL", 56, dim);
    c.setTextSize(7); CenterText(c, big, 76, fg);

    char line[40];
    snprintf(line, sizeof(line), "today %u   best day %u", (unsigned)logbook.TodayCount(now), (unsigned)logbook.Best());
    c.setTextSize(1); CenterText(c, line, SCREEN_SIZE_DIV_2 + 30, dim);
    snprintf(line, sizeof(line), "%d%% during bite windows", logbook.BitePercent());
    CenterText(c, line, SCREEN_SIZE_DIV_2 + 50, accent);
    snprintf(line, sizeof(line), "streak %u d   last %s", (unsigned)logbook.CurrentStreak(now), LocalHM(logbook.LastCatch()).c_str());
    CenterText(c, line, SCREEN_SIZE_DIV_2 + 70, faint);

    c.setTextSize(1);
    CenterText(c, "tap to log a catch", SCREEN_SIZE - 34, dim);
}

// --------------------------------------------------------------------------------- splash
void AnglerManager::DrawSplash(BandCanvas& c)
{
    const float gf = GlowFactor();
    const uint32_t fg     = angler::ScaleColor(palette.fg, gf);
    const uint32_t dim    = angler::ScaleColor(palette.dim, gf);
    const uint32_t accent = angler::ScaleColor(palette.accent, gf);

    c.setTextSize(3);
    CenterText(c, "BLIPSCOPE", SCREEN_SIZE_DIV_2 - 44, fg);
    c.setTextSize(2);
    CenterText(c, "angler edition", SCREEN_SIZE_DIV_2 - 8, dim);

    c.setTextSize(1);
    const char* hint = !hasLatLon ? "add your location in web config"
                                  : "computing the bite forecast...";
    CenterText(c, hint, SCREEN_SIZE_DIV_2 + 36, accent);
}

// --------------------------------------------------------------------------------- clock
void AnglerManager::DrawClock(BandCanvas& c)
{
    const float gf = GlowFactor();
    const uint32_t fg    = angler::ScaleColor(palette.fg, gf);
    const uint32_t faint = angler::ScaleColor(palette.faint, gf);

    const time_t utc = time(nullptr);
    char hhmm[16] = "--:--";
    if (utc > NTP_FLOOR) {
        const time_t local = utc + tzOffsetSec;
        struct tm t; gmtime_r(&local, &t);
        snprintf(hhmm, sizeof(hhmm), "%02d:%02d", t.tm_hour, t.tm_min);
    }
    c.setTextSize(1); CenterText(c, "LOCAL", SCREEN_SIZE_DIV_2 - 44, faint);
    c.setTextSize(6); CenterText(c, hhmm, SCREEN_SIZE_DIV_2 - 28, fg);
}

// --------------------------------------------------------------------------------- detail card
void AnglerManager::DrawDetailCard(BandCanvas& c)
{
    const float gf = GlowFactor();
    const uint32_t fg     = angler::ScaleColor(palette.fg, gf);
    const uint32_t dim    = angler::ScaleColor(palette.dim, gf);
    const uint32_t faint  = angler::ScaleColor(palette.faint, gf);
    const uint32_t accent = angler::ScaleColor(palette.accent, gf);

    const time_t now = time(nullptr);
    const int m = 18;
    c.fillRoundRect(m, m, SCREEN_SIZE - 2 * m, SCREEN_SIZE - 2 * m, 16, palette.bg);

    int y = 64;
    auto row = [&](const String& s, uint32_t col) { CenterText(c, s, y, col); y += 24; };

    if (current == Screen::Bite && selectedPeriod >= 0 && selectedPeriod < today.count) {
        // ---- a single feeding period ----
        const angler::Period& p = today.periods[selectedPeriod];
        const bool major = p.major();
        const uint32_t head = major ? accent : fg;
        c.drawRoundRect(m, m, SCREEN_SIZE - 2 * m, SCREEN_SIZE - 2 * m, 16, head);

        c.setTextSize(2);
        row(String(angler::PeriodShort(p.kind)) + " - " + angler::PeriodLabel(p.kind), head);
        y += 6;
        c.setTextSize(3);
        row(LocalHM(p.start) + " - " + LocalHM(p.end), fg);
        y += 4;
        c.setTextSize(2);
        if (now > NTP_FLOOR) {
            if (now < p.start)      row("opens in " + FormatDur(p.start - now), dim);
            else if (now < p.end)   row("active - " + FormatDur(p.end - now) + " left", accent);
            else                    row("ended", dim);
        }
        const char* ev = (p.kind == angler::PeriodKind::MajorOverhead)  ? "Moon overhead"
                       : (p.kind == angler::PeriodKind::MajorUnderfoot) ? "Moon underfoot"
                       : (p.kind == angler::PeriodKind::MinorRise)      ? "Moonrise"
                                                                        : "Moonset";
        row(String(ev) + " " + LocalHM(p.center), dim);
        row(major ? "major - stronger feeding" : "minor - lighter feeding", faint);
        bool green = (today.sunrise && p.start <= today.sunrise && today.sunrise <= p.end) ||
                     (today.sunset  && p.start <= today.sunset  && today.sunset  <= p.end);
        if (green) row("+ green window (dawn/dusk)", accent);

    } else if (current == Screen::Bite) {
        // ---- day summary ----
        c.drawRoundRect(m, m, SCREEN_SIZE - 2 * m, SCREEN_SIZE - 2 * m, 16, accent);
        c.setTextSize(2);
        row("TODAY", accent);
        y += 4;
        const int stars = angler::RatingStars(today.rating);
        const int cx = SCREEN_SIZE_DIV_2, sx0 = cx - (4 * 18) / 2 + 9;
        for (int i = 0; i < 4; ++i) {
            const bool on = i < stars;
            c.fillCircle(sx0 + i * 18, y + 4, 5, on ? accent : palette.bg);
            c.drawCircle(sx0 + i * 18, y + 4, 5, on ? accent : faint);
        }
        y += 30;
        c.setTextSize(2);
        row(angler::RatingLabel(today.rating), fg);
        char pct[24];
        snprintf(pct, sizeof(pct), "Moon %.0f%% lit", today.moonIllum * 100.0);
        row(pct, dim);
        row("Sun " + LocalHM(today.sunrise) + " - " + LocalHM(today.sunset), dim);
        if (today.sunBump) row("+ green window today", accent);
        row(String(today.count) + " feeding periods", faint);

    } else if (current == Screen::Moon) {
        c.drawRoundRect(m, m, SCREEN_SIZE - 2 * m, SCREEN_SIZE - 2 * m, 16, fg);
        double ra, dec, f = today.moonIllum; bool waxing = true;
        if (now > NTP_FLOOR) {
            double il0, il1, r2, d2;
            space::astro::MoonRaDec(now, ra, dec, il0);
            space::astro::MoonRaDec(now + 3600, r2, d2, il1);
            f = il0; waxing = il1 >= il0;
        }
        c.setTextSize(2);
        row("MOON", fg); y += 6;
        row(angler::MoonPhaseName(f, waxing), fg);
        char pct[16]; snprintf(pct, sizeof(pct), "%.0f%% lit", f * 100.0);
        row(pct, dim);
        row(String(waxing ? "waxing" : "waning"), faint);
        y += 6;
        row("Rise  " + LocalHM(today.moonrise), today.moonrise ? fg : faint);
        row("High  " + LocalHM(today.moonTransit), today.moonTransit ? fg : faint);
        row("Set   " + LocalHM(today.moonset), today.moonset ? fg : faint);

    } else if (current == Screen::Sun) {
        c.drawRoundRect(m, m, SCREEN_SIZE - 2 * m, SCREEN_SIZE - 2 * m, 16, accent);
        c.setTextSize(2);
        row("SUN", accent); y += 6;
        row("Rise  " + LocalHM(today.sunrise), today.sunrise ? fg : faint);
        if (today.sunrise && today.sunset) {
            const time_t noon = today.sunrise + (today.sunset - today.sunrise) / 2;
            row("Noon  " + LocalHM(noon), fg);
        }
        row("Set   " + LocalHM(today.sunset), today.sunset ? fg : faint);
        if (today.sunrise && today.sunset && today.sunset > today.sunrise) {
            const long dl = today.sunset - today.sunrise;
            char b[24]; snprintf(b, sizeof(b), "%ldh %02ldm daylight", dl / 3600, (dl % 3600) / 60);
            row(b, dim);
            row("Golden " + LocalHM(today.sunrise) + " & " + LocalHM(today.sunset - 3000), faint);
        }

    } else if (current == Screen::Tides) {
        const angler::TideData& td = feed.Tide();
        c.drawRoundRect(m, m, SCREEN_SIZE - 2 * m, SCREEN_SIZE - 2 * m, 16, fg);
        c.setTextSize(2); row("TIDES", fg); y += 6;
        int shown = 0;
        for (const angler::TideEvent& e : td.events) {
            if (e.t < now - 3600) continue;
            char b[40];
            snprintf(b, sizeof(b), "%s  %s  %.1f %s", e.high ? "High" : "Low ", LocalHM(e.t).c_str(), e.height, WaveUnit());
            row(b, e.high ? accent : dim);
            if (++shown >= 6) break;
        }
        if (feed.HaveWaterTemp()) { char b[24]; snprintf(b, sizeof(b), "Water %.0f%s", feed.WaterTemp(), TempUnit()); row(b, fg); }

    } else if (current == Screen::Barometer) {
        const angler::WeatherData& w = feed.Weather();
        const BaroTrend bt = ComputeBaro();
        c.drawRoundRect(m, m, SCREEN_SIZE - 2 * m, SCREEN_SIZE - 2 * m, 16, accent);
        c.setTextSize(2); row("BAROMETER", accent); y += 6;
        char b[28];
        if (imperial) snprintf(b, sizeof(b), "%.2f %s", PressDisp(w.pressureHpa), PressUnit());
        else          snprintf(b, sizeof(b), "%.0f %s", PressDisp(w.pressureHpa), PressUnit());
        row(b, fg);
        if (bt.valid) {
            if (imperial) snprintf(b, sizeof(b), "%+.2f inHg/h", bt.rateHpaPerH * 0.02953f);
            else          snprintf(b, sizeof(b), "%+.1f hPa/h", bt.rateHpaPerH);
            row(b, dim);
            row(bt.rateHpaPerH <= -0.2f ? "falling - fish feeding" : bt.rateHpaPerH >= 0.2f ? "rising - bite may slow" : "steady", dim);
            if (imperial) snprintf(b, sizeof(b), "6h ago %.2f %s", PressDisp(bt.pastHpa), PressUnit());
            else          snprintf(b, sizeof(b), "6h ago %.0f %s", PressDisp(bt.pastHpa), PressUnit());
            row(b, faint);
        }

    } else if (current == Screen::Wind) {
        const angler::WeatherData& w = feed.Weather();
        c.drawRoundRect(m, m, SCREEN_SIZE - 2 * m, SCREEN_SIZE - 2 * m, 16, fg);
        c.setTextSize(2); row("WIND", fg); y += 6;
        char b[40];
        snprintf(b, sizeof(b), "%d %s from %s", (int)lround(w.windSpeed), WindUnit(), CompassPoint(w.windDir));
        row(b, fg);
        snprintf(b, sizeof(b), "gust %d %s", (int)lround(w.windGust), WindUnit()); row(b, dim);
        snprintf(b, sizeof(b), "air %.0f%s   cloud %d%%", w.airTemp, TempUnit(), w.cloud); row(b, dim);
        row(WmoText(w.weatherCode), faint);

    } else if (current == Screen::Water) {
        const angler::MarineData& mr = feed.Marine();
        const angler::BuoyData& bu = feed.Buoy();
        c.drawRoundRect(m, m, SCREEN_SIZE - 2 * m, SCREEN_SIZE - 2 * m, 16, accent);
        c.setTextSize(2); row("WATER", accent); y += 6;
        char b[40];
        if (feed.HaveWaterTemp())              { snprintf(b, sizeof(b), "Temp %.0f%s (station)", feed.WaterTemp(), TempUnit()); row(b, fg); }
        else if (bu.valid && bu.haveWaterTemp) { snprintf(b, sizeof(b), "Temp %.0f%s (buoy)", SeaTempDisp(bu.waterTempC), TempUnit()); row(b, fg); }
        else if (mr.valid && mr.haveSst)       { snprintf(b, sizeof(b), "Temp %.0f%s (model)", SeaTempDisp(mr.seaTempC), TempUnit()); row(b, fg); }
        if (bu.valid && bu.haveWave) {
            if (bu.wavePeriodS > 0) snprintf(b, sizeof(b), "Waves %.1f %s @ %.0fs (buoy)", WaveDisp(bu.waveHeightM), WaveUnit(), bu.wavePeriodS);
            else                    snprintf(b, sizeof(b), "Waves %.1f %s (buoy)", WaveDisp(bu.waveHeightM), WaveUnit());
            row(b, dim);
        } else if (mr.valid && mr.haveWave) {
            snprintf(b, sizeof(b), "Waves %.1f %s (model)", WaveDisp(mr.waveHeightM), WaveUnit()); row(b, dim);
        }
    }

    c.setTextSize(1);
    CenterText(c, "tap to close", SCREEN_SIZE - 40, faint);
}

// --------------------------------------------------------------------------------- chrome
void AnglerManager::DrawScreenDots(BandCanvas& c, const std::vector<Screen>& rot) const
{
    const int n = (int)rot.size();
    if (n <= 1) return;
    int activeIdx = 0;
    for (int i = 0; i < n; ++i) if (rot[i] == current) { activeIdx = i; break; }

    const int gap = 9;
    const int totalW = (n - 1) * gap;
    int x = SCREEN_SIZE_DIV_2 - totalW / 2;
    const int y = SCREEN_SIZE - 12;
    for (int i = 0; i < n; ++i) {
        c.fillCircle(x, y, i == activeIdx ? 2 : 1, i == activeIdx ? palette.fg : palette.faint);
        x += gap;
    }
}
