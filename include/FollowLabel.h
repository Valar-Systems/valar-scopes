#pragma once

#include <stddef.h>
#include <stdint.h>   // HeaderForm's underlying type

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


/**
 * Word-wrap `text` across at most `maxLines`, given each line's usable width.
 *
 * ALL OR NOTHING. Returns the number of lines used, or 0 if the text does not
 * fit -- never a partial result, and never a shortened one.
 *
 * WHY IT REFUSES INSTEAD OF TRUNCATING. The arc face used to clamp its
 * explanation to the available width and append "...". On SIGNAL LOST that cut
 *
 *     "Out of receiver range, not off the radar."
 *
 * down to "Out of receiver range, not" -- which is not a shortened sentence, it
 * is a DIFFERENT one, and it reads as complete. The clause carrying the whole
 * meaning ("not off the radar") is exactly the clause a right-truncation
 * removes, because reassurance lives at the end of a sentence and the caveat
 * lives at the start.
 *
 * So the renderer no longer decides which half of a sentence the customer gets.
 * If a string does not fit its region, that is a fact about the STRING, and the
 * copy changes deliberately -- which a caller can only do if this function
 * refuses rather than quietly coping.
 *
 * Fixed advance per character: the panel font is 6 px at size 1, and both this
 * and the host test measure the same way.
 */
inline int WrapBreaks(const char* text, const int* widthPx, int maxLines,
                      int charPx, int* startOut, int* lenOut)
{
    if (!text || !widthPx || !startOut || !lenOut || maxLines <= 0 || charPx <= 0)
        return 0;
    int n = 0; while (text[n]) ++n;
    int pos = 0, line = 0;
    while (pos < n && line < maxLines) {
        while (text[pos] == ' ') ++pos;          // no line starts with a space
        if (pos >= n) break;
        const int budget = widthPx[line] / charPx;
        if (budget <= 0) return 0;
        int take = n - pos;
        if (take > budget) {
            // Break at the last space that fits; a word longer than the line
            // cannot be placed at all, and that is a refusal, not a hyphenation.
            int brk = -1;
            for (int i = 0; i <= budget && pos + i < n; ++i)
                if (text[pos + i] == ' ') brk = i;
            if (brk <= 0) return 0;
            take = brk;
        }
        startOut[line] = pos;
        lenOut[line]   = take;
        pos += take;
        ++line;
    }
    while (pos < n && text[pos] == ' ') ++pos;
    return (pos >= n) ? line : 0;   // leftover text means it did not fit
}


/**
 * WHAT THE GLOBE'S HEADER ROW CARRIES, decided by what FITS rather than by what
 * was concatenated.
 *
 * THE DEFECT THIS ENCODES AWAY. The header was built as
 * `label + "  " + org + " -> " + dst` and handed whole to a width-fitter, which
 * cuts from the right and appends "...". At y=26 the chord holds 23 characters
 * and "BENCH-LHR-JFK  LHR -> JFK" is 25, so the glass showed
 *
 *     LHR -...
 *
 * with the destination deleted. A string appended without measuring the space it
 * lands in -- and the ellipsis, which is honest on a single identifier, lies in a
 * composite: it attaches to whichever component came last, so the first reads as
 * whole and the rest read as absent. A PARTIAL AIRPORT CODE IS A DIFFERENT
 * AIRPORT.
 *
 * SO: MEASURE, THEN FIT OR OMIT. Each component goes in whole or not at all.
 * The MEASURING stays at the call site, where the font and the panel are; the
 * POLICY lives here, where it can be graded without either.
 */
enum class HeaderForm : uint8_t {
    Nothing,          ///< nothing safe to draw -- draw nothing
    LabelOnly,        ///< the codes are at the route's ends; header names the flight
    Combined,         ///< "<label>  ORG -> DST" on one row
    RouteThenLabel,   ///< route on the header row, label whole on the row below
    RouteOnly,        ///< the label will not fit whole anywhere; codes still shown
};

/**
 * @param labelAtEnds  the codes are drawn at the route's endpoints
 * @param combinedFits "<label>  ORG -> DST" fits the header row
 * @param routeFits    "ORG -> DST" alone fits the header row
 * @param labelFits    the label alone fits a row
 *
 * WHEN labelAtEnds IS FALSE THE CODES CANNOT BE THE PART THAT IS DROPPED --
 * that flag being false is precisely what means they appear nowhere else on the
 * face. A globe with an unnamed route is a picture with no caption; a globe
 * whose caption omits the flight is still a route somebody chose to watch.
 */
inline HeaderForm HeaderFormFor(bool labelAtEnds, bool combinedFits,
                                bool routeFits, bool labelFits)
{
    if (labelAtEnds) return HeaderForm::LabelOnly;
    if (combinedFits) return HeaderForm::Combined;
    if (routeFits)   return labelFits ? HeaderForm::RouteThenLabel
                                      : HeaderForm::RouteOnly;
    return HeaderForm::Nothing;
}

} // namespace follow
