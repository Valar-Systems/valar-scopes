#pragma once
// The radar's "touch unavailable" strip in Weatherscope's colours (docs/weatherscope-stage3.md A12.119): the same
// words and box (TouchWedgeCap.h's DrawStrip), inked from the theme on the screen, so it reads in light as in dark.
// Roles only -- no colour literal (NL, A12.47). Host-tested with the text rule's contrast (test_touch_wedge_cap.cpp).
#include <stdint.h>
#include "TouchWedgeCap.h"
#include "WxTheme.h"

namespace wxtheme {

// RGB888 as lgfx::color888 packs it, so a canvas draws exactly what C888() would.
inline uint32_t Pack888(Rgb c) { return ((uint32_t)c.r << 16) | ((uint32_t)c.g << 8) | (uint32_t)c.b; }

// Ground bg; border and headline uiWarn (the status screen's "No Wi-Fi"); the support address and id uiFg.
inline touchwedge::StripInk StripInkFor(const Theme& th)
{
    touchwedge::StripInk ink;
    ink.ground = Pack888(th.bg);
    ink.border = Pack888(th.uiWarn);
    ink.headline = Pack888(th.uiWarn);
    ink.body = Pack888(th.uiFg);
    return ink;
}

} // namespace wxtheme
