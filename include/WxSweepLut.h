#pragma once
// P5 1b, the sweep blend (docs/speed-p5p6.md section 1b, on weatherscope/speed-p5p6): the lit wedge's per-pixel colour
// as ONE table lookup, byte-identical to wxsweep::Blend BY CONSTRUCTION.
//
// A wedge pixel's colour is Lit565(code, pct, SliceLevel(mark)): mark is 1..kSegs (Rasterise writes nothing else),
// pct is the frame's look percent, and only code varies per pixel. So the table holds Lit565 itself for every mark and
// every code 0-15, built by the same two functions, and is rebuilt only when pct changes. A code past the table (the
// layer's kOffDisc, 255) or a mark past kSegs takes Lit565 directly: exact, never masked into range.
//
// A separate header so include/WxSweep.h stays as it is: wxsweep::Blend is this file's reference (test_p5_blend.cpp's
// W5c rows), and test/host/sabotage_wx_sweep.py's plants and anchors live in that file.
//
// ONE TABLE SERVES BOTH BLENDS (REG-1b-main). The map's blend (include/MapSweep.h, mapsweep::Blend's Table overload)
// reads v for a drawn code and lvl, SliceLevel per mark, for the green it adds to the map pixel; mapsweep::Blend's pct
// overload is that one's reference (test_p5_blend.cpp's W5d rows). AircraftManager.cpp keeps one Table for both paths.
#include <stddef.h>
#include <stdint.h>

#include "WxSweep.h"

namespace wxsweeplut {

constexpr int kCodes = 16;   // codes 0-15 come from the table; anything else is computed

struct Table {
    int pct = -1;                                   // the look percent it was built for; -1: never built
    uint16_t v[wxsweep::kSegs + 1][kCodes] = {};    // [mark][code]; row 0 unused (mark 0 is outside the wedge)
    uint8_t lvl[wxsweep::kSegs + 1] = {};           // [mark]: SliceLevel, for the map's added green; row 0 unused
};

inline void Build(Table& t, int pct)
{
    for (int m = 1; m <= wxsweep::kSegs; ++m) {
        const uint8_t lvl = wxsweep::SliceLevel(m);
        t.lvl[m] = lvl;
        for (int c = 0; c < kCodes; ++c) t.v[m][c] = wxsweep::Lit565((uint8_t)c, pct, lvl);
    }
    t.pct = pct;
}

// The table for this frame's percent: rebuilt only when it changed.
inline const Table& For(Table& t, int pct)
{
    if (t.pct != pct) Build(t, pct);
    return t;
}

/** wxsweep::Blend with the table: the same visits, the same writes, the same cleared marks and count. */
template <typename Set, typename Code>
inline uint32_t Blend(const Table& t, uint8_t* slice, int w, int yLo, int yHi, const int16_t* rowLo,
                      const int16_t* rowHi, Set&& set, Code&& code)
{
    uint32_t n = 0;
    for (int y = yLo; y <= yHi; ++y) {
        uint8_t* row = slice + (size_t)y * w;
        for (int x = rowLo[y]; x <= rowHi[y]; ++x) {
            const uint8_t mark = row[x];
            if (mark == 0) continue;
            const uint8_t c = code(x, y);
            if (mark <= wxsweep::kSegs && c < kCodes) set(x, y, t.v[mark][c]);
            else set(x, y, wxsweep::Lit565(c, t.pct, wxsweep::SliceLevel(mark)));
            row[x] = 0;
            ++n;
        }
    }
    return n;
}

}  // namespace wxsweeplut
