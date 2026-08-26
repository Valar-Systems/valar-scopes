#pragma once

// How a route is turned into words, in ONE place.
//
// The route arrives as two airport codes from /api/v1/blipscope/enrich (`o` and
// `d`). Two screens render it -- the detail card and the List screen's Route
// info field -- and before this header they each formatted it inline. That is
// the arrangement CI now has a dedicated checker for elsewhere in this repo
// (MIL_RANGES / NON_ICAO_RANGES): two copies of one fact, drifting silently.
//
// =============================================================================
// WHY o == d IS TRUSTWORTHY, WHICH IS THE WHOLE BASIS FOR THIS FILE
//
// A route that starts and ends at the same field used to be a bug we made. The
// route mirror's rev-2 endpoint rule handled a rotation (A-B-A) by returning
// legs[1], which guaranteed "not first-and-last" when the requirement was "not
// equal" -- and on DLH8985 = EGTE-EGTE-EGTE, legs[1] is also EGTE, so it
// manufactured the exact self-loop it existed to prevent.
//
// RULE_REV 3 (proxy/scripts/routerule.ts) states the requirement directly and
// proxy/test/routerule.test.ts asserts it over the whole shape space:
//
//     origin == destination   IFF   every leg of the source route is the field
//
// So on the wire, `o == d` now means one thing only: the aircraft came back to
// where it started. 39 of the 619,103 mirrored callsigns are like that, and
// every one is real -- RAF Cranwell circuits (CWL91, EGYD-EGYD), Nice
// sightseeing runs (AFR49UR, NCE-NCE), survey patterns. Verified live through
// production on 2026-08-26, alongside AAL1208 correctly rendering DFW-BUR rather
// than DFW-DFW.
//
// That is why the device can decide this from two three-letter strings and needs
// no extra flag on the wire. If the rule ever changes, this header's premise
// goes with it.
//
// =============================================================================
// WHY IT IS A CODE AND AN ASCII COLON
//
// "Local flight: NCE", not "Local flight - Nice":
//
//   * The device has no airport NAMES. include/Airports.h is ~250 IATA codes and
//     coordinates, deliberately, and adding a name table costs flash for one
//     line on one card. Names, if ever wanted, belong in an enrich field.
//   * The card draws in LovyanGFX's default font. There is no setFont() anywhere
//     in the draw path, so the glyph set is ASCII -- a UTF-8 middot arrives as
//     two bytes and renders as two pieces of garbage. The existing card writes
//     " -> " rather than an arrow for the same reason.
//   * ": " is the separator the card already uses ("Type: A320"), so this reads
//     as one more card row rather than a new idiom.
// =============================================================================

#include <Arduino.h>

namespace routelabel {

// A route we have. Both codes present.
inline bool HasRoute(const String& origin, const String& dest)
{
    return !origin.isEmpty() && !dest.isEmpty();
}

// The aircraft returned to its departure field. See the header comment: on the
// current wire rule this is a fact about the flight, not a defect in the data.
inline bool IsLocalFlight(const String& origin, const String& dest)
{
    return HasRoute(origin, dest) && origin.equalsIgnoreCase(dest);
}

// Detail-card row. "" when there is no route to draw, so callers can keep using
// `if (!line.isEmpty())` and never render an empty row.
inline String CardLine(const String& origin, const String& dest)
{
    if (!HasRoute(origin, dest)) return "";
    if (IsLocalFlight(origin, dest)) return "Local flight: " + origin;
    return origin + " -> " + dest;
}

// List-screen info field. Same decision, tighter: this shares a line with other
// fields, so it drops the word "flight" rather than the airport.
inline String InfoField(const String& origin, const String& dest)
{
    if (!HasRoute(origin, dest)) return "";
    if (IsLocalFlight(origin, dest)) return "Local: " + origin;
    return origin + ">" + dest;
}

} // namespace routelabel
