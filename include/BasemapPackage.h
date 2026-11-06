// The basemap package reader (BMPK v1, docs/basemap-package-format.md), product-neutral: Weatherscope reads it first
// and Blipscope adopts it without a move (Q23). Pure C++11, no allocation, no copy: Validate checks a package in place
// and records where its sections are; the walkers then read a VALIDATED package's items in place.
//
// THE ORDER OF THE REFUSALS IS PINNED (docs/weatherscope-stage3.md A10.11, with A12.15's two counts), so each negative
// package has exactly one reason:
//   1 at least 32 bytes (kHeader)          2 the magic (kMagic)        3 the version (kVersion)
//   4 the header flags (kFlags)            5 totalBytes = the size (kTotal)                6 the CRC (kCrc)
//   7 nSections 1-32, the directory inside the package (kSectionCount)
//   8 per entry, in order: flags/reserved zero (kReserved), then inside [directory end, total] (kSectionBounds)
//   9 contiguity in directory order, from the directory's end to totalBytes (kOverlap before, kGap after)
//  10 duplicates of types 1-5 or 7 (kDuplicate)      11 PROVENANCE present (kNoProvenance)
//  12 the bodies, in directory order, each failing at its FIRST error in reading order: kBody (ran out, or bytes
//     left over), kVarint (over 5 bytes, or overlong), kVertexRange (a feature's running total outside +-32,767,
//     checked once its vertices are read), kVertexCount, kText (past the section, or not UTF-8), kRiverName,
//     kRoadClass, kTerrainLength (shorter than 8, N = 0, or length != 8 + N^2), then TERRAIN's reserved (kReserved),
//     then its count (kTerrainCount, != 1), and PROVENANCE's count against its lines (kProvenanceCount: 0 lines for
//     an empty body, else the number of '\n' + 1).
// There is no partial result: a package that is half right is refused whole (format section 8).

#pragma once

#include <stddef.h>
#include <stdint.h>

namespace bmpk {

enum class Status : uint8_t {
    kOk = 0,
    kHeader,
    kMagic,
    kVersion,
    kFlags,
    kTotal,
    kCrc,
    kSectionCount,
    kReserved,
    kSectionBounds,
    kOverlap,
    kGap,
    kDuplicate,
    kNoProvenance,
    kVarint,
    kVertexRange,
    kVertexCount,
    kText,
    kRiverName,
    kRoadClass,
    kTerrainLength,
    kBody,
    kTerrainCount,
    kProvenanceCount,
};

inline const char* Name(Status s)
{
    static const char* const k[] = {"kOk",          "kHeader",       "kMagic",        "kVersion",    "kFlags",
                                    "kTotal",       "kCrc",          "kSectionCount", "kReserved",   "kSectionBounds",
                                    "kOverlap",     "kGap",          "kDuplicate",    "kNoProvenance", "kVarint",
                                    "kVertexRange", "kVertexCount",  "kText",         "kRiverName",  "kRoadClass",
                                    "kTerrainLength", "kBody",       "kTerrainCount", "kProvenanceCount"};
    const unsigned i = (unsigned)s;
    return i < sizeof k / sizeof k[0] ? k[i] : "?";
}

enum Type : uint8_t { kWater = 1, kRivers = 2, kRoads = 3, kPeaks = 4, kPlaces = 5, kTerrain = 6, kProvenance = 7 };

constexpr uint32_t kHeaderBytes = 32;
constexpr uint32_t kEntryBytes = 16;
constexpr int kMaxSections = 32;
constexpr int32_t kQMax = 32767;

struct Section {
    uint8_t type = 0;
    uint32_t count = 0, offset = 0, length = 0;
};

// A validated package: its header, where each section is, and the largest ring or line (so a caller can size its
// vertex scratch once). `data` points into the caller's bytes, which must outlive the Package.
struct Package {
    const uint8_t* data = nullptr;
    uint32_t size = 0;
    int32_t lat0Micro = 0, lon0Micro = 0;
    uint32_t radiusMm = 0, toleranceMm = 0, crc32 = 0;
    int nSections = 0;
    Section sections[kMaxSections];
    uint32_t maxVertices = 0;

    const Section* Find(uint8_t type) const
    {
        for (int i = 0; i < nSections; ++i)
            if (sections[i].type == type) return &sections[i];
        return nullptr;
    }
};

// One terrain level, read in place.
struct Level {
    uint32_t radiusMm = 0;
    int n = 0;
    const uint8_t* values = nullptr;   // n x n, row-major, row 0 north
};

namespace detail {

inline uint32_t Le16(const uint8_t* p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8); }
inline uint32_t Le32(const uint8_t* p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

// CRC-32 (IEEE 802.3, zlib's), over `len` bytes, with bytes [28, 32) read as zero (the field itself).
inline uint32_t Crc32ZeroingField(const uint8_t* p, uint32_t len)
{
    static const uint32_t kNib[16] = {0x00000000, 0x1DB71064, 0x3B6E20C8, 0x26D930AC, 0x76DC4190, 0x6B6B51F4,
                                      0x4DB26158, 0x5005713C, 0xEDB88320, 0xF00F9344, 0xD6D6A3E8, 0xCB61B38C,
                                      0x9B64C2B0, 0x86D3D2D4, 0xA00AE278, 0xBDBDF21C};
    uint32_t c = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < len; ++i) {
        const uint8_t b = (i >= 28 && i < 32) ? 0 : p[i];
        c ^= b;
        c = (c >> 4) ^ kNib[c & 15];
        c = (c >> 4) ^ kNib[c & 15];
    }
    return c ^ 0xFFFFFFFFu;
}

// A cursor over one section's body.
struct Cur {
    const uint8_t* b;
    uint32_t n, i;
    uint32_t Left() const { return n - i; }
};

inline Status U8(Cur& c, uint32_t* v)
{
    if (c.i >= c.n) return Status::kBody;
    *v = c.b[c.i++];
    return Status::kOk;
}

// Unsigned LEB128, at most 5 bytes; a redundant trailing zero group is overlong. Up to 35 bits, kept whole.
inline Status Varint(Cur& c, uint64_t* out)
{
    uint64_t v = 0;
    int shift = 0, n = 0;
    for (;;) {
        if (c.i >= c.n) return Status::kBody;   // ran out mid-item: the body does not parse to its length
        const uint8_t byte = c.b[c.i++];
        if (++n > 5) return Status::kVarint;
        v |= (uint64_t)(byte & 0x7F) << shift;
        shift += 7;
        if (!(byte & 0x80)) {
            if (n > 1 && byte == 0) return Status::kVarint;
            *out = v;
            return Status::kOk;
        }
    }
}

// Zigzag over the low 32 bits.
inline Status Svarint(Cur& c, int64_t* out)
{
    uint64_t u;
    const Status s = Varint(c, &u);
    if (s != Status::kOk) return s;
    const uint32_t z = (uint32_t)(u & 0xFFFFFFFFu);
    *out = (int64_t)(int32_t)((z >> 1) ^ (0u - (z & 1u)));
    return Status::kOk;
}

// Strict UTF-8: no overlong forms, no surrogates, nothing past U+10FFFF.
inline bool Utf8(const uint8_t* p, uint32_t n)
{
    uint32_t i = 0;
    while (i < n) {
        const uint8_t b = p[i];
        if (b < 0x80) {
            ++i;
            continue;
        }
        int more;
        uint8_t lo = 0x80, hi = 0xBF;
        if (b >= 0xC2 && b <= 0xDF) more = 1;
        else if (b == 0xE0) { more = 2; lo = 0xA0; }
        else if (b >= 0xE1 && b <= 0xEC) more = 2;
        else if (b == 0xED) { more = 2; hi = 0x9F; }
        else if (b >= 0xEE && b <= 0xEF) more = 2;
        else if (b == 0xF0) { more = 3; lo = 0x90; }
        else if (b >= 0xF1 && b <= 0xF3) more = 3;
        else if (b == 0xF4) { more = 3; hi = 0x8F; }
        else return false;
        if (n - i - 1 < (uint32_t)more) return false;
        if (p[i + 1] < lo || p[i + 1] > hi) return false;
        for (int k = 2; k <= more; ++k)
            if (p[i + k] < 0x80 || p[i + k] > 0xBF) return false;
        i += 1 + (uint32_t)more;
    }
    return true;
}

// A text: a u8 length, then that many bytes of UTF-8.
inline Status Text(Cur& c, const char** s, uint32_t* len)
{
    uint32_t n;
    const Status st = U8(c, &n);
    if (st != Status::kOk) return st;
    if (n > c.Left()) return Status::kText;
    if (!Utf8(c.b + c.i, n)) return Status::kText;
    *s = (const char*)(c.b + c.i);
    *len = n;
    c.i += n;
    return Status::kOk;
}

// One feature's n vertices: the first absolute, then n - 1 deltas. Read whole first, then the range is checked, so a
// read error always wins over a range error. When `qx`/`qy` are given (cap >= n), the vertices are written there.
inline Status Vertices(Cur& c, uint64_t n, int32_t* qx, int32_t* qy, uint64_t cap)
{
    int64_t x = 0, y = 0, d;
    bool out = false;
    for (uint64_t k = 0; k < n; ++k) {
        Status s = Svarint(c, &d);
        if (s != Status::kOk) return s;
        x = k ? x + d : d;
        s = Svarint(c, &d);
        if (s != Status::kOk) return s;
        y = k ? y + d : d;
        if (x < -kQMax || x > kQMax || y < -kQMax || y > kQMax) out = true;
        if (qx && k < cap) {
            qx[k] = (int32_t)x;
            qy[k] = (int32_t)y;
        }
    }
    return out ? Status::kVertexRange : Status::kOk;
}

inline Status Body(uint8_t type, uint32_t count, Cur& c, uint32_t* maxVertices)
{
    Status s = Status::kOk;
    uint64_t v;
    if (type == kWater) {
        for (uint32_t k = 0; k < count; ++k) {
            if ((s = Varint(c, &v)) != Status::kOk) return s;
            if (v < 3) return Status::kVertexCount;
            if ((s = Vertices(c, v, nullptr, nullptr, 0)) != Status::kOk) return s;
            if (v > *maxVertices) *maxVertices = (uint32_t)v;
        }
    } else if (type == kRivers) {
        uint64_t names;
        if ((s = Varint(c, &names)) != Status::kOk) return s;
        for (uint64_t k = 0; k < names; ++k) {
            const char* t;
            uint32_t len;
            if ((s = Text(c, &t, &len)) != Status::kOk) return s;
        }
        for (uint32_t k = 0; k < count; ++k) {
            if ((s = Varint(c, &v)) != Status::kOk) return s;
            if (v >= names) return Status::kRiverName;
            if ((s = Varint(c, &v)) != Status::kOk) return s;
            if (v < 2) return Status::kVertexCount;
            if ((s = Vertices(c, v, nullptr, nullptr, 0)) != Status::kOk) return s;
            if (v > *maxVertices) *maxVertices = (uint32_t)v;
        }
    } else if (type == kRoads) {
        for (uint32_t k = 0; k < count; ++k) {
            uint32_t cls;
            if ((s = U8(c, &cls)) != Status::kOk) return s;
            if (cls != 1 && cls != 2) return Status::kRoadClass;
            if ((s = Varint(c, &v)) != Status::kOk) return s;
            if (v < 2) return Status::kVertexCount;
            if ((s = Vertices(c, v, nullptr, nullptr, 0)) != Status::kOk) return s;
            if (v > *maxVertices) *maxVertices = (uint32_t)v;
        }
    } else if (type == kPeaks || type == kPlaces) {
        for (uint32_t k = 0; k < count; ++k) {
            int64_t a;
            if ((s = Svarint(c, &a)) != Status::kOk) return s;
            if ((s = Svarint(c, &a)) != Status::kOk) return s;
            if (type == kPeaks) {
                if ((s = Svarint(c, &a)) != Status::kOk) return s;
            } else {
                if ((s = Varint(c, &v)) != Status::kOk) return s;
            }
            const char* t;
            uint32_t len;
            if ((s = Text(c, &t, &len)) != Status::kOk) return s;
        }
    } else if (type == kTerrain) {
        if (c.n < 8) return Status::kTerrainLength;
        const uint32_t n = Le16(c.b + 4);
        if (n == 0 || (uint64_t)c.n != 8u + (uint64_t)n * n) return Status::kTerrainLength;
        if (Le16(c.b + 6) != 0) return Status::kReserved;
        if (count != 1) return Status::kTerrainCount;
        return Status::kOk;
    } else if (type == kProvenance) {
        uint32_t lines = 0;
        if (c.n) {
            lines = 1;
            for (uint32_t i = 0; i < c.n; ++i) lines += c.b[i] == '\n';
        }
        return count == lines ? Status::kOk : Status::kProvenanceCount;
    } else {
        return Status::kOk;   // an unknown type: skipped by its length
    }
    return c.Left() == 0 ? Status::kOk : Status::kBody;
}

}  // namespace detail

// Checks a whole package (format section 8) in the pinned order. On kOk, `out` (if given) describes it; on any refusal
// `out` is left empty, so nothing half-right can be drawn.
inline Status Validate(const uint8_t* p, size_t size, Package* out)
{
    using namespace detail;
    if (out) *out = Package();
    if (size < kHeaderBytes) return Status::kHeader;
    if (p[0] != 'B' || p[1] != 'M' || p[2] != 'P' || p[3] != 'K') return Status::kMagic;
    if (p[4] != 1) return Status::kVersion;
    const uint32_t nSec = p[5];
    if (Le16(p + 6) != 0) return Status::kFlags;
    const uint32_t total = Le32(p + 24);
    if ((uint64_t)total != (uint64_t)size) return Status::kTotal;
    if (Crc32ZeroingField(p, total) != Le32(p + 28)) return Status::kCrc;
    const uint64_t dirEnd = kHeaderBytes + (uint64_t)kEntryBytes * nSec;
    if (nSec < 1 || nSec > (uint32_t)kMaxSections || dirEnd > total) return Status::kSectionCount;

    Section sec[kMaxSections];
    for (uint32_t i = 0; i < nSec; ++i) {
        const uint8_t* e = p + kHeaderBytes + kEntryBytes * i;
        if (e[1] != 0 || Le16(e + 2) != 0) return Status::kReserved;
        sec[i].type = e[0];
        sec[i].count = Le32(e + 4);
        sec[i].offset = Le32(e + 8);
        sec[i].length = Le32(e + 12);
        if (sec[i].offset < dirEnd || (uint64_t)sec[i].offset + sec[i].length > total) return Status::kSectionBounds;
    }
    uint64_t expect = dirEnd;
    for (uint32_t i = 0; i < nSec; ++i) {
        if (sec[i].offset < expect) return Status::kOverlap;
        if (sec[i].offset > expect) return Status::kGap;
        expect = (uint64_t)sec[i].offset + sec[i].length;
    }
    if (expect != total) return Status::kGap;
    bool seen[8] = {false, false, false, false, false, false, false, false};
    for (uint32_t i = 0; i < nSec; ++i) {
        const uint8_t t = sec[i].type;
        if (t >= 1 && t <= 7 && t != kTerrain && seen[t]) return Status::kDuplicate;
        if (t >= 1 && t <= 7) seen[t] = true;
    }
    if (!seen[kProvenance]) return Status::kNoProvenance;
    uint32_t maxVertices = 0;
    for (uint32_t i = 0; i < nSec; ++i) {
        Cur c{p + sec[i].offset, sec[i].length, 0};
        const Status s = Body(sec[i].type, sec[i].count, c, &maxVertices);
        if (s != Status::kOk) return s;
    }
    if (out) {
        out->data = p;
        out->size = total;
        out->lat0Micro = (int32_t)Le32(p + 8);
        out->lon0Micro = (int32_t)Le32(p + 12);
        out->radiusMm = Le32(p + 16);
        out->toleranceMm = Le32(p + 20);
        out->crc32 = Le32(p + 28);
        out->nSections = (int)nSec;
        for (uint32_t i = 0; i < nSec; ++i) out->sections[i] = sec[i];
        out->maxVertices = maxVertices;
    }
    return Status::kOk;
}

// ------------------------------------------------------------------------------------ the walkers (validated only)

// The levels, in directory order; returns how many (at most cap are written).
inline int Levels(const Package& pk, Level* out, int cap)
{
    int n = 0;
    for (int i = 0; i < pk.nSections; ++i) {
        const Section& s = pk.sections[i];
        if (s.type != kTerrain) continue;
        if (n < cap) {
            const uint8_t* b = pk.data + s.offset;
            out[n].radiusMm = detail::Le32(b);
            out[n].n = (int)detail::Le16(b + 4);
            out[n].values = b + 8;
        }
        ++n;
    }
    return n;
}

// Each water ring's vertices, into qx/qy (cap >= pk.maxVertices), then f(n).
template <class F>
inline bool ForEachRing(const Package& pk, int32_t* qx, int32_t* qy, uint32_t cap, F f)
{
    const Section* s = pk.Find(kWater);
    if (!s) return true;
    if (cap < pk.maxVertices) return false;
    detail::Cur c{pk.data + s->offset, s->length, 0};
    for (uint32_t k = 0; k < s->count; ++k) {
        uint64_t n = 0;
        detail::Varint(c, &n);
        detail::Vertices(c, n, qx, qy, cap);
        f((int)n);
    }
    return true;
}

// Each river: f(nameIndex, n) after its vertices are in qx/qy.
template <class F>
inline bool ForEachRiver(const Package& pk, int32_t* qx, int32_t* qy, uint32_t cap, F f)
{
    const Section* s = pk.Find(kRivers);
    if (!s) return true;
    if (cap < pk.maxVertices) return false;
    detail::Cur c{pk.data + s->offset, s->length, 0};
    uint64_t names = 0, v = 0;
    detail::Varint(c, &names);
    for (uint64_t k = 0; k < names; ++k) {
        const char* t = nullptr;
        uint32_t len = 0;
        detail::Text(c, &t, &len);
    }
    for (uint32_t k = 0; k < s->count; ++k) {
        uint64_t n = 0;
        detail::Varint(c, &v);
        detail::Varint(c, &n);
        detail::Vertices(c, n, qx, qy, cap);
        f((int)v, (int)n);
    }
    return true;
}

// Each road: f(class, n) after its vertices are in qx/qy.
template <class F>
inline bool ForEachRoad(const Package& pk, int32_t* qx, int32_t* qy, uint32_t cap, F f)
{
    const Section* s = pk.Find(kRoads);
    if (!s) return true;
    if (cap < pk.maxVertices) return false;
    detail::Cur c{pk.data + s->offset, s->length, 0};
    for (uint32_t k = 0; k < s->count; ++k) {
        uint32_t cls = 0;
        uint64_t n = 0;
        detail::U8(c, &cls);
        detail::Varint(c, &n);
        detail::Vertices(c, n, qx, qy, cap);
        f((int)cls, (int)n);
    }
    return true;
}

// Each peak, in rank order: f(qx, qy, elevM, name, nameLen). The name is not NUL-terminated.
template <class F>
inline void ForEachPeak(const Package& pk, F f)
{
    const Section* s = pk.Find(kPeaks);
    if (!s) return;
    detail::Cur c{pk.data + s->offset, s->length, 0};
    for (uint32_t k = 0; k < s->count; ++k) {
        int64_t x = 0, y = 0, e = 0;
        const char* t = nullptr;
        uint32_t len = 0;
        detail::Svarint(c, &x);
        detail::Svarint(c, &y);
        detail::Svarint(c, &e);
        detail::Text(c, &t, &len);
        f((int32_t)x, (int32_t)y, (int32_t)e, t, len);
    }
}

// Each place, in the file's order: f(qx, qy, pop, name, nameLen).
template <class F>
inline void ForEachPlace(const Package& pk, F f)
{
    const Section* s = pk.Find(kPlaces);
    if (!s) return;
    detail::Cur c{pk.data + s->offset, s->length, 0};
    for (uint32_t k = 0; k < s->count; ++k) {
        int64_t x = 0, y = 0;
        uint64_t pop = 0;
        const char* t = nullptr;
        uint32_t len = 0;
        detail::Svarint(c, &x);
        detail::Svarint(c, &y);
        detail::Varint(c, &pop);
        detail::Text(c, &t, &len);
        f((int32_t)x, (int32_t)y, (uint32_t)pop, t, len);
    }
}

}  // namespace bmpk
