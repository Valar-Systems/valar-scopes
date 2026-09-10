#pragma once

/* ===========================================================================
 * BENCH-ONLY: FORCE THE SOLAR-NIGHT STATE.
 *
 * WHY THIS EXISTS (2026-09-10). Gate B3 -- the only reading in this programme a
 * log cannot take -- needs a person watching a DIMMED panel through a reboot.
 * Two things make that hard to schedule:
 *
 *   * the dim depends on solar elevation, so it is only true at night, and
 *   * the person has to be awake, in a dark room, at that hour.
 *
 * The first attempt ran at 22:00 local and produced nothing, because the board
 * turned out not to be dimmed at all. Waiting another night to retry a gate that
 * costs somebody their evening is the wrong trade when the precondition can be
 * created on demand.
 *
 * So this forces `night` true. It does NOT force the dim: the production
 * expression stays `autoDim && night`, so a board with auto-dim switched off
 * still will not dim, and the run is invalid for the same reason a real night
 * would be. That is deliberate -- the override supplies the CONDITION, and
 * production logic still decides what to do about it. An override that reached
 * past `autoDim` straight to `setBrightness` would be testing itself.
 *
 * SAME DISCIPLINE AS BLIPSCOPE_QUIET_HOUR, for the same reason: the risk is not
 * that the override fails, it is that a bench build ESCAPES -- reaching a
 * customer, or far likelier, a capture from one being read weeks later and its
 * permanently-dim panel taken for a defect. So it announces itself in the boot
 * log, and `** BENCH OVERRIDE **` must be provably ABSENT from the shipping ELF
 * (verified with a positive control against a bench ELF, because a bare "0
 * occurrences" is equally consistent with "my grep cannot read this file").
 *
 * FORCE_NIGHT is constexpr false in a normal build, so the announcement string
 * is folded out entirely rather than merely unreached.
 * ======================================================================== */

namespace nightoverride {

#ifdef BLIPSCOPE_FORCE_NIGHT
constexpr bool FORCE_NIGHT = true;
#else
constexpr bool FORCE_NIGHT = false;
#endif

/**
 * Apply the override to a computed solar-night verdict.
 *
 * Takes the real answer and returns what the board should act on, so the call
 * site reads as one expression and there is no second place where "is it night"
 * can be decided differently.
 */
constexpr bool Resolve(bool computedNight)
{
    return FORCE_NIGHT ? true : computedNight;
}

} // namespace nightoverride
