#include "FishingManager.h"

#include <math.h>
#include <time.h>

#include "Layout.h"
#include "Board.h"   // board::BuzzerChirp (no-op unless the variant HAS_AUDIO)

namespace {

constexpr unsigned long AUTO_DWELL_MS    = 8000;   // ms per dial when auto-rotating
constexpr unsigned long INTERACT_HOLD_MS = 30000;  // pause auto-rotate this long after a touch
constexpr unsigned long SOLUNAR_RECALC_MS = 300000; // recompute sun/moon/windows every 5 min

double Deg2Rad(double d) { return d * M_PI / 180.0; }

// Low-precision solar elevation (deg) -- drives the same night auto-dim the other editions use.
// Copied from SeismicManager/SpaceManager so this app dims identically without a cross-TU dependency.
float SunElevationDeg(double latDeg, double lonDeg, time_t utc)
{
    const double n = (double)utc / 86400.0 - 10957.5;
    double L = fmod(280.460 + 0.9856474 * n, 360.0);
    double g = fmod(357.528 + 0.9856003 * n, 360.0);
    const double lambda = L + 1.915 * sin(Deg2Rad(g)) + 0.020 * sin(Deg2Rad(2 * g));
    const double eps = 23.439 - 0.0000004 * n;
    const double lam = Deg2Rad(lambda), e = Deg2Rad(eps);
    const double dec = asin(sin(e) * sin(lam));
    const double ra = atan2(cos(e) * sin(lam), cos(lam));
    double gmst = fmod(18.697374558 + 24.06570982441908 * n, 24.0);
    if (gmst < 0) gmst += 24.0;
    const double lstDeg = gmst * 15.0 + lonDeg;
    const double H = Deg2Rad(lstDeg - ra * 180.0 / M_PI);
    const double lat = Deg2Rad(latDeg);
    double sinEl = sin(lat) * sin(dec) + cos(lat) * cos(dec) * cos(H);
    if (sinEl > 1) sinEl = 1;
    if (sinEl < -1) sinEl = -1;
    return (float)(asin(sinEl) * 180.0 / M_PI);
}

} // namespace

void FishingManager::Initialise()
{
    palette = fishing::PaletteDefault();
    backendBaseUrl = configServer.GetStoredString("fi-base-url");

    const String wt = configServer.GetStoredString("fi-water");
    waterType = (wt == "fresh") ? FishingFeedClient::FRESH
              : (wt == "salt")  ? FishingFeedClient::SALT
                                : FishingFeedClient::BOTH;

    usgsSite    = configServer.GetStoredString("fi-usgs");
    noaaStation = configServer.GetStoredString("fi-noaa");
    buoyId      = configServer.GetStoredString("fi-buoy");

    const String units = configServer.GetStoredString("fi-units");
    imperial = (units != "metric");   // default imperial

    const String latStr = configServer.GetStoredString("latitude");
    const String lonStr = configServer.GetStoredString("longitude");
    hasLatLon = latStr.length() && lonStr.length();
    deviceLat = latStr.toDouble();
    deviceLon = lonStr.toDouble();

    const String tz = configServer.GetStoredString("fi-tz-offset");
    tzOffsetSec = tz.isEmpty() ? 0 : (long)lround(tz.toFloat() * 3600.0f);

    auto numOrNan = [&](const char* k) {
        const String v = configServer.GetStoredString(k);
        return v.isEmpty() ? NAN : v.toFloat();
    };
    flowAlertCfs = numOrNan("fi-flow-cfs");
    tempLoF = numOrNan("fi-temp-lo");
    tempHiF = numOrNan("fi-temp-hi");

    auto boolCfg = [&](const char* k, bool def) {
        const String v = configServer.GetStoredString(k);
        return v.isEmpty() ? def : (v == "true");
    };

    // Per-view toggles, in canonical rotation order. Splash is always present (it self-drops via
    // HasData once any data lands). Default every dial on.
    struct ToggleDef { Screen s; const char* key; };
    static const ToggleDef toggles[] = {
        {Screen::Tide,     "fi-v-tide"},
        {Screen::Flow,     "fi-v-flow"},
        {Screen::Temp,     "fi-v-temp"},
        {Screen::Solunar,  "fi-v-solunar"},
        {Screen::Weather,  "fi-v-weather"},
        {Screen::Moon,     "fi-v-moon"},
        {Screen::CatchLog, "fi-v-catch"},
        {Screen::Clock,    "fi-v-clock"},
    };
    enabledOrder.clear();
    for (const ToggleDef& t : toggles)
        if (boolCfg(t.key, true)) enabledOrder.push_back(t.s);
    enabledOrder.push_back(Screen::Splash);

    const String br = configServer.GetStoredString("brightness");
    configuredBrightness = br.isEmpty() ? 255 : (uint8_t)constrain(br.toInt(), 10, 255);
    const String ad = configServer.GetStoredString("autodim");
    autoDim = ad.isEmpty() || ad == "true";

    ntfyTopic = configServer.GetStoredString("ntfy-topic");
    alertFlow = boolCfg("fi-a-flow", false);
    alertTemp = boolCfg("fi-a-temp", false);
    alertSolunar = boolCfg("fi-a-solunar", false);
    alertBaro = boolCfg("fi-a-baro", false);
    alertTide = boolCfg("fi-a-tide", false);
    chimeOnAlert = boolCfg("fi-chime", false);

    logbook.Begin();

    FishingFeedClient::Config fcfg;
    fcfg.baseUrl = backendBaseUrl;
    fcfg.waterType = waterType;
    fcfg.hasLatLon = hasLatLon;
    fcfg.lat = deviceLat;
    fcfg.lon = deviceLon;
    fcfg.usgsSite = usgsSite;
    fcfg.noaaStation = noaaStation;
    fcfg.buoyId = buoyId;
    fcfg.imperial = imperial;
    fcfg.intervalScale = 1.0f;
    feed.Begin();
    feed.Configure(fcfg);

    currentBrightness = configuredBrightness;
    tft.setBrightness(currentBrightness);
    lastBrightnessCheck = 0;

    // A re-init (config save) shouldn't re-fire alerts for conditions already present.
    alertSeeded = false;
    lastFlowSide = 0;
    lastTempSide = 0;
    lastSolunarAlertEpoch = 0;
    baroAlerted = false;
    lastAlertedTideEpoch = 0;

    lastSolunarCalc = 0;
    RecomputeSolunar(true);

    Serial.printf("[fishing] init; water=%u latlon=%d usgs=%d noaa=%d buoy=%d\n",
                  (unsigned)waterType, (int)hasLatLon,
                  (int)usgsSite.length(), (int)noaaStation.length(), (int)buoyId.length());
}

void FishingManager::Update()
{
    feed.Poll();
    RecomputeSolunar(false);
    CheckAlerts();
    UpdateBrightness();
    HandleTouch();
    AutoRotate();
}

void FishingManager::Draw(BandCanvas& backbuffer, bool /*firstPass*/)
{
    std::vector<Screen> rot = BuildRotation();
    bool inRot = false;
    for (Screen s : rot) if (s == current) { inRot = true; break; }
    if (!inRot && !rot.empty()) current = rot.front();

    DrawScreen(backbuffer, current);

    if (inDetail) DrawDetailCard(backbuffer);
    else DrawScreenDots(backbuffer, rot);
}

void FishingManager::DrawScreen(BandCanvas& c, Screen s)
{
    switch (s) {
        case Screen::Tide:    DrawTide(c); break;
        case Screen::Flow:    DrawFlow(c); break;
        case Screen::Temp:    DrawTemp(c); break;
        case Screen::Solunar: DrawSolunar(c); break;
        case Screen::Weather:  DrawWeather(c); break;
        case Screen::Moon:     DrawMoon(c); break;
        case Screen::CatchLog: DrawCatchLog(c); break;
        case Screen::Splash:   DrawSplash(c); break;
        case Screen::Clock:
        default:               DrawClock(c); break;
    }
}

// ----------------------------------------------------------------------------- rotation

bool FishingManager::HasData(Screen s) const
{
    const bool wantSalt  = (waterType == FishingFeedClient::SALT  || waterType == FishingFeedClient::BOTH);
    const bool wantFresh = (waterType == FishingFeedClient::FRESH || waterType == FishingFeedClient::BOTH);
    float tF; const char* src;
    switch (s) {
        case Screen::Tide:     return wantSalt && feed.Tide().valid && !feed.Tide().events.empty();
        case Screen::Flow:     return wantFresh && feed.Flow().valid;
        case Screen::Temp:     return BestWaterTemp(tF, src);
        case Screen::Solunar:  return today.valid && !today.windows.empty();
        case Screen::Weather:  return feed.Weather().valid;
        case Screen::Moon:     return today.valid;
        case Screen::CatchLog: return true; // always available: tap it to log a catch
        case Screen::Clock:    return true;
        case Screen::Splash:   return !feed.HasAny() && !today.valid; // cold-start welcome, self-drops
        default:               return false;
    }
}

std::vector<FishingManager::Screen> FishingManager::BuildRotation() const
{
    std::vector<Screen> rot;
    for (Screen s : enabledOrder)
        if (HasData(s)) rot.push_back(s);
    if (rot.empty()) rot.push_back(Screen::Clock);
    return rot;
}

void FishingManager::AdvanceRotation(int dir)
{
    std::vector<Screen> rot = BuildRotation();
    int idx = 0;
    for (int i = 0; i < (int)rot.size(); ++i) if (rot[i] == current) { idx = i; break; }
    idx = (idx + dir + (int)rot.size()) % (int)rot.size();
    current = rot[idx];
    lastAdvanceMs = millis();
}

void FishingManager::AutoRotate()
{
    if (inDetail) return;                                        // hold while inspecting
    if (millis() - lastInteractionMs < INTERACT_HOLD_MS) return; // user is driving
    if (millis() - lastAdvanceMs < AUTO_DWELL_MS) return;
    AdvanceRotation(+1);
}

// ----------------------------------------------------------------------------- input

void FishingManager::HandleTouch()
{
    // Serialize the touch I2C read against network use of the shared bus (legacy C3 constraint; inert
    // on the dual-core S3 but kept per CLAUDE.md).
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

    if (abs(dx) >= abs(dy)) {
        if (inDetail) { ExitDetail(); return; }
        AdvanceRotation(dx < 0 ? +1 : -1);
    }
    // vertical swipes are unused on the dials (no scrollable list)
}

void FishingManager::HandleTap(int /*tx*/, int /*ty*/)
{
    if (inDetail) { ExitDetail(); return; }

    // The Catch Log screen is a tally, not a dial: a tap RECORDS a catch, tagged with whether a
    // solunar feeding window is active right now. It never opens a detail card.
    if (current == Screen::CatchLog) {
        const time_t now = time(nullptr);
        logbook.RecordCatch((long)now, ActiveNow((long)now));
        if (chimeOnAlert && variant::HAS_AUDIO) board::BuzzerChirp(90); // tactile confirmation
        return;
    }

    // Tap a data dial to open its detail card (chrome screens have nothing to expand).
    if (current != Screen::Splash && current != Screen::Clock && HasData(current)) {
        detailFor = current;
        inDetail = true;
    }
}

// ----------------------------------------------------------------------------- solunar

void FishingManager::RecomputeSolunar(bool force)
{
    if (!hasLatLon) { today = fishing::SolunarDay(); return; }
    const time_t now = time(nullptr);
    if (now < 1600000000) return; // need NTP before the astronomy is meaningful
    if (!force && lastSolunarCalc != 0 && millis() - lastSolunarCalc < SOLUNAR_RECALC_MS) return;
    lastSolunarCalc = millis();
    fishing::ComputeSolunar(deviceLat, deviceLon, now, tzOffsetSec, today);
}

// ----------------------------------------------------------------------------- alerts

void FishingManager::CheckAlerts()
{
    if (!feed.HasAny() && !today.valid) return;

    // Current flow side vs the CFS threshold.
    int flowSide = 0;
    if (!isnan(flowAlertCfs) && feed.Flow().valid && !isnan(feed.Flow().dischargeCfs))
        flowSide = feed.Flow().dischargeCfs < flowAlertCfs ? -1 : +1;

    // Current water-temp side vs the active-feeding band. The band thresholds are read in the user's
    // display units (the config labels them degF), matching BestWaterTemp's display-unit return.
    int tempSide = 0;
    float tF = NAN; const char* tSrc = nullptr;
    if ((!isnan(tempLoF) || !isnan(tempHiF)) && BestWaterTemp(tF, tSrc)) {
        const bool inBand = (isnan(tempLoF) || tF >= tempLoF) && (isnan(tempHiF) || tF <= tempHiF);
        tempSide = inBand ? +1 : -1;
    }

    const time_t now = time(nullptr);

    // Is a major solunar window open right now?
    long activeMajorStart = 0;
    if (today.valid) {
        for (const fishing::SolunarWindow& w : today.windows)
            if (w.major && (long)now >= w.startEpoch && (long)now < w.endEpoch) { activeMajorStart = w.startEpoch; break; }
    }

    // Barometer falling fast (fish feed ahead of a front); re-arm once the drop eases past -0.4 hPa/h.
    const BaroTrend bt = ComputeBaro();
    const bool baroFalling = bt.valid && bt.rateHpaPerH <= -1.0f;
    const bool baroEased   = bt.valid && bt.rateHpaPerH >  -0.4f;

    // Next hi/lo tide within ~30 min.
    long nextTide = 0; bool nextTideHigh = false;
    if (feed.Tide().valid)
        for (const fishing::TideEvent& e : feed.Tide().events)
            if (e.timeEpoch > (long)now && (nextTide == 0 || e.timeEpoch < nextTide)) { nextTide = e.timeEpoch; nextTideHigh = (e.type == 'H'); }
    const bool tideSoon = nextTide != 0 && (nextTide - (long)now) <= 1800;

    if (!alertSeeded) {
        alertSeeded = true;
        lastFlowSide = flowSide;
        lastTempSide = tempSide;
        lastSolunarAlertEpoch = activeMajorStart; // don't fire for a window already open at boot
        baroAlerted = baroFalling;                // don't fire for a fall already underway at boot
        lastAlertedTideEpoch = tideSoon ? nextTide : 0;
        return; // never alert for the state present at boot / re-config
    }

    auto chime = [&](uint16_t ms) { if (chimeOnAlert && variant::HAS_AUDIO) board::BuzzerChirp(ms); };

    // Flow crossed the threshold.
    if (alertFlow && flowSide != 0 && lastFlowSide != 0 && flowSide != lastFlowSide) {
        char body[80];
        chime(120);
        if (flowSide < 0) {
            snprintf(body, sizeof(body), "%.0f CFS - below %.0f", feed.Flow().dischargeCfs, flowAlertCfs);
            SendNtfy("River dropped below threshold", body, "fish,arrow_down", 4);
        } else {
            snprintf(body, sizeof(body), "%.0f CFS - above %.0f", feed.Flow().dischargeCfs, flowAlertCfs);
            SendNtfy("River rising past threshold", body, "fish,arrow_up", 3);
        }
    }
    if (flowSide != 0) lastFlowSide = flowSide;

    // Water temp entered the active-feeding band.
    if (alertTemp && tempSide == +1 && lastTempSide == -1) {
        char body[80];
        chime(120);
        snprintf(body, sizeof(body), "%.0f%s - in the active-feeding band", tF, TempUnit());
        SendNtfy("Water temp in the strike zone", body, "fish,thermometer", 4);
    }
    if (tempSide != 0) lastTempSide = tempSide;

    // A major solunar window opened.
    if (alertSolunar && activeMajorStart != 0 && activeMajorStart != lastSolunarAlertEpoch) {
        lastSolunarAlertEpoch = activeMajorStart;
        chime(140);
        SendNtfy("Bite window opening", "A major solunar feeding period is starting", "fish,sparkles", 4);
    }

    // Barometer started falling fast.
    if (baroFalling && !baroAlerted) {
        baroAlerted = true;
        if (alertBaro) {
            chime(140);
            char b[80];
            snprintf(b, sizeof(b), "Falling %.1f hPa/h - fish feed ahead of a front", bt.rateHpaPerH);
            SendNtfy("Barometer dropping", b, "chart_with_downwards_trend,fish", 4);
        }
    } else if (baroEased) {
        baroAlerted = false; // re-arm once the drop eases
    }

    // A tide turn is ~30 min out.
    if (tideSoon && nextTide != lastAlertedTideEpoch) {
        lastAlertedTideEpoch = nextTide;
        if (alertTide) {
            chime(120);
            SendNtfy(nextTideHigh ? "High tide soon" : "Low tide soon",
                     String(nextTideHigh ? "High" : "Low") + " tide near " + FormatClock(nextTide, tzOffsetSec),
                     "ocean,fish", 3);
        }
    }
}

void FishingManager::SendNtfy(const String& title, const String& body, const String& tags, int priority)
{
    if (ntfyTopic.isEmpty()) return;
    if (lastNotifyMs != 0 && millis() - lastNotifyMs < 5000) return; // throttle bursts
    lastNotifyMs = millis();

    const std::vector<std::pair<String, String>> headers = {
        {"Title", title}, {"Tags", tags}, {"Priority", String(priority)}
    };
    (void)http.Post(String("https://ntfy.sh/") + ntfyTopic, body, headers);
}

// ----------------------------------------------------------------------------- brightness

void FishingManager::UpdateBrightness()
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

bool FishingManager::BestWaterTemp(float& outVal, const char*& outSource) const
{
    // Gate by water type so a fresh-only / salt-only device never reports the other family's
    // temperature (the feed store is also cleared on a narrowing, but keep the read side honest too).
    // Returns a value in the CONFIGURED display units. NOAA station gauge + NDBC buoy WTMP are already
    // in display units (fetched english/metric); the USGS river field is Fahrenheit-normalised at parse
    // and the Open-Meteo Marine SST is always degC -- both converted to the display units here.
    const bool wantSalt  = (waterType == FishingFeedClient::SALT  || waterType == FishingFeedClient::BOTH);
    const bool wantFresh = (waterType == FishingFeedClient::FRESH || waterType == FishingFeedClient::BOTH);
    // Convert a Fahrenheit-normalised value (river field) to the display units.
    auto fromF = [&](float f) { return imperial ? f : (f - 32.0f) / 1.8f; };
    if (wantSalt && feed.SeaTemp().valid && !isnan(feed.SeaTemp().tempF)) { outVal = feed.SeaTemp().tempF; outSource = "tide stn"; return true; }
    if (wantSalt && feed.Buoy().valid && !isnan(feed.Buoy().waterTempF)) { outVal = fromF(feed.Buoy().waterTempF); outSource = "buoy"; return true; }
    if (wantSalt && feed.Marine().valid && feed.Marine().haveSst) { outVal = SeaTempDisp(feed.Marine().seaTempC); outSource = "model"; return true; }
    if (wantFresh && feed.Flow().valid && !isnan(feed.Flow().waterTempF)) { outVal = fromF(feed.Flow().waterTempF); outSource = "river"; return true; }
    return false;
}

bool FishingManager::BestWaves(float& waveDisp, float& periodS, const char*& source) const
{
    // Waves priority: real NDBC buoy (with dominant period) > Open-Meteo Marine model. The buoy field
    // is Fahrenheit/feet-normalised at parse (waveHeightFt); marine is always metres.
    // Waves are a saltwater concept: a fresh-only device never reports them. The buoy feed is already
    // water-type gated at fetch, but Marine is fetched on lat/lon alone, so gate the read side too.
    if (waterType == FishingFeedClient::FRESH) return false;
    const fishing::BuoyObs& b = feed.Buoy();
    if (b.valid && !isnan(b.waveHeightFt)) {
        waveDisp = imperial ? b.waveHeightFt : b.waveHeightFt / 3.28084f;
        periodS = isnan(b.dominantPeriodS) ? 0.0f : b.dominantPeriodS;
        source = "buoy";
        return true;
    }
    if (feed.Marine().valid && feed.Marine().haveWave) {
        waveDisp = WaveDisp(feed.Marine().waveHeightM);
        periodS = 0.0f;
        source = "model";
        return true;
    }
    return false;
}

bool FishingManager::ActiveNow(long nowUtc) const
{
    if (!today.valid) return false;
    for (const fishing::SolunarWindow& w : today.windows)
        if (nowUtc >= w.startEpoch && nowUtc < w.endEpoch) return true;
    return false;
}

FishingManager::BaroTrend FishingManager::ComputeBaro() const
{
    BaroTrend b;
    const fishing::WeatherObs& w = feed.Weather();
    if (!w.valid || w.pressHist.size() < 2) return b;

    b.nowHpa = !isnan(w.pressureHpa) ? w.pressureHpa : w.pressHist.back().second;
    const long nowE  = w.timeEpoch > 0 ? w.timeEpoch : w.pressHist.back().first;
    const long target = nowE - 6 * 3600;  // compare against the sample nearest ~6 h ago

    long  chosenT = w.pressHist.front().first;
    float chosenP = w.pressHist.front().second;
    long best = 0x7fffffffL;
    for (const auto& s : w.pressHist) {
        long dt = s.first - target; if (dt < 0) dt = -dt;
        if (dt < best) { best = dt; chosenT = s.first; chosenP = s.second; }
    }
    const float hours = (float)(nowE - chosenT) / 3600.0f;
    if (hours < 1.0f) return b;           // not enough span yet to state a rate
    b.rateHpaPerH = (b.nowHpa - chosenP) / hours;
    b.valid = true;
    return b;
}

String FishingManager::FormatClock(long epoch, long tzOffsetSec)
{
    if (epoch < 1600000000) return "--:--";
    time_t t = (time_t)(epoch + tzOffsetSec);
    struct tm g; gmtime_r(&t, &g);
    char b[8];
    snprintf(b, sizeof(b), "%02d:%02d", g.tm_hour, g.tm_min);
    return String(b);
}

String FishingManager::FormatCountdown(long secsUntil)
{
    if (secsUntil <= 0) return "now";
    const long h = secsUntil / 3600, m = (secsUntil % 3600) / 60;
    char b[16];
    if (h > 0) snprintf(b, sizeof(b), "%ldh%02ldm", h, m);
    else       snprintf(b, sizeof(b), "%ldm", m);
    return String(b);
}

String FishingManager::FormatAgo(long epochSecs)
{
    const time_t now = time(nullptr);
    if (now < 1600000000 || epochSecs <= 0) return "";
    long s = (long)now - epochSecs;
    if (s < 0) s = 0;
    char b[16];
    if (s < 60)         snprintf(b, sizeof(b), "%lds", s);
    else if (s < 3600)  snprintf(b, sizeof(b), "%ldm", s / 60);
    else if (s < 86400) snprintf(b, sizeof(b), "%ldh", s / 3600);
    else                snprintf(b, sizeof(b), "%ldd", s / 86400);
    return String(b);
}

void FishingManager::CenterText(BandCanvas& c, const String& s, int y, uint32_t color)
{
    c.setTextColor(color);
    c.drawString(s, SCREEN_SIZE_DIV_2 - c.textWidth(s) / 2, y);
}
