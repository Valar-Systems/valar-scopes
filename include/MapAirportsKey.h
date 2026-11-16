// The airports half of the map's key (docs/blipscope-map-client-steps.md, "Step C's scope addition: airportsGen moves
// only when the airports CONTENT changes", and its amendment). Pure: no Arduino, no allocation, no output.
//
// The names' obstacles (MapAirportBoxes) read four fields of each airports-list row -- lat, lon, code and kind -- in list
// order. EVERY row counts: MapAirportBoxes counts all the rows in view and writes a box for at most maxAirports of them,
// and when more than that are in view the composer refuses ALL the names (CB11) rather than keep a prefix. So the list's
// CONTENT is those four fields of every row, in that order, plus its length (a change anywhere can change whether the
// names are drawn), and a refetch whose content is identical changes nothing on screen: it must not move airportsGen, or the
// map redraws (185-262 ms on the s3-128, FT-SEA) for nothing -- every 5 min while the list stays empty (APT_EMPTY_RETRY_MS).
//
// ContentHash: 32-bit FNV-1a over the length, then each row's lat and lon float bits, its five code bytes and its kind.
// kEmptyHash: the hash of an empty list (the length 0 alone), written out; AP-EMPTYEQ holds it equal to ContentHash's.
// Held: the hash of the list the map last drew from. Its default is kEmptyHash, the empty list the device boots with
// (AP-BOOT), so a first EMPTY arrival is no change and a first LISTED arrival is one.
// Changed: whether an arrival's hash differs from the held one; it stores the new hash either way.
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace mapairports {

constexpr uint32_t kFnvBasis = 2166136261u;
constexpr uint32_t kFnvPrime = 16777619u;

// The length 0 hashed as four zero bytes: each step is (h ^ 0) * prime.
constexpr uint32_t kEmptyHash = (uint32_t)((uint32_t)((uint32_t)((uint32_t)(kFnvBasis * kFnvPrime) * kFnvPrime) *
                                                      kFnvPrime) * kFnvPrime);

inline uint32_t MixByte(uint32_t h, uint8_t b) { return (uint32_t)((h ^ b) * kFnvPrime); }

inline uint32_t MixWord(uint32_t h, uint32_t v)
{
    for (int i = 0; i < 4; ++i) h = MixByte(h, (uint8_t)(v >> (8 * i)));
    return h;
}

inline uint32_t FloatBits(float f)
{
    uint32_t u = 0;
    memcpy(&u, &f, sizeof u);
    return u;
}

// Row: anything with float lat, float lon, char code[5] and char kind (CloudFeed::CloudAirport on the device).
template <typename Row>
inline uint32_t ContentHash(const Row* rows, size_t n)
{
    uint32_t h = MixWord(kFnvBasis, (uint32_t)n);
    for (size_t i = 0; i < n; ++i) {
        const Row& r = rows[i];
        h = MixWord(h, FloatBits(r.lat));
        h = MixWord(h, FloatBits(r.lon));
        for (int c = 0; c < 5; ++c) h = MixByte(h, (uint8_t)r.code[c]);
        h = MixByte(h, (uint8_t)r.kind);
    }
    return h;
}

struct Held {
    uint32_t h = kEmptyHash;
};

inline bool Changed(Held& held, uint32_t h)
{
    const bool changed = h != held.h;
    held.h = h;
    return changed;
}

}  // namespace mapairports
