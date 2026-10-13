#pragma once

// Which command-post airframe an ABNCP row is, from what its source STATES about it.
// Pure (no Arduino, no ArduinoJson) so test/host/test_abncp_identity.cpp grades the shipping
// code as-is.
//
// ---------------------------------------------------------------------------------------------
// THE RULES (Daniel, 2026-10-12)
//
//   1. "The ABNCP watchlist matches type B742 plus the E-4B registrations from public sources."
//      Ruled AND: the type B742 AND one of the four registrations below.
//   2. On the device, recognition is a cited HEX table (the four rows below): a row is an E-4B
//      when its ICAO24 address is in the table AND (its type is B742 or E4, or no type is
//      given). A row with no type (an OpenSky state vector never has one) matches on the hex
//      alone. E4 is accepted HERE ONLY (ruled 2026-10-12, "YES, type E4 counts too"): it is how
//      adsbdb and hexdb.io type these airframes. Rule 1 is unchanged and still needs B742.
//   3. Rule 1 stays as a second check wherever a registration exists. A row that states a
//      registration is named only if that registration is one of the four; then either rule 1
//      (B742 + registration) or rule 2 (hex) names it.
//
// WHY THE TYPE CHECK IS AND, NEVER OR. B742 is the ICAO type designator of every Boeing
// 747-200, not only of the E-4B. The database that types the four E-4Bs B742 also types both
// VC-25As B742, flagged military: 82-8000 at ADFDF8 and 92-9000 at ADFDF9
// (https://github.com/wiedehopf/tar1090-db/blob/17679e257733ad808a417cf4a5d52f3b00238887/db/AD.js,
// keys "FDF8"/"FDF9"; adsbdb agrees: https://api.adsbdb.com/v0/aircraft/82-8000). Type alone
// would call Air Force One "Nightwatch"; so would a hex table checked against type alone. The
// VC-25A hexes are not in the table, so neither B742 nor E4 can name them.
//
// ---------------------------------------------------------------------------------------------
// THE FOUR AIRFRAMES. The USAF fact sheet gives the inventory as four, on a militarized
// 747-200 (https://www.af.mil/About-Us/Fact-Sheets/Display/Article/104503/e-4b/). A USAF serial
// is the registration ADS-B databases carry for the aircraft.
//
//   hex     serial   the serial is an E-4B
//   ADFEB3  73-1676  "73-1676 (E-4A/B)"     http://www.joebaugher.com/usaf_serials/1973.html
//   ADFEB4  73-1677  "73-1677 (E-4A/B)"     http://www.joebaugher.com/usaf_serials/1973.html
//   ADFEB5  74-0787  "74-0787 Boeing E-4A ... 1980: Converted to E-4B"
//                                           http://www.joebaugher.com/usaf_serials/1974.html
//   ADFEB6  75-0125  "75-0125 Boeing E-4B"  http://www.joebaugher.com/usaf_serials/1975.html
//
// The hex belongs to the serial in three registries, which agree on all four:
//   - tar1090-db (the readsb/Mictronics database), AD.js at the commit linked above, keys
//     "FEB3".."FEB6": ["73-1676","B742","10","Boeing E-4B"] ... ["75-0125","B742","10","Boeing E-4B"];
//   - adsbdb, by serial: https://api.adsbdb.com/v0/aircraft/73-1676 -> "mode_s":"ADFEB3", "E-4B"
//     (and 73-1677, 74-0787, 75-0125 -> ADFEB4, ADFEB5, ADFEB6);
//   - hexdb.io, by hex: https://hexdb.io/api/v1/aircraft/ADFEB3 -> "73-1676", "E-4B"
//     (and ADFEB4..ADFEB6 -> 73-1677, 74-0787, 75-0125).
// tar1090-db types them "B742"; adsbdb and hexdb.io type them "E4". Rule 2 accepts both.
//
// ---------------------------------------------------------------------------------------------
// NOT THE OLD TABLE. Before 2026-10-12 the firmware held four ICAO24 addresses marked
// PLACEHOLDER, ae0451..ae0454. They are four US Navy C-2A Greyhounds, not E-4Bs: adsbdb
// (https://api.adsbdb.com/v0/aircraft/AE0451 ... AE0454), hexdb.io and tar1090-db (A.js keys
// "E0451".."E0454") all say so. They are gone, and the test keeps them out.
namespace abncp {

constexpr const char* kE4bIcaoType = "B742";   // rules 1 and 2 (tar1090-db's type)
constexpr const char* kE4bAltIcaoType = "E4";  // rule 2 only (adsbdb's and hexdb.io's type)

struct Airframe {
    const char* hex;           // ICAO24, lower-case, exactly 6 hex digits
    const char* registration;  // the USAF serial
};
constexpr Airframe kE4bAirframes[] = {
    { "adfeb3", "73-1676" },
    { "adfeb4", "73-1677" },
    { "adfeb5", "74-0787" },
    { "adfeb6", "75-0125" },
};

// True when s holds anything but spaces/tabs. Absent (null or blank) is "not stated".
inline bool Stated(const char* s)
{
    if (!s) return false;
    for (; *s; ++s)
        if (*s != ' ' && *s != '\t') return true;
    return false;
}

// True when a and b are the same identifier once punctuation, spaces and case are ignored:
// "73-1676" == "731676" == " 73-1676 ", "b742" == "B742". The same normalisation as the
// backend's ABNCP watchlist (valar-eam-feed AdsbMilFeedSource norm(): strip everything that is
// not [a-z0-9], lowercase). An input with no letters or digits never matches.
inline bool SameIdent(const char* a, const char* b)
{
    if (!a || !b) return false;
    auto alnum = [](char ch) {
        return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z');
    };
    auto lower = [](char ch) { return (ch >= 'A' && ch <= 'Z') ? (char)(ch - 'A' + 'a') : ch; };
    bool any = false;
    for (;;) {
        while (*a && !alnum(*a)) ++a;
        while (*b && !alnum(*b)) ++b;
        if (!*a || !*b) return any && !*a && !*b;
        if (lower(*a) != lower(*b)) return false;
        any = true;
        ++a;
        ++b;
    }
}

// True when `hex` is exactly the table's 6-digit address, case-insensitive, surrounding spaces
// allowed. STRICTER than SameIdent on purpose: a '~' marks a non-ICAO (TIS-B) track id, which
// is not the airframe's address, so "~adfeb3" does not match; nor does a prefix or extension.
inline bool SameHex(const char* hex, const char* tableHex)
{
    if (!hex || !tableHex) return false;
    while (*hex == ' ' || *hex == '\t') ++hex;
    auto lower = [](char ch) { return (ch >= 'A' && ch <= 'Z') ? (char)(ch - 'A' + 'a') : ch; };
    for (; *tableHex; ++hex, ++tableHex)
        if (lower(*hex) != *tableHex) return false;
    while (*hex == ' ' || *hex == '\t') ++hex;
    return *hex == '\0';
}

// The registration is one of the four.
inline bool IsE4bRegistration(const char* registration)
{
    for (const Airframe& a : kE4bAirframes)
        if (SameIdent(registration, a.registration)) return true;
    return false;
}

// Rule 1: ICAO type B742 AND one of the four public E-4B registrations. Both required.
inline bool IsE4b(const char* icaoType, const char* registration)
{
    return SameIdent(icaoType, kE4bIcaoType) && IsE4bRegistration(registration);
}

// Rule 2: the hex is in the table AND (the type is B742 or E4, or no type is given).
inline bool IsE4bHex(const char* hex, const char* icaoType)
{
    if (Stated(icaoType) && !SameIdent(icaoType, kE4bIcaoType) && !SameIdent(icaoType, kE4bAltIcaoType))
        return false;
    for (const Airframe& a : kE4bAirframes)
        if (SameHex(hex, a.hex)) return true;
    return false;
}

// What a row is named by: rule 2 for every row, with rule 1 as the second check where the row
// states a registration (a stated registration that is not one of the four names nothing).
inline bool IdentifyE4b(const char* hex, const char* icaoType, const char* registration)
{
    if (Stated(registration)) {
        if (!IsE4bRegistration(registration)) return false;
        return IsE4b(icaoType, registration) || IsE4bHex(hex, icaoType);
    }
    return IsE4bHex(hex, icaoType);
}

} // namespace abncp
