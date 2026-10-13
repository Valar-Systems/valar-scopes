#pragma once

// The icao24 list the OpenSky command-post watch sends. Pure (no Arduino) and templated on the
// string type, so test/host/test_abncp_identity.cpp grades it with std::string and the firmware
// (AbncpProvider.h) runs the same code with Arduino String.
//
// RULED (Daniel, 2026-10-12): "OpenSky mode always includes the four built-in E-4B hexes
// (ADFEB3-ADFEB6) in its watchlist query, alongside the user's own hexes."
//
//   - The four ride in the SAME request at the SAME cadence; nothing here adds a request.
//   - They are not user entries: the config page never shows them, and an empty user list
//     still sends them.
//   - The user's entries come first, in their order, then whichever of the four are not
//     already there. No address is sent twice.
//
// One entry's canonical form, which is also what is sent: surrounding whitespace dropped, ONE
// leading '~' dropped, lower-cased. OpenSky's icao24 parameter is the bare 24-bit address
// ("a hex string (e.g. abc9f3)"); a '~' is a local receiver's non-ICAO marker and means nothing
// to OpenSky, so "~ADFEB3" and "adfeb3" are the same filter. Nothing else is validated or
// dropped: an entry that is not six hex digits is sent as before, only canonicalised.
//
// The built-in addresses are abncp::kE4bAirframes (AbncpIdentity.h), each cited there.
#include <cstring>

#include "AbncpIdentity.h"

namespace abncp {

template <typename Str>
Str CanonicalWatchHex(const char* in)
{
    Str out;
    if (!in) return out;
    auto space = [](char ch) { return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n'; };
    while (space(*in)) ++in;
    if (*in == '~') ++in;
    const char* end = in + std::strlen(in);
    while (end > in && space(end[-1])) --end;
    for (; in < end; ++in) {
        char ch = *in;
        if (ch >= 'A' && ch <= 'Z') ch = (char)(ch - 'A' + 'a');
        out += ch;
    }
    return out;
}

// The user's own entries (canonical, de-duplicated, in order) followed by the four built-in
// E-4B addresses that are not already among them. Never empty: the four are always there.
template <typename Str, typename List>
List OpenSkyWatchQuery(const List& userEntries)
{
    List out;
    auto addOnce = [&out](const Str& e) {
        if (e.length() == 0) return;
        for (const Str& have : out)
            if (have == e) return;
        out.push_back(e);
    };
    for (const Str& u : userEntries) addOnce(CanonicalWatchHex<Str>(u.c_str()));
    for (const Airframe& a : kE4bAirframes) addOnce(Str(a.hex));
    return out;
}

} // namespace abncp
