#pragma once

// Weatherscope's screen logic (docs/weatherscope-stage3.md, Addendum 8 and its rulings in A8.8). Pure C++11: no
// Arduino, no allocation, so test/host/test_wx_logic.cpp holds every rule to its frozen truth table, and
// test/host/sabotage_wx_logic.py proves each table can fail exactly where it says.
//
// Every distance is the MAP's metric (WxRender.h): planar, 111 km per degree, longitude scaled by the cosine of the
// home latitude -- so an echo drawn inside the 15 mi ring is inside the ring, and a storm's distance matches the map.
// Text is returned as tokens; the screen joins them, so no separator glyph is decided here.

#include <initializer_list>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>   // strtod (ParseMotion): A8.15's follow-up, made at this header's next change
#include <string.h>

#include "WxCrop.h"

namespace wxlogic {

constexpr double kPi = 3.14159265358979323846;
constexpr double kKmPerDeg = 111.0;         // the map's metric (WxRender.h; AircraftManager's KM_PER_DEGREE)
constexpr double kKmPerMi = 1.609344;
constexpr double kRingMi = 15.0;            // 1b, ruled final
constexpr double kMphPerKt = 1.150779448;
constexpr int64_t kObsMaxAgeS = 2 * 3600;   // Q1 (A8.8): an observation older than 2 h gives no "here now"

struct LatLon { double lat, lon; };

// ------------------------------------------------------------------------------------------------- units (6a; U)

// Half away from zero (lround), pinned: ports of this math must not inherit another language's rounding.
inline int KtToMph(double kt) { return (int)lround(kt * kMphPerKt); }
inline double KmToMi(double km) { return km / kKmPerMi; }
inline const char* Compass8(double deg)
{
    static const char* const kName[8] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
    int s = (int)floor((deg + 22.5) / 45.0) % 8;
    if (s < 0) s += 8;
    return kName[s];
}

// ------------------------------------------------------------------------------- distance and bearing, map metric

inline void PlanarKm(LatLon from, LatLon to, double* dxKm, double* dyKm)
{
    *dxKm = (to.lon - from.lon) * kKmPerDeg * cos(from.lat * (kPi / 180.0));
    *dyKm = (to.lat - from.lat) * kKmPerDeg;
}
inline double DistanceMi(LatLon from, LatLon to)
{
    double dx, dy;
    PlanarKm(from, to, &dx, &dy);
    return sqrt(dx * dx + dy * dy) / kKmPerMi;
}
inline double BearingDeg(LatLon from, LatLon to)
{
    double dx, dy;
    PlanarKm(from, to, &dx, &dy);
    const double b = atan2(dx, dy) * (180.0 / kPi);
    return b < 0 ? b + 360.0 : b;
}

// -------------------------------------------------------------------------------------------- the ring (1b; R)

// Distance from `here` to the nearest echo (code 1-13, rain or snow alike) cell centre, in miles; -1 when none.
inline double NearestEchoMi(const wxcrop::Header& h, const uint8_t* codes, LatLon here)
{
    const double latNw = h.latNwMicro / 1e6, lonNw = h.lonNwMicro / 1e6;
    const double cellLat = h.cellLatMicro / 1e6, cellLon = h.cellLonMicro / 1e6;
    const double kx = kKmPerDeg * cos(here.lat * (kPi / 180.0));
    const double ky = kKmPerDeg;
    double best = -1.0;
    for (uint32_t r = 0; r < h.rows; ++r) {
        const double dy = (latNw - r * cellLat - here.lat) * ky;
        for (uint32_t c = 0; c < h.cols; ++c) {
            const uint8_t code = codes[r * (uint32_t)h.cols + c];
            if (!wxcrop::IsEcho(code)) continue;
            const double dx = (lonNw + c * cellLon - here.lon) * kx;
            const double d2 = dx * dx + dy * dy;
            if (best < 0 || d2 < best) best = d2;
        }
    }
    return best < 0 ? -1.0 : sqrt(best) / kKmPerMi;
}
inline bool EchoWithin(const wxcrop::Header& h, const uint8_t* codes, LatLon here, double radiusMi = kRingMi)
{
    const double d = NearestEchoMi(h, codes, here);
    return d >= 0 && d <= radiusMi;
}

// ------------------------------------------------------------------------------------- here now (1c, Q1; H)

enum class Precip : uint8_t { None, Rain, Snow };

inline bool Is2(const char* p, const char* code) { return p[0] == code[0] && p[1] == code[1]; }
inline bool SnowCode(const char* p) { return Is2(p, "SN") || Is2(p, "SG") || Is2(p, "PL") || Is2(p, "IC"); }
inline bool RainCode(const char* p) { return Is2(p, "DZ") || Is2(p, "RA") || Is2(p, "GR") || Is2(p, "GS") || Is2(p, "UP"); }

// One METAR weather group, e.g. "-SHRASN": an intensity sign, then two-letter descriptor and precipitation codes.
inline void ClassifyGroup(const char* g, size_t n, bool* rain, bool* snow)
{
    size_t i = 0;
    if (i < n && (g[i] == '-' || g[i] == '+')) ++i;
    if (i + 1 < n && Is2(g + i, "VC")) return;                 // "in the vicinity" is not here (Q1)
    for (; i + 1 < n; i += 2) {
        if (SnowCode(g + i)) *snow = true;
        else if (RainCode(g + i)) *rain = true;
    }
}

// The observation's present weather, one raw string per entry (a string may hold several space-separated groups).
inline Precip HereNow(const char* const* entries, size_t n, bool valid, int64_t obsUtc, int64_t nowUtc)
{
    if (!valid) return Precip::None;
    if (nowUtc - obsUtc > kObsMaxAgeS) return Precip::None;
    bool rain = false, snow = false;
    for (size_t e = 0; e < n; ++e) {
        const char* s = entries[e];
        if (!s) continue;
        while (*s) {
            while (*s == ' ') ++s;
            const char* end = s;
            while (*end && *end != ' ') ++end;
            ClassifyGroup(s, (size_t)(end - s), &rain, &snow);
            s = end;
        }
    }
    return snow ? Precip::Snow : (rain ? Precip::Rain : Precip::None);   // snow wins if any snow token (Q1)
}

// ------------------------------------------------------------------------------------- warnings (1d, 3a-3e)

struct Warn {
    const char* event = nullptr;
    const char* messageType = nullptr;          // "Alert", "Update", "Cancel"
    int64_t expiresUtc = 0;
    const char* expiresIso = nullptr;           // as NWS states it, with its own offset (Q7)
    const LatLon* polygon = nullptr;
    size_t polygonN = 0;
    const char* motion = nullptr;               // eventMotionDescription
    const char* maxHailSize = nullptr;          // inches, e.g. "1.00"
    const char* maxWindGust = nullptr;          // e.g. "60 MPH"
    const char* tornadoDetection = nullptr;     // "RADAR INDICATED" / "OBSERVED"
    const char* tornadoDamageThreat = nullptr;  // "CONSIDERABLE" / "CATASTROPHIC"
    const char* flashFloodDetection = nullptr;
};

// 3a, 3d: the four warnings shown, in order; anything else is never shown. The WHOLE event name is matched.
inline int WarningRank(const char* e)
{
    if (!e) return -1;
    if (strcmp(e, "Tornado Warning") == 0) return 0;
    if (strcmp(e, "Snow Squall Warning") == 0) return 1;
    if (strcmp(e, "Severe Thunderstorm Warning") == 0) return 2;
    if (strcmp(e, "Flash Flood Warning") == 0) return 3;
    return -1;
}

// 3c: ended at `expires` (that instant counts as ended), or when the message is a Cancel.
inline bool Ended(const Warn& w, int64_t nowUtc)
{
    if (w.messageType && strcmp(w.messageType, "Cancel") == 0) return true;
    return nowUtc >= w.expiresUtc;
}

inline bool OnSegment(LatLon a, LatLon b, LatLon p)
{
    const double cross = (b.lon - a.lon) * (p.lat - a.lat) - (b.lat - a.lat) * (p.lon - a.lon);
    if (fabs(cross) > 1e-12) return false;
    return p.lon >= fmin(a.lon, b.lon) && p.lon <= fmax(a.lon, b.lon) && p.lat >= fmin(a.lat, b.lat) &&
           p.lat <= fmax(a.lat, b.lat);
}

// 1d: the location is inside the polygon or on its boundary. GeoJSON's repeated closing vertex is accepted.
inline bool Covers(const LatLon* ring, size_t n, LatLon p)
{
    if (!ring) return false;
    if (n >= 2 && ring[0].lat == ring[n - 1].lat && ring[0].lon == ring[n - 1].lon) --n;
    if (n < 3) return false;
    for (size_t i = 0, j = n - 1; i < n; j = i++) if (OnSegment(ring[j], ring[i], p)) return true;   // the boundary counts
    bool in = false;
    for (size_t i = 0, j = n - 1; i < n; j = i++) {
        if ((ring[i].lat > p.lat) != (ring[j].lat > p.lat)) {
            const double x = (ring[j].lon - ring[i].lon) * (p.lat - ring[i].lat) / (ring[j].lat - ring[i].lat) + ring[i].lon;
            if (p.lon < x) in = !in;
        }
    }
    return in;
}

// --------------------------------------------------------------------------------------- storm motion (§3; M)

constexpr int kMaxPos = 8;
struct Motion {
    bool valid = false;
    int fromDeg = 0;     // the direction the storm comes FROM, as NWS writes it
    int speedKt = 0;
    LatLon pos[kMaxPos];
    int nPos = 0;
};

inline bool ParseUInt(const char* s, size_t n, int* out)
{
    if (n == 0 || n > 4) return false;
    int v = 0;
    for (size_t i = 0; i < n; ++i) {
        if (s[i] < '0' || s[i] > '9') return false;
        v = v * 10 + (s[i] - '0');
    }
    *out = v;
    return true;
}

// "<time>...storm...<deg>DEG...<kt>KT...<lat>,<lon>[ <lat>,<lon>...]". Anything malformed is invalid, never guessed.
inline Motion ParseMotion(const char* s)
{
    Motion m;
    if (!s || !*s) return m;
    const char* f[8];
    size_t len[8];
    int nf = 0;
    for (const char* p = s; nf < 8;) {
        const char* q = strstr(p, "...");
        f[nf] = p;
        len[nf] = q ? (size_t)(q - p) : strlen(p);
        ++nf;
        if (!q) break;
        p = q + 3;
    }
    bool haveDeg = false, haveKt = false;
    for (int i = 0; i < nf; ++i) {
        if (len[i] > 3 && strncmp(f[i] + len[i] - 3, "DEG", 3) == 0) haveDeg = ParseUInt(f[i], len[i] - 3, &m.fromDeg);
        else if (len[i] > 2 && strncmp(f[i] + len[i] - 2, "KT", 2) == 0) haveKt = ParseUInt(f[i], len[i] - 2, &m.speedKt);
    }
    if (!haveDeg || !haveKt || nf < 2) return m;
    char buf[160];
    const size_t n = len[nf - 1] < sizeof(buf) - 1 ? len[nf - 1] : sizeof(buf) - 1;
    memcpy(buf, f[nf - 1], n);
    buf[n] = '\0';
    for (char* p = buf; *p && m.nPos < kMaxPos;) {
        while (*p == ' ') ++p;
        if (!*p) break;
        char* end = nullptr;
        const double lat = strtod(p, &end);
        if (end == p || *end != ',') return Motion();
        char* end2 = nullptr;
        const double lon = strtod(end + 1, &end2);
        if (end2 == end + 1 || (*end2 && *end2 != ' ')) return Motion();
        m.pos[m.nPos].lat = lat;
        m.pos[m.nPos].lon = lon;
        ++m.nPos;
        p = end2;
    }
    m.valid = m.nPos > 0;
    return m;
}

inline const char* TravelCompass(const Motion& m) { return Compass8(fmod(m.fromDeg + 180.0, 360.0)); }

// Q4: a line of storms uses the position nearest the location.
inline int NearestPos(const Motion& m, LatLon here)
{
    int best = 0;
    for (int k = 1; k < m.nPos; ++k) if (DistanceMi(here, m.pos[k]) < DistanceMi(here, m.pos[best])) best = k;
    return best;
}

struct MotionText {
    bool valid = false;
    double distanceMi = 0;
    const char* bearing = "";  // from the location to the storm
    char distance[24] = {};    // "15 MI", or "<1 MI" under 0.5 mi (Q4)
    char travel[40] = {};      // "MOVING NE 43 MPH", or "STATIONARY" at 0 kt (Q4, frozen in A8.8)
};

inline MotionText MotionLine(const Motion& m, LatLon here)
{
    MotionText t;
    if (!m.valid) return t;
    const int k = NearestPos(m, here);
    t.distanceMi = DistanceMi(here, m.pos[k]);
    t.bearing = Compass8(BearingDeg(here, m.pos[k]));
    if (t.distanceMi < 0.5) snprintf(t.distance, sizeof(t.distance), "<1 MI");
    else snprintf(t.distance, sizeof(t.distance), "%ld MI", lround(t.distanceMi));
    if (m.speedKt == 0) snprintf(t.travel, sizeof(t.travel), "STATIONARY");
    else snprintf(t.travel, sizeof(t.travel), "MOVING %s %d MPH", TravelCompass(m), KtToMph(m.speedKt));
    t.valid = true;
    return t;
}

// ------------------------------------------------------------------------------- warning order (3d, Q2; W)

// Type first; within a type, the nearer storm when both carry a position, otherwise the later expiry (Q2).
inline bool Before(const Warn& a, const Warn& b, LatLon here)
{
    const int ra = WarningRank(a.event), rb = WarningRank(b.event);
    if (ra != rb) return ra < rb;
    const Motion ma = ParseMotion(a.motion), mb = ParseMotion(b.motion);
    if (ma.valid && mb.valid) {
        const double da = DistanceMi(here, ma.pos[NearestPos(ma, here)]), db = DistanceMi(here, mb.pos[NearestPos(mb, here)]);
        if (da != db) return da < db;
    }
    return a.expiresUtc > b.expiresUtc;
}

inline bool Counts(const Warn& w, LatLon here, int64_t nowUtc)
{
    return WarningRank(w.event) >= 0 && !Ended(w, nowUtc) && Covers(w.polygon, w.polygonN, here);
}

// The indices of the warnings that count, in display order; returns how many were written (at most maxOut).
inline int Order(const Warn* ws, size_t n, LatLon here, int64_t nowUtc, int* out, int maxOut)
{
    int k = 0;
    int all[16];
    for (size_t i = 0; i < n && k < 16; ++i) if (Counts(ws[i], here, nowUtc)) all[k++] = (int)i;
    for (int i = 1; i < k; ++i) {                                        // insertion sort: stable, tiny k
        const int v = all[i];
        int j = i - 1;
        while (j >= 0 && Before(ws[v], ws[all[j]], here)) { all[j + 1] = all[j]; --j; }
        all[j + 1] = v;
    }
    const int m = k < maxOut ? k : maxOut;
    for (int i = 0; i < m; ++i) out[i] = all[i];
    return m;
}

// ---------------------------------------------------------------------------------- which screen (1a; P)

enum class Screen : uint8_t { Warning, PrecipNow, PrecipApproaching, Dry };
struct Choice {
    Screen screen;
    Precip type;
    int warning;   // index into the warnings, or -1
};

inline Choice Choose(const Warn* ws, size_t n, LatLon here, int64_t nowUtc, Precip hereNow, bool approaching)
{
    int first = -1;
    if (Order(ws, n, here, nowUtc, &first, 1) > 0) return Choice{Screen::Warning, Precip::None, first};
    if (hereNow != Precip::None) return Choice{Screen::PrecipNow, hereNow, -1};
    if (approaching) return Choice{Screen::PrecipApproaching, Precip::None, -1};
    return Choice{Screen::Dry, Precip::None, -1};
}

// ----------------------------------------------------------------------------------------- headlines (Q5; HL)

inline const char* Headline(const Choice& c)
{
    if (c.screen == Screen::PrecipNow) return c.type == Precip::Snow ? "SNOW NOW" : "RAIN NOW";
    if (c.screen == Screen::PrecipApproaching) return "PRECIP WITHIN 15 MI";
    return "";
}

// ------------------------------------------------------------------------ the first bottom line (3b, 3e, Q3; T, B)

struct Tokens {
    char t[3][32];
    int n = 0;
    void Add(const char* s)
    {
        if (n < 3) { snprintf(t[n], sizeof(t[n]), "%s", s); ++n; }
    }
};

// "1.00" -> "HAIL 1 IN", "1.50" -> "HAIL 1.5 IN". A value that isn't a plain decimal is omitted.
inline bool HailText(const char* s, char* out, size_t cap)
{
    if (!s || !*s) return false;
    char num[16];
    size_t n = 0;
    bool dot = false;
    for (const char* p = s; *p; ++p) {
        if (*p == '.' && !dot) dot = true;
        else if (*p < '0' || *p > '9') return false;
        if (n + 1 >= sizeof(num)) return false;
        num[n++] = *p;
    }
    num[n] = '\0';
    if (dot) { while (n > 0 && num[n - 1] == '0') num[--n] = '\0'; if (n > 0 && num[n - 1] == '.') num[--n] = '\0'; }
    if (n == 0) return false;
    snprintf(out, cap, "HAIL %s IN", num);
    return true;
}

// "60 MPH" -> "WIND 60 MPH": the leading integer, or nothing.
inline bool WindText(const char* s, char* out, size_t cap)
{
    if (!s) return false;
    size_t n = 0;
    while (s[n] >= '0' && s[n] <= '9') ++n;
    int v = 0;
    if (!ParseUInt(s, n, &v)) return false;
    snprintf(out, cap, "WIND %d MPH", v);
    return true;
}

inline Tokens LineA(const Warn& w)
{
    Tokens k;
    char buf[32];
    const int rank = WarningRank(w.event);
    if (rank == 0) {                                                                 // Tornado (Q3)
        if (w.tornadoDamageThreat && strcmp(w.tornadoDamageThreat, "CATASTROPHIC") == 0) k.Add("TORNADO EMERGENCY");
        else if (w.tornadoDetection && (strcmp(w.tornadoDetection, "OBSERVED") == 0 ||
                                        strcmp(w.tornadoDetection, "RADAR INDICATED") == 0)) k.Add(w.tornadoDetection);
        if (HailText(w.maxHailSize, buf, sizeof(buf))) k.Add(buf);
    } else if (rank == 2) {                                                          // Severe Thunderstorm
        if (HailText(w.maxHailSize, buf, sizeof(buf))) k.Add(buf);
        if (WindText(w.maxWindGust, buf, sizeof(buf))) k.Add(buf);
    } else if (rank == 3) {                                                          // Flash Flood (3b)
        if (w.flashFloodDetection && *w.flashFloodDetection) k.Add(w.flashFloodDetection);
    }                                                                                // Snow Squall: none (3e)
    return k;
}

// ------------------------------------------------------------------------------------------- "UNTIL" (Q7; X)

// "UNTIL h:mm AM/PM" in the alert's own offset: the clock time NWS wrote, so no conversion. Known limit (A8.8):
// near a time-zone border the alert's offset can differ from the location's by an hour.
inline bool UntilText(const char* iso, char* out, size_t cap)
{
    if (!iso || strlen(iso) < 20) return false;
    for (int i : {0, 1, 2, 3, 5, 6, 8, 9, 11, 12, 14, 15, 17, 18}) if (iso[i] < '0' || iso[i] > '9') return false;
    if (iso[4] != '-' || iso[7] != '-' || iso[10] != 'T' || iso[13] != ':' || iso[16] != ':') return false;
    const int hour = (iso[11] - '0') * 10 + (iso[12] - '0'), minute = (iso[14] - '0') * 10 + (iso[15] - '0');
    if (hour > 23 || minute > 59) return false;
    const char* z = iso + 19;
    int offMin = 0;
    if (z[0] == 'Z' && z[1] == '\0') offMin = 0;
    else if ((z[0] == '+' || z[0] == '-') && strlen(z) == 6 && z[3] == ':') {
        int oh = 0, om = 0;
        if (!ParseUInt(z + 1, 2, &oh) || !ParseUInt(z + 4, 2, &om)) return false;
        offMin = (z[0] == '-' ? -1 : 1) * (oh * 60 + om);
    } else return false;
    (void)offMin;
    const int local = hour * 60 + minute;
    const int h = local / 60 % 24, mm = local % 60;
    snprintf(out, cap, "UNTIL %d:%02d %s", h % 12 == 0 ? 12 : h % 12, mm, h < 12 ? "AM" : "PM");
    return true;
}

// ------------------------------------------------------------------------------------- town labels (5d, Q6; L, LP)

// "Bend city" -> "BEND": upper-cased, a trailing " CITY" dropped once (only LSAD 25 places are labelled, 5b).
inline void LabelText(const char* name, char* out, size_t cap)
{
    // Title case (Q13, superseding 5d): the Gazetteer's own case is kept, and its LSAD suffix " city" dropped once.
    size_t i = 0;
    for (; name && name[i] && i + 1 < cap; ++i) out[i] = name[i];
    out[i] = '\0';
    const size_t n = strlen(out);
    if (n >= 5 && strcmp(out + n - 5, " city") == 0) out[n - 5] = '\0';
}

struct LabelBox {
    int x, y, w, h;   // pixels, the box spans [x, x + w) x [y, y + h)
    double distMi;    // from the centre, for the drawing order
};

inline bool Overlap(const LabelBox& a, const LabelBox& b)
{
    return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}

// The legend's round-screen margin, reused: every corner within 112 px of the centre (A8.8, a layout rule).
inline bool InsideCircle(const LabelBox& b)
{
    const int xs[2] = {b.x, b.x + b.w}, ys[2] = {b.y, b.y + b.h};
    for (int xi : xs) for (int yi : ys) if ((xi - 120) * (xi - 120) + (yi - 120) * (yi - 120) > 112 * 112) return false;
    return true;
}

// Q6(a): nearest to the centre first; skip a label that would overlap one already DRAWN. Writes the drawn indices.
inline int PlaceLabels(const LabelBox* in, int n, int* drawn)
{
    int order[64];
    const int m = n < 64 ? n : 64;
    for (int i = 0; i < m; ++i) order[i] = i;
    for (int i = 1; i < m; ++i) {
        const int v = order[i];
        int j = i - 1;
        while (j >= 0 && in[v].distMi < in[order[j]].distMi) { order[j + 1] = order[j]; --j; }
        order[j + 1] = v;
    }
    int nd = 0;
    for (int i = 0; i < m; ++i) {
        const LabelBox& b = in[order[i]];
        if (!InsideCircle(b)) continue;
        bool clash = false;
        for (int k = 0; k < nd; ++k) if (Overlap(b, in[drawn[k]])) clash = true;
        if (!clash) drawn[nd++] = order[i];
    }
    return nd;
}

}  // namespace wxlogic
