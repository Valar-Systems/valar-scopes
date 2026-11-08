#pragma once
// OTA IDENTITY: which board an image is for, and which scope (edition) it is
// (docs/weatherscope-stage3.md section 3, A1.2, A12.103, A12.104).
//
// Every image carries a 64-byte record at file offset 0x120 -- ESP-IDF's custom
// app descriptor slot, right after the 256-byte esp_app_desc_t -- naming its
// board (variant::SLUG) and its scope (FW_OTA_PREFIX) as two separate fields.
// Two fields because a joined name cannot be split: slugs contain hyphens, so
// "eam-s3-128" could be scope "eam-" on "s3-128" or no scope on a board of that
// name.
//
// THE RULE (Q75 (b), ruled 2026-11-05). A device accepts an image only if it is
// exactly what the device asked for: the board must be the device's own -- that
// is its identity, and it is hardware -- and the scope must be the one in the
// request. A normal update asks for the device's own scope; a scope switch asks
// for the chosen scope's file. So every scope's release for a board stays
// installable when it is the one asked for, and an image that is not what was
// asked for (a mislabelled publish) costs a 352-byte head read, never a flash.
//
// Pure and header-only, C++11, so the host tests compile exactly this.

#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace ota {

constexpr uint32_t kRecordOffset = 0x120;   // file offset: right after esp_app_desc_t
constexpr size_t kRecordSize = 64;
constexpr size_t kHeadBytes = kRecordOffset + kRecordSize;   // 352: all a decision reads
constexpr size_t kFieldMax = 24;
constexpr uint8_t kRecordVersion = 1;
constexpr uint8_t kImageMagic = 0xE9;        // an ESP image's first byte

// "VSID". Pairwise distinct, so a read shifted by one byte either way can never
// re-form it (A3.3); and neither WXC1 -- what a pre-check Weatherscope image holds
// at 0x120 -- nor esp_app_desc_t's 32 54 cd ab.
constexpr uint8_t kMagic[4] = {0x56, 0x53, 0x49, 0x44};
static_assert(kMagic[0] != kMagic[1] && kMagic[0] != kMagic[2] && kMagic[0] != kMagic[3] &&
              kMagic[1] != kMagic[2] && kMagic[1] != kMagic[3] && kMagic[2] != kMagic[3],
              "the magic's four bytes must be pairwise distinct (A3.3)");

// The record, byte for byte (A12.104). Every member is a byte, so it has no
// padding and an alignment of 1: it lands at exactly 0x120.
struct Record {
    uint8_t magic[4];
    uint8_t version;
    uint8_t length;
    uint8_t boardLen;
    uint8_t scopeLen;
    char board[kFieldMax];    // variant::SLUG, NUL-padded
    char scope[kFieldMax];    // FW_OTA_PREFIX, NUL-padded; empty for the radar
    uint8_t reserved[8];      // zero
};
static_assert(sizeof(Record) == kRecordSize, "the record is 64 bytes");

struct Identity {
    char board[kFieldMax + 1];
    char scope[kFieldMax + 1];
};

enum class Parsed : uint8_t { Ok, NoRecord, BadRecord };

constexpr bool SlugChar(char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-'; }

// Reads one field of `len` bytes from a `kFieldMax`-byte slot: every byte inside
// the length from the slug alphabet, every byte after it zero.
inline bool ReadField(const uint8_t* slot, uint8_t len, char* out)
{
    for (size_t i = 0; i < kFieldMax; ++i) {
        const char c = (char)slot[i];
        if (i < len ? !SlugChar(c) : c != 0) return false;
        out[i] = i < len ? c : '\0';
    }
    out[kFieldMax] = '\0';
    return true;
}

// The 64 record bytes -> an identity. NoRecord: the magic is not there.
// BadRecord: anything else this version does not know, refused rather than read.
inline Parsed ParseRecord(const uint8_t* r, Identity& out)
{
    if (memcmp(r, kMagic, 4) != 0) return Parsed::NoRecord;
    if (r[4] != kRecordVersion || r[5] != kRecordSize) return Parsed::BadRecord;
    const uint8_t boardLen = r[6], scopeLen = r[7];
    if (boardLen == 0 || boardLen > kFieldMax || scopeLen > kFieldMax) return Parsed::BadRecord;
    if (!ReadField(r + 8, boardLen, out.board) || !ReadField(r + 8 + kFieldMax, scopeLen, out.scope))
        return Parsed::BadRecord;
    for (size_t i = 8 + 2 * kFieldMax; i < kRecordSize; ++i)
        if (r[i] != 0) return Parsed::BadRecord;
    return Parsed::Ok;
}

// An image's first `n` bytes -> its identity. Fewer than kHeadBytes cannot hold a
// record (the caller refuses that as truncated before it gets here).
inline Parsed Parse(const uint8_t* head, size_t n, Identity& out)
{
    if (n < kHeadBytes) return Parsed::NoRecord;
    return ParseRecord(head + kRecordOffset, out);
}

// What a device asks for: its OWN board, and a scope. A name longer than a field
// gives an identity with an empty board, which no record can match (a record's
// board is never empty) -- refused, never truncated into a different name.
inline Identity Request(const char* board, const char* scope)
{
    Identity id;
    memset(&id, 0, sizeof id);
    if (strlen(board) > kFieldMax || strlen(scope) > kFieldMax) return id;
    memcpy(id.board, board, strlen(board));
    memcpy(id.scope, scope, strlen(scope));
    return id;
}

// THE CHECK: exactly what was asked for, board and scope.
inline bool Accept(const Identity& requested, const Identity& candidate)
{
    return strcmp(requested.board, candidate.board) == 0 && strcmp(requested.scope, candidate.scope) == 0;
}

// THE MODEL, as a device names itself to the proxy in X-Blip-Model (docs/weatherscope-stage4.md S4.1.2, S4.5): the
// scope followed by the board, exactly what it asks OTA for. True, with the model in `out`; or false, with NOTHING
// written, when it does not fit or the identity has no board. A cut model is a different model, so a short buffer is
// refused, never truncated (M1b). Header-only and inline: an image that never calls it carries none of it (Q123 (a)).
inline bool ModelString(const Identity& id, char* out, size_t cap)
{
    const size_t s = strlen(id.scope), b = strlen(id.board);
    if (b == 0 || s + b + 1 > cap) return false;
    memcpy(out, id.scope, s);
    memcpy(out + s, id.board, b);
    out[s + b] = '\0';
    return true;
}

// The device's own record, defined in src/OtaIdentity.cpp in .rodata_custom_desc.
// Declared here so the boot line can read it back; the host never links it.
extern const Record kOwnRecord;

}  // namespace ota
