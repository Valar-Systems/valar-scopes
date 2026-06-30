#include "SpaceManager.h"

#include <math.h>
#include <time.h>

#include "Layout.h"

// The Spacescope screens. Each draws one full frame into the band canvas in absolute screen
// coordinates (the S3 renders a single full-height band). Colours come from the palette scaled by
// GlowFactor() so night-dim fades everything uniformly. New screens are added here + a Screen enum
// entry + a HasData() case in SpaceManager.cpp; nothing else changes.

namespace {

// Trim a string to fit maxW pixels at the canvas's current text size, appending an ellipsis.
String FitWidth(BandCanvas& c, String s, int maxW)
{
    if (c.textWidth(s) <= maxW) return s;
    while (s.length() > 1 && c.textWidth(s + "...") > maxW) s.remove(s.length() - 1);
    return s + "...";
}

// Human data-rate, e.g. 1559000 -> "1.6 Mbps", 14220 -> "14 kbps", 160 -> "160 bps".
String FmtRate(double bps)
{
    if (bps >= 1e6) return String(bps / 1e6, 1) + " Mbps";
    if (bps >= 1e3) return String(bps / 1e3, 0) + " kbps";
    return String((long)bps) + " bps";
}

// Light-travel time from seconds, e.g. 84945 -> "23h 36m", 312 -> "5m 12s", 5 -> "5s".
String FmtLightTime(double s)
{
    char b[24];
    if (s >= 3600)     snprintf(b, sizeof(b), "%dh %02dm", (int)(s / 3600), (int)(s / 60) % 60);
    else if (s >= 60)  snprintf(b, sizeof(b), "%dm %02ds", (int)(s / 60), (int)s % 60);
    else               snprintf(b, sizeof(b), "%ds", (int)(s + 0.5));
    return String(b);
}

// DSN dish -> ground complex from the DSS number (11-29 Goldstone, 30-49 Canberra, 50-69 Madrid).
String DishComplex(const String& dish)
{
    int i = 0;
    while (i < (int)dish.length() && (dish[i] < '0' || dish[i] > '9')) ++i;
    const int n = dish.substring(i).toInt();
    if (n >= 11 && n <= 29) return "Goldstone";
    if (n >= 30 && n <= 49) return "Canberra";
    if (n >= 50 && n <= 69) return "Madrid";
    return "DSN";
}

// Friendly names for a curated set of well-known DSN spacecraft codes; raw code otherwise.
String FriendlyCraft(const String& code)
{
    if (code == "VGR1") return "Voyager 1";
    if (code == "VGR2") return "Voyager 2";
    if (code == "NHPC") return "New Horizons";
    if (code == "JWST") return "James Webb";
    if (code == "MRO")  return "Mars Recon Orbiter";
    if (code == "MVN")  return "MAVEN";
    if (code == "M01O") return "Mars Odyssey";
    if (code == "TGO")  return "ExoMars TGO";
    if (code == "PSP")  return "Parker Solar Probe";
    if (code == "DSCO") return "DSCOVR";
    if (code == "ACE")  return "ACE";
    if (code == "CHDR") return "Chandra";
    return code;
}

// Moon phase name from the phase fraction p (0=new, 0.5=full).
const char* PhaseName(double p)
{
    if (p < 0.033 || p >= 0.967) return "New Moon";
    if (p < 0.217) return "Waxing Crescent";
    if (p < 0.283) return "First Quarter";
    if (p < 0.467) return "Waxing Gibbous";
    if (p < 0.533) return "Full Moon";
    if (p < 0.717) return "Waning Gibbous";
    if (p < 0.783) return "Last Quarter";
    return "Waning Crescent";
}

} // namespace

// --------------------------------------------------------------------------------- ISS tracker
void SpaceManager::DrawIss(BandCanvas& c)
{
    const float gf = GlowFactor();
    const uint32_t fg     = space::ScaleColor(palette.fg, gf);
    const uint32_t dim    = space::ScaleColor(palette.dim, gf);
    const uint32_t faint  = space::ScaleColor(palette.faint, gf);
    const uint32_t accent = space::ScaleColor(palette.accent, gf);
    const uint32_t warn   = space::ScaleColor(palette.warn, gf);

    const int cx = SCREEN_SIZE_DIV_2, cy = SCREEN_SIZE_DIV_2;
    const int R = SCREEN_SIZE_DIV_2 - 30;

    // North-polar azimuthal "globe": pole at centre, rim is the south pole, the equator a mid ring.
    c.drawCircle(cx, cy, R, dim);
    c.drawCircle(cx, cy, R / 2, faint);
    c.drawFastHLine(cx - R, cy, 2 * R, faint);
    c.drawFastVLine(cx, cy - R, 2 * R, faint);
    c.fillCircle(cx, cy, 2, faint);

    const space::IssState& s = feed.Iss();
    if (!s.valid) { c.setTextSize(2); CenterText(c, "ISS", cy - 8, fg); return; }

    // r grows from the pole (lat +90) to the rim (lat -90); lon sets the bearing (0 at top).
    const float rr = (float)((90.0 - s.lat) / 180.0) * R;
    const float ang = (float)(s.lon * M_PI / 180.0) - (float)M_PI / 2.0f;
    const int bx = cx + (int)(rr * cosf(ang));
    const int by = cy + (int)(rr * sinf(ang));
    c.fillCircle(bx, by, 4, accent);
    c.drawCircle(bx, by, 8, fg);

    c.setTextSize(2); CenterText(c, "ISS", 16, fg);
    c.setTextSize(1); CenterText(c, s.sunlit ? "SUNLIT" : "ECLIPSED", 42, s.sunlit ? warn : dim);

    char line[40];
    snprintf(line, sizeof(line), "%d km   %d km/h", (int)lround(s.altKm), (int)lround(s.velocityKmh));
    c.setTextSize(1); CenterText(c, line, SCREEN_SIZE - 30, dim);
}

// ------------------------------------------------------------------------------ launch T-minus
void SpaceManager::DrawLaunch(BandCanvas& c)
{
    const float gf = GlowFactor();
    const uint32_t fg     = space::ScaleColor(palette.fg, gf);
    const uint32_t dim    = space::ScaleColor(palette.dim, gf);
    const uint32_t faint  = space::ScaleColor(palette.faint, gf);
    const uint32_t accent = space::ScaleColor(palette.accent, gf);

    const std::vector<space::Launch>& ls = feed.Launches();
    if (ls.empty()) { c.setTextSize(2); CenterText(c, "NO LAUNCH", SCREEN_SIZE_DIV_2 - 8, dim); return; }
    const space::Launch& L = ls.front();

    c.setTextSize(2);
    CenterText(c, FitWidth(c, L.provider.length() ? L.provider : String("LAUNCH"), SCREEN_SIZE - 60), 22, fg);
    if (L.vehicle.length()) { c.setTextSize(1); CenterText(c, FitWidth(c, L.vehicle, SCREEN_SIZE - 80), 50, dim); }

    // The big countdown ticks locally from NTP; only the launch instant comes from the feed.
    const time_t now = time(nullptr);
    const bool synced = now > 1600000000;
    String big;
    uint32_t bigColor = fg;
    if (L.precise && L.t0Epoch > 0 && synced) {
        const long delta = (long)L.t0Epoch - (long)now;
        big = FormatTMinus(delta);
        if (delta < 0) bigColor = dim; // already lifted off
    } else if (L.dateStr.length()) {
        big = "NET " + L.dateStr;
    } else {
        big = "SCHEDULED";
    }
    c.setTextSize(big.length() > 9 ? 3 : 4);
    CenterText(c, big, SCREEN_SIZE_DIV_2 - 22, bigColor);

    c.setTextSize(1);
    if (L.mission.length()) CenterText(c, FitWidth(c, L.mission, SCREEN_SIZE - 70), SCREEN_SIZE_DIV_2 + 24, accent);
    if (L.pad.length())     CenterText(c, FitWidth(c, L.pad, SCREEN_SIZE - 70), SCREEN_SIZE - 40, faint);
}

// -------------------------------------------------------------------------------- Kp gauge
void SpaceManager::DrawKp(BandCanvas& c)
{
    const float gf = GlowFactor();
    const uint32_t fg     = space::ScaleColor(palette.fg, gf);
    const uint32_t dim    = space::ScaleColor(palette.dim, gf);
    const uint32_t faint  = space::ScaleColor(palette.faint, gf);
    const uint32_t accent = space::ScaleColor(palette.accent, gf);

    const int cx = SCREEN_SIZE_DIV_2, cy = SCREEN_SIZE_DIV_2;
    const int R = SCREEN_SIZE_DIV_2 - 22;
    const int r = R - 28;
    const float startA = 135.0f, sweep = 270.0f; // 270 deg gauge opening at the bottom

    c.setTextSize(1); CenterText(c, "GEOMAGNETIC  Kp", 18, dim);
    c.fillArc(cx, cy, r, R, startA, startA + sweep, faint); // background track

    const space::SpaceWx& wx = feed.Wx();
    if (!wx.valid) { c.setTextSize(2); CenterText(c, "--", cy - 8, dim); return; }

    float kp = wx.kp; if (kp < 0) kp = 0; if (kp > 9) kp = 9;
    const float ang = startA + (kp / 9.0f) * sweep;

    const uint32_t kpRaw = kp >= 7 ? palette.alert : kp >= 5 ? palette.warn
                         : kp >= 4 ? lgfx::color888(120, 210, 90) : lgfx::color888(60, 180, 255);
    const uint32_t kpColor = space::ScaleColor(kpRaw, gf);
    c.fillArc(cx, cy, r, R, startA, ang, kpColor);
    c.fillArc(cx, cy, r - 3, R + 3, ang - 1.5f, ang + 1.5f, accent); // current-value tick

    char val[8]; snprintf(val, sizeof(val), "%.1f", kp);
    c.setTextSize(5); CenterText(c, val, cy - 36, fg);

    String label;
    if (kp >= 5)      label = "G" + String((int)floorf(kp) - 4); // Kp 5..9 -> G1..G5
    else if (kp >= 4) label = "ACTIVE";
    else if (kp >= 3) label = "UNSETTLED";
    else              label = "QUIET";
    c.setTextSize(2); CenterText(c, label, cy + 12, kpColor);

    // Recent-Kp sparkline in the gauge's open bottom wedge.
    if (wx.history.size() >= 2) {
        const int n = (int)wx.history.size();
        const int sw = 132, sh = 22;
        const int sx = cx - sw / 2, baseY = SCREEN_SIZE - 30;
        const int bw = sw / n;
        for (int i = 0; i < n; ++i) {
            float v = wx.history[i]; if (v < 0) v = 0; if (v > 9) v = 9;
            const int bh = (int)(v / 9.0f * sh);
            c.fillRect(sx + i * bw, baseY - bh, bw > 1 ? bw - 1 : 1, bh, i == n - 1 ? accent : dim);
        }
    }
}

// ----------------------------------------------------------------------------------- splash
void SpaceManager::DrawSplash(BandCanvas& c)
{
    const float gf = GlowFactor();
    const uint32_t fg     = space::ScaleColor(palette.fg, gf);
    const uint32_t accent = space::ScaleColor(palette.accent, gf);
    const uint32_t faint  = space::ScaleColor(palette.faint, gf);

    // A few fixed "stars" so the card reads as space, deterministic so the banded passes match.
    static const struct { int x, y, r; } stars[] = {
        {54, 70, 1}, {120, 44, 1}, {210, 58, 2}, {300, 40, 1}, {360, 96, 1},
        {40, 180, 1}, {380, 210, 2}, {70, 300, 1}, {150, 360, 1}, {260, 350, 2},
        {340, 320, 1}, {200, 110, 1}, {90, 130, 1}, {320, 160, 1}, {180, 300, 1},
    };
    for (const auto& s : stars)
        c.fillCircle(s.x, s.y, s.r, s.r > 1 ? accent : faint);

    c.setTextSize(4); CenterText(c, "SPACESCOPE", SCREEN_SIZE_DIV_2 - 40, fg);
    c.setTextSize(1); CenterText(c, "a desk window into everything above you", SCREEN_SIZE_DIV_2 + 4, faint);
    c.setTextSize(2); CenterText(c, "awaiting feeds...", SCREEN_SIZE_DIV_2 + 44, accent);
}

// ------------------------------------------------------------------------------------- clock
void SpaceManager::DrawClock(BandCanvas& c)
{
    const float gf = GlowFactor();
    const uint32_t fg    = space::ScaleColor(palette.fg, gf);
    const uint32_t faint = space::ScaleColor(palette.faint, gf);

    const time_t utc = time(nullptr);
    char hhmmss[16] = "--:--:--";
    if (utc > 1600000000) { // NTP synced
        struct tm t;
        gmtime_r(&utc, &t);
        snprintf(hhmmss, sizeof(hhmmss), "%02d:%02d:%02d", t.tm_hour, t.tm_min, t.tm_sec);
    }

    c.setTextSize(1); CenterText(c, "UTC", SCREEN_SIZE_DIV_2 - 46, faint);
    c.setTextSize(5); CenterText(c, hhmmss, SCREEN_SIZE_DIV_2 - 28, fg);
}

// ------------------------------------------------------------------------------- DSN Now
void SpaceManager::DrawDsn(BandCanvas& c)
{
    const float gf = GlowFactor();
    const uint32_t fg     = space::ScaleColor(palette.fg, gf);
    const uint32_t dim    = space::ScaleColor(palette.dim, gf);
    const uint32_t faint  = space::ScaleColor(palette.faint, gf);
    const uint32_t accent = space::ScaleColor(palette.accent, gf);

    c.setTextSize(1); CenterText(c, "DEEP SPACE NETWORK", 20, dim);

    const space::DsnState& d = feed.Dsn();
    if (!d.valid || d.links.empty()) {
        c.setTextSize(2); CenterText(c, d.valid ? "no active links" : "acquiring...", SCREEN_SIZE_DIV_2 - 8, dim);
        return;
    }

    // Headline: rotate one active link at a time so each gets the round panel to itself.
    const int n = (int)d.links.size();
    const space::DsnLink& L = d.links[cardIndex % n];
    const uint32_t dirColor = L.up ? accent : fg; // uplink (commanding) vs downlink (receiving)

    c.setTextSize(3); CenterText(c, FitWidth(c, FriendlyCraft(L.spacecraft), SCREEN_SIZE - 40), SCREEN_SIZE_DIV_2 - 56, fg);
    c.setTextSize(1); CenterText(c, DishComplex(L.dish) + "  " + L.dish, SCREEN_SIZE_DIV_2 - 18, dim);

    String rate = (L.up ? "^ " : "v ") + FmtRate(L.dataRateBps);
    if (L.band.length()) rate += "   " + L.band + "-band";
    c.setTextSize(2); CenterText(c, rate, SCREEN_SIZE_DIV_2 + 8, dirColor);

    char foot[40];
    snprintf(foot, sizeof(foot), "%d active link%s   %s", n, n == 1 ? "" : "s", L.up ? "uplink" : "downlink");
    c.setTextSize(1); CenterText(c, foot, SCREEN_SIZE - 32, faint);
}

// --------------------------------------------------------------------------- deep-space distance
void SpaceManager::DrawDeepSpace(BandCanvas& c)
{
    const float gf = GlowFactor();
    const uint32_t fg     = space::ScaleColor(palette.fg, gf);
    const uint32_t dim    = space::ScaleColor(palette.dim, gf);
    const uint32_t faint  = space::ScaleColor(palette.faint, gf);
    const uint32_t accent = space::ScaleColor(palette.accent, gf);

    c.setTextSize(1); CenterText(c, "DEEP SPACE", 20, dim);

    // Gather the targets that have a fix; rotate through them.
    std::vector<const space::DeepSpaceTarget*> live;
    for (const space::DeepSpaceTarget& t : feed.DeepTargets()) if (t.valid) live.push_back(&t);
    if (live.empty()) {
        c.setTextSize(2); CenterText(c, "acquiring...", SCREEN_SIZE_DIV_2 - 8, dim);
        return;
    }
    const space::DeepSpaceTarget& t = *live[cardIndex % (int)live.size()];

    String name = t.name; name.toUpperCase();
    c.setTextSize(2); CenterText(c, name, SCREEN_SIZE_DIV_2 - 58, accent);

    // Distance: AU once past ~1 AU, otherwise million-km so JWST/Parker read sensibly.
    String big;
    if (t.distanceAu >= 1.0) big = String(t.distanceAu, 1) + " AU";
    else                     big = String(t.distanceAu * 149.597871, 2) + "M km";
    c.setTextSize(4); CenterText(c, big, SCREEN_SIZE_DIV_2 - 24, fg);

    String spd = String(t.speedKms, 1) + " km/s " + (t.receding ? "receding" : "approaching");
    c.setTextSize(1); CenterText(c, spd, SCREEN_SIZE_DIV_2 + 22, dim);
    c.setTextSize(1); CenterText(c, FmtLightTime(t.distanceAu * 499.004784) + " light delay", SCREEN_SIZE_DIV_2 + 44, faint);
}

// ----------------------------------------------------------------------------- solar flare
void SpaceManager::DrawFlare(BandCanvas& c)
{
    const float gf = GlowFactor();
    const uint32_t fg     = space::ScaleColor(palette.fg, gf);
    const uint32_t dim    = space::ScaleColor(palette.dim, gf);
    const uint32_t faint  = space::ScaleColor(palette.faint, gf);
    const uint32_t accent = space::ScaleColor(palette.accent, gf);
    const uint32_t warn   = space::ScaleColor(palette.warn, gf);
    const uint32_t alert  = space::ScaleColor(palette.alert, gf);

    const int cx = SCREEN_SIZE_DIV_2, cy = SCREEN_SIZE_DIV_2;
    const int R = SCREEN_SIZE_DIV_2 - 22, r = R - 28;
    const float startA = 135.0f, sweep = 270.0f;

    c.setTextSize(1); CenterText(c, "SOLAR X-RAY FLUX", 18, dim);
    c.fillArc(cx, cy, r, R, startA, startA + sweep, faint);

    const space::Flare& f = feed.Flare();
    if (!f.valid) { c.setTextSize(2); CenterText(c, "acquiring...", cy - 8, dim); return; }

    // Log scale across the NOAA decades A(1e-8) .. X(1e-3).
    float frac = (log10f(f.fluxWm2 > 0 ? f.fluxWm2 : 1e-9f) + 8.0f) / 5.0f;
    if (frac < 0) frac = 0; if (frac > 1) frac = 1;
    const float ang = startA + frac * sweep;

    const String cls = space::XrayClass(f.fluxWm2);
    const char L = cls.length() ? cls[0] : 'A';
    const uint32_t col = (L == 'X') ? alert : (L == 'M') ? warn : (L == 'C') ? fg : dim;

    c.fillArc(cx, cy, r, R, startA, ang, col);
    c.fillArc(cx, cy, r - 3, R + 3, ang - 1.5f, ang + 1.5f, accent);

    c.setTextSize(5); CenterText(c, cls, cy - 30, col);
    c.setTextSize(1); CenterText(c, "A   B   C   M   X", cy + 18, faint);
    c.setTextSize(1); CenterText(c, "6h peak " + space::XrayClass(f.peakFluxWm2), SCREEN_SIZE - 30, dim);
}

// --------------------------------------------------------------------------- humans in space
void SpaceManager::DrawHumans(BandCanvas& c)
{
    const float gf = GlowFactor();
    const uint32_t fg     = space::ScaleColor(palette.fg, gf);
    const uint32_t dim    = space::ScaleColor(palette.dim, gf);
    const uint32_t faint  = space::ScaleColor(palette.faint, gf);
    const uint32_t accent = space::ScaleColor(palette.accent, gf);

    c.setTextSize(1); CenterText(c, "HUMANS IN SPACE", 22, dim);

    const space::Crew& cr = feed.Crew();
    if (!cr.valid || cr.number <= 0) { c.setTextSize(2); CenterText(c, "acquiring...", SCREEN_SIZE_DIV_2 - 8, dim); return; }

    char num[8]; snprintf(num, sizeof(num), "%d", cr.number);
    c.setTextSize(6); CenterText(c, num, SCREEN_SIZE_DIV_2 - 58, fg);
    c.setTextSize(1); CenterText(c, "aboard right now", SCREEN_SIZE_DIV_2 + 6, faint);

    int iss = 0, tg = 0, other = 0;
    for (const auto& p : cr.people) {
        if (p.first == "ISS") ++iss;
        else if (p.first == "Tiangong") ++tg;
        else ++other;
    }
    String tally;
    if (iss) tally += "ISS " + String(iss);
    if (tg)  { if (tally.length()) tally += "    "; tally += "Tiangong " + String(tg); }
    if (other) { if (tally.length()) tally += "    "; tally += "+" + String(other); }
    c.setTextSize(1); CenterText(c, tally, SCREEN_SIZE_DIV_2 + 28, dim);

    // Rotate one crew member's name at a time.
    if (!cr.people.empty()) {
        const auto& p = cr.people[cardIndex % (int)cr.people.size()];
        c.setTextSize(2); CenterText(c, FitWidth(c, p.second, SCREEN_SIZE - 40), SCREEN_SIZE - 58, accent);
        c.setTextSize(1); CenterText(c, p.first, SCREEN_SIZE - 32, faint);
    }
}

// ------------------------------------------------------------------------------------- moon
void SpaceManager::DrawMoon(BandCanvas& c)
{
    const float gf = GlowFactor();
    const uint32_t dim    = space::ScaleColor(palette.dim, gf);
    const uint32_t faint  = space::ScaleColor(palette.faint, gf);
    const uint32_t accent = space::ScaleColor(palette.accent, gf);
    const uint32_t bodyDark = space::ScaleColor(lgfx::color888(34, 38, 52), gf);
    const uint32_t lit      = space::ScaleColor(lgfx::color888(225, 228, 235), gf); // pale moonlight

    const int cx = SCREEN_SIZE_DIV_2, cyTop = SCREEN_SIZE_DIV_2 - 30, R = 76;
    c.setTextSize(1); CenterText(c, "MOON", 18, dim);
    c.fillCircle(cx, cyTop, R, bodyDark);

    const time_t now = time(nullptr);
    if (now <= 1600000000) { // need NTP for the absolute phase
        c.drawCircle(cx, cyTop, R, faint);
        c.setTextSize(1); CenterText(c, "awaiting clock", cyTop + R + 22, dim);
        return;
    }

    // Phase fraction p (0=new, 0.5=full) from the synodic month since a known new moon.
    constexpr double SYN = 29.530588853;
    constexpr long REF = 947182440; // 2000-01-06 18:14 UTC new moon
    double age = fmod(((double)now - (double)REF) / 86400.0, SYN);
    if (age < 0) age += SYN;
    const double p = age / SYN;
    const double cph = cos(2.0 * M_PI * p);

    // Lit span per scanline: waxing lit on the right, waning on the left (N-hemisphere convention).
    for (int y = -R; y <= R; ++y) {
        const double hw = sqrt((double)R * R - (double)y * y);
        int left, right;
        if (p <= 0.5) { left = (int)(cph * hw); right = (int)hw; }
        else          { left = (int)(-hw);      right = (int)(-cph * hw); }
        if (right > left) c.drawFastHLine(cx + left, cyTop + y, right - left, lit);
    }
    c.drawCircle(cx, cyTop, R, faint);

    const double illum = (1.0 - cph) / 2.0;
    const double daysToFull = fmod(0.5 - p + 1.0, 1.0) * SYN;
    c.setTextSize(2); CenterText(c, PhaseName(p), cyTop + R + 14, accent);
    char info[44];
    snprintf(info, sizeof(info), "%d%% lit   full in %.0fd", (int)(illum * 100 + 0.5), daysToFull);
    c.setTextSize(1); CenterText(c, info, cyTop + R + 40, dim);
}

// --------------------------------------------------------------------------------- solar wind
void SpaceManager::DrawSolarWind(BandCanvas& c)
{
    const float gf = GlowFactor();
    const uint32_t fg     = space::ScaleColor(palette.fg, gf);
    const uint32_t dim    = space::ScaleColor(palette.dim, gf);
    const uint32_t faint  = space::ScaleColor(palette.faint, gf);
    const uint32_t warn   = space::ScaleColor(palette.warn, gf);
    const uint32_t alert  = space::ScaleColor(palette.alert, gf);
    const uint32_t green  = space::ScaleColor(lgfx::color888(120, 210, 90), gf);

    c.setTextSize(1); CenterText(c, "SOLAR WIND", 20, dim);

    const space::SolarWind& sw = feed.SolarWind();
    if (!sw.valid) { c.setTextSize(2); CenterText(c, "acquiring...", SCREEN_SIZE_DIV_2 - 8, dim); return; }

    c.setTextSize(4); CenterText(c, String((int)lround(sw.speedKms)) + " km/s", SCREEN_SIZE_DIV_2 - 52, fg);
    c.setTextSize(1); CenterText(c, String(sw.densityPcc, 1) + " p/cm3 density", SCREEN_SIZE_DIV_2 - 14, dim);

    // Bz: negative (southward) drives aurora -> red; positive (northward) -> green.
    const uint32_t bzCol = sw.bzNt <= -10 ? alert : sw.bzNt < 0 ? warn : green;
    c.setTextSize(3); CenterText(c, "Bz " + String(sw.bzNt, 1) + " nT", SCREEN_SIZE_DIV_2 + 18, bzCol);
    c.setTextSize(1);
    CenterText(c, sw.bzNt <= -10 ? "strongly southward - aurora drive"
                  : sw.bzNt < 0 ? "southward" : "northward (quiet)",
               SCREEN_SIZE - 32, faint);
}

// ----------------------------------------------------------------------- NOAA R/S/G scales
void SpaceManager::DrawScales(BandCanvas& c)
{
    const float gf = GlowFactor();
    const uint32_t fg     = space::ScaleColor(palette.fg, gf);
    const uint32_t dim    = space::ScaleColor(palette.dim, gf);
    const uint32_t faint  = space::ScaleColor(palette.faint, gf);
    const uint32_t warn   = space::ScaleColor(palette.warn, gf);
    const uint32_t alert  = space::ScaleColor(palette.alert, gf);

    const int cx = SCREEN_SIZE_DIV_2, cy = SCREEN_SIZE_DIV_2;
    const float startA = 135.0f, sweep = 270.0f;
    c.setTextSize(1); CenterText(c, "NOAA SPACE-WX SCALES", 18, dim);

    const space::NoaaScales& sc = feed.Scales();
    if (!sc.valid) { c.setTextSize(2); CenterText(c, "acquiring...", cy - 8, dim); return; }

    auto lvlColor = [&](int v) -> uint32_t { return v >= 4 ? alert : v >= 3 ? warn : v >= 1 ? fg : faint; };
    const int R = SCREEN_SIZE_DIV_2 - 22;
    // Three concentric gauges: G (outer), S (middle), R (inner).
    const int rings[3][2] = { {R - 20, R}, {R - 46, R - 26}, {R - 72, R - 52} };
    const int vals[3] = { sc.g, sc.s, sc.r };
    for (int i = 0; i < 3; ++i) {
        c.fillArc(cx, cy, rings[i][0], rings[i][1], startA, startA + sweep, faint);
        if (vals[i] > 0)
            c.fillArc(cx, cy, rings[i][0], rings[i][1], startA, startA + sweep * (vals[i] / 5.0f), lvlColor(vals[i]));
    }

    // Center readout.
    c.setTextSize(2);
    CenterText(c, "G" + String(sc.g), cy - 26, lvlColor(sc.g));
    CenterText(c, "S" + String(sc.s), cy - 2, lvlColor(sc.s));
    CenterText(c, "R" + String(sc.r), cy + 22, lvlColor(sc.r));
    c.setTextSize(1); CenterText(c, "geomag / radiation / radio", SCREEN_SIZE - 30, faint);
}

// ------------------------------------------------------------------------------- aurora (local)
void SpaceManager::DrawAurora(BandCanvas& c)
{
    const float gf = GlowFactor();
    const uint32_t fg     = space::ScaleColor(palette.fg, gf);
    const uint32_t dim    = space::ScaleColor(palette.dim, gf);
    const uint32_t faint  = space::ScaleColor(palette.faint, gf);
    const uint32_t warn   = space::ScaleColor(palette.warn, gf);
    const uint32_t green  = space::ScaleColor(lgfx::color888(120, 210, 90), gf);

    c.setTextSize(1); CenterText(c, "AURORA FORECAST", 22, dim);

    const space::SpaceWx& wx = feed.Wx();
    if (!wx.valid || !hasLatLon) { c.setTextSize(2); CenterText(c, "set location", SCREEN_SIZE_DIV_2 - 8, dim); return; }

    const double gm = fabs(space::GeomagLatitude(deviceLat, deviceLon));
    const float oval = space::AuroraOvalLat(wx.kp);

    const char* verdict; uint32_t vcol;
    if (gm >= oval)          { verdict = "OVERHEAD POSSIBLE"; vcol = green; }
    else if (gm >= oval - 5) { verdict = "LOW ON N HORIZON";  vcol = warn; }
    else                     { verdict = "UNLIKELY TONIGHT";  vcol = dim; }

    c.setTextSize(3); CenterText(c, verdict, SCREEN_SIZE_DIV_2 - 40, vcol);
    char l1[40]; snprintf(l1, sizeof(l1), "Kp %.1f   oval edge %.0f deg", wx.kp, oval);
    char l2[40]; snprintf(l2, sizeof(l2), "your geomag lat %.0f deg", gm);
    c.setTextSize(1); CenterText(c, l1, SCREEN_SIZE_DIV_2 + 2, dim);
    c.setTextSize(1); CenterText(c, l2, SCREEN_SIZE_DIV_2 + 24, faint);

    // Latitude bar 40..80 deg: oval edge tick vs your latitude marker.
    const int bx = SCREEN_SIZE_DIV_2 - 120, bw = 240, by = SCREEN_SIZE - 48;
    auto xfor = [&](float lat) { float f = (lat - 40.0f) / 40.0f; if (f < 0) f = 0; if (f > 1) f = 1; return bx + (int)(f * bw); };
    c.drawFastHLine(bx, by, bw, faint);
    c.fillRect(xfor(oval), by - 6, 2, 12, warn);                  // oval edge
    c.fillCircle(xfor((float)gm), by, 4, gm >= oval ? green : fg); // you
}
