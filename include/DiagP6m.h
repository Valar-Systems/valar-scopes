#pragma once
// P6-MOVE (weatherscope; REG-p6m.md 798ccca51afe, accepted 2026-11-21): the BOX-CALL recorder and its replay, for B4 on
// G6's moving workload. Bench-only: AircraftManager includes it inside its FRAME_PROFILE region, and only the
// blipscope-s3-128-diag-p6m env (FRAME_PROFILE_GAINS == 9) turns it on (DIAG_P6M_ON).
//
// Header-only and free of Arduino and LovyanGFX, like LabelCache.h, so test/host/test_p6m_replay.cpp compiles exactly
// this. It holds:
//   - Record / FrameHead / Key: the bytes REG-p6m section 2 fixes (28 B a record, 6 B a frame, 194 B a key);
//   - Recorder: one call per BOX call (A-1: at AircraftLabelBox's caller site, after LabelLayout returns), the
//     geometry exactly as returned; keys deduped per contact; it STOPS and flags overflow, never truncates (G2);
//   - Decode<kSabotage>: LabelKey's exact inverse (src/AircraftInfoFields.cpp :137-153), into a scratch aircraft.
//     kSabotage = true parses every field but does not assign the callsign: the host test's row (iv) only;
//   - Replay: per recorded frame, per record, decode, re-run OLD's layout (the caller's functor) and compare; counted
//     coverage (G1); an optional PLANT (G3, as REG-p6m A-4 words it) on a copy: frame F's FIRST record with n >= 1.
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "LabelCache.h"

namespace diagp6m {

constexpr int kLines = labelcache::kLines;   // 3 (labellines::MAX_LINES)
constexpr int kContacts = 40;                // the bench level every timed arm draws (I-4)
constexpr int kFramesMax = 3000;             // F_max: 120 s at a 40 ms mean frame (REG section 2)
constexpr int kKeysMax = 6000;               // 40 contacts x 121 polls at one a second, x 1.24
constexpr uint16_t kNoKey = 0xFFFF;

#pragma pack(push, 1)
struct Record {   // one box call, as returned to AircraftLabelBox
    int16_t x, y;
    uint16_t key;   // index into the key table; kNoKey = undecodable (the key overflowed its buffer)
    int8_t n;
    uint8_t idx[kLines];
    int16_t lx[kLines], ly[kLines], lw[kLines];
};
struct FrameHead {
    uint32_t frame;   // the bench's frame counter at the frame's first box call
    uint16_t count;   // the box calls recorded in that frame
};
struct Key {
    uint16_t len;
    uint8_t b[labelcache::kKeyBytes];
};
#pragma pack(pop)
static_assert(sizeof(Record) == 28, "REG-p6m section 2: 28 B a record");
static_assert(sizeof(FrameHead) == 6, "REG-p6m section 2: 6 B a frame header");
static_assert(sizeof(Key) == 194, "REG-p6m section 2: 192 + 2 B a key");

constexpr size_t kFrameBytes = sizeof(FrameHead) + (size_t)kContacts * sizeof(Record);   // 1,126
constexpr size_t kRecBytes = (size_t)kFramesMax * kFrameBytes;                            // 3,378,000
constexpr size_t kKeyTableBytes = (size_t)kKeysMax * sizeof(Key);                         // 1,164,000
constexpr size_t kLastBytes = (size_t)kContacts * sizeof(uint16_t);                       // 80
constexpr size_t kBufferBytes = kRecBytes + kKeyTableBytes + kLastBytes;                  // 4,542,080
static_assert(kBufferBytes == 4542080, "REG-p6m section 2's boot line: bytes=4542080");

// The bench contact's index from its hex ("fb%04x", FrameBenchTable.h), for the per-contact dedupe; -1 otherwise (an
// unknown contact still records, its key just is never reused).
inline int ContactIndex(const char* hex)
{
    if (!hex || hex[0] != 'f' || hex[1] != 'b' || std::strlen(hex) != 6) return -1;
    char* end = nullptr;
    const long v = std::strtol(hex + 2, &end, 16);
    return (end && *end == '\0' && v >= 0 && v < kContacts) ? (int)v : -1;
}

struct Recorder {
    uint8_t* base = nullptr;   // kRecBytes of records, then the key table, then the last-key indices
    size_t recCap = 0, keyCap = 0;
    size_t used = 0;           // bytes of the record area in use
    uint32_t keys = 0;
    uint32_t frames = 0, records = 0;
    bool overflow = false;
    FrameHead* cur = nullptr;

    Key* KeyAt(uint32_t i) const { return reinterpret_cast<Key*>(base + recCap) + i; }
    uint16_t* Last() const { return reinterpret_cast<uint16_t*>(base + recCap + keyCap * sizeof(Key)); }

    // buf holds recBytes + keyCount * sizeof(Key) + kLastBytes. The device passes the boot buffer and the defaults;
    // the host test passes small capacities to drive the overflow row.
    void Init(uint8_t* buf, size_t recBytes = kRecBytes, size_t keyCount = kKeysMax)
    {
        base = buf;
        recCap = recBytes;
        keyCap = keyCount;
        Reset();
    }
    void Reset()   // at each scene's measure start
    {
        used = 0;
        keys = frames = records = 0;
        overflow = false;
        cur = nullptr;
        if (base)
            for (int i = 0; i < kContacts; ++i) Last()[i] = kNoKey;
    }

    // One BOX call: the key the formatters read, and the geometry exactly as LabelLayout returned it.
    // makeKey(KeyBuf&) is LabelKey(tracked, k). Once overflow is set, nothing more is recorded for this scene.
    template <class MakeKey>
    void Add(uint32_t frame, const char* hex, MakeKey makeKey, int x, int y, int n, const int* idx, const int* lx,
             const int* ly, const int* lw)
    {
        if (!base || overflow) return;
        if (!cur || cur->frame != frame) {
            if (used + sizeof(FrameHead) > recCap) { overflow = true; return; }
            cur = reinterpret_cast<FrameHead*>(base + used);
            cur->frame = frame;
            cur->count = 0;
            used += sizeof(FrameHead);
            ++frames;
        }
        if (used + sizeof(Record) > recCap) { overflow = true; return; }
        labelcache::KeyBuf k;
        makeKey(k);
        uint16_t ki = kNoKey;
        if (!k.overflow) {
            const int ci = ContactIndex(hex);
            const uint16_t last = ci >= 0 ? Last()[ci] : kNoKey;
            if (last != kNoKey && KeyAt(last)->len == k.len && std::memcmp(KeyAt(last)->b, k.b, k.len) == 0) {
                ki = last;
            } else {
                if (keys >= keyCap) { overflow = true; return; }
                Key* d = KeyAt(keys);
                d->len = k.len;
                std::memcpy(d->b, k.b, k.len);
                ki = (uint16_t)keys++;
                if (ci >= 0) Last()[ci] = ki;
            }
        }
        Record* r = reinterpret_cast<Record*>(base + used);
        r->x = (int16_t)x;
        r->y = (int16_t)y;
        r->key = ki;
        r->n = (int8_t)n;
        for (int i = 0; i < kLines; ++i) {
            const bool on = i < n;
            r->idx[i] = on ? (uint8_t)idx[i] : 0;
            r->lx[i] = on ? (int16_t)lx[i] : 0;
            r->ly[i] = on ? (int16_t)ly[i] : 0;
            r->lw[i] = on ? (int16_t)lw[i] : 0;
        }
        used += sizeof(Record);
        ++cur->count;
        ++records;
    }
};

// LabelKey's inverse, field for field in its order (src/AircraftInfoFields.cpp :137-153): eight strings, five floats, a
// string, two int32s. True only if the whole key is consumed exactly. kSabotage: the callsign is parsed and NOT
// assigned (row (iv) of the host test; never instantiated on the device).
template <bool kSabotage = false, class T>
inline bool Decode(const uint8_t* b, uint16_t len, T& t)
{
    size_t p = 0;
    char tmp[labelcache::kKeyBytes + 1];
    auto str = [&](const char*& out) -> bool {
        uint16_t n = 0;
        if (p + sizeof n > len) return false;
        std::memcpy(&n, b + p, sizeof n);
        p += sizeof n;
        if (p + n > len || n > labelcache::kKeyBytes) return false;
        std::memcpy(tmp, b + p, n);
        tmp[n] = '\0';
        p += n;
        out = tmp;
        return true;
    };
    auto f32 = [&](float& out) -> bool {
        if (p + sizeof out > len) return false;
        std::memcpy(&out, b + p, sizeof out);
        p += sizeof out;
        return true;
    };
    auto i32 = [&](int32_t& out) -> bool {
        if (p + sizeof out > len) return false;
        std::memcpy(&out, b + p, sizeof out);
        p += sizeof out;
        return true;
    };
    const char* s = nullptr;
    if (!str(s)) return false;
    if (!kSabotage) t.state.callsign = s;
    if (!str(s)) return false;
    t.typeCode = s;
    if (!str(s)) return false;
    t.operatorName = s;
    if (!str(s)) return false;
    t.registration = s;
    if (!str(s)) return false;
    t.routeOrigin = s;
    if (!str(s)) return false;
    t.routeDest = s;
    if (!str(s)) return false;
    t.state.icao24 = s;
    if (!str(s)) return false;
    t.state.originCountry = s;
    float v = 0.0f;
    if (!f32(v)) return false;
    t.state.velocity = v;
    if (!f32(v)) return false;
    t.state.verticalRate = v;
    if (!f32(v)) return false;
    t.state.baroAltitude = v;
    if (!f32(v)) return false;
    t.state.geoAltitude = v;
    if (!f32(v)) return false;
    t.state.trueTrack = v;
    if (!str(s)) return false;
    t.state.squawk = s;
    int32_t c = 0;
    if (!i32(c)) return false;
    t.state.category = static_cast<decltype(t.state.category)>(c);
    if (!i32(c)) return false;
    t.state.positionSource = static_cast<decltype(t.state.positionSource)>(c);
    return p == len;
}

struct Geometry {
    int n = 0;
    int idx[kLines], x[kLines], y[kLines], w[kLines];
};

inline bool SameAs(const Record& r, const Geometry& g)
{
    if (r.n != g.n) return false;
    for (int k = 0; k < g.n; ++k)
        if (r.idx[k] != g.idx[k] || r.lx[k] != g.x[k] || r.ly[k] != g.y[k] || r.lw[k] != g.w[k]) return false;
    return true;
}

struct ReplayResult {
    uint32_t frames = 0, records = 0;
    uint32_t recMin = 0, recMax = 0;
    uint32_t undecodable = 0;   // a record whose key is kNoKey or fails to decode: coverage is NOT complete (G1)
    uint32_t mismatches = 0;
    int32_t first = -1;         // the ORDINAL of the first mismatching frame in this scene's record, or -1
    // the plant (G3 / A-4), when asked for: frame plantFrame's FIRST record with n >= 1, line 0, lx + 1, on a copy.
    // planted stays false if that frame has no record with n >= 1: the reader's INSTRUMENT.
    bool planted = false;
    int32_t plantFrame = -1;
    int32_t plantRecord = -1;   // the record's ordinal within frame F
    int lxBefore = 0, lxAfter = 0;
};

// Replays the recorder's scene. layout(scratch, x, y, Geometry&) runs OLD's LayoutToday on the decoded aircraft with
// the caller's adapter; yield() is called every 25 frames (the device's vTaskDelay(1)). plantFrame < 0: no plant.
template <bool kSabotage = false, class T, class LayoutFn, class YieldFn>
inline ReplayResult Replay(const Recorder& rec, T& scratch, LayoutFn layout, YieldFn yield, int32_t plantFrame = -1)
{
    ReplayResult o;
    size_t p = 0;
    uint32_t ord = 0;
    while (p + sizeof(FrameHead) <= rec.used) {
        const FrameHead* h = reinterpret_cast<const FrameHead*>(rec.base + p);
        p += sizeof(FrameHead);
        if (o.frames == 0 || h->count < o.recMin) o.recMin = h->count;
        if (h->count > o.recMax) o.recMax = h->count;
        bool frameMiss = false;
        for (uint16_t j = 0; j < h->count; ++j) {
            if (p + sizeof(Record) > rec.used) break;
            Record r;
            std::memcpy(&r, rec.base + p, sizeof r);
            p += sizeof(Record);
            ++o.records;
            if (plantFrame >= 0 && (int32_t)ord == plantFrame && !o.planted && r.n >= 1) {
                o.planted = true;
                o.plantFrame = plantFrame;
                o.plantRecord = (int32_t)j;
                o.lxBefore = r.lx[0];
                r.lx[0] = (int16_t)(r.lx[0] + 1);
                o.lxAfter = r.lx[0];
            }
            if (r.key == kNoKey || r.key >= rec.keys) { ++o.undecodable; continue; }
            const Key* k = rec.KeyAt(r.key);
            if (!Decode<kSabotage>(k->b, k->len, scratch)) { ++o.undecodable; continue; }
            Geometry g;
            layout(scratch, (int)r.x, (int)r.y, g);
            if (!SameAs(r, g)) {
                ++o.mismatches;
                frameMiss = true;
            }
        }
        if (frameMiss && o.first < 0) o.first = (int32_t)ord;
        ++o.frames;
        ++ord;
        if (o.frames % 25 == 0) yield();
    }
    return o;
}

}  // namespace diagp6m

// The device gate: only the P6-MOVE env (FRAME_PROFILE_GAINS == 9) compiles AircraftManager.cpp's recorder hooks. The
// host never defines FRAME_PROFILE, so the host test compiles the header alone.
#if defined(FRAME_PROFILE) && defined(FRAME_PROFILE_GAINS) && FRAME_PROFILE_GAINS == 9
#define DIAG_P6M_ON 1
#endif
