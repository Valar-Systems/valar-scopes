#include "SpaceManager.h"

#include <math.h>
#include <time.h>

#include "Layout.h"

// Optional Valar space-feed backend base URL default. Normally NOT injected (Spacescope talks
// directly to free public space APIs and bakes in nothing); guarded so the file compiles whether
// or not -DSPACE_FEED_BASE is set. The runtime value ("space-base-url") overrides it.
#ifndef SPACE_FEED_BASE
#define SPACE_FEED_BASE ""
#endif

namespace {

constexpr unsigned long AUTO_DWELL_MS    = 8000;   // ms per screen when auto-rotating
constexpr unsigned long INTERACT_HOLD_MS = 30000;  // pause auto-rotate this long after a touch

double Deg2Rad(double d) { return d * M_PI / 180.0; }

// Low-precision solar elevation (degrees) at lat/lon for a UTC epoch -- drives the same night
// auto-dim the radar / EAM use (sun below -0.833 deg = civil horizon). Copied from EamManager so
// the space app dims identically without depending on the EAM translation unit.
float SunElevationDeg(double latDeg, double lonDeg, time_t utc)
{
    const double n = (double)utc / 86400.0 - 10957.5; // days since J2000.0 (2000-01-01 12:00 UTC)
    double L = fmod(280.460 + 0.9856474 * n, 360.0);
    double g = fmod(357.528 + 0.9856003 * n, 360.0);
    const double lambda = L + 1.915 * sin(Deg2Rad(g)) + 0.020 * sin(Deg2Rad(2 * g));
    const double eps = 23.439 - 0.0000004 * n;
    const double lam = Deg2Rad(lambda), e = Deg2Rad(eps);
    const double dec = asin(sin(e) * sin(lam));
    const double ra = atan2(cos(e) * sin(lam), cos(lam)); // radians
    double gmst = fmod(18.697374558 + 24.06570982441908 * n, 24.0);
    if (gmst < 0) gmst += 24.0;
    const double lstDeg = gmst * 15.0 + lonDeg;
    const double Hdeg = lstDeg - ra * 180.0 / M_PI;
    const double H = Deg2Rad(Hdeg);
    const double lat = Deg2Rad(latDeg);
    double sinEl = sin(lat) * sin(dec) + cos(lat) * cos(dec) * cos(H);
    if (sinEl > 1) sinEl = 1;
    if (sinEl < -1) sinEl = -1;
    return (float)(asin(sinEl) * 180.0 / M_PI);
}

} // namespace

void SpaceManager::Initialise()
{
    // Optional backend (Phase-3 valar-space-feed). Empty default = talk directly to public APIs.
    backendBaseUrl = configServer.GetStoredString("space-base-url");
    if (backendBaseUrl.isEmpty())
        backendBaseUrl = SPACE_FEED_BASE;

    palette = space::PaletteDefault();

    const String br = configServer.GetStoredString("brightness");
    configuredBrightness = br.isEmpty() ? 255 : (uint8_t)constrain(br.toInt(), 10, 255);
    const String ad = configServer.GetStoredString("autodim");
    autoDim = ad.isEmpty() || ad == "true";

    // Screen enable/order. "space-screens" is a CSV of screen ids in display order; empty = all.
    enabledOrder.clear();
    const String screensCfg = configServer.GetStoredString("space-screens");
    auto idToScreen = [](const String& id, Screen& out) -> bool {
        if (id == "splash") { out = Screen::Splash; return true; }
        if (id == "clock")  { out = Screen::Clock;  return true; }
        return false;
    };
    if (screensCfg.length()) {
        for (const String& id : SplitList(screensCfg, true)) {
            Screen s;
            if (idToScreen(id, s)) enabledOrder.push_back(s);
        }
    }
    if (enabledOrder.empty()) {
        for (int i = 0; i < (int)Screen::COUNT; ++i) enabledOrder.push_back((Screen)i);
    }

    // Optional device location: solar auto-dim now; ISS passes / local aurora in a later stage.
    const String latStr = configServer.GetStoredString("latitude");
    const String lonStr = configServer.GetStoredString("longitude");
    hasLatLon = latStr.length() && lonStr.length();
    deviceLat = latStr.toDouble();
    deviceLon = lonStr.toDouble();

    currentBrightness = configuredBrightness;
    tft.setBrightness(currentBrightness);
    lastBrightnessCheck = 0;

    Serial.printf("[space] init; backend=%s screens=%u latlon=%d\n",
                  backendBaseUrl.isEmpty() ? "(direct)" : backendBaseUrl.c_str(),
                  (unsigned)enabledOrder.size(), (int)hasLatLon);
}

void SpaceManager::Update()
{
    // Stage 2 will Poll() the SpaceFeedClient here and check ntfy alert triggers.
    UpdateBrightness();
    HandleTouch();
    AutoRotate();
}

void SpaceManager::Draw(BandCanvas& backbuffer, bool /*firstPass*/)
{
    std::vector<Screen> rot = BuildRotation();
    // Keep `current` valid against the live rotation set (data can come and go).
    bool inRot = false;
    for (Screen s : rot) if (s == current) { inRot = true; break; }
    if (!inRot && !rot.empty()) current = rot.front();

    switch (current) {
        case Screen::Splash: DrawSplash(backbuffer); break;
        case Screen::Clock:
        default:             DrawClock(backbuffer); break;
    }

    DrawScreenDots(backbuffer, rot);
}

bool SpaceManager::HasData(Screen s) const
{
    switch (s) {
        case Screen::Splash: return true; // static welcome card until the feeds populate
        case Screen::Clock:  return true; // always-available idle screen
        default:             return false;
    }
}

std::vector<SpaceManager::Screen> SpaceManager::BuildRotation() const
{
    std::vector<Screen> rot;
    for (Screen s : enabledOrder)
        if (HasData(s)) rot.push_back(s);
    if (rot.empty()) rot.push_back(Screen::Clock); // always have the idle clock
    return rot;
}

void SpaceManager::AdvanceRotation(int dir)
{
    std::vector<Screen> rot = BuildRotation();
    int idx = 0;
    for (int i = 0; i < (int)rot.size(); ++i) if (rot[i] == current) { idx = i; break; }
    idx = (idx + dir + (int)rot.size()) % (int)rot.size();
    current = rot[idx];
    lastAdvanceMs = millis();
}

void SpaceManager::AutoRotate()
{
    if (millis() - lastInteractionMs < INTERACT_HOLD_MS) return; // user is driving
    if (millis() - lastAdvanceMs < AUTO_DWELL_MS) return;
    AdvanceRotation(+1);
}

void SpaceManager::HandleTouch()
{
    // Serialize the touch I2C read against any network use of the shared bus -- the same pattern
    // the radar / EAM use (inert on dual-core S3, where TryAcquireBus is uncontended).
    if (!http.TryAcquireBus()) return;
    int32_t tx = 0, ty = 0;
    const bool touched = tft.getTouch(&tx, &ty);
    http.ReleaseBus();

    const unsigned long now = millis();
    if (touched) {
        if (!wasTouched) { wasTouched = true; touchStartX = tx; touchStartY = ty; }
        touchLastX = tx;
        touchLastY = ty;
        lastInteractionMs = now;
        return;
    }
    if (!wasTouched) return;
    wasTouched = false;
    lastInteractionMs = now;

    const int dx = touchLastX - touchStartX;
    const int dy = touchLastY - touchStartY;
    if (abs(dx) < 40 && abs(dy) < 40) return; // tap: just holds auto-rotate (handled above)
    if (abs(dx) >= abs(dy))
        AdvanceRotation(dx < 0 ? +1 : -1);    // swipe left -> next, right -> prev
}

void SpaceManager::UpdateBrightness()
{
    if (lastBrightnessCheck != 0 && millis() - lastBrightnessCheck < 20000) return;
    lastBrightnessCheck = millis();

    bool night = false;
    if (autoDim && hasLatLon) {
        const time_t utc = time(nullptr);
        if (utc > 1600000000) // NTP synced
            night = SunElevationDeg(deviceLat, deviceLon, utc) < -0.833f;
    }
    nightDim = night;

    uint8_t target = configuredBrightness;
    if (night) {
        target = configuredBrightness / 5;
        if (target < 10) target = 10;
    }
    if (target != currentBrightness) {
        currentBrightness = target;
        tft.setBrightness(target);
    }
}

void SpaceManager::DrawScreenDots(BandCanvas& c, const std::vector<Screen>& rot) const
{
    const int n = (int)rot.size();
    if (n <= 1) return;
    int activeIdx = 0;
    for (int i = 0; i < n; ++i) if (rot[i] == current) { activeIdx = i; break; }

    const int gap = 9;
    const int totalW = (n - 1) * gap;
    int x = SCREEN_SIZE_DIV_2 - totalW / 2;
    const int y = SCREEN_SIZE - 14;
    for (int i = 0; i < n; ++i) {
        c.fillCircle(x, y, i == activeIdx ? 2 : 1, i == activeIdx ? palette.fg : palette.faint);
        x += gap;
    }
}

// ----------------------------------------------------------------------------- screens

void SpaceManager::DrawSplash(BandCanvas& c)
{
    const uint32_t fg     = space::ScaleColor(palette.fg, GlowFactor());
    const uint32_t accent = space::ScaleColor(palette.accent, GlowFactor());
    const uint32_t faint  = space::ScaleColor(palette.faint, GlowFactor());

    // A few fixed "stars" so the card reads as space, deterministic so the banded passes match.
    static const struct { int x, y, r; } stars[] = {
        {54, 70, 1}, {120, 44, 1}, {210, 58, 2}, {300, 40, 1}, {360, 96, 1},
        {40, 180, 1}, {380, 210, 2}, {70, 300, 1}, {150, 360, 1}, {260, 350, 2},
        {340, 320, 1}, {200, 110, 1}, {90, 130, 1}, {320, 160, 1}, {180, 300, 1},
    };
    for (const auto& s : stars)
        c.fillCircle(s.x, s.y, s.r, s.r > 1 ? accent : faint);

    c.setTextSize(4);
    CenterText(c, "SPACESCOPE", SCREEN_SIZE_DIV_2 - 40, fg);
    c.setTextSize(1);
    CenterText(c, "a desk window into everything above you", SCREEN_SIZE_DIV_2 + 4, faint);
    c.setTextSize(2);
    CenterText(c, "awaiting feeds...", SCREEN_SIZE_DIV_2 + 44, accent);
}

void SpaceManager::DrawClock(BandCanvas& c)
{
    const uint32_t fg    = space::ScaleColor(palette.fg, GlowFactor());
    const uint32_t faint = space::ScaleColor(palette.faint, GlowFactor());

    const time_t utc = time(nullptr);
    char hhmmss[16] = "--:--:--";
    if (utc > 1600000000) { // NTP synced
        struct tm t;
        gmtime_r(&utc, &t);
        snprintf(hhmmss, sizeof(hhmmss), "%02d:%02d:%02d", t.tm_hour, t.tm_min, t.tm_sec);
    }

    c.setTextSize(1);
    CenterText(c, "UTC", SCREEN_SIZE_DIV_2 - 46, faint);
    c.setTextSize(5);
    CenterText(c, hhmmss, SCREEN_SIZE_DIV_2 - 28, fg);
}

// ----------------------------------------------------------------------------- helpers

std::vector<String> SpaceManager::SplitList(const String& s, bool lower)
{
    std::vector<String> out;
    int start = 0;
    const int n = (int)s.length();
    for (int i = 0; i <= n; ++i) {
        const bool sep = (i == n) || s[i] == ',' || s[i] == ' ' || s[i] == '\t' ||
                         s[i] == '\r' || s[i] == '\n' || s[i] == ';';
        if (sep) {
            if (i > start) {
                String tok = s.substring(start, i);
                tok.trim();
                if (lower) tok.toLowerCase();
                if (tok.length()) out.push_back(tok);
            }
            start = i + 1;
        }
    }
    return out;
}

void SpaceManager::CenterText(BandCanvas& c, const String& s, int y, uint32_t color)
{
    c.setTextColor(color);
    c.drawString(s, SCREEN_SIZE_DIV_2 - c.textWidth(s) / 2, y);
}
