#pragma once

#include <stddef.h>

/**
 * The identifier shown in Follow's label slot.
 *
 * WHAT THIS IS FOR. The slot normally holds a live ADS-B callsign, which the
 * feed already constrains. With no contact in the table there is no callsign
 * and the label falls back to the follow TARGET -- the one string on that
 * screen arriving verbatim from a field the owner typed. The fallback is
 * deliberate: somebody watching an empty scope needs to know what it is waiting
 * for, and on-screen was never the privacy boundary (§17 governs what LEAVES
 * the device, and this does not leave it).
 *
 * THE HYPHEN IS PART OF THE CHARSET, AND THAT IS THE WHOLE POINT OF THIS FILE.
 * The first version of this filter allowed A-Z and 0-9 only. That mangles every
 * hyphenated registration in the world -- G-ABCD, D-AIBL, VH-OQA become GABCD,
 * DAIBL, VHOQA -- and the result is worse than an unfiltered string, because a
 * mangled identifier still LOOKS like a legitimate one. Nobody reading GABCD
 * off a screen has any way to know it is wrong. Dropping a character that
 * carries meaning is not sanitising, it is corrupting quietly.
 *
 * THERE IS NO LENGTH CAP HERE, DELIBERATELY. AircraftManager::FitToDisc already
 * truncates for the panel, VISIBLY (trailing "...") and by MEASURED WIDTH,
 * which is strictly better than a character count -- it knows the chord it has
 * to fit and the font it is fitting. A second cap in this function was silent,
 * cruder, and fired first, which made the good one unreachable. The only bound
 * below is a cost bound, set far past any real identifier, and it marks itself
 * when it bites.
 */
namespace follow {

/// Far beyond any real callsign or registration. This is a guard against a
/// pathological config string costing a per-frame O(n) fitting loop, NOT a
/// display width -- the panel's own limit is FitToDisc's business.
constexpr size_t LABEL_COST_BOUND = 24;

/// The marker appended when the cost bound bites, so that truncation here is
/// as visible as truncation at the draw layer. Matches FitToDisc's marker on
/// purpose: two different ellipses would read as two different meanings.
constexpr const char* LABEL_TRUNCATED_MARK = "...";

/**
 * Copy `in` to `out`, keeping A-Z, 0-9 and '-', upper-casing a-z, and dropping
 * everything else. Always NUL-terminates when n > 0.
 *
 * Characters outside the set are DROPPED rather than substituted: a placeholder
 * glyph would imply the character mattered, and these are the ones that do not.
 *
 * @return the number of characters written, excluding the NUL.
 */
inline size_t SanitiseLabel(const char* in, char* out, size_t n)
{
    if (out == nullptr || n == 0) return 0;
    out[0] = '\0';
    if (in == nullptr) return 0;

    const size_t markLen = 3;                    // strlen(LABEL_TRUNCATED_MARK)
    size_t w = 0;
    size_t kept = 0;                             // valid chars seen, capped or not
    bool over = false;

    for (const char* p = in; *p != '\0'; ++p) {
        const char c = *p;
        char keep = '\0';
        if (c >= 'a' && c <= 'z')                        keep = (char)(c - 'a' + 'A');
        else if ((c >= 'A' && c <= 'Z') ||
                 (c >= '0' && c <= '9') || c == '-')     keep = c;
        if (keep == '\0') continue;

        ++kept;
        if (kept > LABEL_COST_BOUND) { over = true; continue; }
        if (w + 1 < n) out[w++] = keep;
    }

    // If the cost bound bit, say so in the string itself rather than handing
    // back a shortened identifier that reads as a whole one.
    if (over) {
        while (w + markLen + 1 > n && w > 0) --w;    // make room, if there is any
        for (size_t i = 0; i < markLen && w + 1 < n; ++i)
            out[w++] = LABEL_TRUNCATED_MARK[i];
    }

    out[w] = '\0';
    return w;
}

} // namespace follow
