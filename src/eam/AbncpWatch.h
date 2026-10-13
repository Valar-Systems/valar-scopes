#pragma once

// The OpenSky command-post watch's icao24 list, and which returned states survive the cap.
// Pure (no Arduino) and templated on the string type, so test/host/test_abncp_identity.cpp
// grades it with std::string and the firmware (AbncpProvider.h, EamModels.cpp, EamManager.cpp,
// the config page) runs the same code with Arduino String.
//
// RULED (Daniel, 2026-10-12):
//   - "OpenSky mode always includes the four built-in E-4B hexes (ADFEB3-ADFEB6) in its
//     watchlist query, alongside the user's own hexes." Same request, same cadence; they are
//     not user entries, and an empty user list still sends them.
//   - "Invalid entries are dropped from the query, never sent. The config page marks each one:
//     'not a valid 6-digit hex, ignored'."
//   - "The 8-aircraft cap: keep the built-in four first, then the user's, up to 8. Don't raise
//     the cap (heap)."
//
// One entry's canonical form, which is also what is sent: surrounding whitespace dropped, ONE
// leading '~' dropped, lower-cased. OpenSky's icao24 parameter is the bare 24-bit address
// ("a hex string (e.g. abc9f3)"); a '~' is a local receiver's non-ICAO marker and means nothing
// to OpenSky, so "~ADFEB3" and "adfeb3" are the same filter. A canonical entry that is not
// exactly six hex digits is invalid: never sent.
//
// The built-in addresses are abncp::kE4bAirframes (AbncpIdentity.h), each cited there.
#include <cstddef>
#include <cstring>
#include <vector>

#include "AbncpIdentity.h"

namespace abncp {

// The separators of the stored `abncp-watch` string -- the same set EamManager::SplitList has
// always used for it (comma, semicolon, space, tab, CR, LF). One splitter for the device and
// the config page, so the page marks exactly the entries the device drops.
inline bool IsWatchSeparator(char ch)
{
    return ch == ',' || ch == ';' || ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
}

// The stored string's entries, raw (not canonicalised), in order, empties skipped.
template <typename Str, typename List>
List SplitWatchList(const char* s)
{
    List out;
    if (!s) return out;
    while (*s) {
        while (*s && IsWatchSeparator(*s)) ++s;
        const char* start = s;
        while (*s && !IsWatchSeparator(*s)) ++s;
        if (s > start) {
            Str tok;
            for (const char* p = start; p < s; ++p) tok += *p;
            out.push_back(tok);
        }
    }
    return out;
}

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

// Exactly six hex digits (a canonical entry is already lower-case).
inline bool IsWatchHex(const char* canonical)
{
    if (!canonical) return false;
    int n = 0;
    for (; canonical[n]; ++n) {
        const char ch = canonical[n];
        if (n >= 6) return false;
        if (!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f'))) return false;
    }
    return n == 6;
}

// The user's entries that are NOT sent, exactly as typed (for the config page to mark).
template <typename Str, typename List>
List InvalidWatchEntries(const char* stored)
{
    List bad;
    for (const Str& raw : SplitWatchList<Str, List>(stored))
        if (!IsWatchHex(CanonicalWatchHex<Str>(raw.c_str()).c_str())) bad.push_back(raw);
    return bad;
}

// The query: the user's valid entries (canonical, de-duplicated, in their order), then the four
// built-in E-4B addresses not already among them. Never empty: the four are always there.
template <typename Str, typename List>
List OpenSkyWatchQuery(const List& userEntries)
{
    List out;
    auto addOnce = [&out](const Str& e) {
        if (!IsWatchHex(e.c_str())) return;  // invalid: dropped, never sent
        for (const Str& have : out)
            if (have == e) return;
        out.push_back(e);
    };
    for (const Str& u : userEntries) addOnce(CanonicalWatchHex<Str>(u.c_str()));
    for (const Airframe& a : kE4bAirframes) addOnce(Str(a.hex));
    return out;
}

// One of the four built-in E-4B addresses (exact, as SameHex: no '~', no prefix/extension).
inline bool IsBuiltinHex(const char* hex)
{
    for (const Airframe& a : kE4bAirframes)
        if (SameHex(hex, a.hex)) return true;
    return false;
}

// Which of a response's states to keep under `cap`, given their addresses in response order:
// the built-in E-4Bs first, then everything else, each group in response order. Returns indices
// into `hexes`. The cap is not raised (heap); this only decides who is cut.
template <typename List>
std::vector<size_t> SelectWithinCap(const List& hexes, size_t cap)
{
    std::vector<size_t> keep;
    for (size_t i = 0; i < hexes.size() && keep.size() < cap; ++i)
        if (IsBuiltinHex(hexes[i].c_str())) keep.push_back(i);
    for (size_t i = 0; i < hexes.size() && keep.size() < cap; ++i)
        if (!IsBuiltinHex(hexes[i].c_str())) keep.push_back(i);
    return keep;
}

} // namespace abncp
