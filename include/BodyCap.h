#pragma once
// The map package's size cap, as GetBytes applies it (src/HttpRequestManager.cpp): its two decisions, moved here
// unchanged so that a host test (test/host/test_body_cap.cpp) runs the real code. Pure C++11, no Arduino.
//
// WHY THIS IS PINNED. The device asks for a package no larger than a cap it computes from its own request
// (include/MapQuery.h, Build), and the server builds to that cap BY CONSTRUCTION, so packages land at or just under it:
// map-server's offline pass of 2026-11-22 over 22 packages found one EXACTLY at its cap (211,408 B) and 9 within 9 B.
// So "a body of exactly cap bytes is accepted, cap + 1 is refused" is live in the field, and an off-by-one here would
// show as a map that never appears: a refused package is retried twice and then not asked for again
// (include/MapPolicy.h, CA8), and the radar draws without it.
#include <stddef.h>
#include <stdint.h>

namespace bodycap {

/** A body whose length is declared up front (Content-Length): it fits when it is no longer than the buffer. A body of
 *  exactly `cap` bytes FITS; one of cap + 1 does not, and is refused before a byte is read. */
inline bool Fits(size_t declared, size_t cap) { return !(declared > cap); }

/** A body whose length is not declared (chunked): copies bytes from `next()` -- an int, negative at the end of the
 *  body -- into `buf` until the end. Returns true ("over") as soon as byte cap + 1 arrives, without storing it; then
 *  *n == cap. Otherwise returns false with *n the bytes kept, so exactly `cap` bytes are kept and NOT over. */
template <class Next>
inline bool ReadChunked(Next next, uint8_t* buf, size_t cap, size_t* n)
{
    *n = 0;
    for (int c; (c = next()) >= 0;) {
        if (*n == cap) return true;   // byte cap + 1: refused
        buf[(*n)++] = (uint8_t)c;
    }
    return false;
}

}  // namespace bodycap
