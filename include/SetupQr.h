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
// WIFI:T:nopass;S:<ssid>;;  -- the de-facto grammar (ZXing's) that the iPhone Camera
// app and stock Android cameras turn into a "Join network" prompt.
//
// T:nopass IS REQUIRED, EVEN THOUGH THE GRAMMAR SAYS IT MAY BE OMITTED. Measured
// 2026-09-25 on an iPhone, with association and every portal request logged:
//
//   WIFI:S:<name>;;          5 of 5 camera joins DROPPED 2.4..2.9 s after associating,
//                            about a second after the setup page was served
//   (manual join, no code)   stayed
//   WIFI:T:nopass;S:<name>;; stayed; Android joins with it too
//
// The T-less form is 25 bytes and fits QR version 2 (132 px), which is why it was
// tried: it frees room for the name at size 2. It cannot ship. With T:nopass the
// payload is 34..36 bytes, version 3, 148 px at 4 px/module, and on the 240 panel
// the name fits at 1.75 rather than 2 (Place() below finds that on its own).
// Do not "save nine bytes" here again without re-running an iPhone camera join
// and watching for the disassociation in the [setup] log.
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

// ---- THE TWO KNOBS ---------------------------------------------------------------
//
// Chosen 2026-09-25: the QR at 4 px/module (148 px for the v3 payload) and the name in
// FreeSansBold9pt7b on the 240 panel. The layout solver (Place) and the host sweep
// follow whatever these say. A smaller module has never been scanned on this product:
// changing MODULE_PX_240 needs an iPhone AND an Android camera join before it ships.
enum class NameStyle : unsigned char {
    SansBold12,   // FreeSansBold12pt7b
    SansBold9,    // FreeSansBold9pt7b
    Sans9,        // FreeSans9pt7b
    Glcd175,      // the 5x7 font at 1.75 -- last resorts only: see NAME_STYLES
    Glcd15,
    Glcd125,
    Glcd1,
};
constexpr int       MODULE_PX_240  = 4;                    // QR module size on the 240 panel
constexpr NameStyle NAME_STYLE_240 = NameStyle::SansBold9; // the style the layout raises the QR to reach

// ---- the QR's module size ----------------------------------------------------
//
// 4 px on the 240 panel: the pitch of the URL-screen QR, proven to scan. The setup
// payload is a v3 symbol (37 modules with the quiet zone): 148 px. Scaled with the
// panel: 6 px on 412, 7 px on 466, 8 px on 480.
constexpr int ModulePx(int screenSize)
{
    return (screenSize * MODULE_PX_240) / 240 < MODULE_PX_240 ? MODULE_PX_240
                                                                : (screenSize * MODULE_PX_240) / 240;
}

// ---- the name's style ------------------------------------------------------------
//
// NOT THE 5x7 FONT, AT ANY SIZE. The hotspot name is what a person reads -- and may
// have to find in a Wi-Fi list -- when the camera does not offer Join. In LovyanGFX's
// GLCD font 'c' is 38 44 44 44 28 and 'o' is 38 44 44 44 38: they differ by ONE pixel.
// Field photo 2026-09-25 at 1.75: "Blipscope" read as "Blipsoope" at arm's length.
// Scaling it up does not help -- at size 2 the difference is a 2x2 px notch, 0.27 mm
// on this glass. So the name is set in a real font whose 'c' is open, and the 5x7
// sizes remain only as the last rungs, so a name that fits nothing else is still drawn.
//
// NAME_STYLES is the preference order. The layout raises the QR until the name
// reaches NAME_STYLE_240's rung, else keeps the best rung that fits.
constexpr NameStyle NAME_STYLES[] = {
    NameStyle::SansBold12, NameStyle::SansBold9, NameStyle::Sans9,
    NameStyle::Glcd175, NameStyle::Glcd15, NameStyle::Glcd125, NameStyle::Glcd1,
};
constexpr int NAME_STYLE_COUNT = (int)(sizeof(NAME_STYLES) / sizeof(NAME_STYLES[0]));

constexpr int Rank(NameStyle st)
{
    return (int)st;   // NAME_STYLES is in enum order; the host test asserts it stays so
}

inline const char* StyleName(NameStyle st)
{
    switch (st) {
        case NameStyle::SansBold12: return "FreeSansBold12pt7b";
        case NameStyle::SansBold9:  return "FreeSansBold9pt7b";
        case NameStyle::Sans9:      return "FreeSans9pt7b";
        case NameStyle::Glcd175:    return "GLCD x1.75";
        case NameStyle::Glcd15:     return "GLCD x1.5";
        case NameStyle::Glcd125:    return "GLCD x1.25";
        case NameStyle::Glcd1:      return "GLCD x1";
    }
    return "?";
}

/// Best style in NAME_STYLES whose text fits the disc at `yTop`, as a rank (index), or
/// -1 if none does. `measure(style, &w, &h)` reports the rendered width/height: the
/// device passes LovyanGFX's own textWidth/fontHeight, the host test the fonts' own
/// glyph tables -- ONE rule, two measurers.
template <typename Measure>
int PickNameRank(Measure measure, int yTop, int screenSize)
{
    for (int i = 0; i < NAME_STYLE_COUNT; ++i) {
        int w = 0, h = 0;
        measure(NAME_STYLES[i], &w, &h);
        if (w > 0 && w <= discgeom::ChordWidthPx(yTop, h, screenSize)) return i;
    }
    return -1;
}

// ---- where the QR, the title and the name go -----------------------------------
//
// THE QR IS RAISED ONLY AS FAR AS THE NAME NEEDS. Centred, the 148 px symbol on the
// 240 panel leaves the name a band at y 198 where the chord is too short for
// "Blipscope-XXXXXX" in FreeSansBold9pt7b (154 px). Each pixel the QR moves up widens
// the name's chord and narrows the title's, and pushes the QR's corners toward the
// bezel. So: walk the raise up from 0 and stop at the FIRST one where the name
// reaches NAME_STYLE_240's rung, provided the corners stay on the glass and the title
// still fits. If no raise reaches it, keep the raise that gave the best rung.
//
// The corner proof is made HERE because the renderer's inscribed-square bound is
// only valid for a centred symbol (see QrRender.h's Draw).
constexpr int GAP_PX = 4;            // between the QR's quiet zone and a text row
constexpr int CORNER_MARGIN_PX = 4;  // a raised QR's corners stay this far inside the radius

struct Placement {
    int raise;       // px above the disc's centre
    int cy;          // QR centre y
    int titleY;      // top of the title row
    int nameY;       // top of the name row
    int nameRank;    // index into NAME_STYLES; -1 = nothing fits (caller still draws it, GLCD x1)
};

inline bool CornersOnGlass(int screenSize, int side, int raise)
{
    const float r  = screenSize * 0.5f;
    const float hx = side * 0.5f;
    const float hy = side * 0.5f + (float)raise;   // the upper corners are the far ones
    return std::sqrt(hx * hx + hy * hy) <= r - (float)CORNER_MARGIN_PX;
}

/// `titleW`/`titleH`: the title's rendered size (5x7 font, scale 1). `measure`: as for
/// PickNameRank.
template <typename Measure>
Placement Place(int screenSize, int side, int titleW, int titleH, Measure measure)
{
    const int c = screenSize / 2;
    Placement best{0, c, c - side / 2 - GAP_PX - titleH, c + side / 2 + GAP_PX, -1};
    for (int raise = 0; raise < c; ++raise) {
        if (!CornersOnGlass(screenSize, side, raise)) break;
        const int cy = c - raise;
        const int titleY = cy - side / 2 - GAP_PX - titleH;
        if (titleY < 0 || titleW > discgeom::ChordWidthPx(titleY, titleH, screenSize)) break;
        const int nameY = cy + side / 2 + GAP_PX;
        const int rank = PickNameRank(measure, nameY, screenSize);
        if (rank >= 0 && (best.nameRank < 0 || rank < best.nameRank))
            best = Placement{raise, cy, titleY, nameY, rank};
        if (rank >= 0 && rank <= Rank(NAME_STYLE_240)) break;
    }
    return best;
}

} // namespace setupqr
