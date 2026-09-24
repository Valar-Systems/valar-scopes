#include "EamManager.h"
#include "TouchPoll.h"
#include "SolarDim.h"

#include <math.h>
#include <time.h>

#include "Layout.h"
#include "EamModels.h"
#include "UsbOpen.h"

#if defined(FEATURE_EAM_GAME)
#include <esp_timer.h>
#include <sys/time.h>

#include "ClockSync.h"
#include "../game/DrawDrill.h"
#include "../game/DrillPolicy.h"
#endif

// Backend base URL default. Normally injected per-env as a build flag (-DEAM_FEED_BASE=...);
// guarded so a stray build without the flag still compiles. The runtime value ("eam-base-url")
// overrides it, so nothing real is baked in here.
#ifndef EAM_FEED_BASE
#define EAM_FEED_BASE "https://eam.example.com"
#endif

namespace {

constexpr unsigned long AUTO_DWELL_MS  = 8000;   // seconds per screen when auto-rotating
constexpr unsigned long INTERACT_HOLD_MS = 30000; // pause auto-rotate this long after a touch


} // namespace

void EamManager::Initialise()
{
    usbopen::Begin();
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
        if (id == "activity")  { out = Screen::Activity;    return true; }
        if (id == "codewords") { out = Screen::Codewords;   return true; }
        if (id == "abncp")     { out = Screen::Abncp;       return true; }
        if (id == "milair")    { out = Screen::MilAir;      return true; }
        if (id == "prop")      { out = Screen::Propagation; return true; }
        if (id == "icbm")      { out = Screen::Icbm;        return true; }
        if (id == "ref")       { out = Screen::Reference;   return true; }
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
        // Fall back to the default watch when the PARSED list is empty, not just the
        // raw string: a stray comma/space in a cleared textarea is a non-empty string
        // that splits to zero hexes (which would leave the provider inert).
        const String watch = configServer.GetStoredString("abncp-watch");
        cfg.abncpWatch = watch.length() ? SplitList(watch, true) : std::vector<String>();
        if (cfg.abncpWatch.empty())
            cfg.abncpWatch = eam::DefaultAbncpWatch();
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
    ntfy.SetTopic(ntfyTopic);
    auto boolCfg = [&](const char* key, bool def) {
        const String v = configServer.GetStoredString(key);
        return v.isEmpty() ? def : (v == "true");
    };
    alertNew = boolCfg("eam-alert-new", true);
    alertTempo = boolCfg("eam-alert-tempo", true);
    alertAbncp = boolCfg("eam-alert-abncp", true);
    alertSpace = boolCfg("eam-alert-space", true);
    // Reset transition state so a config save never refires a stale tempo/ABNCP/space transition.
    lastTempoRank = -1;
    lastAbncpAirborne = false;
    abncpSeen = false;
    lastHfDegraded = false;
    lastGScaleRank = 0;
    spaceSeen = false;

    logbook.Begin();

#if defined(FEATURE_EAM_GAME)
    // The arming gate needs the AGE of the clock sync, not just "after 2020".
    clocksync::Begin();
    game::KeyTurnParams kp;
    kp.cx = SCREEN_SIZE_DIV_2;
    kp.cy = SCREEN_SIZE_DIV_2;
    kp.r_min = SCREEN_SIZE * 30 / 100;   // outer 40 % of the radius is "the bezel"; 13-D tunable
    keyTurn = game::KeyTurnGesture(kp);
    keyTouch.Begin(tft);
    gameClient.Begin();
#endif

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

#if defined(FEATURE_EAM_GAME)
    std::vector<eam::Msg> fresh;
    UpdateLogbook(&fresh);
    // BACKLOG IS NEVER OFFERED. Only messages first seen LIVE -- i.e. on the poll that
    // raised the new-arrival edge, which is never the first poll after boot -- reach the
    // drill (missileer-game-ui-review.md §6.6 "Backlog: edge-seeded"). A fresh device's
    // empty logbook would otherwise offer a day-old NAM.
    if (!newEam) fresh.clear();
    UpdateDrill(fresh, feed.ConsumeReconnected());
#else
    UpdateLogbook();
#endif
    CheckAlerts(newEam);
    ntfy.Pump(http);

    UpdateBrightness();
    HandleTouch();
    usbopen::Pump();
    AutoRotate();

    // rotate the clock's ambient stat line slowly
    if (millis() - lastAmbientMs > 6000) {
        lastAmbientMs = millis();
        ambientIndex++;
    }
}

void EamManager::Draw(BandCanvas& backbuffer, bool firstPass)
{
#if defined(FEATURE_EAM_GAME)
    // THE DRILL FACE IS A MODE, not a rotation screen (missileer-game-ui-review.md:
    // "the launch face must be an overlay + a mode"). While it is up nothing else draws.
    if (drillScreen) {
        const uint64_t now = NowUs();
        game::DrawDrill(backbuffer, palette, drill.Get(), drill.Cfg(), now,
                        game::ClockAgeS(clocksync::HaveSync(), now, clocksync::LastSyncMonoUs()));
        return;
    }
#endif
    std::vector<Screen> rot = BuildRotation();
    // Keep `current` valid against the live rotation set (data can come and go).
    bool inRot = false;
    for (Screen s : rot) if (s == current) { inRot = true; break; }
    if (!inRot && !rot.empty()) current = rot.front();

    switch (current) {
        case Screen::Ticker:      DrawTicker(backbuffer, firstPass); break;
        case Screen::Tempo:       DrawTempo(backbuffer); break;
        case Screen::Activity:    DrawActivity(backbuffer); break;
        case Screen::Codewords:   DrawCodewords(backbuffer); break;
        case Screen::Abncp:       DrawAbncp(backbuffer); break;
        case Screen::MilAir:      DrawMilAir(backbuffer); break;
        case Screen::Propagation: DrawPropagation(backbuffer); break;
        case Screen::Icbm:        DrawIcbm(backbuffer); break;
        case Screen::Reference:   DrawReference(backbuffer); break;
        case Screen::Clock:
        default:                  DrawClock(backbuffer); break;
    }

    DrawScreenDots(backbuffer, rot);

    // "opening on computer" confirmation after a long press (FEATURE_USB_OPEN).
    if ((long)(usbToastUntilMs - millis()) > 0)
        CenterText(backbuffer, usbToast, (int)(SCREEN_SIZE * 0.80), palette.accent);

#if defined(FEATURE_EAM_GAME)
    // Offered: a STATIC banner over whatever screen is up; the carousel keeps running
    // beneath it (Fable, 2026-09-23).
    if (drill.Get().phase == game::Phase::Offered) {
        const bool decoded = game::AutoDecoded(offeredAtUs, NowUs(), feed.GameCfg().autoDecodeS);
        game::DrawOfferBanner(backbuffer, palette, drill.Get(), decoded);
    }
#endif
}

String EamManager::ShownMessageId() const
{
    // "The message currently shown" is the ticker's: it is the only screen that
    // shows one message. Every other screen is an aggregate, and opens the root.
    if (current == Screen::Ticker && !feed.Latest().empty()) return feed.Latest().front().id;
    return String();
}

void EamManager::OpenOnComputer()
{
#if defined(FEATURE_USB_OPEN)
    const usbopen::Os os = usbopen::OsFromConfig(configServer.GetStoredString("eam-usb-os"));
    // "When nothing is shown": the archive root (default) or nothing at all.
    const bool emptyOpensArchive = configServer.GetStoredString("eam-usb-empty") != "none";
    const String url = usbopen::PlanUrl(os, ShownMessageId(), emptyOpensArchive);
    const bool started = usbopen::Request(os, url);
    usbToast = started ? "opening on computer" : (os == usbopen::Os::Off ? "usb open is off" : "nothing to open");
    usbToastUntilMs = millis() + 1500;
    Serial.printf("[usb-open] long press: shown='%s' started=%d\n", ShownMessageId().c_str(), started ? 1 : 0);
#endif
}

bool EamManager::HasData(Screen s) const
{
    switch (s) {
        case Screen::Ticker:      return !feed.Latest().empty();
        case Screen::Tempo:       return feed.Tempo().valid;
        case Screen::Activity: {  // only when there's an hourly histogram with something in it
            const eam::Stats& st = feed.Stats();
            if (!st.valid || !st.hasByHour) return false;
            for (int i = 0; i < 24; ++i) if (st.byHour[i] > 0) return true;
            return false;
        }
        case Screen::Codewords:   return !feed.Codewords().empty();
        case Screen::Abncp:       return true; // always meaningful (airborne / none / needs-creds)
        case Screen::MilAir:      return feed.MilAir().valid && feed.MilAir().count > 0;
        case Screen::Propagation: return feed.Propagation().valid;
        case Screen::Icbm:        return !feed.Launches().empty(); // hidden when no upcoming launch
        case Screen::Reference:   return true; // static help card, no feed dependency
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
#if defined(FEATURE_EAM_GAME)
    if (drillScreen) return; // the drill face is a mode; the carousel waits under it
#endif
    if (millis() - lastInteractionMs < INTERACT_HOLD_MS) return; // user is driving
    if (millis() - lastAdvanceMs < AUTO_DWELL_MS) return;
    AdvanceRotation(+1);
}

void EamManager::HandleTouch()
{
    // Variant-aware touch poll (TouchPoll.h): serialized against TLS only on the
    // single-core C3; ungated on dual-core S3, where gating on the HTTP mutex
    // (held for the whole of every fetch) would silently drop taps.
    int32_t tx = 0, ty = 0;
#if defined(FEATURE_EAM_GAME)
    // Every touch read goes through the key-window sampler's bus lock: while the key
    // window is open its task owns the reads, and UpdateDrill has already fed its
    // samples to the drill.
    bool touched;
    if (keyTouch.Active()) {
        if (drillScreen) {
            lastInteractionMs = millis();
            wasTouched = false;
            return;
        }
        // KEYTOUCH_BENCH only (the sampler runs outside a drill): the newest sample
        // stands in for this pass's read.
        KeyTouchSampler::Sample s;
        while (keyTouch.Next(UINT64_MAX, s)) {
            benchTouched = s.touched;
            benchX = s.x;
            benchY = s.y;
        }
        touched = benchTouched;
        tx = benchX;
        ty = benchY;
    } else {
        touched = keyTouch.ReadDirect(tx, ty);
    }
#else
    const TouchPoll poll = ReadTouch(tft, http, tx, ty);
    if (poll == TouchPoll::Skipped) return; // C3 only: request mid-flight
    const bool touched = (poll == TouchPoll::Touched);
#endif

#if defined(FEATURE_EAM_GAME)
    if (drillScreen) {
        // Inside the drill: taps and the key turn only. Swipes are blocked
        // (missileer-game-ui-review.md §6.2) and the USB long press is not armed.
        HandleDrillTouch(touched, tx, ty, NowUs());
        lastInteractionMs = millis();
        wasTouched = false;
        return;
    }
#endif

    const unsigned long now = millis();
    if (touched) {
        if (!wasTouched) { wasTouched = true; touchStartX = tx; touchStartY = ty; touchDownMs = now; longPressFired = false; }
        touchLastX = tx;
        touchLastY = ty;
        lastInteractionMs = now;
#if defined(FEATURE_USB_OPEN)
        // LONG PRESS: held still for a second opens the shown message on the computer.
        // NOT during a drill, Offered through Terminal (DrillPolicy UsbLongPressArmed): the
        // drill face already skips this path, and the Offered banner sits over the carousel.
        bool armed = true;
#if defined(FEATURE_EAM_GAME)
        armed = game::UsbLongPressArmed(drill.Get().phase);
#endif
        if (armed && !longPressFired && now - touchDownMs >= LONG_PRESS_MS
            && abs(touchLastX - touchStartX) < 40 && abs(touchLastY - touchStartY) < 40) {
            longPressFired = true;
            OpenOnComputer();
        }
#endif
        return;
    }
    if (!wasTouched) return;
    wasTouched = false;
    lastInteractionMs = now;
    if (longPressFired) return;   // the long press was the gesture; its release is not a tap/swipe

    const int dx = touchLastX - touchStartX;
    const int dy = touchLastY - touchStartY;
    if (abs(dx) < 40 && abs(dy) < 40) {
#if defined(FEATURE_EAM_GAME)
        // THE BANNER TAP IS THE RITUAL BEAT (§5 line 234): it runs the decoder.
        // PlayerOpen -> Printing, and the drill face comes up.
        if (drill.Get().phase == game::Phase::Offered
            && game::BannerRect(SCREEN_SIZE).Contains(touchStartX, touchStartY)) {
            drill.Step(game::Event::PlayerOpen, NowUs());
            drillScreen = true;
            keyTurn.Reset();
            Serial.printf("[drill] open %s\n", drillMsgId.c_str());
        }
#endif
        return; // tap: just holds auto-rotate (handled above)
    }
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
#if defined(FEATURE_EAM_GAME)
    // Auto-dim is inhibited for a committed sortie's lifetime (ui-review §6.3).
    if (drill.Get().committed && drill.Get().phase != game::Phase::Complete
        && drill.Get().phase != game::Phase::Aborted)
        night = false;
#endif
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

void EamManager::UpdateLogbook(std::vector<eam::Msg>* fresh)
{
    const time_t nowUtc = time(nullptr);
    const long nowEpoch = (nowUtc > 1600000000) ? (long)nowUtc : 0;

    for (const eam::Msg& m : feed.Latest()) {
        // NEW TO THIS DEVICE = not in the logbook (the brief's definition of a new EAM).
        const bool isNew = logbook.NoteEam(m.id, m.heardAtEpoch);
        if (isNew && fresh && m.type == eam::MsgType::Eam) fresh->push_back(m);
    }

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

    const eam::SpaceWeather& sw = feed.Propagation().space;
    const bool spaceValid = feed.Propagation().valid && sw.valid;

    if (ntfyTopic.isEmpty()) {
        if (t.valid) lastTempoRank = tempoRank;
        if (a.valid) { lastAbncpAirborne = a.airborne; abncpSeen = true; }
        if (spaceValid) { lastHfDegraded = sw.hfDegraded; lastGScaleRank = sw.gScale > 0 ? sw.gScale : 0; spaceSeen = true; }
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

    // (d) space weather: HF radio blackout turning on, or a geomagnetic storm crossing up into G2+.
    if (spaceValid) {
        const int gRank = sw.gScale > 0 ? sw.gScale : 0;   // 0..5 (-1/unknown treated as 0)
        const int rRank = sw.rScale > 0 ? sw.rScale : 0;
        if (spaceSeen && alertSpace) {
            const bool blackoutOnset = sw.hfDegraded && !lastHfDegraded;
            const bool stormOnset = gRank >= 2 && gRank > lastGScaleRank;
            if (blackoutOnset) {
                String body = "Radio blackout";
                if (rRank >= 1) body += " R" + String(rRank);
                body += " - HF degraded";
                if (sw.xrayClass.length()) body += " (" + sw.xrayClass + ")";
                SendNtfy("Space weather - HF blackout", body, "radioactive,warning", rRank >= 3 ? 5 : 4);
            } else if (stormOnset) {
                String body = "Geomagnetic storm G" + String(gRank);
                if (sw.kp >= 0) body += "  Kp " + String(sw.kp);
                SendNtfy("Space weather - geomagnetic storm", body, "zap,warning", gRank >= 3 ? 5 : 4);
            }
        }
        lastHfDegraded = sw.hfDegraded;
        lastGScaleRank = gRank;
        spaceSeen = true;
    }
}

void EamManager::SendNtfy(const String& title, const String& body, const String& tags, int priority)
{
    // Queue it; NtfyAlerter defers (not drops) co-triggered alerts and Update() pumps the
    // queue, keeping the 5 s spacing between POSTs. The edge-state advance in the callers
    // is therefore safe -- a throttled alert is delivered later, not lost.
    ntfy.Send(title, body, tags, priority);
}

#if defined(FEATURE_EAM_GAME)
// =========================================================================== the drill
// Everything below gathers inputs and feeds DrillMachine. Every DECISION is in
// src/game/DrillPolicy.h (pure, host-tested); if a branch here starts deciding
// something, it belongs there instead.

uint64_t EamManager::NowUs()
{
    return (uint64_t)esp_timer_get_time();
}

int64_t EamManager::UtcMsNow()
{
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    return (int64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

bool EamManager::ClockFreshNow() const
{
    return game::ClockFresh(clocksync::HaveSync(), NowUs(), clocksync::LastSyncMonoUs(),
                            feed.GameCfg().maxClockSyncAgeS);
}

game::Config EamManager::DrillConfig() const
{
    // The served rules, fixed for the life of one drill: a drill runs under the rules in
    // force when it was offered.
    game::Config c;
    const eam::GameConfig& gc = feed.GameCfg();
    if (gc.windowUs) c.window_us = gc.windowUs;
    c.bucket_us = gc.bucketUs;  // 0 = unknown: the figure says so rather than guessing
    c.key_confirm_us = keyTurn.Params().confirm_us;
    return c;
}

void EamManager::UpdateDrill(const std::vector<eam::Msg>& fresh, bool reconnected)
{
    const uint64_t now = NowUs();
    const eam::GameConfig& gc = feed.GameCfg();

    // HOLD: no game surfaces at all (§1.2). A live drill ENDS -- Aborted, reason
    // "hold" -- rather than pausing: one bit reverts the fleet to pure monitoring, and
    // a pause would imply resumption (Fable, 2026-09-23). The ended drill then returns
    // to Idle on the usual 60 s dwell, with its face closed meanwhile.
    if (gc.valid && gc.hold) {
        const game::Phase before = drill.Get().phase;
        drill.Step(game::Event::Hold, now);
        if (drill.Get().phase != before) Serial.println("[drill] HOLD set: drill aborted (hold)");
        drillScreen = false;
        keyTouch.SetActive(false);
        if (drill.Get().phase == game::Phase::Aborted) {
            if (endedAtUs == 0) endedAtUs = now;
            if (game::EndedDwellOver(endedAtUs, now)) { EndDrill(); endedAtUs = 0; }
        }
        return;
    }

    if (reconnected) drill.Step(game::Event::FeedReconnected, now);

    // Oldest first: the feed is newest-first, and "the first MessageArrived" is the
    // earliest heard. Later ones in the same poll are OtherMessageArrived.
    for (auto it = fresh.rbegin(); it != fresh.rend(); ++it) {
        const eam::Msg& m = *it;
        game::OfferInput in;
        in.drill_idle = drill.Get().phase == game::Phase::Idle;
        in.hold = gc.valid && gc.hold;
        in.have_config = gc.valid;
        if (gc.valid)
            in.derivation = game::Derive(m.id.c_str(), m.id.length(), m.heardAt.c_str(), gc.params);
        in.clock_fresh = ClockFreshNow();
        in.now_utc_ms = UtcMsNow();
        in.ack_cutoff_s = gc.ackCutoffS;

        const game::OfferDecision d = game::DecideOffer(in);
        switch (d) {
            case game::OfferDecision::Offer: {
                drill = game::DrillMachine(DrillConfig());
                game::EventArgs a;
                a.cls = in.derivation.cls;
                // WITHDRAWN AT THE ACK CUTOFF (Fable, 2026-09-23). DecideOffer only
                // offers an execution with a fresh clock, so this conversion is honest.
                if (in.derivation.cls == game::MsgClass::Execution)
                    a.withdraw_at_us = game::MonoForUtcMs(
                        in.derivation.t_at_ms - (int64_t)gc.ackCutoffS * 1000, in.now_utc_ms, now);
                drill.Step(game::Event::MessageArrived, now, a);
                offeredAtUs = now;
                endedAtUs = 0;
                drillMsgId = m.id;
                drillDerivation = in.derivation;
                drillHeardAt = m.heardAt;
                executeSent = false;
                gameClient.StartDrill(m.id, in.derivation.cls, in.derivation.t_at_ms);
                Serial.printf("[drill] offer %s class=%d t_at_ms=%lld epoch=%u\n", m.id.c_str(),
                              (int)in.derivation.cls, (long long)in.derivation.t_at_ms,
                              (unsigned)gc.params.epoch);
                break;
            }
            case game::OfferDecision::Busy:
                // NOT queued: it stays in the ticker and the logbook, worked or not.
                drill.Step(game::Event::OtherMessageArrived, now);
                break;
            default:
                Serial.printf("[drill] not offered %s (decision %d)\n", m.id.c_str(), (int)d);
                break;
        }
    }

    // THE KEY WINDOW'S SAMPLES, in time order, BEFORE the tick: a turn made inside
    // the window must be scored before the tick that would close it.
    DrainKeySamples(now);

    const game::Phase beforeTick = drill.Get().phase;
    drill.Step(game::Event::Tick, now);
    if (beforeTick == game::Phase::Offered && drill.Get().withdrawn) {
        // Offered -> Idle at the ack cutoff: the message stays in the ticker, class shown.
        if (withdrawnClass.size() >= WITHDRAWN_KEEP) withdrawnClass.erase(withdrawnClass.begin());
        withdrawnClass[drillMsgId] = drill.Get().cls;
        Serial.printf("[drill] offer withdrawn at the ack cutoff: %s\n", drillMsgId.c_str());
        EndDrill();
    }

    UpdateVotes(now);

    // Complete/Aborted -> Idle on a dismiss tap (OnDrillTap) or after 60 s.
    const game::Phase ph = drill.Get().phase;
    if (ph == game::Phase::Complete || ph == game::Phase::Aborted) {
        if (endedAtUs == 0) endedAtUs = now;
        if (game::EndedDwellOver(endedAtUs, now)) {
            EndDrill();
            drillScreen = false;
            endedAtUs = 0;
        }
    } else {
        endedAtUs = 0;
    }
    if (drill.Get().phase == game::Phase::Idle) drillScreen = false;
}

void EamManager::DrainKeySamples(uint64_t upToUs)
{
    const game::Phase ph = drill.Get().phase;
    keyTouch.SetActive(drillScreen && (ph == game::Phase::Armed || ph == game::Phase::Window));
    if (!keyTouch.Active() || !drillScreen) return;
    KeyTouchSampler::Sample s;
    while (keyTouch.Next(upToUs, s)) {
        // Monotonic: a report stamped just before the last pass's drain but queued
        // after it is moved up to that instant (by at most one I2C read).
        const uint64_t t = s.tUs < lastKeySampleUs ? lastKeySampleUs : s.tUs;
        lastKeySampleUs = t;
        HandleDrillTouch(s.touched, s.x, s.y, t);
    }
}

void EamManager::UpdateVotes(uint64_t now)
{
    gameClient.Poll(now);
    if (!gameClient.Enabled()) return;
    const game::State& st = drill.Get();

    // The key turn was measured: send it, whether the device scored it in or out of
    // the window -- the server records a failed execution's timing too.
    if (st.executed && !executeSent) {
        executeSent = true;
        gameClient.Execute(st.deviation_us, true);  // solo path: the player's own enable (rail 2)
    }

    // 409 stale_config: refetch /config now, then re-derive. Same class and T under
    // the new epoch -> commit again under it; anything else -> the vote is refused.
    const eam::GameConfig& gc = feed.GameCfg();
    if (gameClient.ConsumeConfigRefetch()) {
        staleEpoch = gc.params.epoch;
        feed.RefetchConfigNow();
        Serial.printf("[game] stale_config under epoch %u: refetching /config\n", (unsigned)staleEpoch);
    }
    if (gameClient.AwaitingRefetch() && gc.valid && gc.params.epoch != staleEpoch) {
        const game::Derivation d = game::Derive(drillMsgId.c_str(), drillMsgId.length(),
                                                drillHeardAt.c_str(), gc.params);
        if (d.status == game::DeriveStatus::Ok && d.cls == drillDerivation.cls
            && d.t_at_ms == drillDerivation.t_at_ms) {
            gameClient.RecommitAfterRefetch(gc.params.epoch);
        } else {
            gameClient.RefuseStale();
        }
    }

    // The server's resolution -> VoteResolved. Only Committed takes it; it is held
    // until then (a commit refused early surfaces once the key has turned).
    if (st.phase == game::Phase::Committed) {
        game::VoteOutcome o;
        const char* reason = nullptr;
        if (gameClient.TakeResolution(o, reason)) {
            game::EventArgs a;
            a.outcome = o;
            a.reason = reason;
            drill.Step(game::Event::VoteResolved, now, a);
            Serial.printf("[drill] vote resolved -> %s\n",
                          drill.Get().phase == game::Phase::Terminal ? "terminal" : "aborted");
        }
    }
}

void EamManager::EndDrill()
{
    drill.Reset();
    gameClient.EndDrill();
    keyTouch.SetActive(false);
    executeSent = false;
}

void EamManager::HandleDrillTouch(bool touched, int x, int y, uint64_t tUs)
{
    // The SAMPLE's instant: in the key window, the panel report's edge time.
    const uint64_t now = tUs;
    const game::Phase ph = drill.Get().phase;

    // THE KEY TURN: every sample goes to the recogniser while armed or in the window,
    // EXCEPT a press that starts on ABORT, which stays a tap.
    const bool keyPhase = ph == game::Phase::Armed || ph == game::Phase::Window;
    const bool pressOnAbort = drillPressed
        ? game::AbortRect(SCREEN_SIZE).Contains(drillPressX, drillPressY)
        : (touched && game::AbortRect(SCREEN_SIZE).Contains(x, y));
    if (keyPhase && !pressOnAbort) {
        switch (keyTurn.Sample(touched, x, y, now)) {
            case game::KeyEvent::Arc:     drill.Step(game::Event::PlayerKeyArc, now); break;
            case game::KeyEvent::Confirm: drill.Step(game::Event::PlayerKeyTurn, now);
                                          Serial.printf("[drill] key turn, deviation_us=%lld\n",
                                                        (long long)drill.Get().deviation_us);
                                          break;
            case game::KeyEvent::Release: drill.Step(game::Event::PlayerKeyRelease, now); break;
            case game::KeyEvent::None:    break;
        }
    } else if (!keyPhase) {
        keyTurn.Reset();
    }

    // Taps: down, then up within 40 px and 1 s. No holds anywhere but the key turn.
    if (touched) {
        if (!drillPressed) {
            drillPressed = true;
            drillPressX = x;
            drillPressY = y;
            drillPressMs = millis();
        }
        drillLastX = x;
        drillLastY = y;
        return;
    }
    if (!drillPressed) return;
    drillPressed = false;
    if (abs(drillLastX - drillPressX) < 40 && abs(drillLastY - drillPressY) < 40
        && millis() - drillPressMs < 1000) {
        OnDrillTap(drillPressX, drillPressY);
    }
}

void EamManager::OnDrillTap(int x, int y)
{
    const uint64_t now = NowUs();
    const game::Phase ph = drill.Get().phase;

    if (ph == game::Phase::Complete || ph == game::Phase::Aborted) {
        EndDrill();             // dismiss: back to Idle
        drillScreen = false;
        endedAtUs = 0;
        return;
    }
    if (game::AbortRect(SCREEN_SIZE).Contains(x, y)) {
        drill.Step(game::Event::PlayerAbort, now);
        gameClient.Abort();  // after the ack the server holds a vote: POST /votes/:id/abort now
        Serial.printf("[drill] abort %s\n", drillMsgId.c_str());
        return;
    }
    switch (ph) {
        case game::Phase::Decoded:
        case game::Phase::Authenticate:
            drill.Step(game::Event::PlayerAck, now);
            // §4 "Acking commits you": the ack out of Authenticate is the vote's commit.
            if (ph == game::Phase::Authenticate && drill.Get().phase == game::Phase::WarPlan)
                gameClient.Commit(feed.GameCfg().params.epoch);
            break;
        case game::Phase::WarPlan:
            drill.Step(game::Event::PlayerConfirmWarPlan, now);
            break;
        case game::Phase::Enable: {
            // ARMING NEEDS A SYNCED CLOCK (Fable, 2026-09-23). T is placed on the
            // monotonic clock only with a fresh sync; otherwise the machine is given no
            // T and refuses in place with "clock not synced".
            uint64_t t = 0;
            if (drillDerivation.t_at_ms > 0 && ClockFreshNow())
                t = game::MonoForUtcMs(drillDerivation.t_at_ms, UtcMsNow(), now);
            drill.SetT(t);
            drill.Step(game::Event::PlayerEnable, now);
            Serial.printf("[drill] enable: %s (sync age %llds)\n", t ? "armed" : "refused, clock",
                          clocksync::HaveSync()
                              ? (long long)((now - clocksync::LastSyncMonoUs()) / 1000000ull)
                              : -1LL);
            break;
        }
        default:
            break; // Printing, Armed, Window, Committed, Terminal: taps do nothing
    }
}
#endif
