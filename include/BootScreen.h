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
// phone's camera turns the code into a "Join network" prompt; the name, drawn at the
// largest size its row of the disc can hold, is the fallback for a phone that does not.
//
// FAIL-CLOSED: if the QR cannot be drawn (qr::Draw refuses a payload that will not fit
// its version cap or the disc), this falls back to the old three-line screen, with the
// name at the larger size. Never a blank screen -- this is the screen a customer is
// stuck on until it works.
//
// Composed through the backbuffer exactly like DrawCenteredScreen, for the same reason
// (the SPD2010 drops direct per-glyph writes), with the same fallback to the panel.
inline void DrawSetupQrScreen([[maybe_unused]] LGFX& tft, [[maybe_unused]] LGFX_Sprite& fb,
                              uint32_t fg, const char* title, const char* name)
{
  bool drewQr = false;
  int side = 0;
  float nameScale = 0.0f;
  auto paint = [&](auto& g) {
    g.fillScreen(lgfx::color888(0, 0, 0));
    g.setTextSize(1);
    const int c = SCREEN_SIZE / 2;

    char payload[96];
    drewQr = setupqr::WifiPayload(name, payload, sizeof(payload)) > 0 &&
             qr::Draw(g, payload, c, c, setupqr::ModulePx(SCREEN_SIZE), &side);

    // The measurer is the panel's own: LovyanGFX's textWidth/fontHeight at each
    // candidate scale. SetupQr.h owns the rule; the host test runs the same rule.
    auto measure = [&](float s, int* w, int* h) {
      g.setTextSize(s);
      *w = g.textWidth(name);
      *h = g.fontHeight();
    };

    g.setTextColor(fg);
    if (drewQr) {
      g.setTextSize(1);
      g.drawCenterString(title, c, setupqr::TitleY(SCREEN_SIZE, side, g.fontHeight()));
      const int nameY = setupqr::NameY(SCREEN_SIZE, side);
      nameScale = setupqr::PickNameScale(measure, nameY, SCREEN_SIZE);
      g.setTextSize(nameScale > 0 ? nameScale : 1.0f);   // 0 = nothing fits: still draw it
      g.drawCenterString(name, c, nameY);
    } else {
      // The old screen, name larger. Title and prompt at scale 1 above the centre,
      // the name below it at the largest scale its row can hold.
      g.setTextSize(1);
      const int lh = g.fontHeight() + 10;
      g.drawCenterString(title, c, c - 2 * lh);
      g.drawCenterString("Connect to this Wi-Fi hotspot:", c, c - lh);
      nameScale = setupqr::PickNameScale(measure, c, SCREEN_SIZE);
      g.setTextSize(nameScale > 0 ? nameScale : 1.0f);
      g.drawCenterString(name, c, c);
    }
    g.setTextSize(1);   // the sprite is shared with the rest of the UI
  };

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
  // The version, from what was drawn: side = (4v + 17 + 2*QUIET) * px.
  const int px = setupqr::ModulePx(SCREEN_SIZE);
  const int version = drewQr ? (side / px - 17 - 2 * qr::QUIET) / 4 : 0;
  Serial.printf("[setup] Wi-Fi QR %s: v%d, side %d px at %d px/module, name scale %.2f\n",
                drewQr ? "drawn" : "REFUSED -> three-line fallback", version, side, px,
                (double)nameScale);
}
