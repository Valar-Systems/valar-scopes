#include "BirdingManager.h"

#include <math.h>
#include <time.h>

#include "Layout.h"

// The Birding edition screens. Each draws one full frame into the band canvas in absolute screen
// coordinates (the S3 renders a single full-height band). Colours come from the palette scaled by
// GlowFactor(); the gold accent is reserved for notable/rare sightings.

namespace {

constexpr int RADAR_R = SCREEN_SIZE_DIV_2 - 30;

String FitWidth(BandCanvas& c, String s, int maxW)
{
    if (c.textWidth(s) <= maxW) return s;
    while (s.length() > 1 && c.textWidth(s + "...") > maxW) s.remove(s.length() - 1);
    return s + "...";
}

const char* CompassPoint(double brg)
{
    static const char* pts[] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
    return pts[(int)lround(brg / 45.0) & 7];
}

String CountStr(int howMany)
{
    return howMany > 0 ? String(howMany) : String("X"); // eBird "X" = present, uncounted
}

} // namespace

// --------------------------------------------------------------------------------- radar
void BirdingManager::DrawRadar(BandCanvas& c)
{
    const float gf = GlowFactor();
    const uint32_t fg     = birding::ScaleColor(palette.fg, gf);
    const uint32_t dim    = birding::ScaleColor(palette.dim, gf);
    const uint32_t faint  = birding::ScaleColor(palette.faint, gf);
    const uint32_t accent = birding::ScaleColor(palette.accent, gf);

    const int cx = SCREEN_SIZE_DIV_2, cy = SCREEN_SIZE_DIV_2;

    c.drawCircle(cx, cy, RADAR_R, faint);
    c.drawCircle(cx, cy, RADAR_R * 2 / 3, faint);
    c.drawCircle(cx, cy, RADAR_R / 3, faint);
    c.drawFastHLine(cx - RADAR_R, cy, 2 * RADAR_R, faint);
    c.drawFastVLine(cx, cy - RADAR_R, 2 * RADAR_R, faint);
    c.setTextSize(1);
    CenterText(c, "N", cy - RADAR_R - 12, dim);
    char rng[16];
    snprintf(rng, sizeof(rng), "%d km", radiusKm);
    c.setTextColor(faint);
    c.drawString(rng, 14, SCREEN_SIZE - 22);

    c.fillCircle(cx, cy, 3, accent);

    const std::vector<birding::Sighting>& v = feed.Recent();
    const birding::Sighting* nearest = nullptr;
    double nearestD = 1e18;
    for (const birding::Sighting& s : v) {
        const auto p = ProjectSightingToScreen(s);
        const uint32_t col = s.notable ? accent : fg;
        c.fillCircle(p.first, p.second, s.notable ? 4 : 3, col);
        if (s.notable) c.drawCircle(p.first, p.second, 7, col);
        const double d = birding::DistanceKm(deviceLat, deviceLon, s.lat, s.lon);
        if (d < nearestD) { nearestD = d; nearest = &s; }
    }

    char cnt[24];
    snprintf(cnt, sizeof(cnt), "%u obs", (unsigned)v.size());
    c.setTextSize(2);
    c.setTextColor(dim);
    c.drawString(cnt, 16, 28);

    if (nearest) {
        c.setTextSize(2);
        CenterText(c, FitWidth(c, nearest->comName, SCREEN_SIZE - 60),
                   SCREEN_SIZE - 46, nearest->notable ? accent : fg);
    }
}

// --------------------------------------------------------------------------------- notable list
void BirdingManager::DrawNotable(BandCanvas& c)
{
    const float gf = GlowFactor();
    const uint32_t dim    = birding::ScaleColor(palette.dim, gf);
    const uint32_t faint  = birding::ScaleColor(palette.faint, gf);
    const uint32_t accent = birding::ScaleColor(palette.accent, gf);

    c.setTextSize(2);
    CenterText(c, "NOTABLE", 22, accent);

    const std::vector<birding::Sighting>& v = feed.Notable();
    if (v.empty()) {
        c.setTextSize(1);
        CenterText(c, "no notable sightings nearby", SCREEN_SIZE_DIV_2, dim);
        return;
    }

    const int LIST_TOP = 64, LIST_ROW_H = 40;
    const int rows = (SCREEN_SIZE - LIST_TOP - 24) / LIST_ROW_H;
    int shownCount = 0;
    for (int i = 0; i < rows; ++i) {
        const int idx = notableScroll + i;
        if (idx >= (int)v.size()) break;
        const birding::Sighting& s = v[idx];
        const int y = LIST_TOP + i * LIST_ROW_H;

        c.setTextSize(2);
        c.setTextColor(accent);
        c.drawString(FitWidth(c, s.comName, SCREEN_SIZE - 36), 16, y);

        c.setTextSize(1);
        c.setTextColor(dim);
        String sub;
        if (hasLatLon) {
            const double d = birding::DistanceKm(deviceLat, deviceLon, s.lat, s.lon);
            const double b = birding::BearingDeg(deviceLat, deviceLon, s.lat, s.lon);
            sub = String((int)lround(d)) + " km " + CompassPoint(b) + "  -  " + ShortDate(s.obsDt);
        } else {
            sub = ShortDate(s.obsDt) + "  -  " + s.locName;
        }
        c.drawString(FitWidth(c, sub, SCREEN_SIZE - 36), 16, y + 18);
        shownCount++;
    }

    if (notableScroll > 0 || notableScroll + shownCount < (int)v.size()) {
        char pos[16];
        snprintf(pos, sizeof(pos), "%d-%d/%u", notableScroll + 1, notableScroll + shownCount, (unsigned)v.size());
        c.setTextSize(1);
        c.setTextColor(faint);
        c.drawString(pos, SCREEN_SIZE_DIV_2 - c.textWidth(pos) / 2, SCREEN_SIZE - 20);
    }
}

// --------------------------------------------------------------------------------- big day
void BirdingManager::DrawBigDay(BandCanvas& c)
{
    const float gf = GlowFactor();
    const uint32_t fg    = birding::ScaleColor(palette.fg, gf);
    const uint32_t dim   = birding::ScaleColor(palette.dim, gf);
    const uint32_t faint = birding::ScaleColor(palette.faint, gf);

    const std::vector<birding::Sighting>& v = feed.Recent();
    const int species = DistinctSpecies(v);

    c.setTextSize(1);
    CenterText(c, "SPECIES NEARBY", 54, dim);
    char big[8];
    snprintf(big, sizeof(big), "%d", species);
    c.setTextSize(7);
    CenterText(c, big, 76, fg);

    char sub[40];
    snprintf(sub, sizeof(sub), "within %d km, last %d d", radiusKm, backDays);
    c.setTextSize(1);
    CenterText(c, sub, SCREEN_SIZE_DIV_2 + 30, dim);

    char obs[32];
    snprintf(obs, sizeof(obs), "%u recent observations", (unsigned)v.size());
    CenterText(c, obs, SCREEN_SIZE_DIV_2 + 52, faint);

    // A couple of the most recent species names as a footer.
    if (!v.empty()) {
        c.setTextSize(1);
        CenterText(c, FitWidth(c, v.front().comName, SCREEN_SIZE - 60), SCREEN_SIZE - 50, fg);
        if (v.size() > 1)
            CenterText(c, FitWidth(c, v[1].comName, SCREEN_SIZE - 60), SCREEN_SIZE - 32, dim);
    }
}

// --------------------------------------------------------------------------------- hotspot
void BirdingManager::DrawHotspot(BandCanvas& c)
{
    const float gf = GlowFactor();
    const uint32_t fg    = birding::ScaleColor(palette.fg, gf);
    const uint32_t dim   = birding::ScaleColor(palette.dim, gf);
    const uint32_t faint = birding::ScaleColor(palette.faint, gf);

    c.setTextSize(1);
    CenterText(c, "NEAREST HOTSPOT", 40, dim);

    const std::vector<birding::Hotspot>& hs = feed.Hotspots();
    if (hs.empty()) { c.setTextSize(2); CenterText(c, "--", SCREEN_SIZE_DIV_2, dim); return; }

    const birding::Hotspot* nearest = &hs.front();
    double nd = 1e18;
    if (hasLatLon) {
        for (const birding::Hotspot& h : hs) {
            const double d = birding::DistanceKm(deviceLat, deviceLon, h.lat, h.lon);
            if (d < nd) { nd = d; nearest = &h; }
        }
    }

    c.setTextSize(2);
    CenterText(c, FitWidth(c, nearest->name, SCREEN_SIZE - 50), SCREEN_SIZE_DIV_2 - 40, fg);

    char spec[8];
    snprintf(spec, sizeof(spec), "%d", nearest->numSpecies);
    c.setTextSize(6);
    CenterText(c, spec, SCREEN_SIZE_DIV_2 - 6, fg);
    c.setTextSize(1);
    CenterText(c, "species all-time", SCREEN_SIZE_DIV_2 + 46, dim);

    if (hasLatLon && nd < 1e17) {
        const double b = birding::BearingDeg(deviceLat, deviceLon, nearest->lat, nearest->lon);
        char d[24];
        snprintf(d, sizeof(d), "%.1f km %s", nd, CompassPoint(b));
        CenterText(c, d, SCREEN_SIZE - 44, faint);
    }
}

// --------------------------------------------------------------------------------- targets
void BirdingManager::DrawTargets(BandCanvas& c)
{
    const float gf = GlowFactor();
    const uint32_t fg     = birding::ScaleColor(palette.fg, gf);
    const uint32_t dim    = birding::ScaleColor(palette.dim, gf);
    const uint32_t accent = birding::ScaleColor(palette.accent, gf);

    c.setTextSize(2);
    CenterText(c, "TARGETS", 22, accent);

    // Collect matches across notable + recent, deduped by species.
    std::vector<const birding::Sighting*> hits;
    std::set<String> seen;
    auto scan = [&](const std::vector<birding::Sighting>& v) {
        for (const birding::Sighting& s : v)
            if (MatchesTarget(s) && seen.insert(s.speciesCode.length() ? s.speciesCode : s.comName).second)
                hits.push_back(&s);
    };
    scan(feed.Notable());
    scan(feed.Recent());

    if (hits.empty()) {
        char w[48];
        snprintf(w, sizeof(w), "watching %u - none nearby", (unsigned)targets.size());
        c.setTextSize(1);
        CenterText(c, w, SCREEN_SIZE_DIV_2, dim);
        return;
    }

    const int LIST_TOP = 64, LIST_ROW_H = 40;
    const int rows = (SCREEN_SIZE - LIST_TOP - 20) / LIST_ROW_H;
    for (int i = 0; i < rows && i < (int)hits.size(); ++i) {
        const birding::Sighting& s = *hits[i];
        const int y = LIST_TOP + i * LIST_ROW_H;
        c.setTextSize(2);
        c.setTextColor(s.notable ? accent : fg);
        c.drawString(FitWidth(c, s.comName, SCREEN_SIZE - 36), 16, y);
        c.setTextSize(1);
        c.setTextColor(dim);
        String sub = hasLatLon
            ? String((int)lround(birding::DistanceKm(deviceLat, deviceLon, s.lat, s.lon))) + " km  -  " + s.locName
            : s.locName;
        c.drawString(FitWidth(c, sub, SCREEN_SIZE - 36), 16, y + 18);
    }
}

// --------------------------------------------------------------------------------- splash
void BirdingManager::DrawSplash(BandCanvas& c)
{
    const float gf = GlowFactor();
    const uint32_t fg     = birding::ScaleColor(palette.fg, gf);
    const uint32_t dim    = birding::ScaleColor(palette.dim, gf);
    const uint32_t accent = birding::ScaleColor(palette.accent, gf);

    c.setTextSize(3);
    CenterText(c, "BLIPSCOPE", SCREEN_SIZE_DIV_2 - 44, fg);
    c.setTextSize(2);
    CenterText(c, "birding edition", SCREEN_SIZE_DIV_2 - 8, dim);

    c.setTextSize(1);
    const char* hint = !hasKey      ? "add your eBird API key in web config"
                     : !hasLatLon   ? "add your location in web config"
                                    : "loading sightings from eBird...";
    CenterText(c, hint, SCREEN_SIZE_DIV_2 + 36, accent);
}

// --------------------------------------------------------------------------------- clock
void BirdingManager::DrawClock(BandCanvas& c)
{
    const float gf = GlowFactor();
    const uint32_t fg    = birding::ScaleColor(palette.fg, gf);
    const uint32_t faint = birding::ScaleColor(palette.faint, gf);

    const time_t utc = time(nullptr);
    char hhmmss[16] = "--:--:--";
    if (utc > 1600000000) {
        struct tm t; gmtime_r(&utc, &t);
        snprintf(hhmmss, sizeof(hhmmss), "%02d:%02d:%02d", t.tm_hour, t.tm_min, t.tm_sec);
    }
    c.setTextSize(1); CenterText(c, "UTC", SCREEN_SIZE_DIV_2 - 46, faint);
    c.setTextSize(5); CenterText(c, hhmmss, SCREEN_SIZE_DIV_2 - 28, fg);
}

// --------------------------------------------------------------------------------- detail card
void BirdingManager::DrawDetailCard(BandCanvas& c)
{
    const float gf = GlowFactor();
    const uint32_t fg     = birding::ScaleColor(palette.fg, gf);
    const uint32_t dim    = birding::ScaleColor(palette.dim, gf);
    const uint32_t faint  = birding::ScaleColor(palette.faint, gf);
    const uint32_t accent = birding::ScaleColor(palette.accent, gf);

    const birding::Sighting& s = selected;
    const uint32_t head = s.notable ? accent : fg;

    const int m = 18;
    c.fillRoundRect(m, m, SCREEN_SIZE - 2 * m, SCREEN_SIZE - 2 * m, 16, palette.bg);
    c.drawRoundRect(m, m, SCREEN_SIZE - 2 * m, SCREEN_SIZE - 2 * m, 16, head);

    if (s.notable) { c.setTextSize(1); CenterText(c, "NOTABLE", 40, accent); }

    c.setTextSize(3);
    CenterText(c, FitWidth(c, s.comName, SCREEN_SIZE - 60), 60, head);
    c.setTextSize(1);
    CenterText(c, FitWidth(c, s.sciName, SCREEN_SIZE - 70), 100, dim);

    int y = 140;
    auto row = [&](const String& str, uint32_t col) { CenterText(c, str, y, col); y += 22; };

    row(FitWidth(c, s.locName, SCREEN_SIZE - 80), fg);
    if (hasLatLon)
        row(String((int)lround(s.distanceKm)) + " km " + CompassPoint(s.bearingDeg) + " of you", fg);
    row("seen " + ShortDate(s.obsDt), dim);
    row("count: " + CountStr(s.howMany), dim);

    c.setTextSize(1);
    CenterText(c, "tap to close", SCREEN_SIZE - 40, faint);
}

// --------------------------------------------------------------------------------- chrome
void BirdingManager::DrawScreenDots(BandCanvas& c, const std::vector<Screen>& rot) const
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
