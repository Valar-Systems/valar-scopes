#include "ClaudescopeManager.h"

#include <math.h>
#include <time.h>

#include "Layout.h"
#include "DeviceIdentity.h"

// The Claudescope screens. Each draws one full frame into the band canvas in absolute screen
// coordinates. Colours come from the palette scaled by GlowFactor() so night-dim fades everything
// uniformly. Screens degrade gracefully (show "--") when a field hasn't landed.

namespace {

// A 270-degree gauge opening at the bottom (matches SpaceManager's Kp dial convention).
constexpr float GAUGE_START = 135.0f;
constexpr float GAUGE_SWEEP = 270.0f;

String FitWidth(BandCanvas& c, String s, int maxW)
{
    if (c.textWidth(s) <= maxW) return s;
    while (s.length() > 1 && c.textWidth(s + "...") > maxW) s.remove(s.length() - 1);
    return s + "...";
}

} // namespace

// ------------------------------------------------------------------------------- gauge helper
void ClaudescopeManager::DrawRingGauge(BandCanvas& c, int cx, int cy, int rInner, int rOuter,
                                       float pct, uint32_t fill)
{
    const float gf = GlowFactor();
    const uint32_t faint  = claudescope::ScaleColor(palette.faint, gf);
    const uint32_t accent = claudescope::ScaleColor(palette.accent, gf);

    c.fillArc(cx, cy, rInner, rOuter, GAUGE_START, GAUGE_START + GAUGE_SWEEP, faint); // track
    if (isnan(pct)) return;
    float p = pct; if (p < 0) p = 0; if (p > 100) p = 100;
    const float ang = GAUGE_START + (p / 100.0f) * GAUGE_SWEEP;
    c.fillArc(cx, cy, rInner, rOuter, GAUGE_START, ang, fill);
    c.fillArc(cx, cy, rInner - 3, rOuter + 3, ang - 1.5f, ang + 1.5f, accent); // current-value tick
}

// ------------------------------------------------------------------------------- dots
void ClaudescopeManager::DrawScreenDots(BandCanvas& c, const std::vector<Screen>& rot) const
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

// ------------------------------------------------------------------------------- session
void ClaudescopeManager::DrawSession(BandCanvas& c)
{
    const float gf = GlowFactor();
    const uint32_t fg    = claudescope::ScaleColor(palette.fg, gf);
    const uint32_t dim   = claudescope::ScaleColor(palette.dim, gf);
    const uint32_t faint = claudescope::ScaleColor(palette.faint, gf);

    const claudescope::UsageState& u = feed.Usage();

    c.setTextSize(1);
    CenterText(c, "CURRENT SESSION", 18, dim);
    if (u.planName.length()) { c.setTextSize(1); CenterText(c, u.planName, 34, faint); }

    const int cx = SCREEN_SIZE_DIV_2, cy = SCREEN_SIZE_DIV_2 + 4;
    const int R = SCREEN_SIZE_DIV_2 - 22;
    const int r = R - 30;

    if (!u.session.valid) {
        DrawRingGauge(c, cx, cy, r, R, NAN, faint);
        c.setTextSize(2); CenterText(c, "--", cy - 8, dim);
        return;
    }

    const uint32_t col = claudescope::ScaleColor(claudescope::UtilColor(palette, u.session.pct), gf);
    DrawRingGauge(c, cx, cy, r, R, u.session.pct, col);

    char val[8]; snprintf(val, sizeof(val), "%.0f%%", u.session.pct);
    c.setTextSize(6); CenterText(c, val, cy - 40, fg);
    c.setTextSize(1); CenterText(c, "used", cy + 6, dim);

    // Reset countdown in the gauge's open bottom wedge.
    const long now = (long)time(nullptr);
    if (u.session.resetEpoch > now) {
        const String cd = "resets in " + FormatCountdown(u.session.resetEpoch - now);
        c.setTextSize(2); CenterText(c, cd, SCREEN_SIZE - 40, col);
    }
}

// ------------------------------------------------------------------------------- weekly
void ClaudescopeManager::DrawWeekly(BandCanvas& c)
{
    const float gf = GlowFactor();
    const uint32_t fg    = claudescope::ScaleColor(palette.fg, gf);
    const uint32_t dim   = claudescope::ScaleColor(palette.dim, gf);
    const uint32_t faint = claudescope::ScaleColor(palette.faint, gf);

    const claudescope::UsageState& u = feed.Usage();

    c.setTextSize(2); CenterText(c, "WEEKLY", 26, fg);

    // Assemble the rows: "All models" first, then each per-model window.
    struct Row { String label; float pct; };
    std::vector<Row> rows;
    if (u.weekAll.valid) rows.push_back({"All models", u.weekAll.pct});
    for (const claudescope::UsageWindow& w : u.weekModels)
        rows.push_back({w.label.length() ? w.label : String("model"), w.pct});

    if (rows.empty()) { c.setTextSize(2); CenterText(c, "--", SCREEN_SIZE_DIV_2, dim); return; }

    // Horizontal bars centred in the disc.
    const int barW = SCREEN_SIZE - 120;
    const int barH = 18;
    const int barX = SCREEN_SIZE_DIV_2 - barW / 2;
    const int rowH = 46;
    int y = SCREEN_SIZE_DIV_2 - ((int)rows.size() * rowH) / 2 + 10;

    for (const Row& row : rows) {
        c.setTextSize(1);
        c.setTextColor(dim);
        c.drawString(FitWidth(c, row.label, barW), barX, y - 14);

        char pctStr[8]; snprintf(pctStr, sizeof(pctStr), "%.0f%%", row.pct);
        c.setTextColor(fg);
        c.drawString(pctStr, barX + barW - c.textWidth(pctStr), y - 14);

        // track + fill
        c.fillRoundRect(barX, y, barW, barH, barH / 2, faint);
        float p = row.pct; if (p < 0) p = 0; if (p > 100) p = 100;
        int w = (int)(barW * (p / 100.0f) + 0.5f);
        if (w < barH && p > 0) w = barH; // keep the rounded cap visible for tiny values
        const uint32_t col = claudescope::ScaleColor(claudescope::UtilColor(palette, row.pct), gf);
        if (w > 0) c.fillRoundRect(barX, y, w, barH, barH / 2, col);

        y += rowH;
    }

    // A single shared reset line (the weekly windows reset together).
    const long reset = u.weekAll.valid ? u.weekAll.resetEpoch
                     : (!u.weekModels.empty() ? u.weekModels.front().resetEpoch : 0);
    const String rc = FormatResetClock(reset, tzOffsetSec);
    if (rc.length()) { c.setTextSize(1); CenterText(c, "resets " + rc, SCREEN_SIZE - 34, dim); }
}

// ------------------------------------------------------------------------------- clock
void ClaudescopeManager::DrawClock(BandCanvas& c)
{
    const float gf = GlowFactor();
    const uint32_t fg  = claudescope::ScaleColor(palette.fg, gf);
    const uint32_t dim = claudescope::ScaleColor(palette.dim, gf);

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

// ------------------------------------------------------------------------------- splash
void ClaudescopeManager::DrawSplash(BandCanvas& c)
{
    const float gf = GlowFactor();
    const uint32_t fg     = claudescope::ScaleColor(palette.fg, gf);
    const uint32_t dim    = claudescope::ScaleColor(palette.dim, gf);
    const uint32_t accent = claudescope::ScaleColor(palette.accent, gf);

    c.setTextSize(4);
    CenterText(c, "Claudescope", SCREEN_SIZE_DIV_2 - 44, fg);
    c.setTextSize(1);
    CenterText(c, "Claude usage limits", SCREEN_SIZE_DIV_2 - 6, dim);

    // Context-sensitive setup hint.
    String hint;
    if (backendBaseUrl.isEmpty()) hint = "set the sidecar URL in web config";
    else                          hint = "waiting for the sidecar...";
    c.setTextSize(1);
    CenterText(c, hint, SCREEN_SIZE_DIV_2 + 30, accent);

    String host = "http://" + DeviceIdentity::Name() + ".local";
    host.toLowerCase();
    CenterText(c, host, SCREEN_SIZE - 36, dim);
}

// ------------------------------------------------------------------------------- detail card
void ClaudescopeManager::DrawDetailCard(BandCanvas& c)
{
    const float gf = GlowFactor();
    const uint32_t fg    = claudescope::ScaleColor(palette.fg, gf);
    const uint32_t dim   = claudescope::ScaleColor(palette.dim, gf);
    const uint32_t faint = claudescope::ScaleColor(palette.faint, gf);

    // Dim the frame behind the card so the overlay reads as a modal.
    c.fillRoundRect(24, 40, SCREEN_SIZE - 48, SCREEN_SIZE - 80, 14, palette.bg);
    c.drawRoundRect(24, 40, SCREEN_SIZE - 48, SCREEN_SIZE - 80, 14, faint);

    const claudescope::UsageState& u = feed.Usage();
    const long now = (long)time(nullptr);

    // One "label: value" row helper (left label / right value).
    int y = 78;
    auto row = [&](const String& label, const String& value, uint32_t vcol) {
        c.setTextSize(2);
        c.setTextColor(dim); c.drawString(label, 48, y);
        c.setTextColor(vcol); c.drawString(value, SCREEN_SIZE - 48 - c.textWidth(value), y);
        y += 34;
    };

    if (detailFor == Screen::Session) {
        c.setTextSize(2); CenterText(c, "SESSION", 52, fg);
        y = 96;
        if (u.session.valid) {
            char p[8]; snprintf(p, sizeof(p), "%.0f%%", u.session.pct);
            row("used", p, claudescope::ScaleColor(claudescope::UtilColor(palette, u.session.pct), gf));
            if (u.session.resetEpoch > now)
                row("resets in", FormatCountdown(u.session.resetEpoch - now), fg);
            const String rc = FormatResetClock(u.session.resetEpoch, tzOffsetSec);
            if (rc.length()) row("at", rc, dim);
        }
        if (u.planName.length()) row("plan", u.planName, dim);
    } else { // Weekly
        c.setTextSize(2); CenterText(c, "WEEKLY", 52, fg);
        y = 96;
        if (u.weekAll.valid) {
            char p[8]; snprintf(p, sizeof(p), "%.0f%%", u.weekAll.pct);
            row("all models", p, claudescope::ScaleColor(claudescope::UtilColor(palette, u.weekAll.pct), gf));
        }
        for (const claudescope::UsageWindow& w : u.weekModels) {
            if (y > SCREEN_SIZE - 90) break; // don't overflow the card
            char p[8]; snprintf(p, sizeof(p), "%.0f%%", w.pct);
            row(w.label.length() ? w.label : String("model"), p,
                claudescope::ScaleColor(claudescope::UtilColor(palette, w.pct), gf));
        }
        const long reset = u.weekAll.valid ? u.weekAll.resetEpoch
                         : (!u.weekModels.empty() ? u.weekModels.front().resetEpoch : 0);
        const String rc = FormatResetClock(reset, tzOffsetSec);
        if (rc.length()) row("resets", rc, dim);
    }

    if (u.extraUsage) { c.setTextSize(1); CenterText(c, "usage credits ON", SCREEN_SIZE - 74, dim); }
    c.setTextSize(1);
    CenterText(c, "tap or swipe to close", SCREEN_SIZE - 58, faint);
}
