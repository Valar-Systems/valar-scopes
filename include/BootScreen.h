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

  // Fall back to the panel if the sprite never allocated -- composing into a sprite that
  // does not exist draws nothing at all, and these screens carry the Wi-Fi reset prompt and
  // the "Update failed" message. Silence is the worst possible rendering of either.
  if constexpr (!variant::BANDED_RENDER) {
    if (fb.getBuffer() != nullptr) {
      paint(fb);            // compose off-screen...
      fb.pushSprite(0, 0);  // ...and land it in one blit: no flash
    } else {
      paint(tft);
    }
  } else {
    paint(tft);           // banded board: no full framebuffer to compose into
  }
}

#include "QrRender.h"
#include "SetupQr.h"

// THE SETUP SCREEN: a Wi-Fi QR, the hotspot name, and one small title line. Nothing else.
//
// Replaces three lines of 6x8 text at scale 1, which was hard to read on the unit. A
// phone's camera turns the code into a "Join network" prompt; the name, at size 2 where
// the disc can hold it, is the fallback for a phone that does not. Layout: SetupQr.h.
//
// FAIL-CLOSED: if the QR cannot be drawn (qr::Draw refuses a payload that will not fit
// its version cap or the disc), this falls back to the old three-line screen, with the
// name at the larger size. Never a blank screen -- this is the screen a customer is
// stuck on until it works.
//
// Composed through the backbuffer exactly like DrawCenteredScreen, for the same reason
// (the SPD2010 drops direct per-glyph writes), with the same fallback to the panel.
// Called from the portal's AP callback (main task) and from the setup watcher task in
// WiFiManagerHelpers.h -- never both at once: the watcher starts after the first draw
// and is stopped before autoConnect() returns.
template <typename Paint>
inline void ComposeFullScreen([[maybe_unused]] LGFX& tft, [[maybe_unused]] LGFX_Sprite& fb, Paint paint)
{
  if constexpr (!variant::BANDED_RENDER) {
    if (fb.getBuffer() != nullptr) {
      paint(fb);
      fb.pushSprite(0, 0);
    } else {
      paint(tft);
    }
  } else {
    paint(tft);
  }
}

// The name's style -> the panel's font and scale. The whole UI draws in the default
// 5x7 font (nothing else calls setFont), so every caller of this puts it back.
template <typename G>
inline void ApplyNameStyle(G& g, setupqr::NameStyle st)
{
  switch (st) {
    case setupqr::NameStyle::SansBold12: g.setFont(&lgfx::fonts::FreeSansBold12pt7b); g.setTextSize(1); break;
    case setupqr::NameStyle::SansBold9:  g.setFont(&lgfx::fonts::FreeSansBold9pt7b);  g.setTextSize(1); break;
    case setupqr::NameStyle::Sans9:      g.setFont(&lgfx::fonts::FreeSans9pt7b);      g.setTextSize(1); break;
    case setupqr::NameStyle::Glcd175:    g.setFont(&lgfx::fonts::Font0); g.setTextSize(1.75f); break;
    case setupqr::NameStyle::Glcd15:     g.setFont(&lgfx::fonts::Font0); g.setTextSize(1.5f);  break;
    case setupqr::NameStyle::Glcd125:    g.setFont(&lgfx::fonts::Font0); g.setTextSize(1.25f); break;
    case setupqr::NameStyle::Glcd1:      g.setFont(&lgfx::fonts::Font0); g.setTextSize(1);     break;
  }
}

template <typename G>
inline void RestoreDefaultFont(G& g)
{
  g.setFont(&lgfx::fonts::Font0);
  g.setTextSize(1);
}

inline void DrawSetupQrScreen(LGFX& tft, LGFX_Sprite& fb, uint32_t fg, const char* title, const char* name)
{
  const int px = setupqr::ModulePx(SCREEN_SIZE);
  char payload[96];
  const size_t pn = setupqr::WifiPayload(name, payload, sizeof(payload));
  const int side = pn > 0 ? qr::SideFor(payload, px) : 0;

  bool drewQr = false;
  setupqr::Placement at{};
  at.nameRank = -1;
  int nameW = 0, nameH = 0, chordW = 0;
  auto paint = [&](auto& g) {
    g.fillScreen(lgfx::color888(0, 0, 0));
    g.setTextColor(fg);
    RestoreDefaultFont(g);
    const int c = SCREEN_SIZE / 2;

    // The measurer is the panel's own: LovyanGFX's textWidth/fontHeight in each
    // candidate style. SetupQr.h owns the rule; the host test runs the same rule
    // against the fonts' own glyph tables.
    auto measure = [&](setupqr::NameStyle st, int* w, int* h) {
      ApplyNameStyle(g, st);
      *w = g.textWidth(name) + setupqr::DashPadPx(name);   // the dash's room is part of the width
      *h = g.fontHeight();
    };
    auto drawName = [&](int y) {
      const setupqr::NameStyle st =
          at.nameRank >= 0 ? setupqr::NAME_STYLES[at.nameRank] : setupqr::NameStyle::Glcd1;
      ApplyNameStyle(g, st);                      // nothing fits: still drawn, smallest
      nameW = g.textWidth(name) + setupqr::DashPadPx(name);
      nameH = g.fontHeight();
      chordW = discgeom::ChordWidthPx(y, nameH, SCREEN_SIZE);
      // Centred by hand, glyph run by glyph run, so DASH_PAD_PX lands before each '-'.
      int x = c - nameW / 2;
      char run[48];
      size_t k = 0;
      for (const char* p = name;; ++p) {
        if (*p == '-' || *p == '\0' || k + 1 >= sizeof(run)) {
          run[k] = '\0';
          if (k) { g.drawString(run, x, y); x += g.textWidth(run); }
          k = 0;
          if (*p == '\0') break;
          if (*p == '-') { x += setupqr::DASH_PAD_PX; g.drawString("-", x, y); x += g.textWidth("-"); continue; }
        }
        run[k++] = *p;
      }
    };

    if (side > 0) {
      RestoreDefaultFont(g);
      const int titleW = g.textWidth(title), titleH = g.fontHeight();
      at = setupqr::Place(SCREEN_SIZE, side, titleW, titleH, measure);
      drewQr = qr::Draw(g, payload, c, at.cy, px);
    }
    if (drewQr) {
      RestoreDefaultFont(g);
      g.drawCenterString(title, c, at.titleY);
      drawName(at.nameY);
    } else {
      // The old screen, name larger. Title and prompt in the 5x7 font above the
      // centre, the name below it in the best style its row can hold.
      RestoreDefaultFont(g);
      const int lh = g.fontHeight() + 10;
      g.drawCenterString(title, c, c - 2 * lh);
      g.drawCenterString("Connect to this Wi-Fi hotspot:", c, c - lh);
      at.nameRank = setupqr::PickNameRank(measure, c, SCREEN_SIZE);
      drawName(c);
    }
    RestoreDefaultFont(g);   // the sprite is shared with the rest of the UI
  };
  ComposeFullScreen(tft, fb, paint);

  // The version, from what was drawn: side = (4v + 17 + 2*QUIET) * px.
  const int version = drewQr ? (side / px - 17 - 2 * qr::QUIET) / 4 : 0;
  const setupqr::NameStyle st =
      at.nameRank >= 0 ? setupqr::NAME_STYLES[at.nameRank] : setupqr::NameStyle::Glcd1;
  Serial.printf("[setup] Wi-Fi QR %s: v%d, side %d px at %d px/module, raise %d, "
                "name style %s%s, name %d px (h %d), chord %d px\n",
                drewQr ? "drawn" : "REFUSED -> three-line fallback", version, side, px,
                at.raise, setupqr::StyleName(st), at.nameRank < 0 ? " (NOTHING FIT)" : "",
                nameW, nameH, chordW);
}

// "A PHONE HAS JOINED" -- shown while the phone is associated and before its setup
// page appears. Field result 2026-09-25 (iPhone): ~5 s passed between tapping Join
// and the captive page, and with the QR still on screen it read as "nothing
// happened", so Join was tapped again and again. This says it worked.
//
// In FreeSansBold12pt7b, not the 5x7 font: "connected" has a 'c', and in the 5x7 font
// 'c' and 'o' differ by one pixel (see SetupQr.h). "Setup page opening..." is 254 px
// in this font, wider than the 240 panel, so it is broken into two lines.
inline void DrawPhoneConnectedScreen(LGFX& tft, LGFX_Sprite& fb, uint32_t fg)
{
  static const char* const LINES[] = { "Phone connected", "Setup page", "opening..." };
  ComposeFullScreen(tft, fb, [&](auto& g) {
    g.fillScreen(lgfx::color888(0, 0, 0));
    g.setTextColor(fg);
    g.setFont(&lgfx::fonts::FreeSansBold12pt7b);
    g.setTextSize(1);
    const int c = SCREEN_SIZE / 2;
    const int lh = g.fontHeight() + 2;
    for (int i = 0; i < 3; ++i)
      g.drawCenterString(LINES[i], c, c - lh - lh / 2 + i * lh + (i > 0 ? 6 : 0));
    RestoreDefaultFont(g);
  });
}
