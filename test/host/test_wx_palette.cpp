// Weatherscope's palette (src/weatherscope/WxPalette.h) against the pinned MetPy table and the ruled properties:
// C3 (a)-(l), docs/weatherscope-stage2.md addenda 2, 4, 5 and 8, re-registered for code 1's teal by
// docs/weatherscope-stage3.md A10.16. The table is READ FROM THE COMMITTED FILE here, so the
// header and the pin cannot drift apart unnoticed.
//
//   test_wx_palette.exe <fixtures-dir>        exit 0 all pass, 1 a check failed, 2 the rig (file unreadable)
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "WxPalette.h"

using wxpalette::Rgb;

static int failures = 0;
static void check(bool ok, const char* id, const std::string& what)
{
    if (!ok) ++failures;
    std::printf("  %s  %s %s\n", ok ? "ok  " : "FAIL", id, what.c_str());
}

static std::string S(Rgb c)
{
    char b[32];
    std::snprintf(b, sizeof b, "(%u,%u,%u)", c.r, c.g, c.b);
    return b;
}

static bool Achromatic(Rgb c) { return c.r == c.g && c.g == c.b; }
static int Luma(Rgb c) { return 299 * c.r + 587 * c.g + 114 * c.b; }

// C3g's metric, frozen in addendum 4: CIE76 dE*ab. sRGB (IEC 61966-2-1) -> linear -> XYZ -> CIELAB, D65 white.
struct Lab { double L, a, b; };
static double Lin(uint8_t v)
{
    const double c = v / 255.0;
    return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
}
static double F(double t)
{
    const double d = 6.0 / 29.0;
    return t > d * d * d ? std::cbrt(t) : t / (3 * d * d) + 4.0 / 29.0;
}
static Lab ToLab(Rgb c)
{
    const double R = Lin(c.r), G = Lin(c.g), B = Lin(c.b);
    const double X = 0.4124564 * R + 0.3575761 * G + 0.1804375 * B;
    const double Y = 0.2126729 * R + 0.7151522 * G + 0.0721750 * B;
    const double Z = 0.0193339 * R + 0.1191920 * G + 0.9503041 * B;
    const double fx = F(X / 0.95047), fy = F(Y / 1.0), fz = F(Z / 1.08883);
    return Lab{116 * fy - 16, 500 * (fx - fy), 200 * (fy - fz)};
}

// The pin as a CHECK, not prose (#439 review): the table file's sha256, with CRLF folded to LF -- core.autocrlf
// rewrites the working copy; the committed blob is LF. A compact SHA-256 (FIPS 180-4), controlled below against
// the standard "abc" vector so a broken hash cannot pass as a matching one.
static const char* kPin = "f73f0d6264d420cb55efe79d58415ca056391a0cc607fa0efa061f3331418e2f";
static std::string Sha256Hex(const std::string& msg)
{
    static const uint32_t K[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
        0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
        0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
        0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
        0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
    uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    std::string m = msg;
    const uint64_t bits = (uint64_t)msg.size() * 8;
    m += (char)0x80;
    while (m.size() % 64 != 56) m += (char)0;
    for (int i = 7; i >= 0; --i) m += (char)((bits >> (i * 8)) & 0xff);
    auto rotr = [](uint32_t x, int n) { return (x >> n) | (x << (32 - n)); };
    for (size_t off = 0; off < m.size(); off += 64) {
        uint32_t w[64];
        for (int i = 0; i < 16; ++i)
            w[i] = (uint32_t)(uint8_t)m[off + 4 * i] << 24 | (uint32_t)(uint8_t)m[off + 4 * i + 1] << 16 |
                   (uint32_t)(uint8_t)m[off + 4 * i + 2] << 8 | (uint32_t)(uint8_t)m[off + 4 * i + 3];
        for (int i = 16; i < 64; ++i) {
            const uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], k = h[7];
        for (int i = 0; i < 64; ++i) {
            const uint32_t t1 = k + (rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25)) + ((e & f) ^ (~e & g)) + K[i] + w[i];
            const uint32_t t2 = (rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
            k = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += k;
    }
    char out[65];
    for (int i = 0; i < 8; ++i) std::snprintf(out + 8 * i, 9, "%08x", (unsigned)h[i]);
    return std::string(out, 64);
}

int main(int argc, char** argv)
{
    if (argc < 2) { std::printf("usage: test_wx_palette.exe <fixtures-dir>\n"); return 2; }
    const std::string path = std::string(argv[1]) + "/NWSReflectivity-metpy-v1.6.3.tbl";
    FILE* f = std::fopen(path.c_str(), "r");
    if (!f) { std::printf("FAIL: cannot read %s (rig)\n", path.c_str()); return 2; }
    std::vector<Rgb> tbl;   // the file's entries, 5 dBZ steps from 5 dBZ; each value is k/255
    double r, g, b;
    while (std::fscanf(f, " (%lf, %lf, %lf)", &r, &g, &b) == 3)
        tbl.push_back(Rgb{(uint8_t)(r * 255 + 0.5), (uint8_t)(g * 255 + 0.5), (uint8_t)(b * 255 + 0.5)});
    std::fclose(f);
    if (tbl.size() != 15) { std::printf("FAIL: the pinned table has %u entries, not 15 (rig)\n", (unsigned)tbl.size()); return 2; }

    // Rain code 1 is TEAL (0, 150, 135), a ruled exception beside code 13's white (Daniel, 2026-10-28;
    // docs/weatherscope-stage3.md A10.16). The 55 % dim of the table's 15 dBZ blue retired with it, and so did the
    // dim levels these checks used to loop over.
    const Rgb kTeal{0, 150, 135};
    const Lab r1 = ToLab(wxpalette::Rain(1));
    auto dEof = [](Lab x, Lab y) {
        return std::sqrt((x.L - y.L) * (x.L - y.L) + (x.a - y.a) * (x.a - y.a) + (x.b - y.b) * (x.b - y.b));
    };

    // (a) no coverage and the missing hatch are achromatic: "never violet" as a property, not a promise.
    check(Achromatic(wxpalette::kNoCoverage) && Achromatic(wxpalette::kMissingHatch),
          "C3a", "no coverage " + S(wxpalette::kNoCoverage) + " and the missing hatch " + S(wxpalette::kMissingHatch) +
                     " are achromatic");

    // (b) dry, the missing hatch and no coverage are three different colours.
    check(wxpalette::kDry != wxpalette::kMissingHatch && wxpalette::kDry != wxpalette::kNoCoverage &&
              wxpalette::kMissingHatch != wxpalette::kNoCoverage,
          "C3b", "dry, the missing hatch and no coverage are three different colours");

    // (c) rain 2-12 equal the pinned table's 20-70 dBZ entries; code 13 is white.
    bool tableOk = true;
    for (int code = 2; code <= 12; ++code) {
        const Rgb want = tbl[(size_t)code + 1];   // code 1 = 15 dBZ = entry index 2
        if (wxpalette::Rain((uint8_t)code) != want) {
            tableOk = false;
            std::printf("        rain %d is %s, the table says %s\n", code, S(wxpalette::Rain((uint8_t)code)).c_str(), S(want).c_str());
        }
    }
    const bool white = wxpalette::Rain(13) == Rgb{255, 255, 255};
    check(tableOk && white, "C3c", "rain 2-12 equal the pinned 20-70 dBZ entries; rain 13 is white " + S(wxpalette::Rain(13)));

    // (d) re-registered (A10.16): rain code 1 is exactly the ruled teal. The pinned table's 15 dBZ entry is still its
    // own (the pin below covers the file); code 1 simply no longer draws it.
    check(wxpalette::Rain(1) == kTeal, "C3d", "rain 1 is the ruled teal " + S(kTeal) + ": " + S(wxpalette::Rain(1)) +
                                                  " (the table's 15 dBZ entry " + S(tbl[2]) + " no longer drawn)");

    // (e) snow luma rises with each code up to code 6, flat after; the top is pale blue, not white.
    bool rises = true;
    for (int c = 2; c <= wxpalette::kSnowTopCode; ++c)
        rises &= Luma(wxpalette::Snow((uint8_t)c)) > Luma(wxpalette::Snow((uint8_t)(c - 1)));
    for (int c = wxpalette::kSnowTopCode + 1; c <= 13; ++c) rises &= wxpalette::Snow((uint8_t)c) == wxpalette::Snow((uint8_t)wxpalette::kSnowTopCode);
    const Rgb top = wxpalette::Snow(13);
    const bool pale = top.b > top.r && top != Rgb{255, 255, 255};
    check(rises && pale, "C3e", "snow rises from " + S(wxpalette::Snow(1)) + " to code " + std::to_string(wxpalette::kSnowTopCode) + " and is flat after; its top " + S(top) +
                                    " is pale blue, not white");

    // (f) no snow colour equals any rain colour, codes 1-13.
    int clashes = 0;
    for (int s = 1; s <= 13; ++s)
        for (int rc = 1; rc <= 13; ++rc)
            if (wxpalette::Snow((uint8_t)s) == wxpalette::Rain((uint8_t)rc)) ++clashes;
    check(clashes == 0, "C3f", "no snow colour equals any rain colour (" + std::to_string(clashes) + " clashes)");

    // (g) re-registered (A10.16): snow code 1 against rain code 1 (teal): dE*ab >= 40. The old "and dL* >= 25" cannot
    // hold for teal, which differs from snow 1 in hue, not lightness (dL* 8.9); it is printed, not graded.
    const Lab s1 = ToLab(wxpalette::Snow(1));
    const double dEg = dEof(s1, r1), dLg = std::fabs(s1.L - r1.L);
    char tg[112];
    std::snprintf(tg, sizeof tg, "snow 1 %s vs rain 1 %s: dE*ab %.1f (>= 40); dL* %.1f (printed only)",
                  S(wxpalette::Snow(1)).c_str(), S(wxpalette::Rain(1)).c_str(), dEg, dLg);
    check(dEg >= 40.0, "C3g", tg);

    // (h) adjacent snow codes 1-6 differ by dE*ab >= 10: visible steps where snow sits (addendum 5, frozen first).
    bool steps = true;
    std::string hText;
    for (int c = 1; c < wxpalette::kSnowTopCode; ++c) {
        const Lab a = ToLab(wxpalette::Snow((uint8_t)c)), b2 = ToLab(wxpalette::Snow((uint8_t)(c + 1)));
        const double dE = dEof(a, b2);
        steps &= dE >= 10.0;
        char t[32];
        std::snprintf(t, sizeof t, " %d-%d: %.1f;", c, c + 1, dE);
        hText += t;
    }
    check(steps, "C3h", "adjacent snow codes 1-6 differ by dE*ab >= 10:" + hText);

    // (i) snow code 13 against rain code 13 (white): dE*ab >= 20 (addendum 5, frozen first).
    const double dE13 = dEof(ToLab(wxpalette::Snow(13)), ToLab(wxpalette::Rain(13)));
    char t13[96];
    std::snprintf(t13, sizeof t13, "snow 13 %s vs rain 13 %s: dE*ab %.1f (>= 20)", S(wxpalette::Snow(13)).c_str(),
                  S(wxpalette::Rain(13)).c_str(), dE13);
    check(dE13 >= 20.0, "C3i", t13);

    // (j) rain 12 (violet) against no coverage (14): dE*ab >= 40 -- a CONFIRMATION (addendum 8): they looked alike in a
    // low-light photo; the palette is final, so a failure is reported, never answered by changing a colour.
    const Lab nc = ToLab(wxpalette::kNoCoverage), hatch = ToLab(wxpalette::kMissingHatch);
    const double dEj = dEof(ToLab(wxpalette::Rain(12)), nc);
    char tj[112];
    std::snprintf(tj, sizeof tj, "rain 12 %s vs no coverage %s: dE*ab %.1f (>= 40)", S(wxpalette::Rain(12)).c_str(),
                  S(wxpalette::kNoCoverage).c_str(), dEj);
    check(dEj >= 40.0, "C3j", tj);

    // (k) new (A10.16): rain 1 (teal) against no coverage and against the hatch: dE*ab >= 40 each.
    const double dEk1 = dEof(r1, nc), dEk2 = dEof(r1, hatch);
    char tk[128];
    std::snprintf(tk, sizeof tk, "rain 1 %s vs no coverage: dE*ab %.1f; vs the hatch: dE*ab %.1f (each >= 40)",
                  S(wxpalette::Rain(1)).c_str(), dEk1, dEk2);
    check(dEk1 >= 40.0 && dEk2 >= 40.0, "C3k", tk);

    // (l) new (A10.16): rain 1's lightness stays below rain 2's.
    const Lab r2 = ToLab(wxpalette::Rain(2));
    char tl[96];
    std::snprintf(tl, sizeof tl, "rain 1 L* %.1f < rain 2 %s L* %.1f", r1.L, S(wxpalette::Rain(2)).c_str(), r2.L);
    check(r1.L < r2.L, "C3l", tl);

    // The pin, as a check (#439 review): the file's bytes, CRLF folded to LF, hash to the pinned sha256. An id appended
    // to the table, or any byte changed, fails it -- the guards' pin and this one read the same number.
    std::string bytes;
    if (FILE* fb = std::fopen(path.c_str(), "rb")) {
        int ch;
        while ((ch = std::fgetc(fb)) != EOF) bytes += (char)ch;
        std::fclose(fb);
    }
    std::string lf;
    for (size_t i = 0; i < bytes.size(); ++i)
        if (!(bytes[i] == (char)13 && i + 1 < bytes.size() && bytes[i + 1] == (char)10)) lf += bytes[i];
    const std::string got = Sha256Hex(lf);
    check(got == kPin && tbl[14] == Rgb{0, 0, 0}, "pin", "the table (CRLF folded) hashes to " + got.substr(0, 12) +
                                                            "... (pin f73f0d6264d4...); its 75 dBZ entry is " + S(tbl[14]));
    // And the hash itself is controlled: the FIPS 180-2 "abc" vector.
    check(Sha256Hex("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "sha",
          "SHA-256(\"abc\") is the standard vector (the pin check's arithmetic is wired right)");
    // And one for (g)'s arithmetic: sRGB white is L* 100, a* 0, b* 0 to within rounding.
    const Lab w = ToLab(Rgb{255, 255, 255});
    check(std::fabs(w.L - 100) < 0.01 && std::fabs(w.a) < 0.01 && std::fabs(w.b) < 0.01, "lab",
          "sRGB white converts to L* 100, a* 0, b* 0 (the dE arithmetic is wired right)");
    std::printf("%s\n", failures ? "WX PALETTE TESTS FAILED" : "all WX palette checks passed");
    return failures ? 1 : 0;
}
