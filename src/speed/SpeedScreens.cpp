#include "SpeedManager.h"

#include <math.h>
#include <time.h>

#include "Layout.h"
#include "DeviceIdentity.h"

// The Speedscope edition screens. Each draws one full frame into the band canvas in absolute screen
// coordinates. Colours come from the palette scaled by GlowFactor() so night-dim fades everything
// uniformly. Screens degrade gracefully (show "--") when a field hasn't landed. Speeds are shown in
// the camera's own unit (UnitLabel()); over-limit passes read red, at/under read green.

namespace {

String FitWidth(BandCanvas& c, String s, int maxW)
{
    if (c.textWidth(s) <= maxW) return s;
    while (s.length() > 1 && c.textWidth(s + "...") > maxW) s.remove(s.length() - 1);
    return s + "...";
}

// Proximity "in range" threshold the camera would fire a photo at (0 = proximity gate disabled).
int FireThreshold(const speed::DeviceState& st)
{
    if (st.photoSignal > 0) return st.photoSignal;
    if (st.minSignal > 0)   return st.minSignal;
    return 0;
}

} // namespace

// --------------------------------------------------------------------------------- splash
void SpeedManager::DrawSplash(BandCanvas& c)
{
    const float gf = GlowFactor();
    const uint32_t fg     = speed::ScaleColor(palette.fg, gf);
    const uint32_t dim    = speed::ScaleColor(palette.dim, gf);
    const uint32_t accent = speed::ScaleColor(palette.accent, gf);

    c.setTextSize(4);
    CenterText(c, "Speedscope", SCREEN_SIZE_DIV_2 - 44, fg);
    c.setTextSize(1);
    CenterText(c, "traffic speed radar", SCREEN_SIZE_DIV_2 - 6, dim);

    const String hostLabel = backendBaseUrl.length() ? backendBaseUrl
                            : (camHost.length() ? camHost : String("MiniSpeedCam"));

    String hint;
    if (feedOrigin.isEmpty())        hint = String("resolving ") + hostLabel + "...";
    else if (!DeviceOnline())        hint = "connecting to the camera...";
    else                             hint = "waiting for the first pass...";
    c.setTextSize(1);
    CenterText(c, hint, SCREEN_SIZE_DIV_2 + 30, accent);

    c.setTextSize(1);
    CenterText(c, String("camera: ") + hostLabel, SCREEN_SIZE - 52, dim);
    String host = "http://" + DeviceIdentity::Name() + ".local";
    host.toLowerCase();
    CenterText(c, host, SCREEN_SIZE - 32, dim);
}

// --------------------------------------------------------------------------------- clock
void SpeedManager::DrawClock(BandCanvas& c)
{
    const float gf = GlowFactor();
    const uint32_t fg  = speed::ScaleColor(palette.fg, gf);
    const uint32_t dim = speed::ScaleColor(palette.dim, gf);

    const time_t utc = time(nullptr);
    if (utc < 1600000000) {
        c.setTextSize(2);
        CenterText(c, "syncing time...", SCREEN_SIZE_DIV_2, dim);
        return;
    }
    time_t local = utc + tzOffsetSec;
    struct tm t; gmtime_r(&local, &t);

    char hhmm[8];
    snprintf(hhmm, sizeof(hhmm), "%02d:%02d", t.tm_hour, t.tm_min);
    c.setTextSize(7);
    CenterText(c, hhmm, SCREEN_SIZE_DIV_2 - 30, fg);

    static const char* days[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    static const char* mon[]  = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                 "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    char date[24];
    snprintf(date, sizeof(date), "%s %s %d", days[t.tm_wday & 7], mon[t.tm_mon % 12], t.tm_mday);
    c.setTextSize(2);
    CenterText(c, date, SCREEN_SIZE_DIV_2 + 40, dim);
}

// --------------------------------------------------------------------------------- last pass
void SpeedManager::DrawLast(BandCanvas& c)
{
    const float gf = GlowFactor();
    const uint32_t fg    = speed::ScaleColor(palette.fg, gf);
    const uint32_t dim   = speed::ScaleColor(palette.dim, gf);
    const uint32_t faint = speed::ScaleColor(palette.faint, gf);

    c.setTextSize(2);
    CenterText(c, "LAST PASS", 26, fg);

    if (feed.Events().events.empty()) {
        c.setTextSize(2);
        CenterText(c, "no passes yet", SCREEN_SIZE_DIV_2, dim);
        return;
    }
    const speed::SpeedRecord& e = feed.Events().events.front();

    // Big speed, coloured against the posted limit (if any).
    const uint32_t sc = speed::ScaleColor(speed::SpeedColor(palette, e.speed, speedLimit), gf);
    char big[12];
    snprintf(big, sizeof(big), "%d", e.speed);
    c.setTextSize(9);
    CenterText(c, big, SCREEN_SIZE_DIV_2 - 56, sc);
    c.setTextSize(2);
    CenterText(c, UnitLabel(), SCREEN_SIZE_DIV_2 + 34, dim);

    // Direction + how long ago.
    String meta = FormatAgo(e.epoch, e.ageSec) + " ago";
    const char* dl = speed::DirLabel(e.dir);
    if (dl[0]) meta = String(dl) + "  \xB7  " + meta;   // 0xB7 = middot
    c.setTextSize(2);
    CenterText(c, meta, SCREEN_SIZE_DIV_2 + 70, fg);

    // Over/under-limit chip.
    if (speedLimit > 0) {
        const int delta = e.speed - speedLimit;
        char chip[32];
        if (delta > 0) snprintf(chip, sizeof(chip), "+%d over limit", delta);
        else           snprintf(chip, sizeof(chip), "under limit (%d)", speedLimit);
        c.setTextSize(2);
        CenterText(c, chip, SCREEN_SIZE_DIV_2 + 104,
                   speed::ScaleColor(delta > 0 ? palette.alert : palette.good, gf));
    }

    c.setTextSize(1);
    CenterText(c, "tap for recent", SCREEN_SIZE - 28, faint);
}

// --------------------------------------------------------------------------------- live proximity
void SpeedManager::DrawLive(BandCanvas& c)
{
    const float gf = GlowFactor();
    const uint32_t fg    = speed::ScaleColor(palette.fg, gf);
    const uint32_t dim   = speed::ScaleColor(palette.dim, gf);
    const uint32_t faint = speed::ScaleColor(palette.faint, gf);

    c.setTextSize(2);
    CenterText(c, "LIVE", 26, fg);

    const speed::DeviceState& st = feed.State();
    const bool online = DeviceOnline();
    if (!online) {
        c.setTextSize(2);
        CenterText(c, "camera offline", SCREEN_SIZE_DIV_2, speed::ScaleColor(palette.alert, gf));
        c.setTextSize(1);
        CenterText(c, feedOrigin.length() ? feedOrigin : String("no host"), SCREEN_SIZE_DIV_2 + 30, faint);
        return;
    }

    const long sig = st.signal;
    const int  thr = FireThreshold(st);
    // Scale so a "fire" reading fills most of the gauge; fall back to a nominal ceiling if the
    // camera's proximity gate is off (thr == 0).
    double scaleMax = (thr > 0) ? (double)thr * 1.4 : 2000.0;
    if ((double)sig > scaleMax) scaleMax = (double)sig;
    if (scaleMax < 1) scaleMax = 1;
    double frac = (double)sig / scaleMax;
    if (frac < 0) frac = 0;
    if (frac > 1) frac = 1;

    // Status colour: clear (green) -> motion (amber) -> in range (red).
    uint32_t stateCol; const char* word;
    if (sig <= 0)                    { stateCol = palette.good;  word = "CLEAR"; }
    else if (thr > 0 && sig >= thr)  { stateCol = palette.alert; word = "IN RANGE"; }
    else                             { stateCol = palette.warn;  word = "MOTION"; }
    stateCol = speed::ScaleColor(stateCol, gf);

    // Speedometer-style arc gauge (270-deg sweep opening at the bottom).
    const int cx = SCREEN_SIZE_DIV_2, cy = SCREEN_SIZE_DIV_2 - 4;
    const int rOut = SCREEN_SIZE_DIV_2 - 22, rIn = SCREEN_SIZE_DIV_2 - 40;
    const float a0 = 135.0f, span = 270.0f;
    c.drawArc(cx, cy, rIn, rOut, a0, a0 + span, faint);
    if (frac > 0.001) c.fillArc(cx, cy, rIn, rOut, a0, a0 + span * (float)frac, stateCol);

    c.setTextSize(3);
    CenterText(c, word, cy - 14, stateCol);
    char sigbuf[24];
    snprintf(sigbuf, sizeof(sigbuf), "signal %ld", sig);
    c.setTextSize(1);
    CenterText(c, sigbuf, cy + 16, dim);

    // Last measured speed, if we have one.
    if (!feed.Events().events.empty()) {
        const speed::SpeedRecord& e = feed.Events().events.front();
        char lp[32];
        snprintf(lp, sizeof(lp), "last %d %s", e.speed, UnitLabel());
        c.setTextSize(2);
        CenterText(c, lp, SCREEN_SIZE - 40, fg);
    } else {
        c.setTextSize(1);
        CenterText(c, "monitoring for traffic", SCREEN_SIZE - 30, faint);
    }
}

// --------------------------------------------------------------------------------- recent list
void SpeedManager::DrawList(BandCanvas& c)
{
    const float gf = GlowFactor();
    const uint32_t fg    = speed::ScaleColor(palette.fg, gf);
    const uint32_t dim   = speed::ScaleColor(palette.dim, gf);
    const uint32_t faint = speed::ScaleColor(palette.faint, gf);

    c.setTextSize(2);
    CenterText(c, "RECENT", 26, fg);

    const auto& ev = feed.Events().events;
    if (ev.empty()) {
        c.setTextSize(2);
        CenterText(c, "no passes yet", SCREEN_SIZE_DIV_2, dim);
        return;
    }

    int y = 64;
    const int maxRows = 6;
    for (int i = 0; i < (int)ev.size() && i < maxRows; ++i) {
        const speed::SpeedRecord& e = ev[i];
        const uint32_t col = speed::ScaleColor(speed::SpeedColor(palette, e.speed, speedLimit), gf);
        char left[20];
        snprintf(left, sizeof(left), "%d %s", e.speed, UnitLabel());
        char right[24];
        const char* dg = speed::DirGlyph(e.dir);
        snprintf(right, sizeof(right), "%s %s", FormatAgo(e.epoch, e.ageSec).c_str(), dg[0] ? dg : "");
        c.setTextSize(2);
        c.setTextColor(col);
        c.drawString(left, 40, y);
        c.setTextColor(dim);
        c.drawString(right, SCREEN_SIZE - 40 - c.textWidth(right), y);
        y += 32;
    }

    c.setTextSize(1);
    CenterText(c, "tap for the full list", SCREEN_SIZE - 26, faint);
}

// --------------------------------------------------------------------------------- today stats
void SpeedManager::DrawStats(BandCanvas& c)
{
    const float gf = GlowFactor();
    const uint32_t fg    = speed::ScaleColor(palette.fg, gf);
    const uint32_t dim   = speed::ScaleColor(palette.dim, gf);
    const uint32_t faint = speed::ScaleColor(palette.faint, gf);

    c.setTextSize(2);
    CenterText(c, "TODAY", 26, fg);

    int count, top, over; double avg;
    ComputeToday(count, top, over, avg);

    char big[12];
    snprintf(big, sizeof(big), "%d", count);
    c.setTextSize(8);
    CenterText(c, big, SCREEN_SIZE_DIV_2 - 46, fg);
    c.setTextSize(1);
    CenterText(c, count == 1 ? "pass today" : "passes today", SCREEN_SIZE_DIV_2 + 26, dim);

    int y = SCREEN_SIZE_DIV_2 + 50;
    char line[40];
    snprintf(line, sizeof(line), "top %d   avg %.0f %s", top, avg, UnitLabel());
    c.setTextSize(2);
    CenterText(c, line, y, fg); y += 28;

    if (speedLimit > 0) {
        const int pct = count > 0 ? (int)lround(100.0 * over / count) : 0;
        snprintf(line, sizeof(line), "%d over %d (%d%%)", over, speedLimit, pct);
        c.setTextSize(2);
        CenterText(c, line, y, speed::ScaleColor(over > 0 ? palette.warn : palette.good, gf));
    }

    // Sparkline of the most recent passes (newest at the right), scaled to the day's top.
    const auto& ev = feed.Events().events;
    if (!ev.empty() && top > 0) {
        const int bars = (int)ev.size() < 16 ? (int)ev.size() : 16;
        const int barW = 6, gap = 3, baseY = SCREEN_SIZE - 26, maxH = 34;
        const int totalW = bars * (barW + gap) - gap;
        int x = SCREEN_SIZE_DIV_2 + totalW / 2 - barW;   // newest (index 0) on the right
        for (int i = 0; i < bars; ++i) {
            const speed::SpeedRecord& e = ev[i];
            int h = (int)lround((double)e.speed / top * maxH);
            if (h < 2) h = 2;
            const uint32_t col = speed::ScaleColor(speed::SpeedColor(palette, e.speed, speedLimit), gf);
            c.fillRect(x, baseY - h, barW, h, col);
            x -= (barW + gap);
        }
    } else {
        c.setTextSize(1);
        CenterText(c, "tap for the breakdown", SCREEN_SIZE - 22, faint);
    }
}

// --------------------------------------------------------------------------------- device health
void SpeedManager::DrawDevice(BandCanvas& c)
{
    const float gf = GlowFactor();
    const uint32_t fg    = speed::ScaleColor(palette.fg, gf);
    const uint32_t dim   = speed::ScaleColor(palette.dim, gf);
    const uint32_t faint = speed::ScaleColor(palette.faint, gf);

    c.setTextSize(2);
    CenterText(c, "CAMERA", 26, fg);

    const speed::DeviceState& st = feed.State();
    const bool online = DeviceOnline();

    // Online pill.
    c.setTextSize(2);
    CenterText(c, online ? "ONLINE" : "OFFLINE",
               58, speed::ScaleColor(online ? palette.good : palette.alert, gf));

    int y = 92;
    auto row = [&](const String& label, const String& val, uint32_t col) {
        c.setTextSize(1);
        c.setTextColor(dim);   c.drawString(label, 40, y);
        c.setTextColor(col);   c.drawString(val, SCREEN_SIZE - 40 - c.textWidth(val), y);
        y += 22;
    };

    row("ip",       st.ip.length() ? st.ip : String("--"), fg);
    row("wifi",     st.rssi == "n/a" ? String("--") : (st.rssi + " dBm"), fg);
    row("uptime",   st.uptime.length() ? st.uptime : String("--"), fg);
    char mem[24]; snprintf(mem, sizeof(mem), "%ld / %ldk", st.freeHeap / 1024, st.freePsram / 1024);
    row("heap/psram", mem, fg);

    const uint32_t upCol = (st.lastUpload == 200) ? speed::ScaleColor(palette.good, gf)
                         : (st.lastUpload == 0)   ? faint
                                                  : speed::ScaleColor(palette.warn, gf);
    row("last upload", st.lastUpload ? String(st.lastUpload) : String("--"), upCol);
    row("passes seen", String((int)feed.Events().events.size()), fg);
    row("linked", st.claim.startsWith("Linked") ? String("yes") : String("no"),
        st.claim.startsWith("Linked") ? speed::ScaleColor(palette.good, gf) : dim);

    c.setTextSize(1);
    CenterText(c, FitWidth(c, st.fwVersion.length() ? st.fwVersion : String("MiniSpeedCam"), SCREEN_SIZE - 60),
               SCREEN_SIZE - 26, faint);
}

// --------------------------------------------------------------------------------- detail card
void SpeedManager::DrawDetailCard(BandCanvas& c)
{
    const float gf = GlowFactor();
    const uint32_t fg     = speed::ScaleColor(palette.fg, gf);
    const uint32_t dim    = speed::ScaleColor(palette.dim, gf);
    const uint32_t faint  = speed::ScaleColor(palette.faint, gf);
    const uint32_t accent = speed::ScaleColor(palette.accent, gf);

    const int m = 18;
    c.fillRoundRect(m, m, SCREEN_SIZE - 2 * m, SCREEN_SIZE - 2 * m, 16, palette.bg);
    c.drawRoundRect(m, m, SCREEN_SIZE - 2 * m, SCREEN_SIZE - 2 * m, 16, accent);

    int y = 44;
    auto row = [&](const String& s, uint32_t col) { c.setTextSize(2); CenterText(c, s, y, col); y += 28; };
    char buf[48];

    switch (detailFor) {
        case Screen::Last:
        case Screen::List: {
            c.setTextSize(2); CenterText(c, "RECENT PASSES", y, fg); y += 40;
            const auto& ev = feed.Events().events;
            int shown = 0;
            for (const speed::SpeedRecord& e : ev) {
                const char* dg = speed::DirGlyph(e.dir);
                snprintf(buf, sizeof(buf), "%d %s  %s  %s", e.speed, UnitLabel(),
                         FormatAgo(e.epoch, e.ageSec).c_str(), dg[0] ? dg : " ");
                row(buf, speed::ScaleColor(speed::SpeedColor(palette, e.speed, speedLimit), gf));
                if (++shown >= 7) break;
            }
            if (!shown) row("no passes yet", dim);
            break;
        }
        case Screen::Stats: {
            c.setTextSize(2); CenterText(c, "TODAY", y, fg); y += 40;
            int count, top, over; double avg;
            ComputeToday(count, top, over, avg);
            snprintf(buf, sizeof(buf), "passes   %d", count);        row(buf, fg);
            snprintf(buf, sizeof(buf), "top      %d %s", top, UnitLabel()); row(buf, fg);
            snprintf(buf, sizeof(buf), "average  %.0f %s", avg, UnitLabel()); row(buf, fg);
            if (speedLimit > 0) {
                const int pct = count > 0 ? (int)lround(100.0 * over / count) : 0;
                snprintf(buf, sizeof(buf), "over %d  %d (%d%%)", speedLimit, over, pct);
                row(buf, speed::ScaleColor(over > 0 ? palette.warn : palette.good, gf));
            }
            break;
        }
        case Screen::Live: {
            c.setTextSize(2); CenterText(c, "RADAR SIGNAL", y, fg); y += 40;
            const speed::DeviceState& st = feed.State();
            snprintf(buf, sizeof(buf), "signal    %ld", st.signal);      row(buf, fg);
            snprintf(buf, sizeof(buf), "arm >=    %d", st.minSignal);    row(buf, dim);
            snprintf(buf, sizeof(buf), "fire >=   %d", st.photoSignal);  row(buf, dim);
            snprintf(buf, sizeof(buf), "min speed %d %s", st.minSpeed, UnitLabel());   row(buf, dim);
            snprintf(buf, sizeof(buf), "photo >=  %d %s", st.photoSpeed, UnitLabel()); row(buf, dim);
            break;
        }
        case Screen::Device:
        default: {
            c.setTextSize(2); CenterText(c, "CAMERA", y, fg); y += 40;
            const speed::DeviceState& st = feed.State();
            row(DeviceOnline() ? String("online") : String("offline"),
                speed::ScaleColor(DeviceOnline() ? palette.good : palette.alert, gf));
            row(String("ip ") + (st.ip.length() ? st.ip : String("--")), fg);
            row(String("ssid ") + (st.ssid.length() ? st.ssid : String("--")), dim);
            row(String("power-saver ") + (st.powerSaver ? "on" : "off"), dim);
            row(st.claim.length() ? st.claim : String("not linked"), dim);
            break;
        }
    }

    c.setTextSize(1);
    CenterText(c, "tap to close", SCREEN_SIZE - 40, faint);
}

// --------------------------------------------------------------------------------- chrome
void SpeedManager::DrawScreenDots(BandCanvas& c, const std::vector<Screen>& rot) const
{
    const int n = (int)rot.size();
    if (n <= 1) return;
    const int gap = 10;
    const int totalW = (n - 1) * gap;
    int x = SCREEN_SIZE_DIV_2 - totalW / 2;
    const int y = SCREEN_SIZE - 12;
    for (int i = 0; i < n; ++i) {
        const bool active = (rot[i] == current);
        c.fillCircle(x, y, active ? 2 : 1, active ? palette.fg : palette.faint);
        x += gap;
    }
}
