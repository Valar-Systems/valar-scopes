#pragma once

/* ============================================================================
 * WHY A DETAIL CARD HAS NOTHING TO SHOW -- said out loud, once, in one place.
 *
 * THE DEFECT THIS EXISTS FOR was reported as "the plane has no data at all
 * except for the callsign". It took an afternoon to establish that the backend
 * held a full record, the device had asked and been answered, and the contact
 * was simply a TIS-B/ADS-R relay whose address is a track id no registry can
 * hold. The firmware KNEW that -- it settles those offline and has since
 * counted them as enrichNonIcao -- and the card rendered the same empty space
 * it renders for a lookup that failed.
 *
 * Two different facts, one appearance. A relayed contact will NEVER resolve;
 * a failed lookup might on the next pass. Telling them apart is the whole of
 * this file.
 *
 * NOTHING HERE STATES A FACT THE DEVICE CANNOT KNOW. The rule is the one the
 * Follow ocean copy is held to, and it is asserted rather than remembered: no
 * line contains a digit. A digit here would be an invented altitude, an
 * invented time, or an invented count -- and the one screen whose whole job is
 * to explain an absence is the worst possible place to manufacture a number.
 *
 * SHORT LINES, IN PRIORITY ORDER, because the caller drops what does not fit.
 * The disc holds roughly 38 characters at card text size and the card already
 * carries distance, altitude, speed and heading. "Relayed contact -- registration
 * not in signal." is forty-five characters and would simply vanish. So the
 * sentence is split at its natural break, the indispensable half first: a
 * reader who gets one line gets the word that answers the question.
 * ==========================================================================*/

// uint8_t for the enum's underlying type. Omitting this made the enum fail to
// parse and produced four confusing errors about scoped enums, none of which
// mentioned the missing include.
#include <cstdint>

namespace cardcopy {

/// Why the identity fields are empty.
enum class Absence : uint8_t {
    None,      ///< there IS identity to show -- say nothing
    Relayed,   ///< a TIS-B/ADS-R track id: no registry can ever answer
    NotFound,  ///< we asked, and the answer was empty
};

/// The most lines any absence explains itself in.
constexpr int MAX_LINES = 4;

/* ---- HOW LONG A LINE MAY BE, and why this is a constant rather than a habit.
 *
 * The card's line() DROPS anything that will not fit, silently and by design --
 * dropping beats truncating, because a cut word is a wrong word. The cost is
 * that an over-long string does not look wrong in the source, does not warn,
 * and does not appear. A forty-five character first line was written for this
 * very feature and would have vanished without a trace.
 *
 * So the limit is stated here beside the strings, and the host test asserts BOTH
 * halves: that every string fits MAX_CHARS, and that MAX_CHARS itself still fits
 * the disc. The second half matters because a number copied from a measurement
 * goes stale the moment the geometry moves, and then the first half is checking
 * the copy against a fiction.
 */
constexpr int CHAR_W    = 6;    ///< advance per character at card text size
constexpr int LINE_H    = 10;   ///< card line height, for the chord lookup
constexpr int ROW_Y     = 160;  ///< the LOWEST row this block realistically reaches;
                                ///< the chord is narrowest there, so it decides
constexpr int MAX_CHARS = 33;

/// Fill `out` with up to MAX_LINES short lines, most important first, and
/// return how many. Returns 0 for Absence::None -- an aircraft with a type and
/// an operator needs no explanation, and adding one would be noise on the
/// overwhelming majority of cards.
inline int Lines(Absence why, const char* out[MAX_LINES])
{
    switch (why) {
        case Absence::Relayed:
            // "Relayed" is the load-bearing word: it says the absence is a
            // property of the signal rather than of our lookup, so nobody goes
            // looking for a fault. The rest explains it if there is room.
            out[0] = "RELAYED CONTACT";
            out[1] = "Registration not in signal";
            out[2] = "Ground stations relay this";
            out[3] = "aircraft without its ID";
            return 4;
        case Absence::NotFound:
            // Deliberately not "unknown aircraft": we asked and got nothing,
            // which is a statement about this lookup and not about the
            // aeroplane. It may well resolve on a later pass.
            out[0] = "No details found";
            out[1] = "for this aircraft";
            return 2;
        case Absence::None:
        default:
            return 0;
    }
}

}  // namespace cardcopy
