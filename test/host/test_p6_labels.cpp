// P6's host rows (docs/speed-p5p6.md section 2 and amendment 4g, on weatherscope/speed-p5p6): the label cache
// (include/LabelCache.h) against today's layout, with the REAL formatters and the REAL LabelKey
// (src/AircraftInfoFields.cpp, compiled against test/host/p6_fake/TrackedAircraft.h; run.sh copies them together).
//
//   test_p6_labels      exit 0 every row ok, 1 a row failed, 2 the rig
//
// Rows (each prints "ok" or "FAIL" and its id; test/host/sabotage_p6_labels.py reads the ids):
//   CMP control   the comparison sees one changed x
//   P6-COVER      every member the formatters read, moved one at a time through several values: wherever any field's
//                 text changes, LabelKey changes (and every member moves some text at least once: the control)
//   P6-SEQ@20/40/60  a recorded sequence of 400 frames: every input changes while its field is shown alone, contacts
//                 move (the rim's drops and clamps included), the enabled fields change, and inputs too long for the
//                 cache's buffers occur. Each frame, the cache's layout (box, then draw) against today's: 0 mismatches
//                 in count, index, string, x, y or width
//   P6-SHARE      the draw's Refresh is a hit on every frame the box's succeeded: the two share the one entry
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "AircraftInfoFields.h"
#include "DiscGeometry.h"
#include "LabelCache.h"

namespace {

int failures = 0;
void Row(bool ok, const std::string& id, const std::string& what)
{
    if (!ok) ++failures;
    std::printf("  %s  %s  %s\n", ok ? "ok  " : "FAIL", id.c_str(), what.c_str());
}

// The host's adapter: the real table, formatters and key; a 6 px font (any width function serves, both paths use it).
struct Fields {
    const TrackedAircraft& t;
    const std::vector<bool>& on;
    int Count() const { return (int)AIRCRAFT_INFO_FIELD_COUNT; }
    const char* FieldKey(int i) const { return AIRCRAFT_INFO_FIELDS[i].key; }
    bool On(int i) const { return (size_t)i < on.size() && on[(size_t)i]; }
    String Format(int i) const { return AIRCRAFT_INFO_FIELDS[i].format(t); }
    int Width(const String& s) const { return 6 * (int)s.length(); }
    int LineHeight() const { return 9; }
    int Chord(int rowY, int lineH) const { return discgeom::ChordWidthPx(rowY, lineH, 240, discgeom::DISC_TEXT_INSET_PX); }
    int Half() const { return 120; }
    void MakeKey(labelcache::KeyBuf& k) const { LabelKey(t, k); }
};

constexpr int L = labelcache::kLines;
struct Lay {
    int n = 0;
    int idx[L], x[L], y[L], w[L];
    std::string text[L];
};

Lay Today(const Fields& f, int x, int y)
{
    Lay o;
    o.n = labelcache::LayoutToday(f, x, y, o.idx, o.x, o.y, o.w);
    for (int k = 0; k < o.n; ++k) o.text[k] = f.Format(o.idx[k]).c_str();
    return o;
}

// The cache's path, as AircraftManager::LabelLayout + DrawAircraftInfo take it: Refresh, then place; an uncacheable
// contact is laid out as today and its text formatted.
Lay Cached(const Fields& f, labelcache::Slot& slot, int x, int y, labelcache::Stats& st, bool* refreshed)
{
    Lay o;
    labelcache::Entry* e = slot.Get();
    if (e && labelcache::Refresh(f, *e, &st)) {
        int s[L];
        o.n = labelcache::PlaceCached(f, *e, x, y, o.idx, o.x, o.y, o.w, s);
        for (int k = 0; k < o.n; ++k) o.text[k] = e->text[s[k]];
        if (refreshed) *refreshed = true;
        return o;
    }
    if (refreshed) *refreshed = false;
    return Today(f, x, y);
}

bool Same(const Lay& a, const Lay& b)
{
    if (a.n != b.n) return false;
    for (int k = 0; k < a.n; ++k)
        if (a.idx[k] != b.idx[k] || a.x[k] != b.x[k] || a.y[k] != b.y[k] || a.w[k] != b.w[k] || a.text[k] != b.text[k])
            return false;
    return true;
}

std::vector<std::string> Texts(const TrackedAircraft& t)
{
    std::vector<std::string> v;
    for (size_t i = 0; i < AIRCRAFT_INFO_FIELD_COUNT; ++i) v.push_back(AIRCRAFT_INFO_FIELDS[i].format(t).c_str());
    return v;
}

labelcache::KeyBuf KeyOf(const TrackedAircraft& t)
{
    labelcache::KeyBuf k;
    LabelKey(t, k);
    return k;
}

TrackedAircraft Base()
{
    TrackedAircraft t;
    t.state.icao24 = "a1b2c3";
    t.state.callsign = "ASA537  ";
    t.state.originCountry = "United States";
    t.state.squawk = "4521";
    t.state.baroAltitude = 3200.0f;
    t.state.velocity = 210.0f;
    t.state.trueTrack = 93.0f;
    t.state.verticalRate = -4.5f;
    t.state.geoAltitude = 3260.0f;
    t.state.positionSource = 0;
    t.state.category = 4;
    t.typeCode = "B738";
    t.operatorName = "Alaska Airlines";
    t.registration = "N123AS";
    t.routeOrigin = "SEA";
    t.routeDest = "SFO";
    return t;
}

struct CoverTally {
    long violations = 0, members = 0, silent = 0, moved = 0;
    std::string silentNames;
};

// One member through several values: every value that moves some field's text must move the key.
template <class V, class SetFn>
void Probe(CoverTally& c, const char* name, SetFn set, const V* values, size_t nv)
{
    ++c.members;
    long movedHere = 0;
    const TrackedAircraft base = Base();
    const std::vector<std::string> t0 = Texts(base);
    const labelcache::KeyBuf k0 = KeyOf(base);
    for (size_t i = 0; i < nv; ++i) {
        TrackedAircraft m = base;
        set(m, values[i]);
        if (Texts(m) != t0) {
            ++movedHere;
            if (KeyOf(m) == k0) ++c.violations;
        }
    }
    c.moved += movedHere;
    if (movedHere == 0) { ++c.silent; c.silentNames += std::string(" ") + name; }
}

void Cover()
{
    static const char* const strs[] = {"", "A", "B", " A ", "ab", "AB", "SEA", "a much longer value than any field"};
    static const float floats[] = {0.0f, 0.1f, 0.2f, 0.6f, 1.0f, -1.0f, 123.4f, 1000.0f, 3200.0f};
    static const int ints[] = {-1, 0, 1, 2, 5, 8, 9, 13, 14, 18, 21};
    CoverTally c;
#define P6_PROBE_STATE_S(mem) Probe(c, #mem, [](TrackedAircraft& t, const char* v) { t.state.mem = v; }, strs, sizeof strs / sizeof strs[0]);
#define P6_PROBE_STATE_F(mem) Probe(c, #mem, [](TrackedAircraft& t, float v) { t.state.mem = v; }, floats, sizeof floats / sizeof floats[0]);
#define P6_PROBE_STATE_I(mem) Probe(c, #mem, [](TrackedAircraft& t, int v) { t.state.mem = v; }, ints, sizeof ints / sizeof ints[0]);
#define P6_PROBE_TOP_S(mem) Probe(c, #mem, [](TrackedAircraft& t, const char* v) { t.mem = v; }, strs, sizeof strs / sizeof strs[0]);
    P6_STATE_STRINGS(P6_PROBE_STATE_S)
    P6_STATE_FLOATS(P6_PROBE_STATE_F)
    P6_STATE_INTS(P6_PROBE_STATE_I)
    P6_TOP_STRINGS(P6_PROBE_TOP_S)
    Row(c.violations == 0 && c.silent == 0, "P6-COVER",
        std::to_string(c.members) + " members, " + std::to_string(c.moved) + " moves of some field's text, " +
            std::to_string(c.violations) + " with the key unmoved (want 0); members that moved no text: " +
            std::to_string(c.silent) + (c.silent ? " (" + c.silentNames + " )" : ""));
}

// The recorded sequence. Every value is a function of (contact, frame), so every run plays the same 400 frames.
std::string Pick(const char* stem, int c, int period, int f)
{
    return std::string(stem) + std::to_string(c % 7) + std::to_string((f / period) % 4);
}

void Set(TrackedAircraft& t, int c, int f)
{
    t.state.callsign = (f / 5) % 3 == 0 ? (" CS" + std::to_string(c) + " ").c_str() : Pick("CS", c, 5, f).c_str();
    t.state.icao24 = Pick("a0", c, 7, f).c_str();
    t.state.originCountry = c == 0 ? std::string(170, 'C').c_str() : Pick("Land", c, 9, f).c_str();   // c 0: past the key
    t.state.squawk = (f / 11) % 3 == 1 ? "" : Pick("7", c, 11, f).c_str();
    t.typeCode = f < 13 ? "" : Pick("T", c, 13, f).c_str();                                         // enrichment arrives
    t.operatorName = c == 1 ? std::string(45, 'O').c_str() : Pick("Op ", c, 15, f).c_str();           // c 1: a label past the buffer
    t.registration = (f / 17) % 2 ? Pick("N", c, 17, f).c_str() : "";
    t.routeOrigin = (f / 6) % 3 == 2 ? "" : Pick("O", c, 6, f).c_str();
    t.routeDest = (f / 8) % 4 == 3 ? t.routeOrigin : Pick("D", c, 8, f).c_str();                      // a Local flight
    t.state.velocity = 100.0f + c + 0.37f * f;
    t.state.baroAltitude = 1000.0f + 30.0f * c + 2.1f * f;
    t.state.geoAltitude = 1050.0f + 30.0f * c + 1.7f * f;
    t.state.verticalRate = -6.0f + 0.13f * ((f + c) % 90);
    t.state.trueTrack = (float)((c * 37 + f * 3) % 360);
    static const int cats[] = {0, 1, 2, 4, 6, 8, 9, 13, 14, 20};
    t.state.category = cats[(c + f / 10) % 10];
    t.state.positionSource = (c + f / 12) % 5;   // 4 formats to ""
}

std::vector<std::vector<bool>> Masks()
{
    const int n = (int)AIRCRAFT_INFO_FIELD_COUNT;
    std::vector<std::vector<bool>> m;
    for (int i = 0; i < n; ++i) {   // each field alone
        std::vector<bool> v((size_t)n, false);
        v[(size_t)i] = true;
        m.push_back(v);
    }
    std::vector<bool> def((size_t)n, false), all((size_t)n, true);
    for (int i = 0; i < n; ++i) def[(size_t)i] = AIRCRAFT_INFO_FIELDS[i].defaultOn;
    m.push_back(def);
    m.push_back(all);
    for (int i = 0; i + 1 < n; i += 5) {   // a few pairs
        std::vector<bool> v((size_t)n, false);
        v[(size_t)i] = v[(size_t)i + 1] = true;
        m.push_back(v);
    }
    return m;
}

constexpr int kFrames = 400;
constexpr int kMaskFrames = 20;   // every input's period is shorter, so each changes while its field is shown alone

long gShareMiss = 0, gShareChecks = 0;

void Seq(int nc)
{
    const auto masks = Masks();
    std::vector<TrackedAircraft> ts((size_t)nc);
    std::vector<labelcache::Slot> slots((size_t)nc);
    std::vector<int> px((size_t)nc), py((size_t)nc), vx((size_t)nc), vy((size_t)nc);
    std::vector<long> changes((size_t)nc, 0);
    for (int c = 0; c < nc; ++c) {   // spread over the disc, the rim included, each drifting
        px[(size_t)c] = 12 + (c * 53) % 216;
        py[(size_t)c] = 12 + (c * 97) % 216;
        vx[(size_t)c] = (c % 5) - 2;
        vy[(size_t)c] = ((c / 5) % 5) - 2;
        if (!vx[(size_t)c] && !vy[(size_t)c]) vx[(size_t)c] = 1;
    }
    labelcache::Stats st;
    long mismatches = 0, frames = 0, moves = 0;
    for (int f = 0; f < kFrames; ++f) {
        const std::vector<bool>& on = masks[(size_t)(f / kMaskFrames) % masks.size()];
        for (int c = 0; c < nc; ++c) {
            TrackedAircraft& t = ts[(size_t)c];
            const auto before = Texts(t);
            Set(t, c, f);
            if (f && Texts(t) != before) ++changes[(size_t)c];
            int& x = px[(size_t)c];
            int& y = py[(size_t)c];
            x += vx[(size_t)c];
            y += vy[(size_t)c];
            if (x < 4 || x > 235) { vx[(size_t)c] = -vx[(size_t)c]; x += 2 * vx[(size_t)c]; }
            if (y < 4 || y > 235) { vy[(size_t)c] = -vy[(size_t)c]; y += 2 * vy[(size_t)c]; }
            ++moves;
            const Fields fl{t, on};
            bool boxed = false, drew = false;
            Cached(fl, slots[(size_t)c], x, y, st, &boxed);   // the box (the first pass)
            const long hitsMid = st.hits;
            const Lay got = Cached(fl, slots[(size_t)c], x, y, st, &drew);   // the draw
            if (boxed) {
                ++gShareChecks;
                if (st.hits != hitsMid + 1) ++gShareMiss;
            }
            if (!Same(got, Today(fl, x, y))) ++mismatches;
            ++frames;
        }
    }
    long minChanges = changes.empty() ? 0 : changes[0];
    for (long v : changes) minChanges = v < minChanges ? v : minChanges;
    const bool control = minChanges >= 1 && st.refills > 0 && st.uncached > 0;   // hits are P6-SHARE's
    char what[512];
    std::snprintf(what, sizeof what,
                  "%ld mismatches over %ld contact-frames (want 0); hits %ld, refills %ld, uncached %ld; fewest text "
                  "changes of a contact %ld (control: >= 1, refills and uncached both above 0: %s)",
                  mismatches, frames, st.hits, st.refills, st.uncached, minChanges, control ? "yes" : "NO");
    Row(mismatches == 0 && control, "P6-SEQ@" + std::to_string(nc), what);
}

}  // namespace

int main()
{
    {
        Lay a, b;
        a.n = b.n = 1;
        a.idx[0] = b.idx[0] = 0;
        a.x[0] = 10;
        b.x[0] = 11;
        a.y[0] = b.y[0] = 20;
        a.w[0] = b.w[0] = 30;
        a.text[0] = b.text[0] = "X";
        Row(!Same(a, b), "CMP control", std::string("one changed x ") + (Same(a, b) ? "reads the same" : "reads a mismatch"));
    }
    std::printf("the cache entry: %u bytes (PSRAM on the device), the key buffer %d, a line %d\n",
                (unsigned)sizeof(labelcache::Entry), labelcache::kKeyBytes, labelcache::kTextBytes);
    Cover();
    for (int n : {20, 40, 60}) Seq(n);
    Row(gShareMiss == 0 && gShareChecks > 0, "P6-SHARE",
        std::to_string(gShareChecks) + " frames whose box refreshed; the draw missed on " + std::to_string(gShareMiss) +
            " (want 0)");
    std::printf("%s\n", failures ? "P6 LABEL TESTS FAILED" : "all P6 label checks passed");
    return failures ? 1 : 0;
}
