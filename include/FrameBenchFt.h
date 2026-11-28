#pragma once
// THE v20 SIBLING's FT rows (docs/frame-time-bench.md, "FT on the sibling" and "THE REBUILD ROW"): each row's pinned
// packages, in its pin list's order (test/fixtures/frame-bench/ft_sea.pins, ft_hr.pins), and its scene table.
// Included by FrameBench.h only where FRAME_BENCH_FT is defined, after Kind, Role and Scene.
// The packages are embedded by the FT env's board_build.embed_files from .pio/ftpkg/, where scripts/ft_prebuild.py
// copies them after FT-PKG and FT-CTR pass on the env's pin list. A mismatch fails the build; a symbol that does not
// match the embed list fails the link. No coordinate is written here: each scene's home is its package's own centre.
// It is included INSIDE namespace framebench, so it includes nothing itself (FrameBench.h has stdint/stddef already).
// The asm labels fix each symbol's exact name, so the namespace does not change what the linker matches.

struct FtPkg {
    const char* point;
    const char* variant;
    const char* sha12;   // the pin's sha256 prefix, printed in the scene line so a reading names its package
    const uint8_t* start;
    const uint8_t* end;
};

#define FRAME_BENCH_FT_PKG(id, sym)                                                     \
    extern const uint8_t id##Start[] asm("_binary__pio_ftpkg_" sym "_bmpk_start");      \
    extern const uint8_t id##End[] asm("_binary__pio_ftpkg_" sym "_bmpk_end");

#if defined(FRAME_BENCH_FT_SEA)
// FT-SEA (docs/basemap-dense-code.md; FT-MAP: the weather OFF): ft_sea.pins, in order
FRAME_BENCH_FT_PKG(ftKjfkNosea, "KJFK_nosea")
FRAME_BENCH_FT_PKG(ftKjfkSea, "KJFK_sea")
FRAME_BENCH_FT_PKG(ftKbosNosea, "KBOS_nosea")
FRAME_BENCH_FT_PKG(ftKbosSea, "KBOS_sea")
FRAME_BENCH_FT_PKG(ftKordNosea, "KORD_nosea")
FRAME_BENCH_FT_PKG(ftKordSea, "KORD_sea")
static const FtPkg kFtPkgs[] = {
    {"KJFK", "nosea", "ee8c834bc81a", ftKjfkNoseaStart, ftKjfkNoseaEnd},
    {"KJFK", "sea", "52ac9dc39de0", ftKjfkSeaStart, ftKjfkSeaEnd},
    {"KBOS", "nosea", "ed5393b4fc0f", ftKbosNoseaStart, ftKbosNoseaEnd},
    {"KBOS", "sea", "f21d705d8b8a", ftKbosSeaStart, ftKbosSeaEnd},
    {"KORD", "nosea", "4238243a57a6", ftKordNoseaStart, ftKordNoseaEnd},
    {"KORD", "sea", "cb59bb06a506", ftKordSeaStart, ftKordSeaEnd},
};
constexpr uint8_t kFtAA = 5;   // KORD's A/A package (cb59bb06a506, the same bytes twice)
#define FT_K FT_MAP
// KORD's A/A pair 1 at each rung; the plant; each point's baseline (no-sea) then option (sea) at each rung; pair 2.
static const Scene kScenes[] = {
    {FT_K, kFtAA, 10, PAIR1}, {FT_K, kFtAA, 10, PAIR1}, {FT_K, kFtAA, 25, PAIR1}, {FT_K, kFtAA, 25, PAIR1},
    {FT_K, kFtAA, 50, PAIR1}, {FT_K, kFtAA, 50, PAIR1},
    {FT_K, kFtAA, 25, PLANT},
    {FT_K, 0, 10, GRADED}, {FT_K, 1, 10, GRADED}, {FT_K, 0, 25, GRADED}, {FT_K, 1, 25, GRADED},
    {FT_K, 0, 50, GRADED}, {FT_K, 1, 50, GRADED},
    {FT_K, 2, 10, GRADED}, {FT_K, 3, 10, GRADED}, {FT_K, 2, 25, GRADED}, {FT_K, 3, 25, GRADED},
    {FT_K, 2, 50, GRADED}, {FT_K, 3, 50, GRADED},
    {FT_K, 4, 10, GRADED}, {FT_K, 5, 10, GRADED}, {FT_K, 4, 25, GRADED}, {FT_K, 5, 25, GRADED},
    {FT_K, 4, 50, GRADED}, {FT_K, 5, 50, GRADED},
    {FT_K, kFtAA, 10, PAIR2}, {FT_K, kFtAA, 10, PAIR2}, {FT_K, kFtAA, 25, PAIR2}, {FT_K, kFtAA, 25, PAIR2},
    {FT_K, kFtAA, 50, PAIR2}, {FT_K, kFtAA, 50, PAIR2},
};
#undef FT_K
#elif defined(FRAME_BENCH_FT_HR)
// FT-HR (docs/basemap-hudson-rule.md; FT-WX: map AND weather ON): ft_hr.pins, in order
FRAME_BENCH_FT_PKG(ftKewrA0, "KEWR_A0")
FRAME_BENCH_FT_PKG(ftMidtownA0, "MIDTOWN_A0")
FRAME_BENCH_FT_PKG(ftKjfkA0, "KJFK_A0")
FRAME_BENCH_FT_PKG(ftKlgaA0, "KLGA_A0")
FRAME_BENCH_FT_PKG(ftKbosA0, "KBOS_A0")
FRAME_BENCH_FT_PKG(ftKordA0, "KORD_A0")
FRAME_BENCH_FT_PKG(ftKewr2L, "KEWR_2L194")
FRAME_BENCH_FT_PKG(ftMidtown2L, "MIDTOWN_2L194")
FRAME_BENCH_FT_PKG(ftKjfk2L, "KJFK_2L194")
FRAME_BENCH_FT_PKG(ftKlga2L, "KLGA_2L194")
FRAME_BENCH_FT_PKG(ftKbos2L, "KBOS_2L194")
static const FtPkg kFtPkgs[] = {
    {"KEWR", "A0", "dff28487eaf4", ftKewrA0Start, ftKewrA0End},
    {"MIDTOWN", "A0", "20a22bed5510", ftMidtownA0Start, ftMidtownA0End},
    {"KJFK", "A0", "52ac9dc39de0", ftKjfkA0Start, ftKjfkA0End},
    {"KLGA", "A0", "feb6ef3bdf04", ftKlgaA0Start, ftKlgaA0End},
    {"KBOS", "A0", "f21d705d8b8a", ftKbosA0Start, ftKbosA0End},
    {"KORD", "A0", "cb59bb06a506", ftKordA0Start, ftKordA0End},
    {"KEWR", "2L194", "c619f4a89f14", ftKewr2LStart, ftKewr2LEnd},
    {"MIDTOWN", "2L194", "aaede1785364", ftMidtown2LStart, ftMidtown2LEnd},
    {"KJFK", "2L194", "e042d61aa8f1", ftKjfk2LStart, ftKjfk2LEnd},
    {"KLGA", "2L194", "6ab44cd8fbea", ftKlga2LStart, ftKlga2LEnd},
    {"KBOS", "2L194", "696c6d00122d", ftKbos2LStart, ftKbos2LEnd},
};
constexpr uint8_t kFtAA = 5;   // KORD-A0 (cb59bb06a506), the same bytes twice
#define FT_K FT_WX
// KORD's A/A pair 1 at each rung; the plant; each point's run 10 (A0) then option 2-L at 19.4 at each rung (KORD's
// own pair is the same bytes, as registered); pair 2.
static const Scene kScenes[] = {
    {FT_K, kFtAA, 10, PAIR1}, {FT_K, kFtAA, 10, PAIR1}, {FT_K, kFtAA, 25, PAIR1}, {FT_K, kFtAA, 25, PAIR1},
    {FT_K, kFtAA, 50, PAIR1}, {FT_K, kFtAA, 50, PAIR1},
    {FT_K, kFtAA, 25, PLANT},
    {FT_K, 0, 10, GRADED}, {FT_K, 6, 10, GRADED}, {FT_K, 0, 25, GRADED}, {FT_K, 6, 25, GRADED},
    {FT_K, 0, 50, GRADED}, {FT_K, 6, 50, GRADED},
    {FT_K, 1, 10, GRADED}, {FT_K, 7, 10, GRADED}, {FT_K, 1, 25, GRADED}, {FT_K, 7, 25, GRADED},
    {FT_K, 1, 50, GRADED}, {FT_K, 7, 50, GRADED},
    {FT_K, 2, 10, GRADED}, {FT_K, 8, 10, GRADED}, {FT_K, 2, 25, GRADED}, {FT_K, 8, 25, GRADED},
    {FT_K, 2, 50, GRADED}, {FT_K, 8, 50, GRADED},
    {FT_K, 3, 10, GRADED}, {FT_K, 9, 10, GRADED}, {FT_K, 3, 25, GRADED}, {FT_K, 9, 25, GRADED},
    {FT_K, 3, 50, GRADED}, {FT_K, 9, 50, GRADED},
    {FT_K, 4, 10, GRADED}, {FT_K, 10, 10, GRADED}, {FT_K, 4, 25, GRADED}, {FT_K, 10, 25, GRADED},
    {FT_K, 4, 50, GRADED}, {FT_K, 10, 50, GRADED},
    {FT_K, 5, 10, GRADED}, {FT_K, 5, 10, GRADED}, {FT_K, 5, 25, GRADED}, {FT_K, 5, 25, GRADED},
    {FT_K, 5, 50, GRADED}, {FT_K, 5, 50, GRADED},
    {FT_K, kFtAA, 10, PAIR2}, {FT_K, kFtAA, 10, PAIR2}, {FT_K, kFtAA, 25, PAIR2}, {FT_K, kFtAA, 25, PAIR2},
    {FT_K, kFtAA, 50, PAIR2}, {FT_K, kFtAA, 50, PAIR2},
};
#undef FT_K
#else
#error "FRAME_BENCH_FT needs FRAME_BENCH_FT_SEA or FRAME_BENCH_FT_HR"
#endif
#undef FRAME_BENCH_FT_PKG

constexpr int kFtNumPkgs = (int)(sizeof(kFtPkgs) / sizeof(kFtPkgs[0]));
inline const uint8_t* FtPkgStart(int i) { return (i >= 0 && i < kFtNumPkgs) ? kFtPkgs[i].start : nullptr; }
inline size_t FtPkgLen(int i) { return (i >= 0 && i < kFtNumPkgs) ? (size_t)(kFtPkgs[i].end - kFtPkgs[i].start) : 0; }
