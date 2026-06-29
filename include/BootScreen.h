#pragma once

#include "LGFX.h"     // LGFX + LGFX_Sprite
#include "Layout.h"   // SCREEN_SIZE

// Render up to three centered text lines as a full-screen message (boot / Wi-Fi-setup screens).
//
// Why this helper exists: the SPD2010 panel (1.46") can't take direct partial writes -- per-glyph
// text drawn straight to the panel is dropped or garbled (it only accepts even-aligned, full-frame
// blits; see Panel_SPD2010.hpp). So on that panel we compose the message into the full-screen 8bpp
// sprite and push it in one go -- exactly how the radar renders, which works perfectly. Every other
// SKU draws straight to the display as before. Call sites stay panel-agnostic.
inline void DrawCenteredScreen([[maybe_unused]] LGFX& tft, [[maybe_unused]] LGFX_Sprite& fb,
                               uint32_t bg, uint32_t fg,
                               const char* l0, const char* l1 = nullptr, const char* l2 = nullptr)
{
#if defined(BLIPSCOPE_PANEL_SPD2010)
  auto& g = fb;   // compose into the sprite, then push it even-aligned below
#else
  auto& g = tft;  // direct to the panel
#endif

  g.fillScreen(bg);
  g.setTextColor(fg);
  const int cx = SCREEN_SIZE / 2;
  const int cy = SCREEN_SIZE / 2;
  if (l1) { // multi-line layout
    const int lh = g.fontHeight() + 10;
    g.drawCenterString(l0, cx, cy - lh);
    g.drawCenterString(l1, cx, cy);
    if (l2) { g.drawCenterString(l2, cx, cy + lh); }
  } else {
    g.drawCenterString(l0, cx, cy);
  }

#if defined(BLIPSCOPE_PANEL_SPD2010)
  fb.pushSprite(0, 0);
#endif
}
