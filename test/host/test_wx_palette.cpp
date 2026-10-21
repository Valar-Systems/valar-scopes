// Weatherscope's palette (src/weatherscope/WxPalette.h) against the pinned MetPy table and the ruled properties:
// C3 (a)-(j), docs/weatherscope-stage2.md addenda 2, 4, 5 and 8. The table is READ FROM THE COMMITTED FILE here, so the
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

    const int dims[1] = {wxpalette::kDim55};   // rain code 1's one ruled level (addendum 5: final)

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
        if (wxpalette::Rain((uint8_t)code, wxpalette::kDim55) != want) {
            tableOk = false;
            std::printf("        rain %d is %s, the table says %s\n", code, S(wxpalette::Rain((uint8_t)code, 55)).c_str(), S(want).c_str());
        }
    }
    const bool white = wxpalette::Rain(13, wxpalette::kDim55) == Rgb{255, 255, 255};
    check(tableOk && white, "C3c", "rain 2-12 equal the pinned 20-70 dBZ entries; rain 13 is white " + S(wxpalette::Rain(13, 55)));

    // (d) rain code 1 = the table's 15 dBZ entry x the dim factor, (v * p + 50) / 100, at the ruled 55 %.
    bool dimOk = true;
    std::string dimText;
    for (int p : dims) {
        const Rgb t = tbl[2];
        const Rgb want{(uint8_t)((t.r * p + 50) / 100), (uint8_t)((t.g * p + 50) / 100), (uint8_t)((t.b * p + 50) / 100)};
        dimOk &= wxpalette::Rain(1, p) == want;
        dimText += " " + std::to_string(p) + "% " + S(wxpalette::Rain(1, p));
    }
    check(dimOk, "C3d", "rain 1 = the 15 dBZ entry " + S(tbl[2]) + " dimmed:" + dimText);

    // (e) snow luma rises with each code up to code 6, flat after; the top is pale blue, not white.
    bool rises = true;
    for (int c = 2; c <= wxpalette::kSnowTopCode; ++c)
        rises &= Luma(wxpalette::Snow((uint8_t)c)) > Luma(wxpalette::Snow((uint8_t)(c - 1)));
    for (int c = wxpalette::kSnowTopCode + 1; c <= 13; ++c) rises &= wxpalette::Snow((uint8_t)c) == wxpalette::Snow((uint8_t)wxpalette::kSnowTopCode);
    const Rgb top = wxpalette::Snow(13);
    const bool pale = top.b > top.r && top != Rgb{255, 255, 255};
    check(rises && pale, "C3e", "snow rises from " + S(wxpalette::Snow(1)) + " to code " + std::to_string(wxpalette::kSnowTopCode) + " and is flat after; its top " + S(top) +
                                    " is pale blue, not white");

    // (f) no snow colour equals any rain colour, codes 1-13, every dim level.
    int clashes = 0;
    for (int s = 1; s <= 13; ++s)
        for (int q : dims)
            for (int rc = 1; rc <= 13; ++rc)
                if (wxpalette::Snow((uint8_t)s) == wxpalette::Rain((uint8_t)rc, q)) ++clashes;
    check(clashes == 0, "C3f", "no snow colour equals any rain colour (" + std::to_string(clashes) + " clashes)");

    // (g) snow code 1 against rain code 1 at the ruled 55 %: dE*ab >= 40 AND dL* >= 25 (addenda 4 and 5, frozen first).
    bool clear = true;
    std::string gText;
    const Lab s1 = ToLab(wxpalette::Snow(1));
    for (int p : dims) {
        const Lab r1 = ToLab(wxpalette::Rain(1, p));
        const double dE = std::sqrt((s1.L - r1.L) * (s1.L - r1.L) + (s1.a - r1.a) * (s1.a - r1.a) + (s1.b - r1.b) * (s1.b - r1.b));
        const double dL = std::fabs(s1.L - r1.L);
        clear &= dE >= 40.0 && dL >= 25.0;
        char t[96];
        std::snprintf(t, sizeof t, " %d%%: dE %.1f dL %.1f;", p, dE, dL);
        gText += t;
    }
    check(clear, "C3g", "snow 1 " + S(wxpalette::Snow(1)) + " vs rain 1 (dE*ab >= 40 and dL* >= 25):" + gText);

    // (h) adjacent snow codes 1-6 differ by dE*ab >= 10: visible steps where snow sits (addendum 5, frozen first).
    bool steps = true;
    std::string hText;
    for (int c = 1; c < wxpalette::kSnowTopCode; ++c) {
        const Lab a = ToLab(wxpalette::Snow((uint8_t)c)), b2 = ToLab(wxpalette::Snow((uint8_t)(c + 1)));
        const double dE = std::sqrt((a.L - b2.L) * (a.L - b2.L) + (a.a - b2.a) * (a.a - b2.a) + (a.b - b2.b) * (a.b - b2.b));
        steps &= dE >= 10.0;
        char t[32];
        std::snprintf(t, sizeof t, " %d-%d: %.1f;", c, c + 1, dE);
        hText += t;
    }
    check(steps, "C3h", "adjacent snow codes 1-6 differ by dE*ab >= 10:" + hText);

    // (i) snow code 13 against rain code 13 (white): dE*ab >= 20 (addendum 5, frozen first).
    const Lab s13 = ToLab(wxpalette::Snow(13)), r13 = ToLab(wxpalette::Rain(13, wxpalette::kDim55));
    const double dE13 = std::sqrt((s13.L - r13.L) * (s13.L - r13.L) + (s13.a - r13.a) * (s13.a - r13.a) + (s13.b - r13.b) * (s13.b - r13.b));
    char t13[96];
    std::snprintf(t13, sizeof t13, "snow 13 %s vs rain 13 %s: dE*ab %.1f (>= 20)", S(wxpalette::Snow(13)).c_str(),
                  S(wxpalette::Rain(13, 55)).c_str(), dE13);
    check(dE13 >= 20.0, "C3i", t13);

    // (j) rain 12 (violet) against no coverage (14): dE*ab >= 40 -- a CONFIRMATION (addendum 8): they looked alike in a
    // low-light photo; the palette is final, so a failure is reported, never answered by changing a colour.
    const Lab v12 = ToLab(wxpalette::Rain(12, wxpalette::kDim55)), nc = ToLab(wxpalette::kNoCoverage);
    const double dEj = std::sqrt((v12.L - nc.L) * (v12.L - nc.L) + (v12.a - nc.a) * (v12.a - nc.a) + (v12.b - nc.b) * (v12.b - nc.b));
    char tj[112];
    std::snprintf(tj, sizeof tj, "rain 12 %s vs no coverage %s: dE*ab %.1f (>= 40)", S(wxpalette::Rain(12, 55)).c_str(),
                  S(wxpalette::kNoCoverage).c_str(), dEj);
    check(dEj >= 40.0, "C3j", tj);

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
