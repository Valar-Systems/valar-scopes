#pragma once

#include <stdint.h>
#include <string.h>   // strcmp -- NOT transitive in the pure host build

/**
 * One-shot migrations for stored configuration.
 *
 * WHY THIS EXISTS -- A CLASS OF BUG, NOT AN INSTANCE.
 *
 * A firmware default only ever reaches a key that was NEVER SAVED. The config
 * page posts the whole form, and an unchecked box is absent from the body, so
 * ConfigurationWebServer's SaveToggle() writes an explicit "false" for it. From
 * that moment the key has a value, and no future change to `defaultOn` can ever
 * reach that device again.
 *
 * So the first time a customer saves ANYTHING -- and setting a location, which
 * every device needs, is a whole-form save -- the defaults in force that day are
 * frozen into their NVS as explicit values. Changing a default in firmware
 * afterwards is a change for factory-fresh boards only.
 *
 * That is why #238 (aircraft type + operator ship on) could not reach a single
 * configured device, and why it needed this rather than another default change.
 *
 * THE RULE THIS ESTABLISHES: any future change to a `defaultOn` needs a
 * migration alongside it, or it reaches nobody who already owns the product.
 */
namespace configmigration {

/**
 * Current configuration schema revision, stored under "cfg-rev".
 *
 *   (absent/0) pre-migration
 *   1          reserved: the implicit revision of every build before this
 *   2          info-type / info-operator default ON (#238)
 *   3          spotting logbook default ON (v8)
 *   4          local-details "adsbdb" -> "cloud" (adsbdb left the stack)
 */
constexpr int CONFIG_REV = 4;

/**
 * The firmware default for the spotting logbook.
 *
 * ON, decided for v8. The collection game is the out-of-box hook the printed
 * insert card is built around, so a device that collects nothing until its owner
 * finds a checkbox is the wrong product. It also means a unit already in the
 * field starts collecting the moment it takes the update, with no action from
 * the owner.
 */
constexpr bool LOGBOOK_DEFAULT_ON = true;

/**
 * Resolve one stored toggle against the firmware default.
 *
 * `stored` is the raw NVS string: nullptr or "" means the key was never written,
 * which is the ONLY state a firmware default can reach. Anything else is an
 * explicit value and wins.
 *
 * Exists as ONE function so the DEVICE and the CONFIG PAGE cannot disagree about
 * what a default is. They previously each hardcoded their own answer, and the
 * page's copy of the logbook default was a bare "false" literal -- so flipping
 * the firmware default alone would have shipped a device that collects while its
 * own settings page shows the box unticked.
 */
inline bool ResolveToggle(const char* stored, bool defaultOn)
{
    if (stored == nullptr || stored[0] == 0)
        return defaultOn;
    return strcmp(stored, "true") == 0;
}

/**
 * Should the info-field keys be CLEARED for a device at `storedRev`?
 *
 * Pure, so it is host-tested. The NVS work lives in Apply().
 *
 * CLEARED, NOT FORCED TRUE -- and the distinction is the whole design. Deleting
 * says "you never made a choice about this", which is the truth: the old default
 * was off, so nobody had to uncheck anything to end up with off. Forcing true
 * would instead overwrite a genuine preference, and would do it again on any
 * future device that legitimately wanted these off.
 *
 * The cost, stated rather than hidden: someone who deliberately unchecked those
 * two fields gets them back once. That population can only be reached by having
 * ticked them first, so it is a subset of a subset; everyone else never had them
 * at all and could not tell the product was withholding them.
 */
constexpr bool NeedsInfoFieldReset(int storedRev) { return storedRev < 2; }

/**
 * Should the `logbook` key be CLEARED for a device at `storedRev`?
 *
 * THE HONEST PART, because the brief asked for something that cannot be done.
 *
 * The ask was: units that never touched the toggle get the new default, units
 * that deliberately turned it off stay off. For devices that ALREADY EXIST those
 * two states are byte-identical. The box has always rendered unticked, and the
 * config page posts the whole form, so SaveToggle() wrote an explicit "false"
 * the first time the owner saved anything at all -- and setting a location,
 * which every device needs, is a whole-form save. Nothing recorded intent,
 * because until now there was no intent to record: nobody had to untick a box
 * that was never ticked.
 *
 * So for the existing population the choice is forced, and this clears the key --
 * everyone gets the new default. At the current fleet size that population is
 * three boards and none of them chose off. The cost, stated rather than hidden:
 * if anyone HAD deliberately turned the logbook off, they get it back once.
 *
 * GOING FORWARD THE ASK IS SATISFIED, AND THAT IS WHY THIS IS A ONE-SHOT. Once
 * the default is ON the box renders TICKED, so a stored "false" can only have
 * come from someone actively unticking it. From rev 3 onward `logbook=false`
 * means what the brief wanted it to mean, and no future migration may clear it.
 */
constexpr bool NeedsLogbookReset(int storedRev) { return storedRev < 3; }

/**
 * Does this device still hold "adsbdb" as its local-receiver detail source?
 *
 * THIS ONE IS NOT A DEFAULT CHANGE -- IT IS A DELETED ENUM VALUE, and that makes
 * it the one migration that CANNOT be skipped.
 *
 * The other two in this file clear a key so a firmware default can reach it; skip
 * them and a device merely keeps an old behaviour. Skip this one and a device
 * boots holding a string that no longer maps to anything. AircraftManager parses
 * an unrecognised value as LocalDetails::Off, so the device would silently stop
 * showing card details entirely -- and it would do so on exactly the population
 * that went out of its way to opt INTO detail lookups.
 *
 * WRITE, don't remove. The other migrations remove because absence is what lets a
 * default apply; here absence means Off, which is the wrong answer. The owner
 * asked for remote detail lookups and that request is still satisfiable -- only
 * the party serving them changed, from a third party we had no permission to use
 * to our own proxy. So the stored value is rewritten rather than cleared.
 *
 * If the device has no proxy configured, UseCloudEnrich() is false and it lands
 * on the same no-lookup behaviour it would have had anyway -- but by a route that
 * is legible in the config page rather than by a parse failure.
 */
constexpr bool NeedsLocalDetailsMigration(int storedRev) { return storedRev < 4; }

/**
 * Run any pending migrations against the "config" namespace.
 *
 * Idempotent: stamps CONFIG_REV on completion, so a second boot is a no-op.
 * Must run BEFORE the app reads its settings, or the app reads the pre-migration
 * values for one session.
 */
void Apply();

} // namespace configmigration
