#include "BirdingManager.h"
#include "TouchPoll.h"
#include "SolarDim.h"

#include <math.h>
#include <stdio.h>
#include <time.h>

#include "Layout.h"

namespace {

constexpr unsigned long AUTO_DWELL_MS    = 8000;   // ms per screen when auto-rotating
constexpr unsigned long INTERACT_HOLD_MS = 30000;  // pause auto-rotate this long after a touch


} // namespace

void BirdingManager::Initialise()
{
    palette = birding::PaletteDefault();

    const String key = configServer.GetStoredString("ebird-key");
    hasKey = key.length() > 0;

    const String latStr = configServer.GetStoredString("latitude");
    const String lonStr = configServer.GetStoredString("longitude");
    hasLatLon = latStr.length() && lonStr.length();
    deviceLat = latStr.toDouble();
    deviceLon = lonStr.toDouble();

    auto intCfg = [&](const char* k, int def) {
        const String v = configServer.GetStoredString(k);
        return v.isEmpty() ? def : (int)v.toInt();
    };
    radiusKm = intCfg("bd-radius-km", 25);
    backDays = intCfg("bd-back-days", 7);

    targets = SplitList(configServer.GetStoredString("bd-targets"), true);

    // Screen enable/order. "bd-screens" is a CSV of screen ids in display order; empty = all.
    enabledOrder.clear();
    auto idToScreen = [](const String& id, Screen& out) -> bool {
        if (id == "radar")   { out = Screen::Radar;   return true; }
        if (id == "notable") { out = Screen::Notable; return true; }
        if (id == "bigday")  { out = Screen::BigDay;  return true; }
        if (id == "hotspot") { out = Screen::Hotspot; return true; }
        if (id == "targets") { out = Screen::Targets; return true; }
        if (id == "splash")  { out = Screen::Splash;  return true; }
        if (id == "clock")   { out = Screen::Clock;   return true; }
        return false;
    };
    const String screensCfg = configServer.GetStoredString("bd-screens");
    if (screensCfg.length()) {
        for (const String& id : SplitList(screensCfg, true)) {
            Screen s;
            if (idToScreen(id, s)) enabledOrder.push_back(s);
        }
    }
    if (enabledOrder.empty())
        for (int i = 0; i < (int)Screen::COUNT; ++i) enabledOrder.push_back((Screen)i);

    const String br = configServer.GetStoredString("brightness");
    configuredBrightness = br.isEmpty() ? 255 : (uint8_t)constrain(br.toInt(), 10, 255);
    const String ad = configServer.GetStoredString("autodim");
    autoDim = ad.isEmpty() || ad == "true";

    ntfyTopic = configServer.GetStoredString("ntfy-topic");
    auto boolCfg = [&](const char* k, bool def) {
        const String v = configServer.GetStoredString(k);
        return v.isEmpty() ? def : (v == "true");
    };
    alertNotable = boolCfg("bd-alert-rare", true);
    alertTarget = boolCfg("bd-alert-target", true);

    BirdingFeedClient::Config fcfg;
    fcfg.apiKey = key;
    fcfg.hasLatLon = hasLatLon;
    fcfg.lat = deviceLat;
    fcfg.lon = deviceLon;
    fcfg.radiusKm = radiusKm;
    fcfg.backDays = backDays;
    fcfg.maxResults = 80;
    fcfg.intervalScale = 1.0f;
    feed.Begin();
    feed.Configure(fcfg);

    currentBrightness = configuredBrightness;
    tft.setBrightness(currentBrightness);
    lastBrightnessCheck = 0;

    // A re-init (config save) shouldn't re-fire alerts for sightings already on screen.
    notableSeeded = false;
    recentSeeded = false;
    seenNotable.clear();
    seenTarget.clear();

    Serial.printf("[birding] init; key=%d latlon=%d radius=%dkm back=%dd targets=%u\n",
                  (int)hasKey, (int)hasLatLon, radiusKm, backDays, (unsigned)targets.size());
}

void BirdingManager::Update()
{
    feed.Poll();
    CheckAlerts();
    UpdateBrightness();
    HandleTouch();
    AutoRotate();
}

void BirdingManager::Draw(BandCanvas& backbuffer, bool /*firstPass*/)
{
    std::vector<Screen> rot = BuildRotation();
    bool inRot = false;
    for (Screen s : rot) if (s == current) { inRot = true; break; }
    if (!inRot && !rot.empty()) current = rot.front();

    switch (current) {
        case Screen::Radar:   DrawRadar(backbuffer); break;
        case Screen::Notable: DrawNotable(backbuffer); break;
        case Screen::BigDay:  DrawBigDay(backbuffer); break;
        case Screen::Hotspot: DrawHotspot(backbuffer); break;
        case Screen::Targets: DrawTargets(backbuffer); break;
        case Screen::Splash:  DrawSplash(backbuffer); break;
        case Screen::Clock:
        default:              DrawClock(backbuffer); break;
    }

    if (inDetail && selectedValid) DrawDetailCard(backbuffer);
    else DrawScreenDots(backbuffer, rot);
}

// ----------------------------------------------------------------------------- rotation

bool BirdingManager::HasData(Screen s) const
{
    switch (s) {
        case Screen::Radar:   return hasKey && hasLatLon && !feed.Recent().empty();
        case Screen::Notable: return !feed.Notable().empty();
        case Screen::BigDay:  return !feed.Recent().empty();
        case Screen::Hotspot: return !feed.Hotspots().empty();
        case Screen::Targets: return !targets.empty();   // shown whenever targets are configured
        case Screen::Splash:  return !feed.HasAny();      // cold-start welcome, drops out once data lands
        case Screen::Clock:   return true;
        default:              return false;
    }
}

std::vector<BirdingManager::Screen> BirdingManager::BuildRotation() const
{
    std::vector<Screen> rot;
    for (Screen s : enabledOrder)
        if (HasData(s)) rot.push_back(s);
    if (rot.empty()) rot.push_back(Screen::Clock);
    return rot;
}

void BirdingManager::AdvanceRotation(int dir)
{
    std::vector<Screen> rot = BuildRotation();
    int idx = 0;
    for (int i = 0; i < (int)rot.size(); ++i) if (rot[i] == current) { idx = i; break; }
    idx = (idx + dir + (int)rot.size()) % (int)rot.size();
    current = rot[idx];
    notableScroll = 0;
    lastAdvanceMs = millis();
}

void BirdingManager::AutoRotate()
{
    if (inDetail) return;                                            // hold while inspecting
    if (millis() - lastInteractionMs < INTERACT_HOLD_MS) return;     // user is driving
    if (millis() - lastAdvanceMs < AUTO_DWELL_MS) return;
    AdvanceRotation(+1);
}

// ----------------------------------------------------------------------------- input

void BirdingManager::HandleTouch()
{
    // Variant-aware touch poll (TouchPoll.h): serialized against TLS only on the
    // single-core C3; ungated on dual-core S3, where gating on the HTTP mutex
    // (held for the whole of every fetch) would silently drop taps.
    int32_t tx = 0, ty = 0;
    const TouchPoll poll = ReadTouch(tft, http, tx, ty);
    if (poll == TouchPoll::Skipped) return; // C3 only: request mid-flight
    const bool touched = (poll == TouchPoll::Touched);

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

    if (abs(dx) < 40 && abs(dy) < 40) { HandleTap(touchLastX, touchLastY); return; }

    if (abs(dx) >= abs(dy)) {
        if (inDetail) { ExitDetail(); return; }
        AdvanceRotation(dx < 0 ? +1 : -1);
    } else if (current == Screen::Notable && !inDetail) {
        notableScroll += (dy < 0 ? 3 : -3);
        if (notableScroll < 0) notableScroll = 0;
    }
}

void BirdingManager::HandleTap(int tx, int ty)
{
    if (inDetail) { ExitDetail(); return; }

    if (current == Screen::Radar && hasKey && hasLatLon) {
        const int hit = HitTestSighting(tx, ty, feed.Recent());
        if (hit >= 0) {
            selected = feed.Recent()[hit];
            selected.distanceKm = birding::DistanceKm(deviceLat, deviceLon, selected.lat, selected.lon);
            selected.bearingDeg = birding::BearingDeg(deviceLat, deviceLon, selected.lat, selected.lon);
            selectedValid = true; inDetail = true;
        }
        return;
    }

    if (current == Screen::Notable) {
        const int LIST_TOP = 64, LIST_ROW_H = 40;
        const int row = (ty - LIST_TOP) / LIST_ROW_H;
        if (row < 0) return;
        const int idx = notableScroll + row;
        if (idx >= 0 && idx < (int)feed.Notable().size()) {
            selected = feed.Notable()[idx];
            if (hasLatLon) {
                selected.distanceKm = birding::DistanceKm(deviceLat, deviceLon, selected.lat, selected.lon);
                selected.bearingDeg = birding::BearingDeg(deviceLat, deviceLon, selected.lat, selected.lon);
            }
            selectedValid = true; inDetail = true;
        }
    }
}

// ----------------------------------------------------------------------------- alerts

bool BirdingManager::MatchesTarget(const birding::Sighting& s) const
{
    if (targets.empty()) return false;
    String name = s.comName; name.toLowerCase();
    String code = s.speciesCode; code.toLowerCase();
    for (const String& t : targets)
        if (code == t || name.indexOf(t) >= 0) return true;
    return false;
}

void BirdingManager::CheckAlerts()
{
    // Bound the seen-sets so a long uptime can't grow them without limit. Dropping
    // a set drops its baseline too, so mark it for silent re-seeding below rather
    // than letting the whole current feed re-insert as "new" and fire.
    if (seenNotable.size() > 400) { seenNotable.clear(); notableSeeded = false; }
    if (seenTarget.size() > 400)  { seenTarget.clear(); notableSeeded = recentSeeded = false; }

    // Seed each feed's baseline silently on ITS first successful fetch (never alert
    // for the backlog present at boot / re-config). The fetches land staggered --
    // one in flight at a time -- so a single seeded-at-first-data flag would
    // baseline off whichever feed arrived first and let the other's week-old
    // backlog fire a push per species on every boot.
    if (!notableSeeded && feed.NotableFetched()) {
        for (const birding::Sighting& s : feed.Notable()) {
            seenNotable.insert(s.speciesCode);
            if (MatchesTarget(s)) seenTarget.insert(s.speciesCode);
        }
        notableSeeded = true;
    }
    if (!recentSeeded && feed.RecentFetched()) {
        for (const birding::Sighting& s : feed.Recent())
            if (MatchesTarget(s)) seenTarget.insert(s.speciesCode);
        recentSeeded = true;
    }

    // New notable species since we last looked.
    const birding::Sighting* newNotable = nullptr;
    for (const birding::Sighting& s : feed.Notable())
        if (seenNotable.insert(s.speciesCode).second && !newNotable) newNotable = &s;
    if (newNotable && alertNotable) {
        char body[96];
        const double d = hasLatLon ? birding::DistanceKm(deviceLat, deviceLon, newNotable->lat, newNotable->lon) : -1;
        if (d >= 0) snprintf(body, sizeof(body), "%.0f km - %s", d,
                             newNotable->locName.length() ? newNotable->locName.c_str() : "nearby");
        else        snprintf(body, sizeof(body), "%s",
                             newNotable->locName.length() ? newNotable->locName.c_str() : "nearby");
        SendNtfy("Notable: " + newNotable->comName, body, "bird,eyes", 4);
    }

    // New target species since we last looked (scan notable + recent).
    const birding::Sighting* newTarget = nullptr;
    auto scan = [&](const std::vector<birding::Sighting>& v) {
        for (const birding::Sighting& s : v)
            if (MatchesTarget(s) && seenTarget.insert(s.speciesCode).second && !newTarget) newTarget = &s;
    };
    scan(feed.Notable());
    scan(feed.Recent());
    if (newTarget && alertTarget) {
        char body[96];
        const double d = hasLatLon ? birding::DistanceKm(deviceLat, deviceLon, newTarget->lat, newTarget->lon) : -1;
        if (d >= 0) snprintf(body, sizeof(body), "%.0f km - %s", d,
                             newTarget->locName.length() ? newTarget->locName.c_str() : "nearby");
        else        snprintf(body, sizeof(body), "%s",
                             newTarget->locName.length() ? newTarget->locName.c_str() : "nearby");
        SendNtfy("Target bird: " + newTarget->comName, body, "dart,bird", 5);
    }
}

void BirdingManager::SendNtfy(const String& title, const String& body, const String& tags, int priority)
{
    if (ntfyTopic.isEmpty()) return;
    if (lastNotifyMs != 0 && millis() - lastNotifyMs < 5000) return;
    lastNotifyMs = millis();

    const std::vector<std::pair<String, String>> headers = {
        {"Title", title}, {"Tags", tags}, {"Priority", String(priority)}
    };
    (void)http.Post(String("https://ntfy.sh/") + ntfyTopic, body, headers);
}

// ----------------------------------------------------------------------------- brightness

void BirdingManager::UpdateBrightness()
{
    if (lastBrightnessCheck != 0 && millis() - lastBrightnessCheck < 20000) return;
    lastBrightnessCheck = millis();

    bool night = false;
    if (autoDim && hasLatLon) {
        const time_t utc = time(nullptr);
        if (utc > 1600000000)
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

// ----------------------------------------------------------------------------- helpers

std::pair<int, int> BirdingManager::ProjectSightingToScreen(const birding::Sighting& s) const
{
    const int cx = SCREEN_SIZE_DIV_2, cy = SCREEN_SIZE_DIV_2;
    const int R = SCREEN_SIZE_DIV_2 - 30;
    double d = s.distanceKm;
    double brg = s.bearingDeg;
    if (d <= 0) {
        d = birding::DistanceKm(deviceLat, deviceLon, s.lat, s.lon);
        brg = birding::BearingDeg(deviceLat, deviceLon, s.lat, s.lon);
    }
    double rr = (d / (double)radiusKm) * R;
    if (rr > R) rr = R;
    const double a = brg * M_PI / 180.0;
    return { cx + (int)lround(rr * sin(a)), cy - (int)lround(rr * cos(a)) };
}

int BirdingManager::HitTestSighting(int tx, int ty, const std::vector<birding::Sighting>& shown) const
{
    int best = -1;
    long bestD2 = 26 * 26;
    for (int i = 0; i < (int)shown.size(); ++i) {
        const auto p = ProjectSightingToScreen(shown[i]);
        const long ddx = p.first - tx, ddy = p.second - ty;
        const long d2 = ddx * ddx + ddy * ddy;
        if (d2 < bestD2) { bestD2 = d2; best = i; }
    }
    return best;
}

int BirdingManager::DistinctSpecies(const std::vector<birding::Sighting>& v) const
{
    std::set<String> seen;
    for (const birding::Sighting& s : v)
        seen.insert(s.speciesCode.length() ? s.speciesCode : s.comName);
    return (int)seen.size();
}

String BirdingManager::ShortDate(const String& obsDt)
{
    int Y = 0, Mo = 0, D = 0, h = 0, mi = 0;
    const int n = sscanf(obsDt.c_str(), "%d-%d-%d %d:%d", &Y, &Mo, &D, &h, &mi);
    if (n >= 3 && Mo >= 1 && Mo <= 12) {
        static const char* mon[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
        char b[24];
        if (n >= 5) snprintf(b, sizeof(b), "%s %d %02d:%02d", mon[Mo - 1], D, h, mi);
        else        snprintf(b, sizeof(b), "%s %d", mon[Mo - 1], D);
        return String(b);
    }
    return obsDt;
}

std::vector<String> BirdingManager::SplitList(const String& s, bool lower)
{
    std::vector<String> out;
    int start = 0;
    const int n = (int)s.length();
    for (int i = 0; i <= n; ++i) {
        const bool sep = (i == n) || s[i] == ',' || s[i] == ';';
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

void BirdingManager::CenterText(BandCanvas& c, const String& s, int y, uint32_t color)
{
    c.setTextColor(color);
    c.drawString(s, SCREEN_SIZE_DIV_2 - c.textWidth(s) / 2, y);
}
