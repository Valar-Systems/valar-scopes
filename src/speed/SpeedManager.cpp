#include "SpeedManager.h"
#include "TouchPoll.h"

#include <math.h>
#include <time.h>
#include <ESPmDNS.h>

#include "Layout.h"

namespace {

constexpr unsigned long AUTO_DWELL_MS    = 8000;   // ms per screen when auto-rotating
constexpr unsigned long INTERACT_HOLD_MS = 30000;  // pause auto-rotate this long after a touch
constexpr unsigned long ONLINE_GRACE_MS  = 12000;  // no fresh /api/state within this = camera offline
constexpr unsigned long RESOLVE_RETRY_MS = 8000;   // re-query mDNS this often while unresolved/offline

double Deg2Rad(double d) { return d * M_PI / 180.0; }

// A bare IPv4 literal is all digits and dots -- skip mDNS for it.
bool LooksLikeIp(const String& h)
{
    if (h.isEmpty()) return false;
    for (size_t i = 0; i < h.length(); ++i) {
        const char c = h[i];
        if ((c < '0' || c > '9') && c != '.') return false;
    }
    return true;
}

// Low-precision solar elevation (deg) -- drives the same night auto-dim the other editions use.
// Copied from FishingManager/SeismicManager so this app dims identically without a cross-TU dependency.
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

void SpeedManager::Initialise()
{
    palette = speed::PaletteDefault();

    camHost = configServer.GetStoredString("sc-host");
    backendBaseUrl = configServer.GetStoredString("sc-base-url");

    auto intCfg = [&](const char* k) {
        const String v = configServer.GetStoredString(k);
        return v.isEmpty() ? 0 : (int)v.toInt();
    };
    speedLimit = intCfg("sc-limit");
    alertSpeed = intCfg("sc-alert-speed");

    const String tz = configServer.GetStoredString("sc-tz-offset");
    tzOffsetSec = tz.isEmpty() ? 0 : (long)lround(tz.toFloat() * 3600.0f);

    const String latStr = configServer.GetStoredString("latitude");
    const String lonStr = configServer.GetStoredString("longitude");
    hasLatLon = latStr.length() && lonStr.length();
    deviceLat = latStr.toDouble();
    deviceLon = lonStr.toDouble();

    auto boolCfg = [&](const char* k, bool def) {
        const String v = configServer.GetStoredString(k);
        return v.isEmpty() ? def : (v == "true");
    };

    // Per-view toggles, in canonical rotation order. Splash is always present (it self-drops via
    // HasData once any data lands). Default every view on.
    struct ToggleDef { Screen s; const char* key; };
    static const ToggleDef toggles[] = {
        {Screen::Last,   "sc-v-last"},
        {Screen::Live,   "sc-v-live"},
        {Screen::List,   "sc-v-list"},
        {Screen::Stats,  "sc-v-stats"},
        {Screen::Device, "sc-v-device"},
        {Screen::Clock,  "sc-v-clock"},
    };
    enabledOrder.clear();
    for (const ToggleDef& t : toggles)
        if (boolCfg(t.key, true)) enabledOrder.push_back(t.s);
    enabledOrder.push_back(Screen::Splash);

    const String br = configServer.GetStoredString("brightness");
    configuredBrightness = br.isEmpty() ? 255 : (uint8_t)constrain(br.toInt(), 10, 255);
    autoDim = boolCfg("autodim", true);

    ntfyTopic = configServer.GetStoredString("ntfy-topic");
    alertSpeeder = boolCfg("sc-a-speeder", false);
    alertRecord  = boolCfg("sc-a-record", false);
    alertOffline = boolCfg("sc-a-offline", false);

    feed.Begin();
    // Resolve the camera host (mDNS name -> IP, or a bare IP / URL) and hand the feed its origin.
    feedOrigin = "\x01";           // sentinel so the first MaybeResolveOrigin() always (re)configures
    resolvedIpOrigin = "";
    resolvedName = "";
    lastResolveMs = 0;
    MaybeResolveOrigin(true);

    currentBrightness = configuredBrightness;
    tft.setBrightness(currentBrightness);
    lastBrightnessCheck = 0;

    // A re-init (config save) shouldn't re-fire alerts for conditions already present.
    alertSeeded = false;
    epochSeeded = false;
    recordSeeded = false;
    newestSeenEpoch = 0;
    recordTop = 0;
    recordDayIndex = 0;
    wasOnline = false;
    everOnline = false;

    Serial.printf("[speed] init; host='%s' base='%s' limit=%d alert=%d\n",
                  camHost.c_str(), backendBaseUrl.c_str(), speedLimit, alertSpeed);
}

void SpeedManager::Update()
{
    MaybeResolveOrigin(false);
    feed.Poll();
    CheckAlerts();
    UpdateBrightness();
    HandleTouch();
    AutoRotate();
}

void SpeedManager::Draw(BandCanvas& backbuffer, bool /*firstPass*/)
{
    std::vector<Screen> rot = BuildRotation();
    bool inRot = false;
    for (Screen s : rot) if (s == current) { inRot = true; break; }
    if (!inRot && !rot.empty()) current = rot.front();

    DrawScreen(backbuffer, current);

    if (inDetail) DrawDetailCard(backbuffer);
    else DrawScreenDots(backbuffer, rot);
}

void SpeedManager::DrawScreen(BandCanvas& c, Screen s)
{
    switch (s) {
        case Screen::Last:   DrawLast(c); break;
        case Screen::Live:   DrawLive(c); break;
        case Screen::List:   DrawList(c); break;
        case Screen::Stats:  DrawStats(c); break;
        case Screen::Device: DrawDevice(c); break;
        case Screen::Splash: DrawSplash(c); break;
        case Screen::Clock:
        default:             DrawClock(c); break;
    }
}

// ----------------------------------------------------------------------------- host resolution

void SpeedManager::MaybeResolveOrigin(bool force)
{
    String origin;

    if (backendBaseUrl.length()) {
        origin = backendBaseUrl;                 // explicit proxy/aggregator wins; no mDNS
    } else {
        String h = camHost; h.trim();
        if (h.isEmpty()) h = "MiniSpeedCam";     // the camera's default mDNS name / soft-AP SSID
        if (h.startsWith("http://") || h.startsWith("https://")) {
            origin = h;
        } else if (LooksLikeIp(h)) {
            origin = "http://" + h;
        } else {
            // mDNS name (bare or ".local"): resolve to an IP -- Arduino's getaddrinfo won't query
            // mDNS itself, so we ask MDNS directly and cache the answer.
            String name = h;
            if (name.endsWith(".local")) name = name.substring(0, name.length() - 6);

            const unsigned long now = millis();
            const bool resolved = resolvedIpOrigin.length() && resolvedName == name;
            bool doQuery = force;
            if (!doQuery) {
                if (!resolved) doQuery = (now - lastResolveMs > RESOLVE_RETRY_MS);
                else if (!DeviceOnline()) doQuery = (now - lastResolveMs > RESOLVE_RETRY_MS);
            }
            if (doQuery) {
                lastResolveMs = now;
                IPAddress ip = MDNS.queryHost(name.c_str(), 1200);
                if ((uint32_t)ip != 0) {
                    resolvedIpOrigin = String("http://") + ip.toString();
                    resolvedName = name;
                }
            }
            origin = (resolvedName == name) ? resolvedIpOrigin : String();
        }
    }

    while (origin.endsWith("/")) origin.remove(origin.length() - 1);

    if (origin != feedOrigin) {
        feedOrigin = origin;
        SpeedFeedClient::Config fcfg;
        fcfg.origin = origin;
        fcfg.intervalScale = 1.0f;
        feed.Configure(fcfg);
        if (origin.length()) Serial.printf("[speed] feed origin -> %s\n", origin.c_str());
    }
}

// ----------------------------------------------------------------------------- rotation

bool SpeedManager::HasData(Screen s) const
{
    const bool haveEvents = feed.HasEvents() && !feed.Events().events.empty();
    switch (s) {
        case Screen::Last:   return haveEvents;
        case Screen::Live:   return feed.HasState();
        case Screen::List:   return haveEvents;
        case Screen::Stats:  return haveEvents;
        case Screen::Device: return feed.HasState();
        case Screen::Clock:  return true;
        case Screen::Splash: return !feed.HasAny(); // cold-start welcome, self-drops once data lands
        default:             return false;
    }
}

std::vector<SpeedManager::Screen> SpeedManager::BuildRotation() const
{
    std::vector<Screen> rot;
    for (Screen s : enabledOrder)
        if (HasData(s)) rot.push_back(s);
    if (rot.empty()) rot.push_back(Screen::Clock);
    return rot;
}

void SpeedManager::AdvanceRotation(int dir)
{
    std::vector<Screen> rot = BuildRotation();
    int idx = 0;
    for (int i = 0; i < (int)rot.size(); ++i) if (rot[i] == current) { idx = i; break; }
    idx = (idx + dir + (int)rot.size()) % (int)rot.size();
    current = rot[idx];
    lastAdvanceMs = millis();
}

void SpeedManager::AutoRotate()
{
    if (inDetail) return;                                        // hold while inspecting
    if (millis() - lastInteractionMs < INTERACT_HOLD_MS) return; // user is driving
    if (millis() - lastAdvanceMs < AUTO_DWELL_MS) return;
    AdvanceRotation(+1);
}

// ----------------------------------------------------------------------------- input

void SpeedManager::HandleTouch()
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
    }
}

void SpeedManager::HandleTap(int /*tx*/, int /*ty*/)
{
    if (inDetail) { ExitDetail(); return; }
    // Tap a data screen to open its detail card (Splash/Clock have nothing to expand).
    if (current != Screen::Splash && current != Screen::Clock && HasData(current)) {
        detailFor = current;
        inDetail = true;
    }
}

// ----------------------------------------------------------------------------- domain helpers

bool SpeedManager::DeviceOnline() const
{
    return feed.EverPolledState() && feed.MsSinceStateOk() < ONLINE_GRACE_MS;
}

bool SpeedManager::UnitKph() const
{
    if (feed.HasEvents()) return feed.Events().kph;
    if (feed.HasState())  return feed.State().isKph;
    return false;
}

void SpeedManager::ComputeToday(int& count, int& top, int& over, double& avg) const
{
    count = 0; top = 0; over = 0;
    double sum = 0;
    const time_t now = time(nullptr);
    const bool haveClock = now > 1600000000;
    const long dayStart = haveClock ? ((long)now - (((long)now + tzOffsetSec) % 86400)) : 0;

    for (const speed::SpeedRecord& e : feed.Events().events) {
        bool today;
        if (haveClock && e.epoch > 0) today = (e.epoch >= dayStart);
        else                          today = (e.ageSec < 86400); // fallback: last 24 h
        if (!today) continue;
        count++;
        sum += e.speed;
        if (e.speed > top) top = e.speed;
        if (speedLimit > 0 && e.speed > speedLimit) over++;
    }
    avg = count ? sum / count : 0.0;
}

// ----------------------------------------------------------------------------- alerts

void SpeedManager::CheckAlerts()
{
    const bool online = DeviceOnline();
    const bool haveEvents = feed.HasEvents();
    if (!feed.HasAny()) return;

    const time_t now = time(nullptr);
    const bool haveClock = now > 1600000000;

    // Newest event epoch + today's aggregates.
    long maxEpoch = 0;
    if (haveEvents)
        for (const speed::SpeedRecord& e : feed.Events().events)
            if (e.epoch > maxEpoch) maxEpoch = e.epoch;
    int tCount = 0, tTop = 0, tOver = 0; double tAvg = 0;
    ComputeToday(tCount, tTop, tOver, tAvg);
    const long dayIdx = haveClock ? LocalDayIndex((long)now) : 0;

    if (!alertSeeded) {
        alertSeeded = true;
        wasOnline = online;
        everOnline = online;
        // newestSeenEpoch and the day-record baseline are seeded separately (below):
        // pre-NTP every event epoch is 0, and /api/state usually lands before
        // /api/events, so baselining recordTop here reads the still-empty event
        // ring as 0 and today's existing fastest pass fires a spurious "new top"
        // the moment events arrive -- on every boot.
        return;
    }

    // Seed the day-record baseline the FIRST time events and the clock are both up.
    if (!recordSeeded && haveClock && haveEvents) {
        recordSeeded = true;
        recordTop = tTop;             // today's existing top isn't a new record
        recordDayIndex = dayIdx;
    }

    // Seed the speeder baseline the FIRST time we have both a synced clock and events, so the passes
    // already in the ring when NTP arrives are suppressed (their real epochs are back-stamped then).
    if (!epochSeeded && haveClock && haveEvents) {
        epochSeeded = true;
        newestSeenEpoch = maxEpoch;
    }

    // --- speeder: a genuinely new pass at/over the threshold ---
    if (epochSeeded && alertSpeeder && alertSpeed > 0) {
        const speed::SpeedRecord* worst = nullptr;
        for (const speed::SpeedRecord& e : feed.Events().events) {
            if (e.epoch > newestSeenEpoch + 2 && e.speed >= alertSpeed) { // +2 s absorbs age jitter
                if (!worst || e.speed > worst->speed) worst = &e;
            }
        }
        if (worst) {
            String title = String("Speeder: ") + worst->speed + " " + UnitLabel();
            if (speedLimit > 0) title += String(" (limit ") + speedLimit + ")";
            const char* dir = speed::DirLabel(worst->dir);
            SendNtfy(title, dir[0] ? String("Heading ") + dir : String("Vehicle over the threshold"),
                     "rotating_light,car", 5);
        }
    }
    if (epochSeeded && maxEpoch > newestSeenEpoch) newestSeenEpoch = maxEpoch;

    // --- new fastest-of-the-day ---
    if (recordSeeded) {
        if (dayIdx != recordDayIndex) {
            recordDayIndex = dayIdx;
            recordTop = tTop;         // a fresh day: reseed silently (first car isn't a "record")
        } else if (tTop > recordTop) {
            if (alertRecord) {
                char title[48];
                snprintf(title, sizeof(title), "New top today: %d %s", tTop, UnitLabel());
                SendNtfy(title, "Fastest pass of the day so far", "checkered_flag,car", 3);
            }
            recordTop = tTop;         // bump even when the alert is off, so enabling it later is clean
        }
    }

    // --- camera offline / back online ---
    if (alertOffline) {
        if (wasOnline && !online)
            SendNtfy("MiniSpeedCam offline", "No response from the camera", "warning", 4);
        else if (!wasOnline && online && everOnline)   // only after a real online->offline->online cycle
            SendNtfy("MiniSpeedCam back online", "The camera is responding again", "white_check_mark", 3);
    }
    if (online) everOnline = true;
    wasOnline = online;
}

void SpeedManager::SendNtfy(const String& title, const String& body, const String& tags, int priority)
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

void SpeedManager::UpdateBrightness()
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

// ----------------------------------------------------------------------------- format helpers

String SpeedManager::FormatClock(long epoch, long tzOffsetSec)
{
    if (epoch < 1600000000) return "--:--";
    time_t t = (time_t)(epoch + tzOffsetSec);
    struct tm g; gmtime_r(&t, &g);
    char b[8];
    snprintf(b, sizeof(b), "%02d:%02d", g.tm_hour, g.tm_min);
    return String(b);
}

// "ago" from an absolute epoch when the clock is set; otherwise from the device-relative age.
String SpeedManager::FormatAgo(long epochSecs, int fallbackAgeSec)
{
    const time_t now = time(nullptr);
    long s;
    if (now > 1600000000 && epochSecs > 0) s = (long)now - epochSecs;
    else                                   s = fallbackAgeSec;
    if (s < 0) s = 0;
    char b[16];
    if (s < 60)         snprintf(b, sizeof(b), "%lds", s);
    else if (s < 3600)  snprintf(b, sizeof(b), "%ldm", s / 60);
    else if (s < 86400) snprintf(b, sizeof(b), "%ldh", s / 3600);
    else                snprintf(b, sizeof(b), "%ldd", s / 86400);
    return String(b);
}

void SpeedManager::CenterText(BandCanvas& c, const String& s, int y, uint32_t color)
{
    c.setTextColor(color);
    c.drawString(s, SCREEN_SIZE_DIV_2 - c.textWidth(s) / 2, y);
}
