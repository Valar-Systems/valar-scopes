#pragma once
#include <Arduino.h>
#include <stdint.h>

/* ===========================================================================
 * EVERY BRIGHTNESS APPLY, NAMED AND LOGGED.
 *
 * WHY (2026-09-10). The reboot flash defect was three separate causes across
 * three call sites, and the only reason it was diagnosable at all was an
 * INFERENCE: `[dim]` prints on a CHANGE, so its presence after a reboot proved
 * the panel had been moved off the carried level by something. That reasoning
 * is correct and it is also two steps removed from the fact.
 *
 * The direct question -- "what values were written to the backlight, in what
 * order, during this boot?" -- had no instrument at all. Three call sites wrote
 * the panel and exactly one of them said so, and it was the one that was
 * BEHAVING. The two that caused the flash were silent.
 *
 * So each apply now names itself. The invariant a reader can now check without
 * knowing any of this history:
 *
 *     across a reboot while dimmed, no [bright] line may exceed the level the
 *     device came back at.
 *
 * WHAT THIS CANNOT SEE, stated because the gap is the reason B3 still needs a
 * person. The backlight can be at full from power-on until the FIRST apply
 * executes -- that is hardware and boot order, not a value anybody wrote. A
 * clean log here is consistent with a panel that flashed before any of this
 * code ran. Serial proves no software-commanded brightening; only glass proves
 * no brightening.
 *
 * Shipping, not bench-only: it is three lines at boot and about two a day
 * after, and it turns "did it flash?" from a question needing a dark room into
 * one a support log can answer. Same argument as [quiet] armed: and the
 * bright= field on the health line -- an invisible quantity made visible at a
 * moment somebody is already looking.
 * ======================================================================== */

namespace brightlog {

/**
 * Record a backlight write. Call IMMEDIATELY before tft.setBrightness().
 *
 * `site` is a short stable token naming WHICH path is writing, because "the
 * value changed" was never the hard part -- "which of the three call sites did
 * it" was.
 */
inline void Applied(const char* site, uint8_t value)
{
    Serial.printf("[bright] %s -> %u\n", site, (unsigned)value);
}

} // namespace brightlog
