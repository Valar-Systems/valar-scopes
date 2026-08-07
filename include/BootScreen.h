#pragma once

#include "LGFX.h"     // LGFX + LGFX_Sprite
#include "Layout.h"   // SCREEN_SIZE

// Render up to three centered text lines as a full-screen message (boot / Wi-Fi-setup screens).
//
// Why this helper exists: the SPD2010 panel (1.46") can't take direct partial writes -- per-glyph
// text drawn straight to the panel is dropped or garbled (it only accepts even-aligned, full-frame
// blits; see Panel_SPD2010.hpp). So on that panel we compose the message into the full-screen 8bpp
// sprite and push it in one go -- exactly how the radar renders, which works perfectly.
//
// That path is now taken by EVERY board that has a full framebuffer, not just the SPD2010, because
// drawing straight to the panel means fillScreen() clears it and the glyphs land a beat later --
// a visible black flash on every redraw. Harmless on a screen shown once; NOT harmless on a
// once-per-second countdown, where it reads as the device glitching. It cost a real acceptance
// run: the flash mid-countdown looked like a fault, the operator reacted, and the gesture
// cancelled. The banded C3 keeps drawing direct -- its backbuffer is a half-height band, not a
// frame, so there is nothing to compose into. Call sites stay panel-agnostic.
inline void DrawCenteredScreen([[maybe_unused]] LGFX& tft, [[maybe_unused]] LGFX_Sprite& fb,
                               uint32_t bg, uint32_t fg,
                               const char* l0, const char* l1 = nullptr, const char* l2 = nullptr)
{
  auto paint = [&](auto& g) {
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
  };

  if constexpr (!variant::BANDED_RENDER) {
    paint(fb);            // compose off-screen...
    fb.pushSprite(0, 0);  // ...and land it in one blit: no flash
  } else {
    paint(tft);           // banded board: no full framebuffer to compose into
  }
}
