#pragma once

// The setup screen's Wi-Fi QR: the payload, and where things go on a round disc.
//
// PURE ON PURPOSE -- no Arduino, no LovyanGFX -- so the host suite grades the SAME
// rule the device runs (test/host/test_wifi_qr.cpp), across every edition's name
// and every panel size, instead of one bench photograph of one name on one board.
// The drawing lives in BootScreen.h and asks this file every question it has.

#include <cmath>
#include <cstddef>
#include "DiscGeometry.h"

namespace setupqr {

// ---- the payload -----------------------------------------------------------
//
// WIFI:S:<ssid>;;  -- the de-facto grammar (ZXing's) that the iPhone Camera app and
// stock Android cameras turn into a "Join network" prompt.
//
// NO "T:" FIELD, AND THAT IS HOW THE AP BEING OPEN IS SAID. The grammar makes T
// optional and omitting it means "no password". It is omitted rather than written
// as T:nopass because nine bytes decide the symbol's version: with T:nopass the
// payload is 34+ bytes and needs QR version 3 (148 px at 4 px/module); without it,
// 25..27 bytes fit version 2 -- the 132 px symbol the URL screen already proved
// scans. Field result 2026-09-25: the v3 symbol worked, but the name under it was
// unreadable at arm's length, and v2's smaller square is what buys the name size 2.
//
// The AP IS open: main.cpp calls wm.autoConnect(name) with no password argument.
// If that ever gains a password, this becomes WIFI:T:WPA;S:..;P:..;; in the same
// change -- a phone told "open" for a WPA network offers a join that fails.
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
    static const char HEAD[] = "WIFI:S:";
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

// ---- the QR's module size ----------------------------------------------------
//
// 4 px on the 240 panel: the pitch of the URL-screen QR, proven to scan. A v2
// symbol (33 modules with the quiet zone) is 132 px. Scaled with the panel: 6 px
// on 412, 7 px on 466, 8 px on 480.
constexpr int ModulePx(int screenSize)
{
    return (screenSize * 4) / 240 < 4 ? 4 : (screenSize * 4) / 240;
}

// ---- the name's size -----------------------------------------------------------
//
// The hotspot name is what a person reads when the camera does not offer Join,
// so it gets size 2 (TARGET_SCALE) wherever the disc can hold it, else the largest
// scale that fits -- measured, not assumed. The 6x8 font at scale 1 is what made
// the original screen hard to read, and 1.5 under the v3 symbol still was.
constexpr float NAME_SCALES[] = { 3.0f, 2.5f, 2.0f, 1.75f, 1.5f, 1.25f, 1.0f };
constexpr float TARGET_SCALE = 2.0f;

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

// ---- where the QR, the title and the name go -----------------------------------
//
// THE QR IS RAISED ONLY AS FAR AS THE NAME NEEDS. Centred, a 132 px symbol on the
// 240 panel leaves the name a band at y 190 where the chord is 159 px -- and
// "Blipscope-XXXXXX" at size 2 is 192. Each pixel the QR moves up widens the
// name's chord and narrows the title's, and pushes the QR's corners toward the
// bezel. So: walk the raise up from 0 and stop at the FIRST one where the name
// reaches TARGET_SCALE, provided the corners stay on the glass and the title
// still fits. If no raise reaches it, keep the raise that gave the best scale.
//
// The corner proof is made HERE because the renderer's inscribed-square bound is
// only valid for a centred symbol (see QrRender.h's Draw).
constexpr int GAP_PX = 4;            // between the QR's quiet zone and a text row
constexpr int CORNER_MARGIN_PX = 4;  // a raised QR's corners stay this far inside the radius

struct Placement {
    int   raise;       // px above the disc's centre
    int   cy;          // QR centre y
    int   titleY;      // top of the title row
    int   nameY;       // top of the name row
    float nameScale;   // 0 = not even scale 1 fits (caller still draws it, at 1)
};

inline bool CornersOnGlass(int screenSize, int side, int raise)
{
    const float r  = screenSize * 0.5f;
    const float hx = side * 0.5f;
    const float hy = side * 0.5f + (float)raise;   // the upper corners are the far ones
    return std::sqrt(hx * hx + hy * hy) <= r - (float)CORNER_MARGIN_PX;
}

/// `titleW`/`titleH`: the title's rendered size at scale 1. `measure`: as for
/// PickNameScale.
template <typename Measure>
Placement Place(int screenSize, int side, int titleW, int titleH, Measure measure)
{
    const int c = screenSize / 2;
    Placement best{0, c, c - side / 2 - GAP_PX - titleH, c + side / 2 + GAP_PX, -1.0f};
    for (int raise = 0; raise < c; ++raise) {
        if (!CornersOnGlass(screenSize, side, raise)) break;
        const int cy = c - raise;
        const int titleY = cy - side / 2 - GAP_PX - titleH;
        if (titleY < 0 || titleW > discgeom::ChordWidthPx(titleY, titleH, screenSize)) break;
        const int nameY = cy + side / 2 + GAP_PX;
        const float s = PickNameScale(measure, nameY, screenSize);
        if (s > best.nameScale) best = Placement{raise, cy, titleY, nameY, s};
        if (s >= TARGET_SCALE) break;
    }
    if (best.nameScale < 0.0f) best.nameScale = 0.0f;
    return best;
}

} // namespace setupqr
