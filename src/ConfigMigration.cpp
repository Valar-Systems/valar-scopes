#include "ConfigMigration.h"
#include "CoordParse.h"
#include "LocalOffset.h"

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

    {
        // tz-offset "0" manufactured by the old form -> auto (absent key).
        //
        // The derived offset comes from localoffset::Resolve with an EMPTY tz, so
        // the migration asks the same question the fallback will answer rather
        // than reimplementing 15-degrees-per-hour here. One derivation, which is
        // the rule that LocalOffset.h exists to enforce -- a second copy is how
        // the original defect got in.
        const String tz = prefs.isKey("tz-offset") ? prefs.getString("tz-offset", "")
                                                   : String();
        const String lon = prefs.isKey("longitude") ? prefs.getString("longitude", "")
                                                    : String();
        const long derived = localoffset::Resolve("", lon.toFloat());
        if (prefs.isKey("tz-offset")
            && NeedsTzOffsetAutoMigration(stored, tz.c_str(), derived)) {
            prefs.remove("tz-offset");
            // Printed under [quiet] rather than [cfg-migrate] deliberately: this is
            // read while asking "why did it reboot then", and that reader is
            // grepping for quiet, not for migrations.
            Serial.printf("[quiet] migrated tz-offset \"0\" -> auto (derived %+ld s)\n",
                          derived);
        }
    }

    if (NeedsCoordPrecisionMigration(stored)) {
        // Rewritten through CoordParse -- the same parse and the same Format the
        // save path uses -- so a migrated value is byte-identical to one saved
        // fresh. Counts only: a coordinate is never printed.
        int rewritten = 0;
        for (const CoordKey& ck : COORD_KEYS) {
            if (!prefs.isKey(ck.key)) continue;
            const String raw = prefs.getString(ck.key, "");
            if (raw.isEmpty()) continue;
            double v = 0.0;
            if (!CoordParse::Parse(raw, ck.isLat, v)) continue;   // unreadable: leave it
            const String f = CoordParse::Format(v);
            if (f != raw) { prefs.putString(ck.key, f); ++rewritten; }
        }
        Serial.printf("[cfg-migrate] rev %d -> %d: coordinates stored at 4 dp "
                      "(%d key(s) rewritten)\n", stored, CONFIG_REV, rewritten);
    }

    prefs.putInt("cfg-rev", CONFIG_REV);
    prefs.end();
}
