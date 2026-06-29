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

void EamManager::DrawPropagation(BandCanvas& c)
{
    c.setTextSize(1);
    const eam::Propagation& p = feed.Propagation();
    CenterText(c, "HF PROPAGATION", (int)(SCREEN_SIZE * 0.10), palette.dim);
    if (!p.valid) {
        CenterText(c, "no data", SCREEN_SIZE_DIV_2, palette.faint);
        return;
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
    std::vector<String> amb;
    if (t.valid) amb.push_back(String(t.countToday) + " EAMs today");
    if (logbook.EamCount() > 0) amb.push_back(String(logbook.EamCount()) + " logged");
    for (int i = 0; i < (int)(sizeof(kHeritageSample) / sizeof(kHeritageSample[0])); ++i)
        amb.push_back(kHeritageSample[i]);
    String ambient = amb.empty() ? String() : amb[ambientIndex % (int)amb.size()];
    if (!synced) ambient = "waiting for time sync";
    if (ambient.length())
        CenterText(c, ambient, y + digitH + pad + 12, palette.faint);
}
