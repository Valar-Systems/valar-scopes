// The map package's exact-cap boundary (include/BodyCap.h; bodycap_registration.txt T2): a body of exactly `cap` bytes
// is ACCEPTED and cap + 1 is REFUSED, down BOTH of GetBytes' paths -- the declared length (Content-Length) and the
// undeclared one (chunked). map-server's offline pass of 2026-11-22 found a package exactly at its cap, so this is live.
// Rows, for caps 64 and 211,408 (CA1's default-view cap):
//   BC-CL<c>-U / -E / -O   Content-Length: cap - 1 fits (the CONTROL), exactly cap fits, cap + 1 is refused
//   BC-CH<c>-U / -E / -O   chunked: cap - 1 kept whole (the CONTROL), exactly cap kept whole and not over, cap + 1 over
// For cap + 1 only "over" is graded, so each plant (test/host/sabotage_body_cap.py) turns exactly its own rows red.
// Prints "ok [ID]" / "FAIL [ID]" per row. Exit 0 all ok, 1 a row red.
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "BodyCap.h"

static int oks = 0, fails = 0;

static void Row(bool ok, const char* id, const char* what)
{
    std::printf("%s [%s] %s\n", ok ? "ok  " : "FAIL", id, what);
    if (ok)
        ++oks;
    else
        ++fails;
}

// A chunked body of `len` bytes: each byte its index's low 8 bits, then the end (-1).
struct Source {
    size_t len;
    size_t i;
    int operator()() { return i < len ? (int)(i++ & 0xFF) : -1; }
};

static bool KeptWhole(const std::vector<uint8_t>& buf, size_t n)
{
    for (size_t i = 0; i < n; ++i)
        if (buf[i] != (uint8_t)(i & 0xFF)) return false;
    return true;
}

int main()
{
    const size_t caps[2] = {64, 211408};
    const char* tags[2] = {"64", "211"};
    char id[32];
    for (int k = 0; k < 2; ++k) {
        const size_t cap = caps[k];
        std::snprintf(id, sizeof id, "BC-CL%s-U", tags[k]);
        Row(bodycap::Fits(cap - 1, cap), id, "Content-Length, cap - 1: fits (the control)");
        std::snprintf(id, sizeof id, "BC-CL%s-E", tags[k]);
        Row(bodycap::Fits(cap, cap), id, "Content-Length, exactly cap: fits");
        std::snprintf(id, sizeof id, "BC-CL%s-O", tags[k]);
        Row(!bodycap::Fits(cap + 1, cap), id, "Content-Length, cap + 1: refused");

        std::vector<uint8_t> buf(cap);
        size_t n = 0;
        bool over = bodycap::ReadChunked(Source{cap - 1, 0}, buf.data(), cap, &n);
        std::snprintf(id, sizeof id, "BC-CH%s-U", tags[k]);
        Row(!over && n == cap - 1 && KeptWhole(buf, n), id, "chunked, cap - 1: kept whole, not over (the control)");
        n = 0;
        over = bodycap::ReadChunked(Source{cap, 0}, buf.data(), cap, &n);
        std::snprintf(id, sizeof id, "BC-CH%s-E", tags[k]);
        Row(!over && n == cap && KeptWhole(buf, n), id, "chunked, exactly cap: kept whole, not over");
        n = 0;
        over = bodycap::ReadChunked(Source{cap + 1, 0}, buf.data(), cap, &n);
        std::snprintf(id, sizeof id, "BC-CH%s-O", tags[k]);
        Row(over, id, "chunked, cap + 1: over");
    }
    std::printf("%d ok, %d FAIL\n", oks, fails);
    return fails ? 1 : 0;
}
