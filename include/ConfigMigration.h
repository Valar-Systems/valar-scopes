#pragma once

#include <stdint.h>

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
 */
constexpr int CONFIG_REV = 2;

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
 * Run any pending migrations against the "config" namespace.
 *
 * Idempotent: stamps CONFIG_REV on completion, so a second boot is a no-op.
 * Must run BEFORE the app reads its settings, or the app reads the pre-migration
 * values for one session.
 */
void Apply();

} // namespace configmigration
