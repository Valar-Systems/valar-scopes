// Board addendum 2's scratch table (docs/speed-p5p6.md section 7, on weatherscope/speed-p5p6): the HOST side of the
// device's scratch self-check (include/OutlineScratchCheck.h). The host scratch is LovyanGFX's raster as ported in
// test/host/LgfxPort.h, on a 32 x 32 bit grid clipped as a sprite would be (the same stand-in test_p5_outline uses).
//
//   scratch_table --emit       print kTable's 32 lines, to paste between BEGIN TABLE and END TABLE
//   scratch_table --selftest   exit 0 every row ok, 1 a row failed
//
// Build (from the repo root): g++ -std=c++11 -Wall -Wextra -Werror -O1 -Iinclude -Itest/host
//                             tools/speed-p5p6/scratch_table.cpp -o scratch_table
//
// Rows (tools/speed-p5p6/sabotage_scratch.py reads the ids):
//   S-COUNT        CONTROL: the pattern sets some bits and not all, and rows 0, 1, 30 and 31 are empty
//   S-ROWS         one pixel at (9, 3) lands in byte 4 * 3 + 1 as 0x02 and nowhere else
//   S-SELF         the device-side comparator, host-compiled, over the host scratch and the header's table, reads 0
//   S-TABLE        the same with the table's LAST byte's bit 7 flipped reads 1 (the registered plant)
//   S-TABLE-FIRST  the same with the FIRST byte's bit 0 flipped reads 1
#include <cstdio>
#include <cstring>
#include <string>

#include "LgfxPort.h"
#include "OutlineScratchCheck.h"

namespace {

struct Bits {
    uint32_t row[outlinemask::kSide];
    void Set(int32_t x, int32_t y, uint32_t c)
    {
        if (c) row[y] |= 1u << x;
        else row[y] &= ~(1u << x);
    }
};
struct HostScratch {
    Bits b;
    lgfxport::Port<Bits> port{b, 0, 0, outlinemask::kSide - 1, outlinemask::kSide - 1};
    bool Begin() { std::memset(b.row, 0, sizeof b.row); return true; }
    uint32_t On() const { return 1u; }
    uint32_t Row(int r) const { return b.row[r]; }
    template <class T> void fillTriangle(int32_t a, int32_t c, int32_t d, int32_t e, int32_t f, int32_t g, const T& k) { port.fillTriangle(a, c, d, e, f, g, k); }
    template <class T> void drawLine(int32_t a, int32_t c, int32_t d, int32_t e, const T& k) { port.drawLine(a, c, d, e, k); }
    template <class T> void fillCircle(int32_t a, int32_t c, int32_t r, const T& k) { port.fillCircle(a, c, r, k); }
    template <class T> void drawCircle(int32_t a, int32_t c, int32_t r, const T& k) { port.drawCircle(a, c, r, k); }
};

int failures = 0;
void Row(bool ok, const char* id, const std::string& what)
{
    if (!ok) ++failures;
    std::printf("  %s  %s  %s\n", ok ? "ok  " : "FAIL", id, what.c_str());
}

int Emit()
{
    HostScratch s;
    s.Begin();
    scratchcheck::DrawPattern(s);
    for (int r = 0; r < outlinemask::kSide; ++r)
        std::printf("    0x%02x, 0x%02x, 0x%02x, 0x%02x,\n", scratchcheck::Byte(s, r, 0), scratchcheck::Byte(s, r, 1),
                    scratchcheck::Byte(s, r, 2), scratchcheck::Byte(s, r, 3));
    return 0;
}

int SelfTest()
{
    HostScratch s;
    s.Begin();
    scratchcheck::DrawPattern(s);
    // S-COUNT (control)
    {
        int bits = 0;
        for (int r = 0; r < outlinemask::kSide; ++r) bits += __builtin_popcount(s.Row(r));
        const bool edges = !s.Row(0) && !s.Row(1) && !s.Row(30) && !s.Row(31);
        Row(bits > 0 && bits < outlinemask::kSide * outlinemask::kSide && edges, "S-COUNT",
            "CONTROL: " + std::to_string(bits) + " of 1024 bits set; rows 0, 1, 30, 31 " + (edges ? "empty" : "NOT empty"));
    }
    // S-ROWS
    {
        HostScratch one;
        one.Begin();
        one.port.drawFastHLine(9, 3, 1, 1u);   // one pixel, through a colour-taking form the port has
        int others = 0;
        for (int i = 0; i < scratchcheck::kBytes; ++i)
            if (i != 4 * 3 + 1 && scratchcheck::Byte(one, i / 4, i % 4)) ++others;
        Row(scratchcheck::Byte(one, 3, 1) == 0x02 && others == 0, "S-ROWS",
            "pixel (9, 3): byte 13 = 0x02, " + std::to_string(others) + " other byte(s) set (want 0)");
    }
    // S-SELF
    {
        const int m = scratchcheck::Mismatches(s, scratchcheck::kTable);
        Row(m == 0, "S-SELF", "header table vs host scratch: mismatches " + std::to_string(m) + " (want 0)");
    }
    // S-TABLE, S-TABLE-FIRST
    {
        uint8_t t[scratchcheck::kBytes];
        std::memcpy(t, scratchcheck::kTable, sizeof t);
        t[scratchcheck::kBytes - 1] ^= 0x80;
        const int m = scratchcheck::Mismatches(s, t);
        Row(m == 1, "S-TABLE", "last byte's bit 7 flipped: mismatches " + std::to_string(m) + " (want 1)");
        std::memcpy(t, scratchcheck::kTable, sizeof t);
        t[0] ^= 0x01;
        const int m0 = scratchcheck::Mismatches(s, t);
        Row(m0 == 1, "S-TABLE-FIRST", "first byte's bit 0 flipped: mismatches " + std::to_string(m0) + " (want 1)");
    }
    std::printf("scratch_table selftest: %d FAIL\n", failures);
    return failures ? 1 : 0;
}

}  // namespace

int main(int argc, char** argv)
{
    const std::string a = argc == 2 ? argv[1] : "";
    if (a == "--emit") return Emit();
    if (a == "--selftest") return SelfTest();
    std::printf("usage: scratch_table --emit | --selftest\n");
    return 2;
}
