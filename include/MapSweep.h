#pragma once
// Step C (docs/blipscope-map-client-steps.md, "Step C: the wedge's green added to the map"; S7 in
// docs/blipscope-basemap-client.md, Q-MC2): the sweep over the map. The wedge's green is ADDED to the pixel under it,
// so the map stays visible under the beam; over black the result is today's opaque wedge, bit for bit.
//
// WxSweep.h is the weather's (WD6) and is not touched (Q-C2): this header reuses its rasteriser, its slice levels and
// its look-4 colour unchanged, and differs in one rule only. Where WxSweep's BlendOne writes Green565(lvl) over a pixel
// that holds no drawn weather code, this one writes Add565(the pixel, lvl). A drawn code is Lit565, exactly as there.
// Pure C++11, no Arduino: test/host/test_map_sweep.cpp grades it (CS1-CS6); AircraftManager::DrawSweepOverMap runs it.
#include <stdint.h>

#include "WxSweep.h"

// CB10 (src/AircraftManager.h) refuses every map build without this macro, and check_map_draw.py's CD4 allows it to be
// defined here and nowhere else.
#define BLIPSCOPE_MAP_STEP_C 1

namespace mapsweep {

/** S7 (Q-C1): the wedge's green added to a logical RGB565 pixel. The 6-bit green rises by lvl >> 2, saturating at 63;
 *  red and blue are kept. Over black this is (lvl >> 2) << 5, which is wxsweep::Green565(lvl) for every lvl (CS1). */
inline uint16_t Add565(uint16_t px, uint8_t lvl)
{
    const unsigned g = ((px >> 5) & 0x3Fu) + ((unsigned)lvl >> 2);
    return (uint16_t)((px & 0xF81Fu) | ((g > 63u ? 63u : g) << 5));
}

namespace detail {
// One wedge pixel's colour: a drawn weather code as the weather's wedge draws it, anything else the pixel plus green.
template <typename Set, typename Get, typename Code>
inline void BlendOne(int pct, uint8_t mark, int x, int y, Set& set, Get& get, Code& code)
{
    const uint8_t lvl = wxsweep::SliceLevel(mark);
    const uint8_t c = code(x, y);
    set(x, y, wxavionics::Drawn(c) ? wxsweep::Lit565(c, pct, lvl) : Add565(get(x, y), lvl));
}
}  // namespace detail

/** wxsweep::Blend's loop with step C's rule: each pixel marked in `slice` (by wxsweep::Rasterise) is blended ONCE and
 *  its mark cleared, visiting only each row's span. `get(x, y)` and `set(x, y, c)` read and write logical RGB565;
 *  `code(x, y)` is the weather's code there, 0 where the map shows alone. Returns the pixels blended. */
template <typename Set, typename Get, typename Code>
inline uint32_t Blend(int pct, uint8_t* slice, int w, int yLo, int yHi, const int16_t* rowLo, const int16_t* rowHi,
                      Set&& set, Get&& get, Code&& code)
{
    uint32_t n = 0;
    for (int y = yLo; y <= yHi; ++y) {
        uint8_t* row = slice + (size_t)y * w;
        for (int x = rowLo[y]; x <= rowHi[y]; ++x) {
            if (row[x] == 0) continue;
            detail::BlendOne(pct, row[x], x, y, set, get, code);
            row[x] = 0;
            ++n;
        }
    }
    return n;
}

}  // namespace mapsweep
