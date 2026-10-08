#pragma once

// Where the Connect screen puts things, as numbers DrawConnect and the host tests both read.
//
// They were literals inside DrawConnect until the touch-unavailable strip had to share the
// screen with the QR (ruling 4 on #376): the strip must never cover the code a customer scans,
// and a test can only hold that line if it reads the same numbers the drawing does.
//
// No hardware here, so test/host can include it. The QR bound is transcribed from QrRender.h
// (which needs the panel and the qrcode library); AircraftManager.cpp static_asserts the two
// against each other, so a change to the encoder's limits fails the firmware build.

namespace connectlayout {

constexpr int TITLE_Y = 14;    // "SET YOUR LOCATION" -- unconfigured units only
constexpr int QR_CY   = 90;    // QR centre row
constexpr int QR_PX   = 4;     // pixels per module
constexpr int URL_Y   = 164;   // "http://<address>" -- what a customer types if the scan fails
constexpr int ID_Y    = 180;   // the device id
constexpr int HINT_Y  = 194;   // "phone on home wifi?" (joined units)
constexpr int RESET_Y = 210;   // "[ Reset ]" -- a touch control

// Transcribed from QrRender.h: the encoder never goes past version 3, and the symbol carries a
// 4-module quiet zone on every side.
constexpr int QR_MAX_VERSION = 3;
constexpr int QR_QUIET       = 4;

/// The lowest row the QR can reach, quiet zone included, for ANY address Connect shows.
constexpr int QrBottomY()
{
    return QR_CY + ((4 * QR_MAX_VERSION + 17) + 2 * QR_QUIET) * QR_PX / 2;
}

} // namespace connectlayout
