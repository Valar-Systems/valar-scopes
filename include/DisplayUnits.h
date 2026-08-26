#pragma once

#include <Arduino.h>

/**
 * ONE PLACE THAT KNOWS HOW LONG A MILE IS.
 *
 * ============================================================================
 * WHY THIS EXISTS
 *
 * Before this header, `1.609344` was inlined at seven sites in
 * AircraftManager.cpp and once more as a JS literal in ConfigurationWebServer,
 * each with its own two-way `if (rangeUnit == "mi")` branch. Adding a third unit
 * to that shape means editing eight branches correctly, and the failure mode is
 * not a crash: a missed site renders statute miles under an "nmi" label.
 *
 * A wrong-but-labelled distance is worse than a missing feature, and worse still
 * on a product aimed at pilots -- 138 mi and 138 nmi are twenty-two miles apart,
 * which is the difference between a nearby aircraft and one over the horizon.
 * Nothing on screen would look wrong.
 *
 * So the conversion, the label, and the list of valid units live here, together,
 * and every render site calls in. Adding a fourth unit is one edit to one file.
 *
 * ============================================================================
 * THE CONSTANTS
 *
 *   1 statute mile   = 1.609344 km   exactly (international mile, by definition)
 *   1 nautical mile  = 1.852    km   exactly (BIPM/ICAO, by definition)
 *
 * Both are exact by definition rather than measured, so they are written in full
 * and never rounded.
 *
 * ============================================================================
 * INTERNAL REPRESENTATION IS ALWAYS KILOMETRES.
 *
 * Feeds arrive in SI, positions are computed in km, and the stored radius
 * (`rangeKmCfg`) is km. Display units exist only at the edge -- the moment a
 * number becomes a string a human reads. Keep it that way: a conversion that
 * leaks inward becomes a second source of truth about what a distance means.
 */
namespace units {

/** Exact by definition -- the international statute mile. */
constexpr double KM_PER_MILE = 1.609344;
/** Exact by definition -- the nautical mile (BIPM, ICAO). */
constexpr double KM_PER_NMI = 1.852;

/**
 * The default when nothing is stored.
 *
 * `mi`, not `km`. Buyers are predominantly American, and this was decided while
 * the store was still draft -- so there is no installed base to migrate and no
 * cfg-rev cost. A device that has never touched the toggle reads this; a device
 * that stored ANY value keeps it, because a stored value is a decision someone
 * made and a default only ever reaches keys that were never written.
 */
inline const char* DefaultUnit() { return "mi"; }

/** True if `u` is a unit this firmware understands. */
inline bool IsValid(const String& u) { return u == "km" || u == "mi" || u == "nmi"; }

/**
 * Normalise a stored/config value to a unit this code can use.
 *
 * An unrecognised value falls to the default rather than being trusted. That
 * matters on a downgrade: firmware that predates `nmi` must not treat the string
 * as a unit it does not know and silently render kilometres under an nmi label.
 */
inline String Normalise(const String& stored) {
  return IsValid(stored) ? stored : String(DefaultUnit());
}

/** Kilometres -> the display unit. */
inline float FromKm(float km, const String& unit) {
  if (unit == "mi") return km / (float)KM_PER_MILE;
  if (unit == "nmi") return km / (float)KM_PER_NMI;
  return km;
}

/** The display unit -> kilometres. The inverse of FromKm, and its only inverse. */
inline double ToKm(double value, const String& unit) {
  if (unit == "mi") return value * KM_PER_MILE;
  if (unit == "nmi") return value * KM_PER_NMI;
  return value;
}

/**
 * Format a kilometre distance for display, in `unit`, with the label attached.
 *
 * One decimal below 10, none above -- the existing convention at every call
 * site, kept here so it cannot drift between the card and the ntfy body. A
 * detail card reading "9.4 mi" and an alert reading "9 mi" for the same aircraft
 * is the kind of inconsistency nobody files a bug about and everybody notices.
 *
 * `space` puts a gap before the label: the list column is tight ("123mi") while
 * prose lines want "123 mi".
 */
inline String FormatKm(float km, const String& unit, bool space = false) {
  const float v = FromKm(km, unit);
  return String(v, v < 10.0f ? 1 : 0) + (space ? " " : "") + unit;
}

/** The value alone, no label -- for the ring labels, which label only the outer ring. */
inline String ValueKm(float km, const String& unit) {
  const float v = FromKm(km, unit);
  return String(v, v < 10.0f ? 1 : 0);
}

} // namespace units
