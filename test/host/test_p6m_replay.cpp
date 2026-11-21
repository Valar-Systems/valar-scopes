// P6-MOVE's host rows (REG-p6m.md 798ccca51afe, section 2's "red first, before the build"): include/DiagP6m.h's
// recorder, decoder and replay, with the REAL formatters and the REAL LabelKey (src/AircraftInfoFields.cpp compiled
// against test/host/p6_fake/TrackedAircraft.h, staged as test_p6_labels is) and the REAL bench table
// (include/FrameBenchTable.h).
//
//   test_p6m_replay      exit 0 every row ok, 1 a row failed, 2 the rig
//
// Rows (each prints "ok" or "FAIL" and its id):
//   (i)    RT        LabelKey -> Decode -> LabelKey round-trips byte for byte: the table's 40 contacts at three times,
//                    plus one contact with every string field set
//   (ii)   CLEAN     an OLD arm and a NEW arm recorded as AircraftLabelBox gets them (A-1), replayed: 0 mismatches;
//                    frames == the frames fed; recmin == recmax == 40; 0 undecodable; no overflow
//   (iii)  PLANT     NEW's record replayed with the plant at F = floor(frames / 2): applied (lx_after == lx_before + 1),
//                    exactly 1 mismatch, first == F
//   (iv)   SABOTAGE  OLD's record replayed with Decode<true> (the callsign parsed, not assigned): mismatches > 0
//   (v)    OVERFLOW  a recorder too small for the frames fed: overflow set, recording stopped, never a silent cut
//                    (records < fed, and the flag is the only way to tell)
//   (vi)   KEYFULL   a key table too small for the keys fed: overflow set the same way
//   (vii)  PLANT-SKIP  a frame whose first record has n = 0 (a contact below the disc): the plant lands on the NEXT
//                    record with n >= 1 and names it (REG-p6m A-4)
//   (viii) PLANT-NONE  a frame with no record of n >= 1: no plant (the reader's INSTRUMENT)
//
// THE RED RUN: built with -DP6M_RED_BREAK=1, every graded decode is Decode<true> (the callsign parsed, not assigned),
// so (i), (ii) and (iii) must FAIL. That run is shown before the clean one.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "AircraftInfoFields.h"
#include "DiscGeometry.h"
#include "FrameBenchTable.h"
#include "LabelCache.h"

#include "DiagP6m.h"

// THE RED RUN: -DP6M_RED_BREAK=1 makes every graded decode the sabotaged instantiation (the callsign parsed, not
// assigned), so (i), (ii) and (iii) must FAIL; (iv) reads > 0 in both runs. Never set in run.sh.
#if defined(P6M_RED_BREAK) && P6M_RED_BREAK
constexpr bool kRed = true;
#else
constexpr bool kRed = false;
#endif

namespace {

int failures = 0;
void Row(bool ok, const std::string& id, const std::string& what)
{
    if (!ok) ++failures;
    std::printf("  %s  %s  %s\n", ok ? "ok  " : "FAIL", id.c_str(), what.c_str());
}

struct Fields {   // the host's adapter, as test_p6_labels.cpp's
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

std::vector<bool> Enabled()
{
    std::vector<bool> on;
    for (size_t i = 0; i < AIRCRAFT_INFO_FIELD_COUNT; ++i)
        on.push_back(AIRCRAFT_INFO_FIELDS[i].defaultOn || std::string(AIRCRAFT_INFO_FIELDS[i].key) == "info-heading");
    return on;
}

const double kLat0 = 44.06, kLon0 = -121.31;   // FrameBench.h's kHomeLat / kHomeLon (the bench's own, in the repo)

TrackedAircraft FromRow(const fbtable::Row& r)
{
    TrackedAircraft t;
    t.state.icao24 = r.hex;
    t.state.callsign = r.flight;
    t.state.originCountry = "United States";
    t.state.baroAltitude = (float)r.altFt;
    t.state.geoAltitude = (float)r.altFt + 40.0f;
    t.state.velocity = (float)r.gs;
    t.state.trueTrack = (float)r.track;
    t.state.verticalRate = 0.0f;
    t.state.category = fbtable::kCategoryA3;
    t.state.positionSource = 0;
    return t;
}

void ScreenXY(const fbtable::Row& r, int& x, int& y)   // a plain projection: 8 mi to ~110 px
{
    const double dn = (r.lat - kLat0) * fbtable::kKmPerDegLat;
    const double de = (r.lon - kLon0) * fbtable::kKmPerDegLat * std::cos(kLat0 * fbtable::kPi / 180.0);
    const double s = 110.0 / (8.0 * fbtable::kMiKm);
    x = (int)std::lround(120.0 + de * s);
    y = (int)std::lround(120.0 - dn * s);
}

std::vector<uint8_t> KeyBytes(const TrackedAircraft& t)
{
    labelcache::KeyBuf k;
    LabelKey(t, k);
    return std::vector<uint8_t>(k.b, k.b + k.len);
}

// One arm, as AircraftLabelBox sees it (A-1): per frame, per contact, the box call's geometry exactly as returned.
// cache = false: OLD (LayoutToday); true: NEW (Refresh, then PlaceCached, else LayoutToday).
long FeedArm(diagp6m::Recorder& rec, bool cache, int frames, uint32_t frame0, double t0)
{
    const std::vector<bool> on = Enabled();
    std::vector<labelcache::Slot> slots(diagp6m::kContacts);
    long fed = 0;
    for (int fr = 0; fr < frames; ++fr) {
        const double t = t0 + fr * 0.6;   // the contacts orbit between frames
        for (int i = 0; i < diagp6m::kContacts; ++i) {
            const fbtable::Row row = fbtable::At(kLat0, kLon0, i, t);
            const TrackedAircraft ta = FromRow(row);
            const Fields f{ta, on};
            int x, y;
            ScreenXY(row, x, y);
            int idx[diagp6m::kLines], lx[diagp6m::kLines], ly[diagp6m::kLines], lw[diagp6m::kLines];
            int n = 0;
            labelcache::Entry* e = cache ? slots[(size_t)i].Get() : nullptr;
            if (e && labelcache::Refresh(f, *e)) {
                int s[diagp6m::kLines];
                n = labelcache::PlaceCached(f, *e, x, y, idx, lx, ly, lw, s);
            } else {
                n = labelcache::LayoutToday(f, x, y, idx, lx, ly, lw);
            }
            rec.Add(frame0 + (uint32_t)fr, row.hex, [&](labelcache::KeyBuf& k) { LabelKey(ta, k); }, x, y, n, idx, lx, ly,
                    lw);
            ++fed;
        }
    }
    return fed;
}

// One OLD box call for contact i at time t, at an explicit screen position (y = 400 is below the disc: n = 0).
void AddAt(diagp6m::Recorder& rec, uint32_t frame, int i, double t, int x, int y)
{
    const std::vector<bool> on = Enabled();
    const fbtable::Row row = fbtable::At(kLat0, kLon0, i, t);
    const TrackedAircraft ta = FromRow(row);
    const Fields f{ta, on};
    int idx[diagp6m::kLines], lx[diagp6m::kLines], ly[diagp6m::kLines], lw[diagp6m::kLines];
    const int n = labelcache::LayoutToday(f, x, y, idx, lx, ly, lw);
    rec.Add(frame, row.hex, [&](labelcache::KeyBuf& k) { LabelKey(ta, k); }, x, y, n, idx, lx, ly, lw);
}

template <bool kSab = false>
diagp6m::ReplayResult ReplayOld(const diagp6m::Recorder& rec, int32_t plant = -1)
{
    const std::vector<bool> on = Enabled();
    TrackedAircraft scratch;
    auto layout = [&](const TrackedAircraft& t, int x, int y, diagp6m::Geometry& g) {
        const Fields f{t, on};
        g.n = labelcache::LayoutToday(f, x, y, g.idx, g.x, g.y, g.w);
    };
    return diagp6m::Replay<kSab>(rec, scratch, layout, [] {}, plant);
}

std::string Num(long v) { return std::to_string(v); }

}  // namespace

int main()
{
    std::printf("== P6-MOVE: the box-call recorder, decoder and replay (include/DiagP6m.h)%s ==\n",
#if defined(P6M_RED_BREAK) && P6M_RED_BREAK
                "  [RED RUN: the decoder is broken on purpose]"
#else
                ""
#endif
    );
    std::vector<uint8_t> buf(diagp6m::kBufferBytes);
    if (buf.size() != 4542080u) { std::printf("RIG: the buffer is not REG-p6m's 4,542,080 B\n"); return 2; }

    // (i) RT
    {
        long n = 0, bad = 0;
        const double times[3] = {(double)1794441386, (double)1794441386 + 37.0, (double)1794441386 + 600.0};
        for (double t : times)
            for (int i = 0; i < diagp6m::kContacts; ++i) {
                const TrackedAircraft a = FromRow(fbtable::At(kLat0, kLon0, i, t));
                const std::vector<uint8_t> k1 = KeyBytes(a);
                TrackedAircraft b;
                const bool ok = diagp6m::Decode<kRed>(k1.data(), (uint16_t)k1.size(), b);
                const std::vector<uint8_t> k2 = KeyBytes(b);
                ++n;
                if (!ok || k1 != k2) ++bad;
            }
        TrackedAircraft full = FromRow(fbtable::At(kLat0, kLon0, 7, (double)1794441386));
        full.typeCode = "B738";
        full.operatorName = "Alaska Airlines";
        full.registration = "N123AS";
        full.routeOrigin = "SEA";
        full.routeDest = "SFO";
        full.state.squawk = "4521";
        full.state.verticalRate = -4.5f;
        full.state.positionSource = 2;
        const std::vector<uint8_t> k1 = KeyBytes(full);
        TrackedAircraft b;
        const bool ok = diagp6m::Decode<kRed>(k1.data(), (uint16_t)k1.size(), b);
        ++n;
        if (!ok || KeyBytes(b) != k1) ++bad;
        Row(bad == 0 && n == 121, "(i) RT", "round trips " + Num(n - bad) + " of " + Num(n) + " (want 121 of 121)");
    }

    // (ii) CLEAN, then (iii) PLANT on NEW's record, (iv) SABOTAGE on OLD's
    const int kFrames = 50;
    diagp6m::Recorder oldRec, newRec;
    std::vector<uint8_t> buf2(diagp6m::kBufferBytes);
    oldRec.Init(buf.data());
    newRec.Init(buf2.data());
    const long fedOld = FeedArm(oldRec, false, kFrames, 1000, (double)1794441386);
    const long fedNew = FeedArm(newRec, true, kFrames, 5000, (double)1794441386);
    {
        const diagp6m::ReplayResult o = ReplayOld<kRed>(oldRec), w = ReplayOld<kRed>(newRec);
        auto clean = [&](const diagp6m::ReplayResult& r, const diagp6m::Recorder& rc, long fed) {
            return r.mismatches == 0 && r.frames == (uint32_t)kFrames && r.records == (uint32_t)fed &&
                   r.recMin == (uint32_t)diagp6m::kContacts && r.recMax == (uint32_t)diagp6m::kContacts &&
                   r.undecodable == 0 && !rc.overflow && r.first == -1;
        };
        Row(clean(o, oldRec, fedOld), "(ii) CLEAN-OLD",
            "frames " + Num(o.frames) + " records " + Num(o.records) + " recmin " + Num(o.recMin) + " recmax " +
                Num(o.recMax) + " keys " + Num(oldRec.keys) + " undecodable " + Num(o.undecodable) + " mismatches " +
                Num(o.mismatches) + " (want 50, " + Num(fedOld) + ", 40, 40, -, 0, 0)");
        Row(clean(w, newRec, fedNew), "(ii) CLEAN-NEW",
            "frames " + Num(w.frames) + " records " + Num(w.records) + " recmin " + Num(w.recMin) + " recmax " +
                Num(w.recMax) + " keys " + Num(newRec.keys) + " undecodable " + Num(w.undecodable) + " mismatches " +
                Num(w.mismatches) + " (want 50, " + Num(fedNew) + ", 40, 40, -, 0, 0)");
        const int32_t F = (int32_t)(w.frames / 2);
        const diagp6m::ReplayResult p = ReplayOld<kRed>(newRec, F);
        Row(p.planted && p.lxAfter == p.lxBefore + 1 && p.mismatches == 1 && p.first == F, "(iii) PLANT",
            "planted " + Num(p.planted) + " frame " + Num(p.plantFrame) + " record " + Num(p.plantRecord) + " lx " +
                Num(p.lxBefore) + " -> " + Num(p.lxAfter) + " mismatches " + Num(p.mismatches) + " first " + Num(p.first) +
                " (want 1, " + Num(F) + ", the first with n >= 1, +1, 1, " + Num(F) + ")");
        const diagp6m::ReplayResult s = ReplayOld<true>(oldRec);
        Row(s.mismatches > 0, "(iv) SABOTAGE",
            "Decode<true> on OLD's record: mismatches " + Num(s.mismatches) + " (want > 0)");
    }

    // (vii) PLANT-SKIP and (viii) PLANT-NONE (REG-p6m A-4)
    {
        std::vector<uint8_t> b3(diagp6m::kBufferBytes);
        diagp6m::Recorder r;
        r.Init(b3.data());
        const double t = (double)1794441386;
        for (uint32_t fr = 0; fr < 3; ++fr) {
            AddAt(r, 7000 + fr, 0, t, 120, 400);   // record 0: below the disc, n = 0
            AddAt(r, 7000 + fr, 1, t, 100, 100);   // record 1: inside, n >= 1
            AddAt(r, 7000 + fr, 2, t, 140, 120);
        }
        const diagp6m::Record* r0 = reinterpret_cast<const diagp6m::Record*>(r.base + sizeof(diagp6m::FrameHead));
        const bool shaped = r0->n == 0 && (r0 + 1)->n >= 1;
        const diagp6m::ReplayResult k = ReplayOld<kRed>(r, 1);
        Row(shaped && k.planted && k.plantFrame == 1 && k.plantRecord == 1 && k.lxAfter == k.lxBefore + 1 &&
                k.mismatches == 1 && k.first == 1,
            "(vii) PLANT-SKIP",
            "record 0 n " + Num(r0->n) + ", record 1 n " + Num((r0 + 1)->n) + "; planted " + Num(k.planted) + " frame " +
                Num(k.plantFrame) + " record " + Num(k.plantRecord) + " mismatches " + Num(k.mismatches) + " first " +
                Num(k.first) + " (want 0, >= 1; 1, 1, 1, 1, 1)");
        std::vector<uint8_t> b4(diagp6m::kBufferBytes);
        diagp6m::Recorder z;
        z.Init(b4.data());
        for (uint32_t fr = 0; fr < 3; ++fr) {
            if (fr == 1) {
                AddAt(z, 7100 + fr, 0, t, 120, 400);   // frame 1: every record below the disc
                AddAt(z, 7100 + fr, 1, t, 120, 420);
            } else {
                AddAt(z, 7100 + fr, 1, t, 100, 100);
            }
        }
        const diagp6m::ReplayResult q = ReplayOld<kRed>(z, 1);
        Row(!q.planted && q.plantRecord == -1 && q.mismatches == 0, "(viii) PLANT-NONE",
            "planted " + Num(q.planted) + " record " + Num(q.plantRecord) + " mismatches " + Num(q.mismatches) +
                " (want 0, -1, 0: the reader's INSTRUMENT)");
    }

    // (v) OVERFLOW: room for 10 frames, 20 fed
    {
        const size_t recBytes = 10 * diagp6m::kFrameBytes;
        std::vector<uint8_t> small(recBytes + (size_t)diagp6m::kKeysMax * sizeof(diagp6m::Key) + diagp6m::kLastBytes);
        diagp6m::Recorder r;
        r.Init(small.data(), recBytes, diagp6m::kKeysMax);
        const long fed = FeedArm(r, true, 20, 9000, (double)1794441386);
        Row(r.overflow && (long)r.records < fed && r.frames <= 11 && r.used <= recBytes, "(v) OVERFLOW",
            "overflow " + Num(r.overflow) + " records " + Num(r.records) + " of " + Num(fed) + " frames " +
                Num(r.frames) + " used " + Num((long)r.used) + " of " + Num((long)recBytes) + " (want 1, fewer, <= 11, <=)");
    }
    // (vi) KEYFULL: room for 30 keys
    {
        std::vector<uint8_t> small(diagp6m::kRecBytes + 30 * sizeof(diagp6m::Key) + diagp6m::kLastBytes);
        diagp6m::Recorder r;
        r.Init(small.data(), diagp6m::kRecBytes, 30);
        const long fed = FeedArm(r, false, 5, 9500, (double)1794441386);
        Row(r.overflow && r.keys == 30 && (long)r.records < fed, "(vi) KEYFULL",
            "overflow " + Num(r.overflow) + " keys " + Num(r.keys) + " records " + Num(r.records) + " of " + Num(fed) +
                " (want 1, 30, fewer)");
    }

    std::printf("== %s: %d row(s) failed ==\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
