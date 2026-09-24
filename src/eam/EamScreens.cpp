#include "EamManager.h"

#include <math.h>
#include <time.h>

#include "Layout.h"
#include "EamModels.h"
#include "SevenSegment.h"

// The EAM screens. Member functions of EamManager (split out of EamManager.cpp to keep the
// controller readable). Everything draws through BandCanvas in absolute screen coordinates and
// degrades to a "no data" line when its endpoint is empty/down.
//
// THE DISPLAY RULES (Fable, 2026-09-23, display PR):
//   - No text at size 1. The 6x8 font is 8 px tall on a 1.28" panel; nothing a person is meant
//     to read is drawn at that size any more. Text is size 2.
//   - One subject per screen. What a screen is ABOUT stays; a second subject sharing the glass
//     (the tempo screen's frequency strip, the propagation screen's solar indices) is gone.
//   - Every number a player reads is SEVEN-SEGMENT, drawn with the Zulu clock's own glyph
//     (DrawSegText below -> eam::DrawSevenSeg), at the clock's scale where the screen has
//     room and never smaller than SCREEN_SIZE/10.
//   - The palette is locked. A number keeps the colour it had as text; its unlit ghost and
//     its bloom are that colour scaled with eam::ScaleColor, exactly as the clock derives its
//     own (ClockGhost ~ 0.06 of ClockLit, ClockBloom ~ 0.38). No new colour exists.

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

// ---- seven-segment numbers -------------------------------------------------------------------

// The floor for any number a player reads.
int SegMinH() { return SCREEN_SIZE / 10; }

// The Zulu clock's glyph height: one definition, read by DrawClock and by every screen that
// shows its number "at the clock's scale". A row of [dd:dd:dd] sized to ~94% of the panel.
int ClockDigitH()
{
    int digitH = (int)(SCREEN_SIZE * (SCREEN_SIZE >= 360 ? 0.30f : 0.24f));
    const int digitW = (int)(digitH * 0.60f);
    const int colonW = (int)(digitW * 0.55f);
    const int gap = (int)(digitW * 0.16f);
    const int rowW = 6 * digitW + 2 * colonW + 7 * gap;
    const int maxW = (int)(SCREEN_SIZE * 0.94f);
    if (rowW > maxW) digitH = (int)(digitH * ((float)maxW / rowW));
    return digitH;
}

// Glyph metrics for height h, in the clock's proportions.
struct SegMetrics {
    int w, gap, colonW, dotW;
};
SegMetrics MetricsFor(int h)
{
    SegMetrics m;
    m.w = (int)(h * 0.60f);
    m.gap = (int)(m.w * 0.16f);
    if (m.gap < 2) m.gap = 2;
    m.colonW = (int)(m.w * 0.55f);
    m.dotW = eam::SevenSegThickness(h);
    return m;
}

// Width of `s` drawn by DrawSegText at height h. Digits, ':', '.', ' '; any other character
// is an UNLIT digit (ghost only) -- the panel showing it has no reading.
int SegTextWidth(const char* s, int h)
{
    const SegMetrics m = MetricsFor(h);
    int w = 0, n = 0;
    for (const char* p = s; *p; ++p, ++n) {
        if (n) w += m.gap;
        w += *p == ':' ? m.colonW : *p == '.' ? m.dotW : *p == ' ' ? m.w / 2 : m.w;
    }
    return w;
}

// The largest height <= want (and >= the floor) at which `s` fits in maxW.
int FitSegH(const char* s, int want, int maxW)
{
    int h = want;
    while (h > SegMinH() && SegTextWidth(s, h) > maxW) h -= 1;
    return h < SegMinH() ? SegMinH() : h;
}

// `s` in seven-segment glyphs, centred on cx, top at y, glyph height h, lit in `lit`.
void DrawSegText(BandCanvas& c, const char* s, int cx, int y, int h, uint32_t lit)
{
    const SegMetrics m = MetricsFor(h);
    const uint32_t ghost = eam::ScaleColor(lit, 0.06f);
    const uint32_t bloom = eam::ScaleColor(lit, 0.38f);
    int x = cx - SegTextWidth(s, h) / 2;
    for (const char* p = s; *p; ++p) {
        const char ch = *p;
        if (ch == ':') {
            eam::DrawColon(c, x, y, m.colonW, h, true, lit, ghost);
            x += m.colonW + m.gap;
        } else if (ch == '.') {
            c.fillRect(x, y + h - m.dotW, m.dotW, m.dotW, lit);
            x += m.dotW + m.gap;
        } else if (ch == ' ') {
            x += m.w / 2 + m.gap;
        } else {
            eam::DrawSevenSeg(c, x, y, m.w, h, (ch >= '0' && ch <= '9') ? ch - '0' : -1, lit, ghost, bloom);
            x += m.w + m.gap;
        }
    }
}

// ---- size-2 text on a round panel -------------------------------------------------------------

// Usable width of the round panel across the band [y, y+h), less a margin.
int ChordW(int y, int h)
{
    const int r = SCREEN_SIZE_DIV_2;
    auto at = [&](int yy) {
        const int dy = abs(yy - r);
        return dy >= r ? 0 : (int)(2.0f * sqrtf((float)(r * r - dy * dy)));
    };
    const int w = min(at(y), at(y + h));
    return w > 16 ? w - 16 : 0;
}

// `s` centred at y, word-wrapped to the panel's width at each line, at most maxLines lines.
// Returns the y below the last line drawn.
int CenterWrap(BandCanvas& c, const String& s, int y, uint32_t col, int maxLines)
{
    c.setTextColor(col);
    const int lh = c.fontHeight() + 2;
    int start = 0;
    const int n = (int)s.length();
    for (int line = 0; line < maxLines && start < n; ++line) {
        while (start < n && s[start] == ' ') ++start;
        const int maxW = ChordW(y, lh);
        int end = start, lastFit = -1;
        while (end <= n) {
            if (end == n || s[end] == ' ') {
                if (c.textWidth(s.substring(start, end)) <= maxW) lastFit = end;
                else break;
            }
            ++end;
        }
        if (lastFit < 0) lastFit = (end > n ? n : end);  // one word wider than the line: draw it
        String part = s.substring(start, lastFit);
        if (line == maxLines - 1 && lastFit < n) {
            // THE LAST LINE IS CUT, not overflowed: the rest of the text used to be drawn
            // whole here and ran off the round edge (the propagation reason, first capture).
            part = s.substring(start);
            while (part.length() > 0 && c.textWidth(part + "...") > maxW) part.remove(part.length() - 1);
            part.trim();
            part += "...";
            lastFit = n;
        }
        c.drawString(part, SCREEN_SIZE_DIV_2 - c.textWidth(part) / 2, y);
        y += lh;
        start = lastFit;
    }
    return y;
}

void CenterAt(BandCanvas& c, const String& s, int y, uint32_t col)
{
    c.setTextColor(col);
    c.drawString(s, SCREEN_SIZE_DIV_2 - c.textWidth(s) / 2, y);
}

// Bearing and range on one row -- two seven-segment numbers with their unit words under them.
void DrawBearingRange(BandCanvas& c, double brgDeg, double km, int y, uint32_t col, uint32_t labelCol)
{
    char b[12], r[12];
    snprintf(b, sizeof(b), "%03d", (int)(brgDeg + 0.5) % 360);
    snprintf(r, sizeof(r), "%d", (int)(km + 0.5) > 9999 ? 9999 : (int)(km + 0.5));
    const int h = SegMinH();
    const int bx = SCREEN_SIZE_DIV_2 - SCREEN_SIZE / 5;
    const int rx = SCREEN_SIZE_DIV_2 + SCREEN_SIZE / 6;
    DrawSegText(c, b, bx, y, h, col);
    DrawSegText(c, r, rx, y, h, col);
    c.setTextColor(labelCol);
    c.drawString("BRG", bx - c.textWidth("BRG") / 2, y + h + 4);
    c.drawString("KM", rx - c.textWidth("KM") / 2, y + h + 4);
}

} // namespace

void EamManager::DrawTicker(BandCanvas& c, bool firstPass)
{
    // SUBJECT: the newest message. Its heard time (Zulu, seven-segment) heads it; the frequency
    // and length that shared the old 8 px header are gone with it.
    const std::vector<eam::Msg>& latest = feed.Latest();
    c.setTextSize(2);
    if (latest.empty()) {
        CenterText(c, "EAM TICKER", SCREEN_SIZE_DIV_2 - 18, palette.dim);
        CenterText(c, "no data", SCREEN_SIZE_DIV_2 + 4, palette.faint);
        return;
    }

    const eam::Msg& m = latest.front();
    const int lh = c.fontHeight() + 2;

    const int timeY = (int)(SCREEN_SIZE * 0.08);
    if (m.heardAtEpoch > 1600000000) {
        const time_t t = (time_t)m.heardAtEpoch;
        struct tm tmv;
        gmtime_r(&t, &tmv);
        char hm[12];
        snprintf(hm, sizeof(hm), "%02d:%02d", tmv.tm_hour, tmv.tm_min);
        DrawSegText(c, hm, SCREEN_SIZE_DIV_2, timeY, SegMinH(), palette.dim);
    }

    // NEW pulse (blink) just under the heard time.
    const int flagY = timeY + SegMinH() + 6;
    const bool pulsing = (long)(newPulseUntilMs - millis()) > 0;
    if (pulsing && ((millis() / 250) % 2 == 0))
        CenterText(c, "NEW", flagY, palette.accent);
#if defined(FEATURE_EAM_GAME)
    // A message whose drill offer was WITHDRAWN at the ack cutoff keeps its class on the
    // ticker (Fable, 2026-09-23): it was decoded, it just can no longer be committed.
    if (!pulsing) {
        auto it = withdrawnClass.find(m.id);
        if (it != withdrawnClass.end()) {
            const char* cls = it->second == game::MsgClass::Execution ? "EXECUTION"
                            : it->second == game::MsgClass::Fdm ? "FDM" : "NAM";
            CenterText(c, cls, flagY, palette.dim);
        }
    }
#endif
    // Copy-quality badges. PARTIAL WINS when both are set: "we are missing some
    // of this" is the more actionable of the two, and the line only fits one.
    const int badgeY = (int)(SCREEN_SIZE * 0.80);
    const bool badge = m.partial || m.malformed;
    if (m.partial)
        CenterText(c, "partial copy", badgeY, palette.accent);
    else if (m.malformed)
        CenterText(c, "+/- copy?", badgeY, palette.faint);

    const int bodyTop = flagY + lh + 4;
    const int bodyBot = badge ? badgeY - 4 : (int)(SCREEN_SIZE * 0.88);

    // Skyking: codeword headline instead of a group body.
    if (m.type == eam::MsgType::Skyking) {
        CenterText(c, "SKYKING", SCREEN_SIZE_DIV_2 - 18, palette.accent);
        const String code = m.codeword.length() ? m.codeword : String("--");
        CenterText(c, code, SCREEN_SIZE_DIV_2 + 6, palette.fg);
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
        if (y < bodyTop || y + lh > bodyBot) continue;
        c.drawString(groups[i], SCREEN_SIZE_DIV_2 - c.textWidth(groups[i]) / 2, y);
    }
}

void EamManager::DrawTempo(BandCanvas& c)
{
    // SUBJECT: today's tempo -- the dial, today's count, the level. (The per-channel frequency
    // strip that shared this screen was a second subject and is gone.)
    c.setTextSize(2);
    const eam::Tempo& t = feed.Tempo();
    CenterText(c, "EAM TEMPO", (int)(SCREEN_SIZE * 0.08), palette.dim);
    if (!t.valid) {
        CenterText(c, "no data", SCREEN_SIZE_DIV_2, palette.faint);
        return;
    }

    // Level drives a restrained colour shift.
    uint32_t col = palette.fg;
    if (t.level == "elevated") col = palette.warn;
    else if (t.level == "high") col = palette.alert;

    // Dial: a 240-degree sweep, value = ratio vs baseline (capped at 3x = full). The dial IS the
    // ratio; the "~1.4x normal" line it used to carry said the same thing in 8 px.
    const int cx = SCREEN_SIZE_DIV_2;
    const int cy = (int)(SCREEN_SIZE * 0.54);
    const int r1 = (int)(SCREEN_SIZE * 0.36);
    const int r0 = (int)(SCREEN_SIZE * 0.27);
    const float start = 150.0f, sweep = 240.0f;
    float frac = t.ratio > 0 ? t.ratio / 3.0f : 0.0f;
    if (frac > 1.0f) frac = 1.0f;
    if (frac < 0.0f) frac = 0.0f;
    c.fillArc(cx, cy, r0, r1, start, start + sweep, palette.faint);
    if (frac > 0.0f)
        c.fillArc(cx, cy, r0, r1, start, start + sweep * frac, col);

    // Centre: today's count at the clock's scale, the level word beneath.
    const String n = String(t.countToday);
    const int h = FitSegH(n.c_str(), ClockDigitH(), 2 * r0 - 16);
    DrawSegText(c, n.c_str(), cx, cy - h / 2 - 10, h, col);
    String lvl = t.level;
    lvl.toUpperCase();
    CenterText(c, lvl, cy + h / 2 - 4, col);
}

void EamManager::DrawActivity(BandCanvas& c)
{
    // SUBJECT: today's EAMs by UTC hour, as a polar histogram native to the round panel: hour 0
    // at 12 o'clock, growing clockwise; the current hour marked in accent; today's total at the hub.
    c.setTextSize(2);
    const eam::Stats& st = feed.Stats();
    CenterText(c, "EAMS TODAY", (int)(SCREEN_SIZE * 0.07), palette.dim);
    if (!st.valid || !st.hasByHour) {
        CenterText(c, "no data", SCREEN_SIZE_DIV_2, palette.faint);
        return;
    }

    int maxCount = 1, total = 0;
    for (int i = 0; i < 24; ++i) { if (st.byHour[i] > maxCount) maxCount = st.byHour[i]; total += st.byHour[i]; }

    const int cx = SCREEN_SIZE_DIV_2;
    const int cy = (int)(SCREEN_SIZE * 0.56f);
    const int r0 = (int)(SCREEN_SIZE * 0.17f);
    const int rMax = (int)(SCREEN_SIZE * 0.36f);
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

    // Hub: today's total, as large as the hub allows.
    const String n = String(total);
    const int h = FitSegH(n.c_str(), ClockDigitH(), 2 * r0 - 12);
    DrawSegText(c, n.c_str(), cx, cy - h / 2, h, palette.fg);
}

void EamManager::DrawCodewords(BandCanvas& c)
{
    // SUBJECT: the recent SKYKING codewords, newest first; a NEW one in accent. (The per-word
    // repeat counts and the month tally were second subjects in 8 px and are gone.)
    c.setTextSize(2);
    const std::vector<eam::Codeword>& cws = feed.Codewords();
    CenterText(c, "CODEWORDS", (int)(SCREEN_SIZE * 0.10), palette.dim);
    if (cws.empty()) {
        CenterText(c, "none recently", SCREEN_SIZE_DIV_2, palette.faint);
        return;
    }

    const int lh = c.fontHeight() + 6;
    int y = (int)(SCREEN_SIZE * 0.24);
    const int bot = (int)(SCREEN_SIZE * 0.86);
    for (const eam::Codeword& cw : cws) {
        if (y + lh > bot) break;
        CenterText(c, cw.codeword, y, newCodewords.count(cw.codeword) > 0 ? palette.accent : palette.fg);
        y += lh;
    }
}

void EamManager::DrawAbncp(BandCanvas& c)
{
    // SUBJECT: whether a command post is up, and where.
    c.setTextSize(2);
    CenterText(c, "COMMAND POST", (int)(SCREEN_SIZE * 0.12), palette.dim);

    const char* inert = feed.AbncpInertReason();
    if (inert) {
        CenterText(c, "OpenSky", SCREEN_SIZE_DIV_2 - 26, palette.faint);
        CenterWrap(c, inert, SCREEN_SIZE_DIV_2 - 4, palette.warn, 3);
        return;
    }

    const eam::Abncp& a = feed.Abncp();
    if (!a.valid) {
        CenterText(c, "checking...", SCREEN_SIZE_DIV_2, palette.faint);
        return;
    }

    if (!a.airborne || a.aircraft.empty()) {
        CenterText(c, "none up", SCREEN_SIZE_DIV_2 - 30, palette.dim);
        const int y = CenterWrap(c, "no command post broadcasting", SCREEN_SIZE_DIV_2 - 4, palette.faint, 2);
        CenterText(c, "(ADS-B only)", y + 4, palette.faint);
        return;
    }

    // First airborne contact headline.
    const eam::AbncpAircraft* up = nullptr;
    for (const eam::AbncpAircraft& ac : a.aircraft) { up = &ac; break; }
    String headline = "AIRBORNE";
    if (up->type == "E-4B") headline = "NIGHTWATCH AIRBORNE";
    else if (up->type == "E-6B") headline = "E-6B UP";
    else if (up->type.length()) headline = up->type + " UP";

    int y = CenterWrap(c, headline, (int)(SCREEN_SIZE * 0.25), palette.alert, 2) + 6;
    if (up->callsign.length()) { CenterText(c, up->callsign, y, palette.fg); y += c.fontHeight() + 8; }

    if (up->hasPos && hasLatLon) {
        double km, brg;
        RangeBearing(deviceLat, deviceLon, up->lat, up->lon, km, brg);
        DrawBearingRange(c, brg, km, y, palette.dim, palette.faint);
    } else if (up->hex.length()) {
        String h = up->hex; h.toUpperCase();
        CenterText(c, h, y, palette.dim);  // an identifier, not a quantity: stays text
    }
}

void EamManager::DrawMilAir(BandCanvas& c)
{
    // SUBJECT: notable military aircraft up now -- how many, and one at a time which. Same honest
    // caveat as the command-post watch: this only sees aircraft transmitting ADS-B.
    c.setTextSize(2);
    CenterText(c, "MIL AIR UP", (int)(SCREEN_SIZE * 0.10), palette.dim);

    const eam::MilAir& m = feed.MilAir();
    if (!m.valid || m.count <= 0) {
        CenterText(c, "none up", SCREEN_SIZE_DIV_2 - 30, palette.dim);
        const int y = CenterWrap(c, "no notable mil air", SCREEN_SIZE_DIV_2 - 4, palette.faint, 2);
        CenterText(c, "(ADS-B only)", y + 4, palette.faint);
        return;
    }

    const String cnt = String(m.count);
    const int countY = (int)(SCREEN_SIZE * 0.19);
    const int h = FitSegH(cnt.c_str(), ClockDigitH() * 85 / 100, ChordW(countY, ClockDigitH()));
    DrawSegText(c, cnt.c_str(), SCREEN_SIZE_DIV_2, countY, h, palette.fg);

    // ONE aircraft at a time, rotating every 3 s, so a busy picture is never truncated silently.
    const int n = (int)m.aircraft.size();
    if (n > 0) {
        const eam::MilAircraft& a = m.aircraft[(millis() / 3000) % n];
        // category is the backend's human label (e.g. "AWACS (E-3)"); fall back to raw type.
        const String label = a.category.length() ? a.category : a.type;
        String line = label.length() ? label : (a.callsign.length() ? a.callsign : (a.hex.length() ? a.hex : String("unknown")));
        if (label.length() && a.callsign.length()) line = label + " " + a.callsign;
        const int y = CenterWrap(c, line, countY + h + 10, palette.fg, 2) + 4;
        if (a.hasPos && hasLatLon) {
            double km, brg;
            RangeBearing(deviceLat, deviceLon, a.lat, a.lon, km, brg);
            DrawBearingRange(c, brg, km, y, palette.dim, palette.faint);
        }
    }
    CenterText(c, "ADS-B ONLY", (int)(SCREEN_SIZE * 0.84), palette.faint);
}

void EamManager::DrawPropagation(BandCanvas& c)
{
    // SUBJECT: the best HFGCS frequency now. A space-weather event that degrades HF is said in
    // words above it; the solar indices and their source line were a second subject.
    c.setTextSize(2);
    const eam::Propagation& p = feed.Propagation();
    CenterText(c, "BEST HF FREQ", (int)(SCREEN_SIZE * 0.12), palette.dim);
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
        const String banner = sw.hfDegraded ? "HF DEGRADED" : "GEO STORM";
        const int bw = c.textWidth(banner) + 16;
        const int bx = SCREEN_SIZE_DIV_2 - bw / 2;
        const int by = (int)(SCREEN_SIZE * 0.21f);
        const int bh = c.fontHeight() + 8;
        c.fillRoundRect(bx, by, bw, bh, 4, eam::ScaleColor(bcol, 0.18f));
        CenterAt(c, banner, by + 4, bcol);
    }

    const int freqY = (int)(SCREEN_SIZE * 0.35);
    if (p.suggestedKhz) {
        const String khz = String(p.suggestedKhz);
        const int h = FitSegH(khz.c_str(), ClockDigitH(), ChordW(freqY, ClockDigitH()));
        DrawSegText(c, khz.c_str(), SCREEN_SIZE_DIV_2, freqY, h, palette.accent);
        CenterText(c, "kHz", freqY + h + 6, palette.faint);
        if (p.suggestedReason.length())
            CenterWrap(c, p.suggestedReason, freqY + h + 6 + c.fontHeight() + 8, palette.dim, 2);
    } else {
        CenterText(c, "no suggestion", SCREEN_SIZE_DIV_2, palette.faint);
    }
}

void EamManager::DrawIcbm(BandCanvas& c)
{
    // SUBJECT: the next ICBM test window, and how long until it opens.
    c.setTextSize(2);
    const std::vector<eam::Launch>& ls = feed.Launches();
    CenterText(c, "ICBM TEST", (int)(SCREEN_SIZE * 0.10), palette.dim);
    if (ls.empty()) { // rotation hides this screen, but guard anyway
        CenterText(c, "no upcoming", SCREEN_SIZE_DIV_2, palette.faint);
        return;
    }

    const eam::Launch& l = ls.front();
    if (l.designation.length())
        CenterWrap(c, l.designation, (int)(SCREEN_SIZE * 0.23), palette.fg, 1);

    const time_t nowUtc = time(nullptr);
    const int cdY = (int)(SCREEN_SIZE * 0.36);
    if (l.windowStartEpoch > 0 && nowUtc > 1600000000 && nowUtc >= l.windowStartEpoch) {
        CenterText(c, "IN WINDOW", SCREEN_SIZE_DIV_2 - 8, palette.alert);
    } else if (l.windowStartEpoch <= 0 || nowUtc <= 1600000000) {
        // Unknown: unlit digits, not a made-up 00:00:00.
        const int h = FitSegH("??:??:??", ClockDigitH(), ChordW(cdY, ClockDigitH()));
        DrawSegText(c, "??:??:??", SCREEN_SIZE_DIV_2, cdY, h, palette.accent);
    } else {
        const long left = (long)(l.windowStartEpoch - nowUtc);
        const long days = left / 86400;
        char buf[16];
        if (days > 0) {
            // A day or more out: the days, and the word, rather than an hours figure nobody reads.
            snprintf(buf, sizeof(buf), "%ld", days > 999 ? 999L : days);
            DrawSegText(c, buf, SCREEN_SIZE_DIV_2, cdY, ClockDigitH(), palette.accent);
            CenterText(c, days == 1 ? "DAY" : "DAYS", cdY + ClockDigitH() + 6, palette.faint);
        } else {
            snprintf(buf, sizeof(buf), "%02ld:%02ld:%02ld", left / 3600, (left % 3600) / 60, left % 60);
            const int h = FitSegH(buf, ClockDigitH(), ChordW(cdY, ClockDigitH()));
            DrawSegText(c, buf, SCREEN_SIZE_DIV_2, cdY, h, palette.accent);
        }
    }

    if (l.site.length())
        CenterWrap(c, l.site, (int)(SCREEN_SIZE * 0.68), palette.dim, 2);
}

void EamManager::DrawReference(BandCanvas& c)
{
    // SUBJECT: the four primary HFGCS frequencies -- static, no feed dependency, always available.
    c.setTextSize(2);
    CenterText(c, "HFGCS", (int)(SCREEN_SIZE * 0.10), palette.accent);
    CenterText(c, "primary kHz", (int)(SCREEN_SIZE * 0.22), palette.faint);

    const char* row1 = "4724 8992";
    const char* row2 = "11175 15016";
    const int y1 = (int)(SCREEN_SIZE * 0.36);
    const int h = FitSegH(row2, ClockDigitH() * 7 / 10, ChordW(y1, ClockDigitH()));
    DrawSegText(c, row1, SCREEN_SIZE_DIV_2, y1, h, palette.fg);
    DrawSegText(c, row2, SCREEN_SIZE_DIV_2, y1 + h + h / 2, h, palette.fg);
}

void EamManager::DrawClock(BandCanvas& c)
{
    // Six red 7-seg digits HH:MM:SS, 24h UTC, no date. Real segments with a faint ghost; lit
    // segments get a bloom, dimmed at night. Steady colons unless the blink toggle is on.
    // SUBJECT: the time, and nothing else -- the rotating 8 px ambient line is gone. Before the
    // first SNTP sync the digits are UNLIT rather than a made-up 00:00:00.
    const time_t nowUtc = time(nullptr);
    struct tm tmv;
    gmtime_r(&nowUtc, &tmv);
    const bool synced = nowUtc > 1600000000;
    const int hh = synced ? tmv.tm_hour : -1;
    const int mm = synced ? tmv.tm_min : -1;
    const int ss = synced ? tmv.tm_sec : 0;

    const float glow = GlowFactor();
    const uint32_t lit = eam::ScaleColor(eam::ClockLit(), glow);
    const uint32_t bloom = eam::ScaleColor(eam::ClockBloom(), glow);
    const uint32_t ghost = eam::ClockGhost();

    const int digitH = ClockDigitH();
    const int digitW = (int)(digitH * 0.60f);
    const int colonW = (int)(digitW * 0.55f);
    const int gap = (int)(digitW * 0.16f);
    const int rowW = 6 * digitW + 2 * colonW + 7 * gap;
    int x = SCREEN_SIZE_DIV_2 - rowW / 2;
    const int y = SCREEN_SIZE_DIV_2 - digitH / 2;

    // Dark rounded bezel frame.
    const int pad = (int)(digitH * 0.18f);
    c.fillRoundRect(x - pad, y - pad, rowW + 2 * pad, digitH + 2 * pad, pad,
                    eam::ScaleColor(lgfx::color888(18, 4, 3), glow));
    c.drawRoundRect(x - pad, y - pad, rowW + 2 * pad, digitH + 2 * pad, pad,
                    eam::ScaleColor(lgfx::color888(60, 12, 8), glow));

    const bool colonLit = synced && (colonBlink ? (ss % 2 == 0) : true);
    auto digit = [&](int d) { eam::DrawSevenSeg(c, x, y, digitW, digitH, d, lit, ghost, bloom); x += digitW + gap; };
    auto colon = [&]() { eam::DrawColon(c, x, y, colonW, digitH, colonLit, lit, ghost); x += colonW + gap; };

    digit(synced ? hh / 10 : -1); digit(synced ? hh % 10 : -1); colon();
    digit(synced ? mm / 10 : -1); digit(synced ? mm % 10 : -1); colon();
    digit(synced ? ss / 10 : -1); digit(synced ? ss % 10 : -1);
}

// ---- the numbers the one-subject rule moved off other screens (nothing removed is lost) -------

void EamManager::DrawLastMsg(BandCanvas& c)
{
    // SUBJECT: the newest message's particulars -- the frequency it came in on, its length,
    // and how long ago. (These shared the ticker's 8 px header before.)
    c.setTextSize(2);
    CenterText(c, "LAST EAM", (int)(SCREEN_SIZE * 0.10), palette.dim);
    const std::vector<eam::Msg>& latest = feed.Latest();
    if (latest.empty()) {
        CenterText(c, "no data", SCREEN_SIZE_DIV_2, palette.faint);
        return;
    }
    const eam::Msg& m = latest.front();
    const int h = SegMinH() * 13 / 10;
    int y = (int)(SCREEN_SIZE * 0.22);
    if (m.frequencyKhz) {
        DrawSegText(c, String(m.frequencyKhz).c_str(), SCREEN_SIZE_DIV_2, y, h, palette.fg);
        CenterText(c, "kHz", y + h + 4, palette.faint);
    }
    y += h + 26;
    // "340+" ON A PARTIAL COPY: the characters are a floor, not a total (see the ticker's
    // old header comment); the + is text beside the digits, since no segment draws it.
    DrawSegText(c, String(m.charCount).c_str(), SCREEN_SIZE_DIV_2, y, h, palette.fg);
    CenterText(c, m.partial ? "chars (floor)" : "chars", y + h + 4, palette.faint);
    y += h + 26;
    const time_t nowUtc = time(nullptr);
    if (m.heardAtEpoch > 0 && nowUtc > 1600000000 && nowUtc >= m.heardAtEpoch) {
        const long mins = (long)(nowUtc - m.heardAtEpoch) / 60;
        char buf[12];
        snprintf(buf, sizeof(buf), "%ld", mins > 9999 ? 9999L : mins);
        DrawSegText(c, buf, SCREEN_SIZE_DIV_2, y, SegMinH(), palette.dim);
        CenterText(c, "min ago", y + SegMinH() + 4, palette.faint);
    }
}

void EamManager::DrawChannels(BandCanvas& c)
{
    // SUBJECT: today's EAMs per HFGCS channel; the busiest in the tempo colour, the channel
    // propagation favours in accent. (The tempo screen's old frequency strip.)
    c.setTextSize(2);
    CenterText(c, "BY CHANNEL", (int)(SCREEN_SIZE * 0.10), palette.dim);
    const std::vector<eam::FreqCount>& byFreq = feed.Stats().byFreq;
    if (byFreq.empty()) {
        CenterText(c, "no data", SCREEN_SIZE_DIV_2, palette.faint);
        return;
    }
    const int suggested = feed.Propagation().valid ? feed.Propagation().suggestedKhz : 0;
    int busiestKhz = 0, busiestCount = -1;
    for (const eam::FreqCount& fc : byFreq)
        if (fc.count > busiestCount) { busiestCount = fc.count; busiestKhz = fc.khz; }
    const int h = SegMinH();
    const int rowH = h + 8;
    int y = (int)(SCREEN_SIZE * 0.24);
    const int xk = SCREEN_SIZE_DIV_2 - SCREEN_SIZE / 7;   // kHz column centre
    const int xc = SCREEN_SIZE_DIV_2 + SCREEN_SIZE / 5;   // count column centre
    for (const eam::FreqCount& fc : byFreq) {
        if (y + h > (int)(SCREEN_SIZE * 0.86)) break;
        const uint32_t kcol = (suggested && fc.khz == suggested) ? palette.accent : palette.dim;
        const uint32_t ccol = (fc.khz == busiestKhz && busiestCount > 0) ? palette.fg : palette.dim;
        if (fc.khz > 0) DrawSegText(c, String(fc.khz).c_str(), xk, y, h, kcol);
        else { c.setTextColor(kcol); c.drawString("other", xk - c.textWidth("other") / 2, y + (h - c.fontHeight()) / 2); }
        DrawSegText(c, String(fc.count).c_str(), xc, y, h, ccol);
        y += rowH;
    }
}

void EamManager::DrawCwMonth(BandCanvas& c)
{
    // SUBJECT: how many distinct SKYKING codewords this device has logged this month.
    c.setTextSize(2);
    CenterText(c, "CODEWORDS", (int)(SCREEN_SIZE * 0.20), palette.dim);
    const time_t nowUtc = time(nullptr);
    const long nowEpoch = (nowUtc > 1600000000) ? (long)nowUtc : 0;
    const String n = String((unsigned)logbook.CodewordsThisMonth(nowEpoch));
    const int h = ClockDigitH();
    DrawSegText(c, n.c_str(), SCREEN_SIZE_DIV_2, SCREEN_SIZE_DIV_2 - h / 2, h, palette.fg);
    CenterText(c, "this month", SCREEN_SIZE_DIV_2 + h / 2 + 8, palette.faint);
}

void EamManager::DrawSolar(BandCanvas& c)
{
    // SUBJECT: the solar indices behind the propagation call -- SFI, K, and the NOAA R and G
    // scales when known -- and whose numbers they are. (The propagation screen's old line.)
    c.setTextSize(2);
    const eam::Propagation& p = feed.Propagation();
    CenterText(c, "SOLAR", (int)(SCREEN_SIZE * 0.10), palette.dim);
    if (!p.valid) {
        CenterText(c, "no data", SCREEN_SIZE_DIV_2, palette.faint);
        return;
    }
    const int h = SegMinH() * 13 / 10;
    const int xl = SCREEN_SIZE_DIV_2 - SCREEN_SIZE / 5, xr = SCREEN_SIZE_DIV_2 + SCREEN_SIZE / 5;
    auto cell = [&](int cx, int y, const char* label, int v, uint32_t col) {
        char buf[12];
        if (v >= 0) snprintf(buf, sizeof(buf), "%d", v); else snprintf(buf, sizeof(buf), "?");
        DrawSegText(c, buf, cx, y, h, col);
        c.setTextColor(palette.faint);
        c.drawString(label, cx - c.textWidth(label) / 2, y + h + 4);
    };
    const int y1 = (int)(SCREEN_SIZE * 0.22), y2 = y1 + h + 30;
    cell(xl, y1, "SFI", p.sfi, palette.fg);
    cell(xr, y1, "K", p.kIndex, palette.fg);
    const eam::SpaceWeather& sw = p.space;
    if (sw.valid) {
        cell(xl, y2, "R", sw.rScale, sw.rScale >= 3 ? palette.alert : sw.rScale >= 1 ? palette.warn : palette.fg);
        cell(xr, y2, "G", sw.gScale, sw.gScale >= 3 ? palette.alert : sw.gScale >= 1 ? palette.warn : palette.fg);
    }
    if (p.source.length()) CenterWrap(c, p.source, (int)(SCREEN_SIZE * 0.72), palette.faint, 2);
}

void EamManager::DrawQuiet(BandCanvas& c)
{
    // SUBJECT: the longest stretch today with no EAM, as H:MM. (The clock's old ambient line.)
    c.setTextSize(2);
    CenterText(c, "LONGEST QUIET", (int)(SCREEN_SIZE * 0.22), palette.dim);
    const int q = feed.Stats().longestQuietMin;
    if (q < 0) {
        CenterText(c, "no data", SCREEN_SIZE_DIV_2, palette.faint);
        return;
    }
    char buf[12];
    snprintf(buf, sizeof(buf), "%d:%02d", q / 60 > 99 ? 99 : q / 60, q % 60);
    const int h = ClockDigitH();
    DrawSegText(c, buf, SCREEN_SIZE_DIV_2, SCREEN_SIZE_DIV_2 - h / 2, h, palette.fg);
    CenterText(c, "today, h:mm", SCREEN_SIZE_DIV_2 + h / 2 + 8, palette.faint);
}

void EamManager::DrawLogbook(BandCanvas& c)
{
    // SUBJECT: how many distinct EAMs this device has logged. (The clock's old ambient line.)
    c.setTextSize(2);
    CenterText(c, "LOGBOOK", (int)(SCREEN_SIZE * 0.22), palette.dim);
    const String n = String((unsigned)logbook.EamCount());
    const int h = FitSegH(n.c_str(), ClockDigitH(), ChordW(SCREEN_SIZE_DIV_2 - ClockDigitH() / 2, ClockDigitH()));
    DrawSegText(c, n.c_str(), SCREEN_SIZE_DIV_2, SCREEN_SIZE_DIV_2 - h / 2, h, palette.fg);
    CenterText(c, "EAMs logged", SCREEN_SIZE_DIV_2 + h / 2 + 8, palette.faint);
}
