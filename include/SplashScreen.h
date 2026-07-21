#pragma once

#include <Arduino.h>
#include "LGFX.h"       // LGFX + LGFX_Sprite
#include "Layout.h"     // SCREEN_SIZE, SCREEN_SIZE_DIV_2
#include "Board.h"      // board::DisplayFlush
#include "OtaUpdater.h" // FW_VERSION

// The Valar Systems boot wordmark -- the FIRST thing this device ever draws.
//
// WHY IT SITS WHERE IT DOES IN setup(): the panel is usable within ~100 ms of reset
// (BoardPreInit + tft.init), but the old ordering blocked on `while (!Serial && ...)`
// for up to 3 s BEFORE touching the display -- and on a wall wart there is no USB CDC
// host, so that wait always ran in full and the screen stayed dark for its duration.
// Bringing the panel up and painting the wordmark ahead of that wait costs nothing and
// buys back the entire window: the brand is on screen while we wait for the (optional)
// serial host rather than after it. See main.cpp for the ordering.
//
// It is deliberately a WORDMARK, not a logo: at 240 px round, inside an inscribed
// square of only ~170 px, letterforms read and glyph-art does not.
//
// COST: no allocation of any kind. It draws with the built-in GLCD font through the
// same path every other boot screen uses (BootScreen.h), so it adds no heap and no
// sprite -- which matters because the C3-class contiguous-heap budget is what the TLS
// handshake later competes for. See CLAUDE.md "three hard constraints". Measured at
// +168 bytes flash / 0 bytes RAM against the same build with the splash compiled out.
//
// NOT A CREDITS SCREEN: data-source attribution (adsb.lol ODbL, Mictronics ODC-By,
// OurAirports, photo credits) lives on the config page and /credits. Nothing
// third-party belongs here, and there is no slot for it by design.

// How long the wordmark is guaranteed to stay up, measured from the moment it is
// painted. This is a MINIMUM, not a timeout -- boot from here to first data is ~20-30 s
// (measured on the s3-128 bench board: first fetch at 21.6 s and 30.0 s across two
// clean boots), so the splash is never what the user is waiting on. It exists to stop
// the wordmark being a single-frame flash before "Connecting to WiFi..." replaces it.
constexpr uint32_t SPLASH_MIN_MS = 1200;

// Render "VALAR SYSTEMS" centered, with the firmware version smaller and dimmer beneath.
//
// Sizing: the built-in font is 6x8 px per glyph. "VALAR SYSTEMS" is 13 glyphs, so at
// textSize 2 it is 156 px wide -- comfortably inside the ~170 px inscribed square of a
// 240 px round panel, with margin for the bezel. Do not raise it to 3 (234 px): it
// would run off the curve on every round SKU.
inline void DrawSplash(LGFX& tft, LGFX_Sprite& fb)
{
#if defined(BLIPSCOPE_PANEL_SPD2010)
    auto& g = fb;   // compose into the sprite, push even-aligned below (see BootScreen.h)
#else
    auto& g = tft;  // direct to the panel
#endif

    g.fillScreen(lgfx::color888(0, 0, 0));

    // Wordmark, full brightness.
    // drawCenterString takes the TOP of the text. At size 2 the wordmark is 16 px tall and
    // the version below is 8 px, so these two offsets put the combined block's centre within
    // a pixel of the screen centre rather than centring the wordmark alone and sitting low.
    g.setTextColor(lgfx::color888(255, 255, 255));
    g.setTextSize(2);
    g.drawCenterString("VALAR SYSTEMS", SCREEN_SIZE_DIV_2, SCREEN_SIZE_DIV_2 - 18);

    // Version, smaller and dimmer -- present for support, subordinate to the brand.
    // Single source of truth: the same FW_VERSION int the OTA gate compares against,
    // so what the screen says can never drift from what the updater believes.
    char version[8];
    snprintf(version, sizeof(version), "v%d", FW_VERSION);
    g.setTextColor(lgfx::color888(120, 120, 120));
    g.setTextSize(1);
    g.drawCenterString(version, SCREEN_SIZE_DIV_2, SCREEN_SIZE_DIV_2 + 8);

    g.setTextSize(1); // leave the text state as every other boot screen expects it

#if defined(BLIPSCOPE_PANEL_SPD2010)
    fb.pushSprite(0, 0);
#endif
    board::DisplayFlush(tft); // RGB panels: make the splash visible (no-op on SPI SKUs)
}

// Hold the wordmark until it has had its minimum time on screen, given the millis()
// value captured when DrawSplash() ran. Usually returns immediately: the serial wait
// and panel bring-up that follow the splash have normally already outlasted the
// minimum, so this only actually sleeps on a boot that got there unusually fast.
inline void HoldSplash(uint32_t splashDrawnAtMs)
{
    const uint32_t elapsed = millis() - splashDrawnAtMs; // u32 wrap-around subtracts correctly
    if (elapsed < SPLASH_MIN_MS)
        delay(SPLASH_MIN_MS - elapsed);
}
