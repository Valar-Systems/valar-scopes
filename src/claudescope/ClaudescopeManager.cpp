#include "ClaudescopeManager.h"
#include "TouchPoll.h"
#include "SolarDim.h"

#include <math.h>
#include <time.h>

#include "Layout.h"

// Optional sidecar base-URL default. Normally NOT injected (Claudescope bakes in no backend -- the
// user points it at their own claudescope-sidecar); guarded so the file compiles whether or not
// -DCLAUDE_FEED_BASE is set. The runtime value ("cl-base-url") overrides it.
#ifndef CLAUDE_FEED_BASE
#define CLAUDE_FEED_BASE ""
#endif

namespace {

constexpr unsigned long AUTO_DWELL_MS    = 8000;   // ms per screen when auto-rotating
constexpr unsigned long INTERACT_HOLD_MS = 30000;  // pause auto-rotate this long after a touch


} // namespace

void ClaudescopeManager::Initialise()
{
    palette = claudescope::PaletteDefault();

    // The sidecar's address (required; empty = the setup splash stays up). No baked-in backend.
    backendBaseUrl = configServer.GetStoredString("cl-base-url");
    if (backendBaseUrl.isEmpty())
        backendBaseUrl = CLAUDE_FEED_BASE;

    const String tz = configServer.GetStoredString("cl-tz-offset");
    tzOffsetSec = tz.isEmpty() ? 0 : (long)lround(tz.toFloat() * 3600.0f);

    const String latStr = configServer.GetStoredString("latitude");
    const String lonStr = configServer.GetStoredString("longitude");
    hasLatLon = latStr.length() && lonStr.length();
    deviceLat = latStr.toDouble();
    deviceLon = lonStr.toDouble();

    // All data screens are always enabled; each still self-drops from the rotation until it has data.
    enabledOrder.clear();
    enabledOrder.push_back(Screen::Session);
    enabledOrder.push_back(Screen::Weekly);
    enabledOrder.push_back(Screen::Clock);
    enabledOrder.push_back(Screen::Splash);

    const String br = configServer.GetStoredString("brightness");
    configuredBrightness = br.isEmpty() ? 255 : (uint8_t)constrain(br.toInt(), 10, 255);
    const String ad = configServer.GetStoredString("autodim");
    autoDim = ad.isEmpty() || ad == "true";

    auto boolCfg = [&](const char* k, bool def) {
        const String v = configServer.GetStoredString(k);
        return v.isEmpty() ? def : (v == "true");
    };
    auto numCfg = [&](const char* k, float def) {
        const String v = configServer.GetStoredString(k);
        return v.isEmpty() ? def : v.toFloat();
    };
    ntfyTopic = configServer.GetStoredString("ntfy-topic");
    ntfy.SetTopic(ntfyTopic);
    alertSession = boolCfg("cl-alert-sess", true);
    alertWeek    = boolCfg("cl-alert-week", true);
    sessionPctThresh = constrain(numCfg("cl-session-pct", 80.0f), 1.0f, 100.0f);
    weekPctThresh    = constrain(numCfg("cl-week-pct", 80.0f), 1.0f, 100.0f);

    ClaudescopeFeedClient::Config fcfg;
    fcfg.baseUrl = backendBaseUrl;
    fcfg.intervalScale = 1.0f;
    feed.Begin();
    feed.Configure(fcfg);

    currentBrightness = configuredBrightness;
    tft.setBrightness(currentBrightness);
    lastBrightnessCheck = 0;

    // A re-init (config save) shouldn't re-fire alerts for a limit already high.
    alertSeeded = false;
    lastSessionSide = 0;
    lastWeekSide = 0;

    Serial.printf("[claude] init; sidecar=%s latlon=%d ntfy=%d\n",
                  backendBaseUrl.isEmpty() ? "(none)" : backendBaseUrl.c_str(),
                  (int)hasLatLon, (int)!ntfyTopic.isEmpty());
}

void ClaudescopeManager::Update()
{
    feed.Poll();
    CheckAlerts();
    ntfy.Pump(http);
    UpdateBrightness();
    HandleTouch();
    AutoRotate();
}

void ClaudescopeManager::Draw(BandCanvas& backbuffer, bool /*firstPass*/)
{
    std::vector<Screen> rot = BuildRotation();
    bool inRot = false;
    for (Screen s : rot) if (s == current) { inRot = true; break; }
    if (!inRot && !rot.empty()) current = rot.front();

    DrawScreen(backbuffer, current);

    if (inDetail) DrawDetailCard(backbuffer);
    else DrawScreenDots(backbuffer, rot);
}

void ClaudescopeManager::DrawScreen(BandCanvas& c, Screen s)
{
    switch (s) {
        case Screen::Session: DrawSession(c); break;
        case Screen::Weekly:  DrawWeekly(c); break;
        case Screen::Splash:  DrawSplash(c); break;
        case Screen::Clock:
        default:              DrawClock(c); break;
    }
}

// ----------------------------------------------------------------------------- rotation

bool ClaudescopeManager::HasData(Screen s) const
{
    const claudescope::UsageState& u = feed.Usage();
    switch (s) {
        case Screen::Session: return u.session.valid;
        case Screen::Weekly:  return u.weekAll.valid || !u.weekModels.empty();
        case Screen::Clock:   return true;                 // always-available idle screen
        case Screen::Splash:  return !u.valid;             // cold-start welcome, self-drops on data
        default:              return false;
    }
}

std::vector<ClaudescopeManager::Screen> ClaudescopeManager::BuildRotation() const
{
    std::vector<Screen> rot;
    for (Screen s : enabledOrder)
        if (HasData(s)) rot.push_back(s);
    if (rot.empty()) rot.push_back(Screen::Clock);
    return rot;
}

void ClaudescopeManager::AdvanceRotation(int dir)
{
    std::vector<Screen> rot = BuildRotation();
    int idx = 0;
    for (int i = 0; i < (int)rot.size(); ++i) if (rot[i] == current) { idx = i; break; }
    idx = (idx + dir + (int)rot.size()) % (int)rot.size();
    current = rot[idx];
    lastAdvanceMs = millis();
}

void ClaudescopeManager::AutoRotate()
{
    if (inDetail) return;                                        // hold while inspecting
    if (millis() - lastInteractionMs < INTERACT_HOLD_MS) return; // user is driving
    if (millis() - lastAdvanceMs < AUTO_DWELL_MS) return;
    AdvanceRotation(+1);
}

// ----------------------------------------------------------------------------- input

void ClaudescopeManager::HandleTouch()
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
        AdvanceRotation(dx < 0 ? +1 : -1);   // swipe left -> next, right -> prev
    }
}

void ClaudescopeManager::HandleTap(int /*tx*/, int /*ty*/)
{
    if (inDetail) { ExitDetail(); return; }
    // Tap a data screen to open its detail card (chrome screens have nothing to expand).
    if (current != Screen::Splash && current != Screen::Clock && HasData(current)) {
        detailFor = current;
        inDetail = true;
    }
}

// ----------------------------------------------------------------------------- alerts

void ClaudescopeManager::CheckAlerts()
{
    const claudescope::UsageState& u = feed.Usage();
    if (!u.valid) return;

    int sessionSide = 0;
    if (u.session.valid)
        sessionSide = u.session.pct >= sessionPctThresh ? +1 : -1;

    int weekSide = 0;
    if (u.weekAll.valid)
        weekSide = u.weekAll.pct >= weekPctThresh ? +1 : -1;

    if (!alertSeeded) {
        alertSeeded = true;
        lastSessionSide = sessionSide;
        lastWeekSide = weekSide;
        return; // never alert for the state present at boot / re-config
    }

    // Session crossed up through the threshold.
    if (alertSession && sessionSide == +1 && lastSessionSide == -1) {
        char body[80];
        snprintf(body, sizeof(body), "Session at %.0f%% (>= %.0f%%), resets in %s",
                 u.session.pct, sessionPctThresh, FormatCountdown(u.session.resetEpoch - (long)time(nullptr)).c_str());
        SendNtfy("Claude session limit", body, "hourglass_flowing_sand,warning", 4);
    }
    if (sessionSide != 0) lastSessionSide = sessionSide;

    // Weekly crossed up through the threshold.
    if (alertWeek && weekSide == +1 && lastWeekSide == -1) {
        char body[80];
        snprintf(body, sizeof(body), "Weekly (all models) at %.0f%% (>= %.0f%%)",
                 u.weekAll.pct, weekPctThresh);
        SendNtfy("Claude weekly limit", body, "calendar,warning", 4);
    }
    if (weekSide != 0) lastWeekSide = weekSide;
}

void ClaudescopeManager::SendNtfy(const String& title, const String& body, const String& tags, int priority)
{
    // Queue it; NtfyAlerter defers (not drops) co-triggered alerts and Update() pumps the
    // queue, keeping the 5 s spacing between POSTs. The edge-state advance in the callers
    // is therefore safe -- a throttled alert is delivered later, not lost.
    ntfy.Send(title, body, tags, priority);
}

// ----------------------------------------------------------------------------- brightness

void ClaudescopeManager::UpdateBrightness()
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

void ClaudescopeManager::CenterText(BandCanvas& c, const String& s, int y, uint32_t color)
{
    c.setTextColor(color);
    c.drawString(s, SCREEN_SIZE_DIV_2 - c.textWidth(s) / 2, y);
}

String ClaudescopeManager::FormatCountdown(long secsUntil)
{
    if (secsUntil <= 0) return "now";
    const long d = secsUntil / 86400;
    const long h = (secsUntil % 86400) / 3600;
    const long m = (secsUntil % 3600) / 60;
    char b[24];
    if (d > 0)      snprintf(b, sizeof(b), "%ldd %02ldh", d, h);
    else if (h > 0) snprintf(b, sizeof(b), "%ldh%02ldm", h, m);
    else            snprintf(b, sizeof(b), "%ldm", m);
    return String(b);
}

String ClaudescopeManager::FormatResetClock(long epoch, long tzOffsetSec)
{
    if (epoch < 1600000000) return "";
    static const char* days[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    time_t t = (time_t)(epoch + tzOffsetSec);
    struct tm g; gmtime_r(&t, &g);
    char b[16];
    snprintf(b, sizeof(b), "%s %02d:%02d", days[g.tm_wday & 7], g.tm_hour, g.tm_min);
    return String(b);
}
