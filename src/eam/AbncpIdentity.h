#pragma once

// Which command-post airframe an ABNCP row is, from what its source STATES about it.
// Pure (no Arduino, no ArduinoJson) so test/host/test_abncp_identity.cpp grades the shipping
// code as-is.
//
// ---------------------------------------------------------------------------------------------
// THE RULE (Daniel, 2026-10-12): "The ABNCP watchlist matches type B742 plus the E-4B
// registrations from public sources."
//
// READ AS "AND", ON PURPOSE. B742 is the ICAO type designator of every Boeing 747-200, not
// only of the E-4B. The same database that types the four E-4Bs B742 also types both VC-25As
// (82-8000 at ADFDF8, 92-9000 at ADFDF9) B742, flagged military
// (https://github.com/wiedehopf/tar1090-db/blob/17679e257733ad808a417cf4a5d52f3b00238887/db/AD.js,
// keys "FDF8"/"FDF9"; adsbdb agrees: https://api.adsbdb.com/v0/aircraft/82-8000). With "OR",
// the type alone would call Air Force One "Nightwatch". So a row is an E-4B only when its type
// is B742 AND its registration is one of the four below. A row with no type, or with no
// registration, is never named. Whether the ruling meant AND is an open question on the PR.
//
// ---------------------------------------------------------------------------------------------
// THE FOUR AIRFRAMES. A USAF serial is the registration ADS-B databases carry for the aircraft.
// The USAF fact sheet gives the inventory as four, on a militarized 747-200
// (https://www.af.mil/About-Us/Fact-Sheets/Display/Article/104503/e-4b/). Each serial:
//
//   73-1676  "73-1676 (E-4A/B)"  http://www.joebaugher.com/usaf_serials/1973.html
//   73-1677  "73-1677 (E-4A/B)"  http://www.joebaugher.com/usaf_serials/1973.html
//   74-0787  "74-0787 Boeing E-4A ... 1980: Converted to E-4B"
//                                http://www.joebaugher.com/usaf_serials/1974.html
//   75-0125  "75-0125 Boeing E-4B" http://www.joebaugher.com/usaf_serials/1975.html
//
// and each is typed "B742", described "Boeing E-4B", in tar1090-db (the readsb/Mictronics
// database), AD.js keys "FEB3".."FEB6", at the commit linked above. adsbdb also lists all four
// as "E-4B", USAF (https://api.adsbdb.com/v0/aircraft/73-1676 etc.), but gives the ICAO type as
// "E4", not "B742". The ruling names B742, which is what tar1090-db carries.
//
// ---------------------------------------------------------------------------------------------
// NO HEXES. The table this replaces held four ICAO24 addresses marked PLACEHOLDER
// (ae0451..ae0454). They are four US Navy C-2A Greyhounds, not E-4Bs: adsbdb
// (https://api.adsbdb.com/v0/aircraft/AE0451 ... AE0454) and tar1090-db (A.js keys
// "E0451".."E0454") both say so. The airframes are identified by type and registration, as
// ruled; a source that states neither (OpenSky /states/all) cannot be matched by this rule, and
// its rows stay unnamed.
namespace abncp {

constexpr const char* kE4bIcaoType = "B742";
constexpr const char* kE4bRegistrations[] = { "73-1676", "73-1677", "74-0787", "75-0125" };

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

// The rule: ICAO type B742 AND one of the four public E-4B registrations. Both required.
inline bool IsE4b(const char* icaoType, const char* registration)
{
    if (!SameIdent(icaoType, kE4bIcaoType)) return false;
    for (const char* reg : kE4bRegistrations)
        if (SameIdent(registration, reg)) return true;
    return false;
}

} // namespace abncp
