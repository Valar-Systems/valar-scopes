#pragma once

// The setup screen's Wi-Fi QR: the payload, and where things go on a round disc.
//
// PURE ON PURPOSE -- no Arduino, no LovyanGFX -- so the host suite grades the SAME
// rule the device runs (test/host/test_setup_qr.cpp), across every edition's name
// and every panel size, instead of one bench photograph of one name on one board.
// The drawing lives in BootScreen.h and asks this file every question it has.

#include <cstddef>
#include "DiscGeometry.h"

namespace setupqr {

// ---- the payload -----------------------------------------------------------
//
// WIFI:T:nopass;S:<ssid>;;  -- the de-facto grammar (ZXing's) that the iPhone
// Camera app and stock Android cameras turn into a "Join network" prompt.
//
// T:nopass BECAUSE THE AP IS OPEN: main.cpp calls wm.autoConnect(name) with no
// password argument. If that ever gains a password, this payload has to become
// T:WPA;S:..;P:..;; in the same change -- a phone told "nopass" for a WPA network
// offers a join that fails, which reads as "the code is broken".
//
// Escaping: \ ; , : " are backslash-escaped in the SSID. Today's names are
// <Product>-<6 hex> and contain none of them; the escaping is here so a future
// product name cannot silently produce a code that joins the wrong network.
//
// Returns the payload length, or 0 if it does not fit `cap` (never a truncated
// payload: half an SSID is a DIFFERENT SSID).
inline size_t WifiPayload(const char* ssid, char* out, size_t cap)
{
    if (ssid == nullptr || ssid[0] == '\0' || out == nullptr) return 0;
    static const char HEAD[] = "WIFI:T:nopass;S:";
    size_t n = 0;
    auto put = [&](char c) -> bool {
        if (n + 1 >= cap) return false;     // keep room for the terminator
        out[n++] = c;
        return true;
    };
    for (const char* p = HEAD; *p; ++p) if (!put(*p)) return 0;
    for (const char* p = ssid; *p; ++p) {
        const char c = *p;
        if (c == '\\' || c == ';' || c == ',' || c == ':' || c == '"')
            if (!put('\\')) return 0;
        if (!put(c)) return 0;
    }
    if (!put(';') || !put(';')) return 0;
    out[n] = '\0';
    return n;
}

// ---- the QR's size -----------------------------------------------------------
//
// THE MODULE PITCH IS KEPT, NOT THE SIDE. The post-setup URL QR is 132 px because
// it is a VERSION 2 symbol (33 modules with the quiet zone) at 4 px. This payload
// is 34+ bytes, which version 2 cannot hold at ECC LOW (32), so it is version 3:
// 37 modules, 148 px at the same 4 px. What decides whether a phone locks on is
// the module size, so the pitch is what stays the same; 132 px would mean 3 px
// modules, a smaller target than the one already proven to scan.
//
// Scaled with the panel: 4 px on 240, 6 px on 412, 8 px on 480.
constexpr int ModulePx(int screenSize)
{
    return (screenSize * 4) / 240 < 4 ? 4 : (screenSize * 4) / 240;
}

// ---- the name's size -----------------------------------------------------------
//
// The hotspot name is the one thing on this screen a person may have to TYPE,
// so it gets the largest size that fits the chord at its row -- measured, not
// assumed. The default font is 6x8 at scale 1, which is exactly what made the old
// screen hard to read.
constexpr float NAME_SCALES[] = { 3.0f, 2.5f, 2.0f, 1.75f, 1.5f, 1.25f, 1.0f };

/// Largest scale in NAME_SCALES whose text fits the disc at `yTop`, or 0 if even
/// scale 1 does not. `measure(scale, &w, &h)` reports the rendered width/height:
/// the device passes LovyanGFX's own textWidth/fontHeight, the host test passes
/// the 6x8 arithmetic -- ONE rule, two measurers.
template <typename Measure>
float PickNameScale(Measure measure, int yTop, int screenSize)
{
    for (float s : NAME_SCALES) {
        int w = 0, h = 0;
        measure(s, &w, &h);
        if (w > 0 && w <= discgeom::ChordWidthPx(yTop, h, screenSize)) return s;
    }
    return 0.0f;
}

// ---- where the title and the name go ---------------------------------------------
//
// From the side the QR ACTUALLY drew (qr::Draw reports it), never from a module
// count computed here: QrRender.h owns the version choice, and a second copy of
// its capacity table in this file would be a second rule free to drift from it.
// The QR is centred on the disc (the inscribed-square bound requires it); the
// title sits just above it and the name just below, 4 px from the quiet zone.
constexpr int GAP_PX = 4;
constexpr int TitleY(int screenSize, int qrSide, int titleH)
{
    return screenSize / 2 - qrSide / 2 - GAP_PX - titleH;
}
constexpr int NameY(int screenSize, int qrSide)
{
    return screenSize / 2 + qrSide / 2 + GAP_PX;
}

} // namespace setupqr
