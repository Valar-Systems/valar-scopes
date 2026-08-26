#include "ConfigMigration.h"

#include <Arduino.h>
#include <Preferences.h>

void configmigration::Apply()
{
    Preferences prefs;
    if (!prefs.begin("config", false)) {
        Serial.println("[cfg-migrate] cannot open config namespace; skipping");
        return;
    }

    const int stored = prefs.getInt("cfg-rev", 0);
    if (stored >= CONFIG_REV) {
        prefs.end();
        return; // nothing to do; the common path on every boot after the first
    }

    if (NeedsInfoFieldReset(stored)) {
        // REMOVE, don't overwrite. An absent key is what makes the firmware
        // default apply -- AircraftManager reads
        // `stored.isEmpty() ? defaultOn : (stored == "true")`, so clearing hands
        // the decision back to the build rather than making a new one here.
        const bool hadType = prefs.isKey("info-type");
        const bool hadOp   = prefs.isKey("info-operator");
        if (hadType) prefs.remove("info-type");
        if (hadOp)   prefs.remove("info-operator");
        Serial.printf("[cfg-migrate] rev %d -> %d: cleared info-type=%d info-operator=%d "
                      "(defaults now apply)\n",
                      stored, CONFIG_REV, (int)hadType, (int)hadOp);
    }

    if (NeedsLogbookReset(stored)) {
        // Same REMOVE-don't-overwrite rule as the info fields, and for the same
        // reason: an absent key is the only state a firmware default can reach.
        //
        // Note this clears a key that is almost certainly PRESENT and "false" --
        // every device that has ever saved the config page has one, because the
        // form posts whole and the box has always rendered unticked. That is the
        // population this exists for; see NeedsLogbookReset for why their intent
        // cannot be recovered and why this is a one-shot.
        const bool had = prefs.isKey("logbook");
        const bool wasOn = had && prefs.getString("logbook", "false") == "true";
        if (had) prefs.remove("logbook");
        Serial.printf("[cfg-migrate] rev %d -> %d: cleared logbook=%d (was %s); "
                      "the spotting logbook now defaults ON\n",
                      stored, CONFIG_REV, (int)had, wasOn ? "on" : "off");
    }

    if (NeedsLocalDetailsMigration(stored)) {
        // WRITE, not remove -- see NeedsLocalDetailsMigration. An absent
        // local-details parses as Off, which would silently strip card details
        // from the devices that explicitly asked for them.
        const String det = prefs.isKey("local-details")
                               ? prefs.getString("local-details", "")
                               : String("");
        if (det == "adsbdb") {
            prefs.putString("local-details", "cloud");
            Serial.printf("[cfg-migrate] rev %d -> %d: local-details migrated to cloud "
                          "(details now come from the proxy)\n",
                          stored, CONFIG_REV);
        }
    }

    prefs.putInt("cfg-rev", CONFIG_REV);
    prefs.end();
}
