#include "AnglerManager.h"

#include <math.h>
#include <stdio.h>
#include <time.h>

#include "Layout.h"
#include "Board.h"          // board::BuzzerChirp + variant::HAS_AUDIO
#include "astro/Astro.h"    // SunAltDeg for the night auto-dim

namespace {

constexpr unsigned long AUTO_DWELL_MS    = 8000;   // ms per screen when auto-rotating
constexpr unsigned long INTERACT_HOLD_MS = 30000;  // pause auto-rotate this long after a touch
constexpr long          NTP_FLOOR        = 1600000000; // clock is real once past this epoch
constexpr float         BARO_FALL        = -1.0f;  // hPa/h: a fast drop worth alerting on
constexpr float         BARO_CLEAR       = -0.4f;  // hPa/h: drop eased -> re-arm the baro alert

} // namespace

void AnglerManager::Initialise()
{
    palette = angler::PaletteDefault();

    const String latStr = configServer.GetStoredString("latitude");
    const String lonStr = configServer.GetStoredString("longitude");
    hasLatLon = latStr.length() && lonStr.length();
    deviceLat = latStr.toDouble();
    deviceLon = lonStr.toDouble();

    // Local offset for bite windows / rise-set / clock. Default to the nominal zone from longitude
    // (matches the config page default); the user can set the exact offset incl. DST.
    const String tzStr = configServer.GetStoredString("tz-offset");
    const double tzHours = tzStr.length() ? tzStr.toDouble() : round(deviceLon / 15.0);
    tzOffsetSec = (long)lround(tzHours * 3600.0);

    tideStation = configServer.GetStoredString("ang-tide-station");
    tideStation.trim();
    String buoyId = configServer.GetStoredString("ang-buoy");
    buoyId.trim();
    const String units = configServer.GetStoredString("ang-units");
    imperial = units.isEmpty() || units == "imperial";   // default to imperial (US-first)

    // Screen enable/order. "ang-screens" is a CSV of screen ids in display order; empty = all.
    enabledOrder.clear();
    auto idToScreen = [](const String& id, Screen& out) -> bool {
        if (id == "bite")      { out = Screen::Bite;      return true; }
        if (id == "tides")     { out = Screen::Tides;     return true; }
        if (id == "barometer") { out = Screen::Barometer; return true; }
        if (id == "wind")      { out = Screen::Wind;      return true; }
        if (id == "water")     { out = Screen::Water;     return true; }
        if (id == "moon")      { out = Screen::Moon;      return true; }
        if (id == "sun")       { out = Screen::Sun;       return true; }
        if (id == "catchlog")  { out = Screen::CatchLog;  return true; }
        if (id == "splash")    { out = Screen::Splash;    return true; }
        if (id == "clock")     { out = Screen::Clock;     return true; }
        return false;
    };
    const String screensCfg = configServer.GetStoredString("ang-screens");
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
    alertBite    = boolCfg("ang-alert-bite", true);
    alertBaro    = boolCfg("ang-alert-baro", false);
    alertTide    = boolCfg("ang-alert-tide", false);
    chimeOnAlert = boolCfg("ang-chime", true);

    // Stage-2 live feeds (tides / weather / marine) + the persistent catch log.
    AnglerFeedClient::Config fc;
    fc.hasLatLon = hasLatLon;
    fc.lat = deviceLat;
    fc.lon = deviceLon;
    fc.tideStation = tideStation;
    fc.buoy = buoyId;
    fc.imperial = imperial;
    fc.intervalScale = 1.0f;
    feed.Begin();
    feed.Configure(fc);
    logbook.Begin();

    currentBrightness = configuredBrightness;
    tft.setBrightness(currentBrightness);
    lastBrightnessCheck = 0;

    // Force a solunar recompute on the next Update; don't re-alert an event already active (a config
    // save shouldn't fire), which alertSeeded=false arranges the first time CheckAlerts runs.
    solunarValid = false;
    lastSolunarCalcMs = 0;
    alertSeeded = false;
    baroAlerted = false;
    lastAlertedTide = 0;

    Serial.printf("[angler] init; latlon=%d lat=%.4f lon=%.4f tz=%+.1fh units=%s station=%s buoy=%s screens=%u\n",
                  (int)hasLatLon, deviceLat, deviceLon, tzOffsetSec / 3600.0,
                  imperial ? "imperial" : "metric",
                  tideStation.isEmpty() ? "(none)" : tideStation.c_str(),
                  buoyId.isEmpty() ? "(none)" : buoyId.c_str(), (unsigned)enabledOrder.size());

    static bool selfChecked = false;   // once per boot, not on every config-save re-init
    if (!selfChecked) { selfChecked = true; SelfCheck(); }
}

void AnglerManager::Update()
{
    board::BuzzerUpdate();        // ends a chirp when its time is up (no-op without audio)
    feed.Poll();                  // apply a ready feed result, dispatch the next due fetch
    RecomputeSolunar(false);
    CheckAlerts();
    UpdateBrightness();
    HandleTouch();
    AutoRotate();
}

void AnglerManager::Draw(BandCanvas& backbuffer, bool /*firstPass*/)
{
    std::vector<Screen> rot = BuildRotation();
    bool inRot = false;
    for (Screen s : rot) if (s == current) { inRot = true; break; }
    if (!inRot && !rot.empty()) { current = rot.front(); inDetail = false; }

    switch (current) {
        case Screen::Bite:      DrawBite(backbuffer);      break;
        case Screen::Tides:     DrawTides(backbuffer);     break;
        case Screen::Barometer: DrawBarometer(backbuffer); break;
        case Screen::Wind:      DrawWind(backbuffer);      break;
        case Screen::Water:     DrawWater(backbuffer);     break;
        case Screen::Moon:      DrawMoon(backbuffer);      break;
        case Screen::Sun:       DrawSun(backbuffer);       break;
        case Screen::CatchLog:  DrawCatchLog(backbuffer);  break;
        case Screen::Splash:    DrawSplash(backbuffer);    break;
        case Screen::Clock:
        default:                DrawClock(backbuffer);     break;
    }

    if (inDetail) DrawDetailCard(backbuffer);
    else DrawScreenDots(backbuffer, rot);
}

// ----------------------------------------------------------------------------- rotation

bool AnglerManager::HasData(Screen s) const
{
    switch (s) {
        case Screen::Bite:
        case Screen::Moon:
        case Screen::Sun:       return hasLatLon && solunarValid;   // need a location + a real clock
        case Screen::Tides:     return !feed.Tide().events.empty();
        case Screen::Barometer:
        case Screen::Wind:      return feed.Weather().valid;
        case Screen::Water:     return feed.HaveWaterTemp()
                                       || (feed.Buoy().valid && (feed.Buoy().haveWaterTemp || feed.Buoy().haveWave))
                                       || (feed.Marine().valid && (feed.Marine().haveWave || feed.Marine().haveSst));
        case Screen::CatchLog:  return TimeReady();                 // needs a real clock to timestamp
        case Screen::Splash:    return !(hasLatLon && solunarValid); // cold-start prompt, drops out once ready
        case Screen::Clock:     return true;
        default:                return false;
    }
}

std::vector<AnglerManager::Screen> AnglerManager::BuildRotation() const
{
    std::vector<Screen> rot;
    for (Screen s : enabledOrder)
        if (HasData(s)) rot.push_back(s);
    if (rot.empty()) rot.push_back(Screen::Clock);
    return rot;
}

void AnglerManager::AdvanceRotation(int dir)
{
    std::vector<Screen> rot = BuildRotation();
    int idx = 0;
    for (int i = 0; i < (int)rot.size(); ++i) if (rot[i] == current) { idx = i; break; }
    idx = (idx + dir + (int)rot.size()) % (int)rot.size();
    current = rot[idx];
    lastAdvanceMs = millis();
}

void AnglerManager::AutoRotate()
{
    if (inDetail) return;                                        // hold while inspecting
    if (millis() - lastInteractionMs < INTERACT_HOLD_MS) return; // user is driving
    if (millis() - lastAdvanceMs < AUTO_DWELL_MS) return;
    AdvanceRotation(+1);
}

// ----------------------------------------------------------------------------- input

void AnglerManager::HandleTouch()
{
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

    if (abs(dx) < 40 && abs(dy) < 40) { HandleTap(touchLastX, touchLastY); return; }

    // a horizontal swipe navigates (and closes the detail card first)
    if (abs(dx) >= abs(dy)) {
        if (inDetail) { ExitDetail(); return; }
        AdvanceRotation(dx < 0 ? +1 : -1);
    }
}

void AnglerManager::HandleTap(int tx, int ty)
{
    if (inDetail) { ExitDetail(); return; }

    if (current == Screen::Bite) {
        selectedPeriod = PeriodHitTest(tx, ty);  // a period marker, or -1 for the day-summary card
        inDetail = true;
        return;
    }
    if (current == Screen::CatchLog) {
        // Tap logs a catch, tagged with whether a solunar feeding window is active right now.
        const time_t now = time(nullptr);
        angler::Period p;
        const bool during = ActiveNow(now, p);
        logbook.RecordCatch((long)now, during);
        if (chimeOnAlert && variant::HAS_AUDIO) board::BuzzerChirp(90);   // tactile confirmation
        return;
    }
    // Tides / Barometer / Wind / Water / Moon / Sun -> a tap opens the detail card.
    if (current != Screen::Clock && current != Screen::Splash) inDetail = true;
}

// ----------------------------------------------------------------------------- solunar cache

void AnglerManager::RecomputeSolunar(bool force)
{
    if (!hasLatLon) { solunarValid = false; return; }
    const time_t now = time(nullptr);
    if (now < NTP_FLOOR) { solunarValid = false; return; }   // wait for NTP

    if (!force && solunarValid && lastSolunarCalcMs != 0 && millis() - lastSolunarCalcMs < 60000) {
        // still refresh immediately on a local-day roll-over so "today" never goes stale
        const long ls = (long)now + tzOffsetSec;
        const long ds = (ls / 86400) * 86400 - tzOffsetSec;
        if (ds == solunarDayStart) return;
    }
    lastSolunarCalcMs = millis();

    today    = angler::ComputeDay(now, deviceLat, deviceLon, tzOffsetSec);
    tomorrow = angler::ComputeDay(now + 86400, deviceLat, deviceLon, tzOffsetSec);
    solunarDayStart = today.dayStart;
    solunarValid = today.valid;
}

void AnglerManager::SelfCheck()
{
    // One-shot self-check against the validated reference (USNO + a published solunar table):
    // NYC 2026-06-01 EDT should give majors 01:33 & 13:58 (2 h, start-at-transit), minors 05:52 &
    // 22:05 (allow a couple minutes for the low-precision ephemeris / geometric horizon). Confirms
    // the on-device math on real hardware. Uses mktime for the reference epoch (TZ=UTC from
    // configTime(0,0)), so it needs neither NTP nor a device location.
    struct tm t = {};
    t.tm_year = 2026 - 1900; t.tm_mon = 5; t.tm_mday = 1; t.tm_hour = 16; // 16:00 UTC = 12:00 EDT
    const time_t ref = mktime(&t);
    const long edt = -4L * 3600;
    angler::SolunarDay ny = angler::ComputeDay(ref, 40.7128, -74.0060, edt);
    Serial.println("[angler] solunar self-check NYC 2026-06-01 (expect major 01:33 & 13:58, minor ~05:52 & ~22:05 EDT):");
    for (int i = 0; i < ny.count; ++i)
        Serial.printf("    %-5s %-9s  event %s  [%s - %s]\n",
                      angler::PeriodShort(ny.periods[i].kind), angler::PeriodLabel(ny.periods[i].kind),
                      HM(ny.periods[i].center, edt).c_str(),
                      HM(ny.periods[i].start, edt).c_str(), HM(ny.periods[i].end, edt).c_str());
    Serial.printf("    rating=%s illum=%.0f%% sunrise %s sunset %s\n",
                  angler::RatingLabel(ny.rating), ny.moonIllum * 100.0,
                  HM(ny.sunrise, edt).c_str(), HM(ny.sunset, edt).c_str());
}

// ----------------------------------------------------------------------------- period search

bool AnglerManager::NextPeriod(time_t nowUtc, angler::Period& out, bool majorOnly) const
{
    bool found = false;
    time_t bestStart = 0;
    auto scan = [&](const angler::SolunarDay& d) {
        for (int i = 0; i < d.count; ++i) {
            const angler::Period& p = d.periods[i];
            if (majorOnly && !p.major()) continue;
            if (p.start > nowUtc && (!found || p.start < bestStart)) { found = true; bestStart = p.start; out = p; }
        }
    };
    scan(today);
    scan(tomorrow);
    return found;
}

bool AnglerManager::ActiveNow(time_t nowUtc, angler::Period& out) const
{
    auto scan = [&](const angler::SolunarDay& d) -> bool {
        for (int i = 0; i < d.count; ++i) {
            const angler::Period& p = d.periods[i];
            if (p.start <= nowUtc && nowUtc < p.end) { out = p; return true; }
        }
        return false;
    };
    return scan(today) || scan(tomorrow);
}

// ----------------------------------------------------------------------------- ring geometry

int AnglerManager::BiteRingRadius() { return SCREEN_SIZE_DIV_2 - 46; }

// 24-hour dial: midnight at the top (12 o'clock), noon at the bottom, advancing clockwise.
std::pair<int, int> AnglerManager::RingXY(long sodLocal, int radius)
{
    const double theta = (double)sodLocal / 86400.0 * 2.0 * M_PI;
    const int cx = SCREEN_SIZE_DIV_2, cy = SCREEN_SIZE_DIV_2;
    return { cx + (int)lround(radius * sin(theta)), cy - (int)lround(radius * cos(theta)) };
}

std::pair<int, int> AnglerManager::PeriodMarkerXY(const angler::Period& p) const
{
    return RingXY(LocalSod(p.center), BiteRingRadius());
}

int AnglerManager::PeriodHitTest(int tx, int ty) const
{
    int best = -1;
    long bestD2 = 30 * 30;   // tap tolerance
    for (int i = 0; i < today.count; ++i) {
        const auto pos = PeriodMarkerXY(today.periods[i]);
        const long ddx = pos.first - tx, ddy = pos.second - ty;
        const long d2 = ddx * ddx + ddy * ddy;
        if (d2 < bestD2) { bestD2 = d2; best = i; }
    }
    return best;
}

// ----------------------------------------------------------------------------- alerts

void AnglerManager::CheckAlerts()
{
    if (!TimeReady()) return;
    const time_t now = time(nullptr);

    // --- gather each alert's current condition (guarded by its own data availability) ---
    // 1) an active MAJOR feeding window (solunar)
    const angler::Period* bite = nullptr;
    if (solunarValid) {
        auto scan = [&](const angler::SolunarDay& d) {
            for (int i = 0; i < d.count; ++i)
                if (d.periods[i].major() && d.periods[i].start <= now && now < d.periods[i].end) bite = &d.periods[i];
        };
        scan(today);
        scan(tomorrow);
    }
    // 2) the barometer falling fast (fish feed ahead of a front)
    const BaroTrend bt = ComputeBaro();
    const bool baroFalling = bt.valid && bt.rateHpaPerH <= BARO_FALL;
    const bool baroEased   = bt.valid && bt.rateHpaPerH >  BARO_CLEAR;
    // 3) a hi/lo tide within ~30 min
    time_t nextTide = 0;
    bool nextTideHigh = false;
    for (const angler::TideEvent& e : feed.Tide().events)
        if (e.t > now && (nextTide == 0 || e.t < nextTide)) { nextTide = e.t; nextTideHigh = e.high; }
    const bool tideSoon = nextTide != 0 && (nextTide - now) <= 1800;

    // --- seed once: adopt the current state so an event already active at boot never fires ---
    if (!alertSeeded) {
        lastAlertedBiteCenter = bite ? bite->center : 0;
        baroAlerted = baroFalling;
        lastAlertedTide = tideSoon ? nextTide : 0;
        alertSeeded = true;
        return;
    }

    auto chime = [&](uint16_t ms) { if (chimeOnAlert && variant::HAS_AUDIO) board::BuzzerChirp(ms); };

    // a major bite window opened
    if (bite && bite->center != lastAlertedBiteCenter) {
        lastAlertedBiteCenter = bite->center;
        chime(140);
        if (alertBite)
            SendNtfy("Bite window open",
                     String("Major feeding (") + angler::PeriodLabel(bite->kind) + ") until " + LocalHM(bite->end),
                     "fish,fishing_pole_and_fish", 4);
    }
    // the barometer started falling fast
    if (baroFalling && !baroAlerted) {
        baroAlerted = true;
        chime(140);
        if (alertBaro) {
            char b[72];
            snprintf(b, sizeof(b), "Falling %.1f hPa/h - fish feed ahead of a front", bt.rateHpaPerH);
            SendNtfy("Barometer dropping", b, "chart_with_downwards_trend,fish", 4);
        }
    } else if (baroEased) {
        baroAlerted = false;   // re-arm once the drop eases
    }
    // a tide turn is ~30 min out
    if (tideSoon && nextTide != lastAlertedTide) {
        lastAlertedTide = nextTide;
        chime(120);
        if (alertTide)
            SendNtfy(nextTideHigh ? "High tide soon" : "Low tide soon",
                     String(nextTideHigh ? "High" : "Low") + " tide near " + LocalHM(nextTide), "ocean,fish", 3);
    }
}

AnglerManager::BaroTrend AnglerManager::ComputeBaro() const
{
    BaroTrend b;
    const angler::WeatherData& w = feed.Weather();
    if (!w.valid || w.pressHist.size() < 2) return b;

    b.nowHpa = w.pressureHpa > 0 ? w.pressureHpa : w.pressHist.back().second;
    const time_t nowE = w.currentEpoch > 0 ? w.currentEpoch : w.pressHist.back().first;
    const time_t target = nowE - 6 * 3600;   // compare against ~6 h ago

    time_t chosenT = w.pressHist.front().first;
    float chosenP = w.pressHist.front().second;
    long best = 0x7fffffffL;
    for (const auto& s : w.pressHist) {
        long dt = (long)s.first - (long)target;
        if (dt < 0) dt = -dt;
        if (dt < best) { best = dt; chosenT = s.first; chosenP = s.second; }
    }
    const float hours = (float)(nowE - chosenT) / 3600.0f;
    if (hours < 1.0f) return b;   // not enough span yet to state a rate
    b.pastHpa = chosenP;
    b.rateHpaPerH = (b.nowHpa - chosenP) / hours;
    b.valid = true;
    return b;
}

const char* AnglerManager::CompassPoint(int deg)
{
    static const char* pts[] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
    int i = ((deg % 360 + 360) % 360 + 22) / 45;
    return pts[i & 7];
}

void AnglerManager::SendNtfy(const String& title, const String& body, const String& tags, int priority)
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

void AnglerManager::UpdateBrightness()
{
    if (lastBrightnessCheck != 0 && millis() - lastBrightnessCheck < 20000) return;
    lastBrightnessCheck = millis();

    bool night = false;
    if (autoDim && hasLatLon) {
        const time_t utc = time(nullptr);
        if (utc > NTP_FLOOR)
            night = space::astro::SunAltDeg(utc, deviceLat, deviceLon) < -0.833;
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

std::vector<String> AnglerManager::SplitList(const String& s, bool lower)
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

void AnglerManager::CenterText(BandCanvas& c, const String& s, int y, uint32_t color)
{
    c.setTextColor(color);
    c.drawString(s, SCREEN_SIZE_DIV_2 - c.textWidth(s) / 2, y);
}

String AnglerManager::HM(time_t utc, long tzSec)
{
    if (utc == 0) return String("--:--");
    const time_t local = utc + tzSec;
    struct tm t;
    gmtime_r(&local, &t);   // gmtime of the shifted epoch == local wall clock
    char b[8];
    snprintf(b, sizeof(b), "%02d:%02d", t.tm_hour, t.tm_min);
    return String(b);
}

String AnglerManager::LocalHM(time_t utc) const { return HM(utc, tzOffsetSec); }
