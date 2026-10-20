#pragma once
// WXC1 weather-crop decoder. Pure C++11: no Arduino, no allocation, no I/O.
//
// One copy, shared by Weatherscope and the Blipscope weather layer (wx spike Part B); agreed with the
// spike session 2026-10-20. The format is "Wire format v1" in docs/wx-spike-2026-10.md, and the
// encoder that writes it is tools/wx/crop.ts (both on spike/wx-overlay). test/host/test_wx_crop.cpp
// holds this decoder to crops that encoder wrote, checked against two references (crop.ts and the
// independent Python decoder in a3_check.py): test/fixtures/wx/MANIFEST.json.
//
// Header, 32 bytes, little-endian:
//    0  "WXC1"            4  u8 version (1)     5  u8 coarsen (1, 2, 4, 8)
//    6  u8 flags (bit 0: coarsened to fit the cap)                 7  reserved
//    8  u32 frame time, UTC epoch seconds
//   12  i32 lat, 16 i32 lon: the north-west cell's centre, microdegrees
//   20  u32 cell height, 24 u32 cell width, microdegrees
//   28  u16 rows, 30 u16 cols
// Body: runs, row-major from the north-west cell; a run may cross row ends. Each run starts with one
// byte: the code in the high nibble, the length 1-15 in the low nibble. A low nibble of 0 means a long
// run whose whole length follows as an unsigned LEB128 varint.
//
// Codes: 0 = no echo or under 15 dBZ, INSIDE radar coverage; 1-13 = 15-75 dBZ in 5 dBZ steps;
// 14 = no radar coverage; 15 = missing data. 14 and 15 are NEVER drawn as dry.
//
// A crop that breaks any rule is refused WHOLE: every byte is validated before a single run is
// reported or a single cell written, so a caller never holds part of a grid.
#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace wxcrop {

constexpr size_t kHeaderBytes = 32;
constexpr size_t kMaxBytes = 32 * 1024;      // the server's cap (crop.ts CAP_BYTES)
constexpr unsigned kMaxLebBytes = 5;         // a 32-bit length never needs more

constexpr uint8_t kCodeDry = 0;
constexpr uint8_t kCodeNoCoverage = 14;
constexpr uint8_t kCodeMissing = 15;
inline bool IsEcho(uint8_t code) { return code >= 1 && code <= 13; }
inline bool NeverDry(uint8_t code) { return code == kCodeNoCoverage || code == kCodeMissing; }

enum class Status : uint8_t {
    Ok = 0,
    TooShort,         // fewer than 32 bytes: no complete header
    TooLong,          // more than 32,768 bytes
    BadMagic,         // not "WXC1"
    BadVersion,       // version != 1
    BadCoarsen,       // coarsen not 1, 2, 4 or 8
    ZeroRows,
    ZeroCols,
    TruncatedLength,  // the input ends inside a LEB128 length
    LengthTooLong,    // a LEB128 length longer than 5 bytes
    Overrun,          // the runs cover more than rows x cols cells
    Underrun,         // the runs end before rows x cols cells
    BufferTooSmall,   // DecodeInto: the caller's buffer holds fewer than rows x cols cells
};

inline const char* StatusName(Status s)
{
    switch (s) {
    case Status::Ok: return "Ok";
    case Status::TooShort: return "TooShort";
    case Status::TooLong: return "TooLong";
    case Status::BadMagic: return "BadMagic";
    case Status::BadVersion: return "BadVersion";
    case Status::BadCoarsen: return "BadCoarsen";
    case Status::ZeroRows: return "ZeroRows";
    case Status::ZeroCols: return "ZeroCols";
    case Status::TruncatedLength: return "TruncatedLength";
    case Status::LengthTooLong: return "LengthTooLong";
    case Status::Overrun: return "Overrun";
    case Status::Underrun: return "Underrun";
    case Status::BufferTooSmall: return "BufferTooSmall";
    }
    return "?";
}

struct Header {
    uint8_t version = 0, coarsen = 0, flags = 0;
    uint32_t frameEpoch = 0;
    int32_t latNwMicro = 0, lonNwMicro = 0;       // the north-west cell's centre
    uint32_t cellLatMicro = 0, cellLonMicro = 0;  // cell height and width
    uint16_t rows = 0, cols = 0;
    uint32_t Cells() const { return (uint32_t)rows * cols; }
    bool Coarsened() const { return (flags & 1) != 0; }
};

namespace detail {
inline uint16_t U16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
inline uint32_t U32(const uint8_t* p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

// Reads the run starting at data[*i]: its code and length. Advances *i past it.
inline Status NextRun(const uint8_t* data, size_t len, size_t* i, uint8_t* code, uint32_t* run)
{
    const uint8_t b = data[(*i)++];
    *code = b >> 4;
    *run = b & 15;
    if (*run != 0) return Status::Ok;
    uint64_t v = 0;                               // long run: LEB128 length
    for (unsigned k = 0;; ++k) {
        if (k == kMaxLebBytes) return Status::LengthTooLong;
        if (*i >= len) return Status::TruncatedLength;
        const uint8_t x = data[(*i)++];
        v |= (uint64_t)(x & 0x7f) << (7 * k);
        if (!(x & 0x80)) break;
    }
    *run = v > 0xffffffffu ? 0xffffffffu : (uint32_t)v;   // anything that large overruns below
    return Status::Ok;
}
}  // namespace detail

// The header alone: Ok when every header field is valid. Does not read the body.
inline Status ParseHeader(const uint8_t* data, size_t len, Header* out)
{
    if (len < kHeaderBytes) return Status::TooShort;
    if (len > kMaxBytes) return Status::TooLong;
    if (data[0] != 'W' || data[1] != 'X' || data[2] != 'C' || data[3] != '1') return Status::BadMagic;
    Header h;
    h.version = data[4];
    h.coarsen = data[5];
    h.flags = data[6];
    h.frameEpoch = detail::U32(data + 8);
    h.latNwMicro = (int32_t)detail::U32(data + 12);
    h.lonNwMicro = (int32_t)detail::U32(data + 16);
    h.cellLatMicro = detail::U32(data + 20);
    h.cellLonMicro = detail::U32(data + 24);
    h.rows = detail::U16(data + 28);
    h.cols = detail::U16(data + 30);
    if (h.version != 1) return Status::BadVersion;
    if (h.coarsen != 1 && h.coarsen != 2 && h.coarsen != 4 && h.coarsen != 8) return Status::BadCoarsen;
    if (h.rows == 0) return Status::ZeroRows;
    if (h.cols == 0) return Status::ZeroCols;
    if (out) *out = h;
    return Status::Ok;
}

// The whole crop: header and every run. Ok only if the runs cover exactly rows x cols cells.
inline Status Validate(const uint8_t* data, size_t len, Header* out)
{
    Header h;
    Status s = ParseHeader(data, len, &h);
    if (s != Status::Ok) return s;
    const uint32_t total = h.Cells();
    uint32_t p = 0;
    for (size_t i = kHeaderBytes; i < len;) {
        uint8_t code;
        uint32_t run;
        s = detail::NextRun(data, len, &i, &code, &run);
        if (s != Status::Ok) return s;
        if (run > total - p) return Status::Overrun;
        p += run;
    }
    if (p != total) return Status::Underrun;
    if (out) *out = h;
    return Status::Ok;
}

// Calls fn(row, col0, len, code) once per run, in order, where (row, col0) is the run's first cell
// and len may cross row ends. Validates the whole crop first: on any refusal fn is never called.
template <typename Fn>
inline Status ForEachRun(const uint8_t* data, size_t len, Fn&& fn, Header* out = nullptr)
{
    Header h;
    const Status s = Validate(data, len, &h);
    if (s != Status::Ok) return s;
    uint32_t p = 0;
    for (size_t i = kHeaderBytes; i < len;) {
        uint8_t code;
        uint32_t run;
        detail::NextRun(data, len, &i, &code, &run);   // already validated
        fn((uint16_t)(p / h.cols), (uint16_t)(p % h.cols), run, code);
        p += run;
    }
    if (out) *out = h;
    return Status::Ok;
}

// Fills buf (row-major, rows x cols codes) from the crop. Validates the whole crop first: on any
// refusal buf is left exactly as it was.
inline Status DecodeInto(const uint8_t* data, size_t len, uint8_t* buf, size_t bufCells, Header* out = nullptr)
{
    Header h;
    const Status s = Validate(data, len, &h);
    if (s != Status::Ok) return s;
    if (bufCells < h.Cells()) return Status::BufferTooSmall;
    uint32_t p = 0;
    for (size_t i = kHeaderBytes; i < len;) {
        uint8_t code;
        uint32_t run;
        detail::NextRun(data, len, &i, &code, &run);
        memset(buf + p, code, run);
        p += run;
    }
    if (out) *out = h;
    return Status::Ok;
}

}  // namespace wxcrop
