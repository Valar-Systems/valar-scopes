#include "EamManager.h"

#include <math.h>
#include <time.h>

#include "Layout.h"
#include "EamModels.h"

// Backend base URL default. Normally injected per-env as a build flag (-DEAM_FEED_BASE=...);
// guarded so a stray build without the flag still compiles. The runtime value ("eam-base-url")
// overrides it, so nothing real is baked in here.
#ifndef EAM_FEED_BASE
#define EAM_FEED_BASE "https://eam.example.com"
#endif

namespace {

constexpr unsigned long AUTO_DWELL_MS  = 8000;   // seconds per screen when auto-rotating
constexpr unsigned long INTERACT_HOLD_MS = 30000; // pause auto-rotate this long after a touch

double Deg2Rad(double d) { return d * M_PI / 180.0; }

// Low-precision solar elevation (degrees) at lat/lon for a UTC epoch -- enough to drive the same
// night auto-dim the radar uses (sun below -0.833 deg = civil horizon).
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

void EamManager::Initialise()
{
    backendBaseUrl = configServer.GetStoredString("eam-base-url");
    if (backendBaseUrl.isEmpty())
        backendBaseUrl = EAM_FEED_BASE;

    palette = eam::PaletteFor(configServer.GetStoredString("eam-palette"));
    colonBlink = configServer.GetStoredString("eam-colon-blink") == "true";

    const String br = configServer.GetStoredString("brightness");
    configuredBrightness = br.isEmpty() ? 255 : (uint8_t)constrain(br.toInt(), 10, 255);
    const String ad = configServer.GetStoredString("autodim");
    autoDim = ad.isEmpty() || ad == "true";

    // Screen enable/order. "eam-screens" is a CSV of screen ids in display order; empty = all.
    enabledOrder.clear();
    const String screensCfg = configServer.GetStoredString("eam-screens");
    auto idToScreen = [](const String& id, Screen& out) -> bool {
        if (id == "ticker")    { out = Screen::Ticker;      return true; }
        if (id == "tempo")     { out = Screen::Tempo;       return true; }
        if (id == "codewords") { out = Screen::Codewords;   return true; }
        if (id == "abncp")     { out = Screen::Abncp;       return true; }
        if (id == "prop")      { out = Screen::Propagation; return true; }
        if (id == "icbm")      { out = Screen::Icbm;        return true; }
        if (id == "clock")     { out = Screen::Clock;       return true; }
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

    // Optional device location: /propagation day/night, ABNCP bearing/distance, solar auto-dim.
    const String latStr = configServer.GetStoredString("latitude");
    const String lonStr = configServer.GetStoredString("longitude");
    hasLatLon = latStr.length() && lonStr.length();
    deviceLat = latStr.toDouble();
    deviceLon = lonStr.toDouble();

    // Feed config (command-post source + creds + watchlist).
    EamFeedClient::Config cfg;
    cfg.baseUrl = backendBaseUrl;
    cfg.hasLatLon = hasLatLon;
    cfg.lat = deviceLat;
    cfg.lon = deviceLon;
    const String source = configServer.GetStoredString("abncp-source");
    if (source == "opensky") {
        cfg.abncpSource = EamFeedClient::AbncpSource::OpenSky;
        cfg.openskyId = configServer.GetStoredString("opensky-id");
        cfg.openskySecret = configServer.GetStoredString("opensky-secret");
        const String watch = configServer.GetStoredString("abncp-watch");
        cfg.abncpWatch = watch.length() ? SplitList(watch, true) : eam::DefaultAbncpWatch();
    } else {
        cfg.abncpSource = EamFeedClient::AbncpSource::Backend;
    }

    // Global poll cadence: Normal / Relaxed / Battery scale every poller's interval.
    const String refresh = configServer.GetStoredString("eam-refresh");
    cfg.intervalScale = refresh == "relaxed" ? 2.0f : refresh == "battery" ? 4.0f : 1.0f;

    feed.Begin();
    feed.Configure(cfg);

    // ntfy alerts (reuses the radar's ntfy-topic key + POST pattern). Each trigger is toggleable;
    // all fire by default once a topic is set (an empty topic disables everything).
    ntfyTopic = configServer.GetStoredString("ntfy-topic");
    auto boolCfg = [&](const char* key, bool def) {
        const String v = configServer.GetStoredString(key);
        return v.isEmpty() ? def : (v == "true");
    };
    alertNew = boolCfg("eam-alert-new", true);
    alertTempo = boolCfg("eam-alert-tempo", true);
    alertAbncp = boolCfg("eam-alert-abncp", true);
    // Reset transition state so a config save never refires a stale tempo/ABNCP transition.
    lastTempoRank = -1;
    lastAbncpAirborne = false;
    abncpSeen = false;

    logbook.Begin();

    currentBrightness = configuredBrightness;
    tft.setBrightness(currentBrightness);
    lastBrightnessCheck = 0;

    Serial.printf("[eam] init; backend=%s abncp=%s palette=%s screens=%u\n",
                  backendBaseUrl.c_str(), source == "opensky" ? "opensky" : "backend",
                  palette.fg == eam::PaletteAmber().fg ? "amber" : "green",
                  (unsigned)enabledOrder.size());
}

void EamManager::Update()
{
    feed.Poll();

    // A genuinely new top-of-feed EAM: pulse + jump to the ticker so it's seen.
    const bool newEam = feed.ConsumeNewLatest();
    if (newEam) {
        newPulseUntilMs = millis() + 2000;
        current = Screen::Ticker;
        lastInteractionMs = millis();
        tickerScroll = 0;
    }

    UpdateLogbook();
    CheckAlerts(newEam);

    UpdateBrightness();
    HandleTouch();
    AutoRotate();

    // rotate the clock's ambient stat line slowly
    if (millis() - lastAmbientMs > 6000) {
        lastAmbientMs = millis();
        ambientIndex++;
    }
}

void EamManager::Draw(BandCanvas& backbuffer, bool firstPass)
{
    std::vector<Screen> rot = BuildRotation();
    // Keep `current` valid against the live rotation set (data can come and go).
    bool inRot = false;
    for (Screen s : rot) if (s == current) { inRot = true; break; }
    if (!inRot && !rot.empty()) current = rot.front();

    switch (current) {
        case Screen::Ticker:      DrawTicker(backbuffer, firstPass); break;
        case Screen::Tempo:       DrawTempo(backbuffer); break;
        case Screen::Codewords:   DrawCodewords(backbuffer); break;
        case Screen::Abncp:       DrawAbncp(backbuffer); break;
        case Screen::Propagation: DrawPropagation(backbuffer); break;
        case Screen::Icbm:        DrawIcbm(backbuffer); break;
        case Screen::Clock:
        default:                  DrawClock(backbuffer); break;
    }

    DrawScreenDots(backbuffer, rot);
}

bool EamManager::HasData(Screen s) const
{
    switch (s) {
        case Screen::Ticker:      return !feed.Latest().empty();
        case Screen::Tempo:       return feed.Tempo().valid;
        case Screen::Codewords:   return !feed.Codewords().empty();
        case Screen::Abncp:       return true; // always meaningful (airborne / none / needs-creds)
        case Screen::Propagation: return feed.Propagation().valid;
        case Screen::Icbm:        return !feed.Launches().empty(); // hidden when no upcoming launch
        case Screen::Clock:       return true;
        default:                  return false;
    }
}

std::vector<EamManager::Screen> EamManager::BuildRotation() const
{
    std::vector<Screen> rot;
    for (Screen s : enabledOrder)
        if (HasData(s)) rot.push_back(s);
    if (rot.empty()) rot.push_back(Screen::Clock); // always have the idle clock
    return rot;
}

void EamManager::AdvanceRotation(int dir)
{
    std::vector<Screen> rot = BuildRotation();
    int idx = 0;
    for (int i = 0; i < (int)rot.size(); ++i) if (rot[i] == current) { idx = i; break; }
    idx = (idx + dir + (int)rot.size()) % (int)rot.size();
    current = rot[idx];
    lastAdvanceMs = millis();
}

void EamManager::AutoRotate()
{
    if (millis() - lastInteractionMs < INTERACT_HOLD_MS) return; // user is driving
    if (millis() - lastAdvanceMs < AUTO_DWELL_MS) return;
    AdvanceRotation(+1);
}

void EamManager::HandleTouch()
{
    // Serialize the touch I2C read against the feed worker's TLS use of the shared bus: on the
    // single-core C3 an overlapping CST816 transfer during a handshake wedges the controller.
    // If a fetch holds the bus, skip touch this frame (fetches are short and infrequent).
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

void EamManager::UpdateBrightness()
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

void EamManager::DrawScreenDots(BandCanvas& c, const std::vector<Screen>& rot) const
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

std::vector<String> EamManager::SplitList(const String& s, bool lower)
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

String EamManager::TimeAgo(long epoch)
{
    if (epoch <= 0) return "";
    const time_t nowUtc = time(nullptr);
    if (nowUtc <= 1600000000) return ""; // clock not synced yet
    long d = (long)nowUtc - epoch;
    if (d < 0) d = 0;
    if (d < 60) return String(d) + "s ago";
    if (d < 3600) return String(d / 60) + "m ago";
    if (d < 86400) return String(d / 3600) + "h ago";
    return String(d / 86400) + "d ago";
}

String EamManager::FormatCountdown(long secondsLeft)
{
    if (secondsLeft < 0) secondsLeft = 0;
    const long days = secondsLeft / 86400;
    const long h = (secondsLeft % 86400) / 3600;
    const long m = (secondsLeft % 3600) / 60;
    const long s = secondsLeft % 60;
    char buf[32];
    if (days > 0)
        snprintf(buf, sizeof(buf), "T-%ldd %02ld:%02ld", days, h, m);
    else
        snprintf(buf, sizeof(buf), "T-%02ld:%02ld:%02ld", h, m, s);
    return String(buf);
}

void EamManager::CenterText(BandCanvas& c, const String& s, int y, uint32_t color)
{
    c.setTextColor(color);
    c.drawString(s, SCREEN_SIZE_DIV_2 - c.textWidth(s) / 2, y);
}

void EamManager::UpdateLogbook()
{
    const time_t nowUtc = time(nullptr);
    const long nowEpoch = (nowUtc > 1600000000) ? (long)nowUtc : 0;

    for (const eam::Msg& m : feed.Latest())
        logbook.NoteEam(m.id, m.heardAtEpoch);

    for (const eam::Codeword& cw : feed.Codewords()) {
        long ep = eam::Iso8601ToEpoch(cw.lastSeen);
        if (ep <= 0) ep = nowEpoch;
        if (logbook.NoteCodeword(cw.codeword, ep))
            newCodewords.insert(cw.codeword);
    }

    logbook.MaybePersist();
}

void EamManager::CheckAlerts(bool newEamArrived)
{
    // No topic -> no alerts, but keep tracking transition state so configuring a topic later
    // doesn't immediately fire on a level/airborne condition that was already true.
    const eam::Tempo& t = feed.Tempo();
    const eam::Abncp& a = feed.Abncp();
    const int tempoRank = t.valid ? (t.level == "high" ? 2 : t.level == "elevated" ? 1 : 0) : lastTempoRank;

    if (ntfyTopic.isEmpty()) {
        if (t.valid) lastTempoRank = tempoRank;
        if (a.valid) { lastAbncpAirborne = a.airborne; abncpSeen = true; }
        return;
    }

    // (a) new EAM
    if (newEamArrived && alertNew && !feed.Latest().empty()) {
        const eam::Msg& m = feed.Latest().front();
        String body = (m.type == eam::MsgType::Skyking) ? ("SKYKING " + m.codeword) : m.text;
        if (m.frequencyKhz) body = String(m.frequencyKhz) + " kHz  " + body;
        SendNtfy("New EAM", body,
                 m.type == eam::MsgType::Skyking ? "radio,rotating_light" : "radio", 4);
    }

    // (b) tempo crossing UP into elevated/high
    if (t.valid) {
        if (lastTempoRank >= 0 && tempoRank > lastTempoRank && tempoRank >= 1 && alertTempo) {
            char b[56];
            snprintf(b, sizeof(b), "EAM tempo %s (%d today)", t.level.c_str(), t.countToday);
            SendNtfy("EAM tempo " + t.level, String(b),
                     tempoRank >= 2 ? "rotating_light" : "chart_with_upwards_trend",
                     tempoRank >= 2 ? 5 : 4);
        }
        lastTempoRank = tempoRank;
    }

    // (c) ABNCP not-airborne -> airborne
    if (a.valid) {
        if (abncpSeen && a.airborne && !lastAbncpAirborne && alertAbncp) {
            String who = "Command post";
            if (!a.aircraft.empty()) {
                const eam::AbncpAircraft& ac = a.aircraft.front();
                if (ac.type.length()) who = ac.type;
                if (ac.callsign.length()) who += " " + ac.callsign;
            }
            SendNtfy("Command post airborne", who + " is airborne", "airplane,rotating_light", 5);
        }
        lastAbncpAirborne = a.airborne;
        abncpSeen = true;
    }
}

void EamManager::SendNtfy(const String& title, const String& body, const String& tags, int priority)
{
    if (ntfyTopic.isEmpty()) return;
    if (lastNotifyMs != 0 && millis() - lastNotifyMs < 5000) return; // throttle bursts
    lastNotifyMs = millis();

    const std::vector<std::pair<String, String>> headers = {
        {"Title", title}, {"Tags", tags}, {"Priority", String(priority)}
    };
    // Blocking POST on the loop task, serialized with the feed worker via HttpRequestManager's
    // mutex -- the same pattern the radar uses for its ntfy alerts. Triggers are rare, so the
    // brief stall (and the wait for any in-flight fetch to release the bus) is acceptable.
    (void)http.Post(String("https://ntfy.sh/") + ntfyTopic, body, headers);
}
