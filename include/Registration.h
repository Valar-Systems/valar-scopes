#pragma once

/* ============================================================================
 * DOES THIS CALLSIGN LOOK LIKE A TAIL NUMBER?
 *
 * WHY IT EXISTS BEFORE THE FEATURE THAT NEEDS IT. A TIS-B/ADS-R contact carries
 * a track id instead of an ICAO address, so no registry can answer for it and
 * the firmware settles it offline with an empty card. But many of those
 * contacts broadcast their REGISTRATION as the callsign -- N998JS resolves
 * perfectly well by tail, and that is how its type was found by hand on
 * 2026-09-16 after the hex lookup came back unknown.
 *
 * Looking those up would convert known-futile requests into useful ones. The
 * number that decides whether it is worth building is the share of non-ICAO
 * contacts that carry a tail, and nothing measured it -- so this predicate
 * ships first, feeding enrichNonIcaoTail, and the feature reuses it unchanged.
 * Writing it now also means the ratio and the feature cannot disagree about
 * what counts as a tail.
 *
 * SCOPE, STATED. US N-numbers only, deliberately. They are the population the
 * question was asked about, they have an unambiguous shape, and a predicate
 * that guesses at every national prefix would be answering a question nobody
 * has measured yet. Other registries are a later widening, and widening it is
 * safe in a way narrowing is not: a miss costs one request that returns
 * nothing, while a false positive sends a lookup for a callsign that was never
 * a tail.
 *
 * THE SHAPE, from the FAA scheme:
 *
 *   N + 1..5 digits                 N1, N12345
 *   N + 1..4 digits + 1 letter      N1A, N1234Z
 *   N + 1..3 digits + 2 letters     N1AA, N999ZZ
 *
 * At most five characters follow the N, the first of which is a digit and is
 * never 0. I and O never appear in the suffix -- they are excluded to avoid
 * being read as 1 and 0 -- so a callsign containing them is not a tail however
 * much it resembles one.
 *
 * WHAT IT MUST REJECT, and these are the cases that matter: airline callsigns
 * are three letters plus digits (UAL123, DAL42), which this rejects on the
 * leading letter unless the airline code begins with N -- and none of those has
 * a digit immediately after the N, which is why the "first character after N is
 * a digit" rule is load-bearing rather than cosmetic.
 * ==========================================================================*/

namespace registration {

/* The suffix alphabet: no I, no O. */
constexpr bool IsSuffixLetter(char c)
{
    return c >= 'A' && c <= 'Z' && c != 'I' && c != 'O';
}

constexpr bool IsDigit(char c) { return c >= '0' && c <= '9'; }

/*
 * True when `cs` has the shape of a US N-number.
 *
 * Takes a const char* so the same function serves the firmware (which holds
 * Arduino Strings) and the host test (which has no Arduino at all). Callers
 * pass an already-uppercased, already-trimmed string; this does NOT normalise,
 * because a callsign that needed normalising to look like a tail is exactly the
 * sort of near-miss that should not be counted as one.
 */
inline bool LooksLikeRegistration(const char* cs)
{
    if (cs == nullptr) return false;

    if (cs[0] != 'N') return false;
    if (!IsDigit(cs[1]) || cs[1] == '0') return false;   // N0... is not issued

    int digits = 0, letters = 0;
    int i = 1;
    while (IsDigit(cs[i])) { ++digits; ++i; }
    while (IsSuffixLetter(cs[i])) { ++letters; ++i; }

    if (cs[i] != '\0') return false;          // trailing junk, or I/O, or a symbol
    if (digits < 1 || digits > 5) return false;
    if (letters > 2) return false;
    if (digits + letters > 5) return false;   // at most five characters follow the N
    if (letters == 2 && digits > 3) return false;
    if (letters == 1 && digits > 4) return false;
    return true;
}

}  // namespace registration
