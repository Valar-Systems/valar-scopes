#pragma once

#include <qrcode.h>
#include "Layout.h"

/* ============================================================================
 * A SCANNABLE QR, WHICH IS NOT THE SAME AS A GOOD-LOOKING ONE.
 *
 * Every decision here is made against "does a phone in a dim room read it on the
 * first try", and several of them are ugly against a green-on-black UI. They are
 * made that way on purpose.
 *
 *   BLACK ON WHITE, not inverted. A light-on-dark QR is out of spec. Plenty of
 *   scanners handle it; plenty do not, and the ones that do not fail silently by
 *   simply never locking on, which reads to the owner as "the code is broken" or
 *   worse "this product is broken". Matching the aesthetic is worth nothing if
 *   it does not scan.
 *
 *   THE FULL 4-MODULE QUIET ZONE. It is part of the symbol, not padding around
 *   it. A borderless QR fails on a large share of scanners for the same silent
 *   reason -- the finder pattern search needs the margin to separate the symbol
 *   from whatever is behind it.
 *
 *   SIZED SO IT FITS THE DISC. "http://192.168.100.100" is 22 bytes, which is
 *   version 2 (25x25 modules) at ECC LOW. 25 + 2*4 quiet = 33 modules. At 6 px
 *   per module that is 198 px, inside 240. VERSION IS COMPUTED, NOT ASSUMED: a
 *   longer host or a portal address could push to version 3, and a symbol that
 *   silently overflowed the glass would be unreadable in the one way nobody
 *   checks for.
 * ========================================================================== */

namespace qr {

/// Largest version we will draw. v3 = 29 modules; +8 quiet = 37; at 5 px = 185.
constexpr uint8_t MAX_VERSION = 3;

/// Quiet zone, in modules. Four is the specified minimum; less is out of spec.
constexpr int QUIET = 4;

/// The largest square inside the disc: side / sqrt(2). See Draw for why this, and
/// not SCREEN_SIZE, is the bound.
constexpr int INSCRIBED = (int)(SCREEN_SIZE * 0.7071f);

/**
 * Encode `text` at the smallest version that holds it. False if it will not fit
 * MAX_VERSION. ONE rule for the version, shared by Draw and SideFor, so a caller
 * that lays out around the symbol before drawing it cannot disagree with the draw.
 */
inline bool Encode(const char* text, QRCode& qrcode)
{
    if (text == nullptr || text[0] == '\0') return false;

    // Pick the smallest version that holds the text. ECC LOW on purpose: this is
    // a clean backlit screen at arm's length, not a label on a dusty crate, and
    // a lower ECC means fewer, larger modules -- which is what actually decides
    // whether a phone locks on across a room.
    // qrcode_getBufferSize() is a FUNCTION, so it cannot size a static array.
    // The size is reproduced as a constexpr -- a QR of version v is (4v+17)
    // modules square, one bit each, rounded up to bytes -- and then CHECKED
    // against the library's own answer below rather than trusted. A transcribed
    // formula is the weak form of a contract; the assert is what makes it safe.
    constexpr int SIDE = 4 * MAX_VERSION + 17;
    constexpr size_t BUFSZ = (size_t)((SIDE * SIDE + 7) / 8);
    static uint8_t buf[BUFSZ];
    if (qrcode_getBufferSize(MAX_VERSION) > BUFSZ) return false;  // formula drifted

    // PICK THE VERSION FROM CAPACITY, NOT FROM THE RETURN CODE.
    //
    // This loop used to trust `qrcode_initText(...) == 0` to mean "it fit". It
    // does not. The library's only failure path is `if (mode < 0) return -1`,
    // which fires when a CHARACTER cannot be encoded -- there is no capacity
    // check anywhere. Hand version 1 a 20-byte URL and it overflows, pads, and
    // reports success.
    //
    // The result was a structurally perfect version-1 symbol -- correct finder
    // patterns, timing patterns, format bits and mask -- wrapped around an
    // unreadable payload. A phone locks onto it, draws a box, and offers
    // nothing, which reads as "the camera can't see it" and sent this chasing
    // module size through two flashes. The tell was that the symbol LOOKED
    // different from a correct one: 21 modules instead of 25.
    //
    // Byte-mode capacities at ECC_LOW, from the QR spec. Transcribed, so the
    // assert below checks the choice against the library's own grid size rather
    // than trusting the table.
    static const uint16_t BYTE_CAPACITY_L[] = { 17, 32, 53 };   // v1, v2, v3
    size_t len = 0;
    while (text[len] != '\0') ++len;

    uint8_t version = 0;
    for (uint8_t v = 1; v <= MAX_VERSION; ++v) {
        if (len > BYTE_CAPACITY_L[v - 1]) continue;             // genuinely too big
        if (qrcode_initText(&qrcode, buf, v, ECC_LOW, text) != 0) continue;
        version = v;
        break;
    }
    if (version == 0) return false;   // too long -- caller falls back to text

    // The library's own answer for the grid it just built. If this ever
    // disagrees with the version chosen above, the capacity table has drifted
    // from the library and we draw nothing rather than something undecodable.
    if (qrcode.size != 4 * version + 17) return false;
    return true;
}

/// The side, in px INCLUDING the quiet zone, that Draw would give `text` at `px`
/// per module -- or 0 if Draw would refuse it. For laying out around the symbol
/// before it is drawn.
inline int SideFor(const char* text, int px)
{
    QRCode qrcode;
    if (!Encode(text, qrcode)) return 0;
    const int side = (qrcode.size + 2 * QUIET) * px;
    return side > INSCRIBED ? 0 : side;
}

/**
 * Draw `text` centred at (cx, cy).
 *
 * Returns false when the text will not fit MAX_VERSION, or when the symbol would
 * not fit the disc -- the caller then draws something else rather than a
 * truncated or overflowing code. A QR that is 90% present is not 90% useful; it
 * is a black square that wastes the owner's time.
 *
 * (cx, cy) off the disc's centre is the CALLER's proof to make: the bound below is
 * the inscribed square, which only holds for a centred symbol. SetupQr.h's Place()
 * raises the setup QR and checks its corners against the radius itself.
 */
template <typename Canvas>
bool Draw(Canvas& g, const char* text, int cx, int cy, int px, int* sideOut = nullptr)
{
    if (sideOut) *sideOut = 0;
    QRCode qrcode;
    if (!Encode(text, qrcode)) return false;

    const int modules = qrcode.size + 2 * QUIET;
    const int side    = modules * px;

    // A SQUARE ON A ROUND DISC. The bound is the INSCRIBED square, not the
    // screen width: the largest square inside a 240 px circle is 240/sqrt(2) =
    // 169 px, so a 198 px symbol "fits the screen" and still loses its corners
    // off the glass -- taking the finder patterns with them, which is the one
    // part of a QR that cannot be lost. This renderer has already shipped three
    // off-glass defects; checking against SCREEN_SIZE would have been the fourth.
    //
    // The symbol must also be CENTRED for this bound to hold. An off-centre
    // square of the same size has a corner further from the middle and can fall
    // outside even when the arithmetic below passes.
    if (side > INSCRIBED) return false;

    const int x0 = cx - side / 2;
    const int y0 = cy - side / 2;

    // The quiet zone is drawn as part of the symbol -- filling the whole square
    // white first is what creates it. Drawing only the dark modules onto the
    // black UI would produce a borderless, inverted code: both defects at once.
    g.fillRect(x0, y0, side, side, lgfx::color888(255, 255, 255));
    for (int my = 0; my < qrcode.size; ++my) {
        for (int mx = 0; mx < qrcode.size; ++mx) {
            if (!qrcode_getModule(&qrcode, mx, my)) continue;
            g.fillRect(x0 + (mx + QUIET) * px, y0 + (my + QUIET) * px, px, px,
                       lgfx::color888(0, 0, 0));
        }
    }
    // The side it DREW, so a caller lays out around the real symbol rather than
    // re-deriving the version with a copy of the capacity table above.
    if (sideOut) *sideOut = side;
    return true;
}

} // namespace qr
