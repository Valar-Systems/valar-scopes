#include "EamManager.h"

#include <math.h>
#include <time.h>

#include "Layout.h"
#include "EamModels.h"
#include "SevenSegment.h"

// The seven EAM screens. Member functions of EamManager (split out of EamManager.cpp to keep the
// controller readable). Everything draws through BandCanvas in absolute screen coordinates and
// degrades to a "no data" line when its endpoint is empty/down.

namespace {

// Great-circle distance (km) and initial bearing (deg) from (lat1,lon1) to (lat2,lon2).
void RangeBearing(double lat1, double lon1, double lat2, double lon2, double& km, double& brgDeg)
{
    const double R = 6371.0;
    const double p1 = lat1 * M_PI / 180.0, p2 = lat2 * M_PI / 180.0;
    const double dp = (lat2 - lat1) * M_PI / 180.0, dl = (lon2 - lon1) * M_PI / 180.0;
    const double a = sin(dp / 2) * sin(dp / 2) + cos(p1) * cos(p2) * sin(dl / 2) * sin(dl / 2);
    km = R * 2 * atan2(sqrt(a), sqrt(1 - a));
    const double y = sin(dl) * cos(p2);
    const double x = cos(p1) * sin(p2) - sin(p1) * cos(p2) * cos(dl);
    double b = atan2(y, x) * 180.0 / M_PI;
    if (b < 0) b += 360.0;
    brgDeg = b;
}

// "this-day in HFGCS heritage" sample lines. SAMPLE DATA -- ship empty or move to a bundled JSON;
// here just to exercise the idle clock's ambient line.
const char* const kHeritageSample[] = {
    "1961: SAC airborne alert begins",
    "1991: HFGCS keeps the watch",
};

} // namespace

void EamManager::DrawTicker(BandCanvas& c, bool firstPass)
{
    const std::vector<eam::Msg>& latest = feed.Latest();
    c.setTextSize(1);
    if (latest.empty()) {
        CenterText(c, "EAM TICKER", SCREEN_SIZE_DIV_2 - 10, palette.dim);
        CenterText(c, "no data", SCREEN_SIZE_DIV_2 + 6, palette.faint);
        return;
    }

    const eam::Msg& m = latest.front();
    const int lh = c.fontHeight() + 2;

    // Header band: frequency . time-ago . length.
    String header;
    if (m.frequencyKhz) header += String(m.frequencyKhz) + " kHz";
    const String ago = TimeAgo(m.heardAtEpoch);
    if (ago.length()) header += (header.length() ? "  -  " : "") + ago;
    header += (header.length() ? "  -  " : "") + (String(m.charCount) + " ch");
    CenterText(c, header, (int)(SCREEN_SIZE * 0.12), palette.dim);

    // NEW pulse (blink) just under the header.
    if ((long)(newPulseUntilMs - millis()) > 0 && ((millis() / 250) % 2 == 0))
        CenterText(c, "NEW", (int)(SCREEN_SIZE * 0.20), palette.accent);
    // malformed copy badge.
    if (m.malformed)
        CenterText(c, "+/- copy?", SCREEN_SIZE - 26, palette.faint);

    const int bodyTop = (int)(SCREEN_SIZE * 0.28);
    const int bodyBot = (int)(SCREEN_SIZE * 0.90);

    // Skyking: codeword headline instead of a group body.
    if (m.type == eam::MsgType::Skyking) {
        c.setTextSize(2);
        CenterText(c, "SKYKING", SCREEN_SIZE_DIV_2 - 22, palette.accent);
        const String code = m.codeword.length() ? m.codeword : String("--");
        CenterText(c, code, SCREEN_SIZE_DIV_2 + 8, palette.fg);
        c.setTextSize(1);
        return;
    }

    // EAM body: groups, one per line, auto-scrolling if they overflow.
    const std::vector<String>& groups = m.groups;
    const int avail = bodyBot - bodyTop;
    const int contentH = (int)groups.size() * lh;

    int offset = 0;
    if (contentH > avail) {
        const int span = contentH - avail + lh; // scroll a little past the end before wrapping
        if (firstPass && millis() - lastScrollMs > 60) {
            lastScrollMs = millis();
            tickerScroll = (tickerScroll + 1) % (span > 0 ? span : 1);
        }
        offset = tickerScroll;
    } else {
        tickerScroll = 0;
    }

    c.setTextColor(palette.fg);
    for (int i = 0; i < (int)groups.size(); ++i) {
        const int y = bodyTop + i * lh - offset;
        if (y < bodyTop - lh || y > bodyBot) continue;
        c.drawString(groups[i], SCREEN_SIZE_DIV_2 - c.textWidth(groups[i]) / 2, y);
    }
}

void EamManager::DrawTempo(BandCanvas& c)
{
    c.setTextSize(1);
    const eam::Tempo& t = feed.Tempo();
    CenterText(c, "EAM TEMPO", (int)(SCREEN_SIZE * 0.12), palette.dim);
    if (!t.valid) {
        CenterText(c, "no data", SCREEN_SIZE_DIV_2, palette.faint);
        return;
    }

    // Level drives a restrained colour shift.
    uint32_t col = palette.fg;
    if (t.level == "elevated") col = palette.warn;
    else if (t.level == "high") col = palette.alert;

    // Dial: a 240-degree sweep, value = ratio vs baseline (capped at 3x = full).
    const int cx = SCREEN_SIZE_DIV_2;
    const int cy = (int)(SCREEN_SIZE * 0.52);
    const int r1 = (int)(SCREEN_SIZE * 0.34);
    const int r0 = (int)(SCREEN_SIZE * 0.25);
    const float start = 150.0f, sweep = 240.0f;
    float frac = t.ratio > 0 ? t.ratio / 3.0f : 0.0f;
    if (frac > 1.0f) frac = 1.0f;
    if (frac < 0.0f) frac = 0.0f;
    c.fillArc(cx, cy, r0, r1, start, start + sweep, palette.faint);
    if (frac > 0.0f)
        c.fillArc(cx, cy, r0, r1, start, start + sweep * frac, col);

    // Centre readout: today's count, the level, and the ratio.
    c.setTextSize(3);
    CenterText(c, String(t.countToday), cy - 12, col);
    c.setTextSize(1);
    String lvl = t.level;
    lvl.toUpperCase();
    CenterText(c, lvl, cy + 18, col);
    if (t.ratio > 0) {
        char rb[16];
        snprintf(rb, sizeof(rb), "~%.1fx normal", t.ratio);
        CenterText(c, rb, cy + 18 + c.fontHeight() + 4, palette.dim);
    }

    // Frequency activity strip: today's count per HFGCS channel (from /eam/stats), busiest
    // highlighted. If the propagation screen suggested a channel, mark it (accent caret + label) so
    // you can see at a glance whether the hot freq is the one conditions actually favour. Sits in
    // the dial's open bottom; absent until the stats poll lands.
    const std::vector<eam::FreqCount>& byFreq = feed.Stats().byFreq;
    if (!byFreq.empty()) {
        const int suggested = feed.Propagation().valid ? feed.Propagation().suggestedKhz : 0;
        int maxCount = 1, busiestKhz = 0, busiestCount = -1;
        for (const eam::FreqCount& fc : byFreq) {
            if (fc.count > maxCount) maxCount = fc.count;
            if (fc.count > busiestCount) { busiestCount = fc.count; busiestKhz = fc.khz; }
        }

        const int n = (int)byFreq.size();
        const int slot = (int)(SCREEN_SIZE * 0.15f);
        const int barW = (int)(slot * 0.46f);
        const int maxBarH = (int)(SCREEN_SIZE * 0.11f);
        const int baseY = (int)(SCREEN_SIZE * 0.86f);
        int sx = SCREEN_SIZE_DIV_2 - (n * slot) / 2 + (slot - barW) / 2;
        for (const eam::FreqCount& fc : byFreq) {
            const int bh = (fc.count * maxBarH) / maxCount;
            const bool busiest = fc.khz == busiestKhz && busiestCount > 0;
            const bool isSuggested = suggested && fc.khz == suggested;
            const int cxBar = sx + barW / 2;
            c.fillRect(sx, baseY - bh, barW, bh > 0 ? bh : 1, busiest ? col : palette.faint);
            if (isSuggested) // accent caret above the favoured channel
                c.fillTriangle(cxBar - 4, baseY - maxBarH - 8, cxBar + 4, baseY - maxBarH - 8,
                               cxBar, baseY - maxBarH - 2, palette.accent);
            char fl[8];
            snprintf(fl, sizeof(fl), "%.1f", fc.khz / 1000.0);
            c.setTextColor(isSuggested ? palette.accent : palette.dim);
            c.drawString(fl, cxBar - c.textWidth(fl) / 2, baseY + 3);
            sx += slot;
        }
    }
}

void EamManager::DrawActivity(BandCanvas& c)
{
    // 24 hour-of-day buckets (current UTC day) as a polar histogram native to the round panel:
    // hour 0 at 12 o'clock, growing clockwise; the current UTC hour is marked in accent.
    c.setTextSize(1);
    const eam::Stats& st = feed.Stats();
    CenterText(c, "EAM ACTIVITY - 24H UTC", (int)(SCREEN_SIZE * 0.09), palette.dim);
    if (!st.valid || !st.hasByHour) {
        CenterText(c, "no data", SCREEN_SIZE_DIV_2, palette.faint);
        return;
    }

    int maxCount = 1, total = 0;
    for (int i = 0; i < 24; ++i) { if (st.byHour[i] > maxCount) maxCount = st.byHour[i]; total += st.byHour[i]; }

    const int cx = SCREEN_SIZE_DIV_2;
    const int cy = (int)(SCREEN_SIZE * 0.53f);
    const int r0 = (int)(SCREEN_SIZE * 0.17f);
    const int rMax = (int)(SCREEN_SIZE * 0.40f);
    const int span = rMax - r0;

    const time_t nowUtc = time(nullptr);
    int curHour = -1;
    if (nowUtc > 1600000000) { struct tm tmv; gmtime_r(&nowUtc, &tmv); curHour = tmv.tm_hour; }

    for (int h = 0; h < 24; ++h) {
        const float centerDeg = 270.0f + h * 15.0f;     // hour 0 at 12 o'clock, clockwise
        const float a0 = centerDeg - 6.5f, a1 = centerDeg + 6.5f;
        const bool isNow = (h == curHour);
        if (st.byHour[h] <= 0) {
            c.fillArc(cx, cy, r0, r0 + 2, a0, a1, isNow ? palette.accent : palette.faint);
        } else {
            int len = (st.byHour[h] * span) / maxCount;
            if (len < 3) len = 3;
            c.fillArc(cx, cy, r0, r0 + len, a0, a1, isNow ? palette.accent : palette.fg);
        }
    }

    // cardinal hour labels for orientation (00 top, 06 right, 12 bottom, 18 left)
    auto label = [&](const char* s, float deg) {
        const float a = deg * (float)M_PI / 180.0f;
        const int lx = cx + (int)((rMax + 10) * cosf(a));
        const int ly = cy + (int)((rMax + 10) * sinf(a));
        c.setTextColor(palette.faint);
        c.drawString(s, lx - c.textWidth(s) / 2, ly - c.fontHeight() / 2);
    };
    label("00", 270); label("06", 0); label("12", 90); label("18", 180);

    // hub readout: today's total
    c.setTextSize(2);
    CenterText(c, String(total), cy - 8, palette.fg);
    c.setTextSize(1);
    CenterText(c, "today", cy + 10, palette.faint);
}

void EamManager::DrawCodewords(BandCanvas& c)
{
    c.setTextSize(1);
    const std::vector<eam::Codeword>& cws = feed.Codewords();
    CenterText(c, "SKYKING CODEWORDS", (int)(SCREEN_SIZE * 0.12), palette.dim);
    if (cws.empty()) {
        CenterText(c, "none recently", SCREEN_SIZE_DIV_2, palette.faint);
        return;
    }

    const int lh = c.fontHeight() + 6;
    int y = (int)(SCREEN_SIZE * 0.24);
    const int bot = SCREEN_SIZE - 24;
    const int x = (int)(SCREEN_SIZE * 0.14);

    for (const eam::Codeword& cw : cws) {
        if (y > bot) break;
        const bool isNew = newCodewords.count(cw.codeword) > 0;
        c.setTextColor(palette.fg);
        c.drawString(cw.codeword, x, y);
        if (isNew) {
            c.setTextColor(palette.accent);
            c.drawString("NEW", SCREEN_SIZE - x - c.textWidth("NEW"), y);
        } else if (cw.count > 0) {
            c.setTextColor(palette.faint);
            const String cnt = "x" + String(cw.count);
            c.drawString(cnt, SCREEN_SIZE - x - c.textWidth(cnt), y);
        }
        y += lh;
    }

    const time_t nowUtc = time(nullptr);
    const long nowEpoch = (nowUtc > 1600000000) ? (long)nowUtc : 0;
    char seen[28];
    snprintf(seen, sizeof(seen), "seen this month: %u", (unsigned)logbook.CodewordsThisMonth(nowEpoch));
    CenterText(c, seen, SCREEN_SIZE - 16, palette.faint);
}

void EamManager::DrawAbncp(BandCanvas& c)
{
    c.setTextSize(1);
    CenterText(c, "COMMAND POST WATCH", (int)(SCREEN_SIZE * 0.12), palette.dim);

    const char* inert = feed.AbncpInertReason();
    if (inert) {
        CenterText(c, "OpenSky", SCREEN_SIZE_DIV_2 - 8, palette.faint);
        CenterText(c, inert, SCREEN_SIZE_DIV_2 + 8, palette.warn);
        return;
    }

    const eam::Abncp& a = feed.Abncp();
    if (!a.valid) {
        CenterText(c, "checking...", SCREEN_SIZE_DIV_2, palette.faint);
        return;
    }

    if (!a.airborne || a.aircraft.empty()) {
        c.setTextSize(2);
        CenterText(c, "none up", SCREEN_SIZE_DIV_2 - 14, palette.dim);
        c.setTextSize(1);
        CenterText(c, "no command post broadcasting", SCREEN_SIZE_DIV_2 + 14, palette.faint);
        CenterText(c, "(only sees ADS-B transmitters)", SCREEN_SIZE_DIV_2 + 14 + c.fontHeight() + 2, palette.faint);
        return;
    }

    // First airborne contact headline.
    const eam::AbncpAircraft* up = nullptr;
    for (const eam::AbncpAircraft& ac : a.aircraft) { up = &ac; break; }
    String headline = "AIRBORNE";
    if (up->type == "E-4B") headline = "NIGHTWATCH AIRBORNE";
    else if (up->type == "E-6B") headline = "E-6B UP";
    else if (up->type.length()) headline = up->type + " UP";

    c.setTextSize(2);
    CenterText(c, headline, (int)(SCREEN_SIZE * 0.34), palette.alert);
    c.setTextSize(1);

    int y = SCREEN_SIZE_DIV_2 + 10;
    if (up->callsign.length()) { CenterText(c, up->callsign, y, palette.fg); y += c.fontHeight() + 4; }

    if (up->hasPos && hasLatLon) {
        double km, brg;
        RangeBearing(deviceLat, deviceLon, up->lat, up->lon, km, brg);
        char bd[28];
        snprintf(bd, sizeof(bd), "BRG %03d  %d km", (int)(brg + 0.5), (int)(km + 0.5));
        CenterText(c, bd, y, palette.dim);
    } else if (up->hex.length()) {
        String h = up->hex; h.toUpperCase();
        CenterText(c, h, y, palette.dim);
    }
}

void EamManager::DrawMilAir(BandCanvas& c)
{
    // Notable military aircraft up now. Same tile language as the command-post watch, and the same
    // honest caveat: this only sees aircraft transmitting ADS-B.
    c.setTextSize(1);
    CenterText(c, "MIL AIR", (int)(SCREEN_SIZE * 0.12), palette.dim);

    const eam::MilAir& m = feed.MilAir();
    if (!m.valid || m.count <= 0) {
        c.setTextSize(2);
        CenterText(c, "none up", SCREEN_SIZE_DIV_2 - 14, palette.dim);
        c.setTextSize(1);
        CenterText(c, "no notable mil air", SCREEN_SIZE_DIV_2 + 14, palette.faint);
        CenterText(c, "(only sees ADS-B transmitters)", SCREEN_SIZE_DIV_2 + 14 + c.fontHeight() + 2, palette.faint);
        return;
    }

    c.setTextSize(3);
    CenterText(c, String(m.count), (int)(SCREEN_SIZE * 0.30), palette.fg);
    c.setTextSize(1);
    CenterText(c, m.count == 1 ? "aircraft up" : "aircraft up", (int)(SCREEN_SIZE * 0.30) + 30, palette.faint);

    // Rotate a window of three through the list so a busy picture isn't truncated silently.
    const int per = 3;
    const int n = (int)m.aircraft.size();
    const int pages = (n + per - 1) / per;
    const int start = (n > per) ? (int)((millis() / 3000) % pages) * per : 0;

    int y = (int)(SCREEN_SIZE * 0.50);
    const int lh = c.fontHeight() + 7;
    const int xL = (int)(SCREEN_SIZE * 0.12);
    for (int i = start; i < start + per && i < n; ++i) {
        const eam::MilAircraft& a = m.aircraft[i];
        // category is the backend's human label (e.g. "AWACS (E-3)"); fall back to raw type.
        const String label = a.category.length() ? a.category : a.type;
        String line = label.length() ? label : (a.callsign.length() ? a.callsign : (a.hex.length() ? a.hex : String("unknown")));
        if (label.length() && a.callsign.length()) line = label + "  " + a.callsign;
        c.setTextColor(palette.fg);
        c.drawString(line, xL, y);
        if (a.hasPos && hasLatLon) {
            double km, brg;
            RangeBearing(deviceLat, deviceLon, a.lat, a.lon, km, brg);
            char bd[20];
            snprintf(bd, sizeof(bd), "%03d  %dkm", (int)(brg + 0.5), (int)(km + 0.5));
            c.setTextColor(palette.dim);
            c.drawString(bd, SCREEN_SIZE - xL - c.textWidth(bd), y);
        }
        y += lh;
    }
    if (n > per) {
        char more[40];
        const int last = (start + per < n) ? start + per : n;
        snprintf(more, sizeof(more), "%d-%d of %d", start + 1, last, n);
        CenterText(c, more, SCREEN_SIZE - 30, palette.faint);
    }
    CenterText(c, "(ADS-B only)", SCREEN_SIZE - 16, palette.faint);
}

void EamManager::DrawPropagation(BandCanvas& c)
{
    c.setTextSize(1);
    const eam::Propagation& p = feed.Propagation();
    CenterText(c, "HF PROPAGATION", (int)(SCREEN_SIZE * 0.10), palette.dim);
    if (!p.valid) {
        CenterText(c, "no data", SCREEN_SIZE_DIV_2, palette.faint);
        return;
    }

    // Space-weather banner: only when there's something to say. Restrained tint (warn, or alert at
    // R3+/G3+), drawn on a dark wash so it reads as a banner without shouting.
    const eam::SpaceWeather& sw = p.space;
    if (sw.valid && (sw.hfDegraded || sw.gScale >= 1)) {
        const bool severe = sw.rScale >= 3 || sw.gScale >= 3;
        const uint32_t bcol = severe ? palette.alert : palette.warn;
        String banner;
        if (sw.hfDegraded) {
            banner = "RADIO BLACKOUT";
            if (sw.rScale >= 1) banner += " R" + String(sw.rScale);
            banner += " - HF DEGRADED";
            if (sw.xrayClass.length()) banner += " (" + sw.xrayClass + ")";
        } else {
            banner = "GEO STORM G" + String(sw.gScale);
            if (sw.kp >= 0) banner += " - Kp " + String(sw.kp);
        }
        const int bw = c.textWidth(banner) + 16;
        const int bx = SCREEN_SIZE_DIV_2 - bw / 2;
        const int by = (int)(SCREEN_SIZE * 0.165f);
        const int bh = c.fontHeight() + 8;
        c.fillRoundRect(bx, by, bw, bh, 4, eam::ScaleColor(bcol, 0.18f));
        c.setTextColor(bcol);
        c.drawString(banner, SCREEN_SIZE_DIV_2 - c.textWidth(banner) / 2, by + 4);
    }

    CenterText(c, "Best HFGCS freq now", (int)(SCREEN_SIZE * 0.26), palette.faint);
    if (p.suggestedKhz) {
        c.setTextSize(3);
        CenterText(c, String(p.suggestedKhz), (int)(SCREEN_SIZE * 0.34), palette.accent);
        c.setTextSize(1);
        CenterText(c, "kHz", (int)(SCREEN_SIZE * 0.34) + 30, palette.faint);
        if (p.suggestedReason.length())
            CenterText(c, p.suggestedReason, (int)(SCREEN_SIZE * 0.58), palette.dim);
    } else {
        c.setTextSize(1);
        CenterText(c, "no suggestion", (int)(SCREEN_SIZE * 0.36), palette.faint);
    }

    char sk[28];
    snprintf(sk, sizeof(sk), "SFI %d   K %d", p.sfi, p.kIndex);
    CenterText(c, sk, (int)(SCREEN_SIZE * 0.72), palette.fg);
    if (p.source.length())
        CenterText(c, "Solar data: " + p.source, SCREEN_SIZE - 16, palette.faint);
}

void EamManager::DrawIcbm(BandCanvas& c)
{
    c.setTextSize(1);
    const std::vector<eam::Launch>& ls = feed.Launches();
    CenterText(c, "ICBM TEST WINDOW", (int)(SCREEN_SIZE * 0.12), palette.dim);
    if (ls.empty()) { // rotation hides this screen, but guard anyway
        CenterText(c, "no upcoming", SCREEN_SIZE_DIV_2, palette.faint);
        return;
    }

    const eam::Launch& l = ls.front();
    if (l.designation.length())
        CenterText(c, l.designation, (int)(SCREEN_SIZE * 0.30), palette.fg);

    const time_t nowUtc = time(nullptr);
    String big;
    if (l.windowStartEpoch <= 0 || nowUtc <= 1600000000) big = "T- --:--:--";
    else if (nowUtc >= l.windowStartEpoch) big = "IN WINDOW";
    else big = FormatCountdown((long)(l.windowStartEpoch - nowUtc));
    c.setTextSize(2);
    CenterText(c, big, SCREEN_SIZE_DIV_2 - 6, nowUtc >= l.windowStartEpoch && l.windowStartEpoch > 0 ? palette.alert : palette.accent);
    c.setTextSize(1);

    if (l.site.length())
        CenterText(c, l.site, (int)(SCREEN_SIZE * 0.72), palette.dim);
    if (l.source.length())
        CenterText(c, l.source, SCREEN_SIZE - 16, palette.faint);
}

void EamManager::DrawReference(BandCanvas& c)
{
    // "What am I looking at" card -- static, no feed dependency, so it's always available.
    c.setTextSize(1);
    CenterText(c, "HFGCS REFERENCE", (int)(SCREEN_SIZE * 0.10), palette.accent);

    const int lh = c.fontHeight() + 4;
    int y = (int)(SCREEN_SIZE * 0.20);

    CenterText(c, "Primary freqs (kHz)", y, palette.faint); y += lh;
    CenterText(c, "4724   8992", y, palette.fg); y += lh;
    CenterText(c, "11175   15016", y, palette.fg); y += lh + 6;

    CenterText(c, "EAM - coded action message", y, palette.dim); y += lh;
    CenterText(c, "SKYKING - priority, no reply", y, palette.dim); y += lh + 6;

    CenterText(c, "Tempo = today vs baseline", y, palette.faint); y += lh;
    CenterText(c, "quiet/normal/elevated/high", y, palette.dim);
}

void EamManager::DrawClock(BandCanvas& c)
{
    // Six red 7-seg digits HH:MM:SS, 24h UTC, no date. Real segments with a faint ghost; lit
    // segments get a bloom, dimmed at night. Steady colons unless the blink toggle is on.
    const time_t nowUtc = time(nullptr);
    struct tm tmv;
    gmtime_r(&nowUtc, &tmv);
    const bool synced = nowUtc > 1600000000;
    const int hh = synced ? tmv.tm_hour : 0;
    const int mm = synced ? tmv.tm_min : 0;
    const int ss = synced ? tmv.tm_sec : 0;

    const float glow = GlowFactor();
    const uint32_t lit = eam::ScaleColor(eam::ClockLit(), glow);
    const uint32_t bloom = eam::ScaleColor(eam::ClockBloom(), glow);
    const uint32_t ghost = eam::ClockGhost();

    // Size a single row of [d d : d d : d d] to fit ~94% of the panel width.
    int digitH = (int)(SCREEN_SIZE * (SCREEN_SIZE >= 360 ? 0.30f : 0.24f));
    int digitW = (int)(digitH * 0.60f);
    int colonW = (int)(digitW * 0.55f);
    int gap = (int)(digitW * 0.16f);
    int rowW = 6 * digitW + 2 * colonW + 7 * gap;
    const int maxW = (int)(SCREEN_SIZE * 0.94f);
    if (rowW > maxW) {
        const float sc = (float)maxW / rowW;
        digitH = (int)(digitH * sc);
        digitW = (int)(digitW * sc);
        colonW = (int)(colonW * sc);
        gap = (int)(gap * sc);
        rowW = 6 * digitW + 2 * colonW + 7 * gap;
    }
    int x = SCREEN_SIZE_DIV_2 - rowW / 2;
    const int y = SCREEN_SIZE_DIV_2 - digitH / 2 - 8;

    // Dark rounded bezel frame.
    const int pad = (int)(digitH * 0.18f);
    c.fillRoundRect(x - pad, y - pad, rowW + 2 * pad, digitH + 2 * pad, pad,
                    eam::ScaleColor(lgfx::color888(18, 4, 3), glow));
    c.drawRoundRect(x - pad, y - pad, rowW + 2 * pad, digitH + 2 * pad, pad,
                    eam::ScaleColor(lgfx::color888(60, 12, 8), glow));

    const bool colonLit = colonBlink ? (ss % 2 == 0) : true;
    auto digit = [&](int d) { eam::DrawSevenSeg(c, x, y, digitW, digitH, d, lit, ghost, bloom); x += digitW + gap; };
    auto colon = [&]() { eam::DrawColon(c, x, y, colonW, digitH, colonLit, lit, ghost); x += colonW + gap; };

    digit(hh / 10); digit(hh % 10); colon();
    digit(mm / 10); digit(mm % 10); colon();
    digit(ss / 10); digit(ss % 10);

    // Ambient line below: rotates the day's count, the logbook odometer, and a (sample) heritage note.
    c.setTextSize(1);
    const eam::Tempo& t = feed.Tempo();
    const eam::Stats& st = feed.Stats();
    std::vector<String> amb;
    if (t.valid) amb.push_back(String(t.countToday) + " EAMs today");
    if (st.valid && st.longestQuietMin >= 0) {
        const int q = st.longestQuietMin;
        amb.push_back("quiet gap " + (q >= 60 ? (String(q / 60) + "h " + String(q % 60) + "m") : (String(q) + "m")));
    }
    if (logbook.EamCount() > 0) amb.push_back(String(logbook.EamCount()) + " logged");
    for (int i = 0; i < (int)(sizeof(kHeritageSample) / sizeof(kHeritageSample[0])); ++i)
        amb.push_back(kHeritageSample[i]);
    String ambient = amb.empty() ? String() : amb[ambientIndex % (int)amb.size()];
    if (!synced) ambient = "waiting for time sync";
    if (ambient.length())
        CenterText(c, ambient, y + digitH + pad + 12, palette.faint);
}
