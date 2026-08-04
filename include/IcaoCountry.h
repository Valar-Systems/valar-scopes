#pragma once

#include <Arduino.h>
#include <cstdint>

// Country of registration, derived offline from the ICAO 24-bit address.
//
// WHY THIS EXISTS. The logbook counts four categories -- types, airlines,
// countries, airports -- and countries are the highest-value one on the
// leaderboard (25 pts each, vs 10/5/2). But only ONE of the three feeds ever
// supplied the field: OpenSky sends it in state[2]. The cloud feed does not
// carry it (it isn't on the /v1/blips wire, and adding a per-aircraft string to
// a poll that already runs into a payload ceiling would be the wrong trade), and
// a local dump1090/readsb aircraft.json has never had it. So on the DEFAULT feed
// a device saw "0 of 0 countries" forever: the category was not merely
// unpopulated, it was unwinnable, and the globetrotter badge (15 countries) was
// unreachable for everyone not on a BYO OpenSky account.
//
// The fix costs no bytes on the wire, because the answer is already in the
// address we receive. ICAO allocates the 24-bit address space in contiguous
// per-state blocks (Annex 10, Vol III), which is exactly what OpenSky itself
// resolves to produce origin_country -- so deriving it here reproduces the same
// semantic from the same input rather than inventing a parallel one.
//
// NAMING. Short, readable names ("South Korea", "Russia"), not the ISO 3166
// officialese OpenSky emits ("Korea, Republic of", "Russian Federation"). They
// are what fits a 240 px round display and a leaderboard row, and Logbook
// truncates at 32 chars anyway. The names therefore do NOT always match
// OpenSky's string for the same country. That is only observable for a device
// that ran on OpenSky and later switched feeds -- it would count e.g. Russia
// twice. Every other case is strictly better than the zero it replaces, and
// devices on the cloud/local feeds have an empty country set today, so there is
// nothing for a new name to collide with.
namespace IcaoCountry {

// The allocation holding this address, or "" when the address falls in an
// unallocated gap (plenty exist) -- callers treat "" as "don't record".
// The returned pointer is to a string literal in flash and is never freed.
const char* Lookup(uint32_t icaoAddr);

// Convenience overload: parse a hex ICAO string ("adf7c8"). Tolerates a leading
// '~' (local-receiver TIS-B addresses) and whitespace, matching
// SpecialAircraft::IsMilitary; "" if unparseable.
const char* Lookup(const String& icao24Hex);

} // namespace IcaoCountry
