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

    prefs.putInt("cfg-rev", CONFIG_REV);
    prefs.end();
}
