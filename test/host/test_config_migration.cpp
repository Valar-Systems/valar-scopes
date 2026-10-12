// Host test for the pure half of the config migration.
//
// Only the DECISION is testable off-device -- Apply() touches NVS. That split is
// deliberate and is the same shape as GameFormat: put the rule in a constexpr
// predicate, test the rule, and keep the I/O thin enough to read.
#include <cstdio>
#include <map>
#include <string>
#include <vector>
#include "../../include/ConfigMigration.h"

// A Preferences stand-in for MigrateOverheadSwitch that records every write, so the table below can
// assert which keys a migration TOUCHED, not only what they read afterwards.
struct RecordingPrefs {
    std::map<std::string, std::string> kv;
    std::vector<std::string> writes;
    bool isKey(const char* k) const { return kv.count(k) != 0; }
    std::string getString(const char* k, const char* def) const { auto it = kv.find(k); return it == kv.end() ? std::string(def) : it->second; }
    void putString(const char* k, const char* v) { kv[k] = v; writes.push_back(k); }
};

static int failures = 0;

static void check(bool ok, const char* what)
{
    if (!ok) { std::printf("  FAIL: %s\n", what); ++failures; }
}

int main()
{
    using namespace configmigration;

    // A device that has never been migrated carries no cfg-rev at all, which
    // getInt() reports as 0. That is the population this migration exists for:
    // every unit shipped before #238.
    check(NeedsInfoFieldReset(0), "rev 0 (never migrated) must be reset");

    // Rev 1 is the implicit revision of every build before the key existed. It
    // is listed explicitly so that a future rev 3 cannot accidentally re-run
    // this migration by treating "anything below me" as needing every step.
    check(NeedsInfoFieldReset(1), "rev 1 must be reset");

    // Idempotency, which is what stops a boot loop of migrations: a device that
    // already ran this must not run it again, or a customer who deliberately
    // turned those fields off would have them restored on every single boot.
    check(!NeedsInfoFieldReset(2), "rev 2 ran this -- must NOT be reset");
    check(!NeedsInfoFieldReset(3), "rev 3 is current -- must NOT be reset");

    // The stamp the device writes must be the one the predicate calls current,
    // or Apply() would migrate forever. Pins the two constants together.
    check(!NeedsInfoFieldReset(CONFIG_REV), "CONFIG_REV must satisfy its own predicate");

    // ---- rev 3: the spotting logbook ships ON -------------------------------
    //
    // THE THREE CASES, which are the whole point of the toggle resolving through
    // one function instead of two hardcoded literals.

    // 1. NEVER TOUCHED -> ON. An absent key is the only state a firmware default
    //    can reach, and after the rev-3 migration clears it this is the state
    //    every existing device is in.
    check(ResolveToggle(nullptr, LOGBOOK_DEFAULT_ON), "unset (nullptr) -> the default, ON");
    check(ResolveToggle("", LOGBOOK_DEFAULT_ON), "unset (empty) -> the default, ON");

    // 2. EXPLICIT OFF -> OFF. From rev 3 on this means what it says: the box now
    //    renders TICKED, so a stored "false" can only come from someone actively
    //    unticking it, and nothing may quietly turn it back on.
    check(!ResolveToggle("false", LOGBOOK_DEFAULT_ON), "explicit false -> OFF, default ignored");

    // 3. EXPLICIT ON -> ON.
    check(ResolveToggle("true", LOGBOOK_DEFAULT_ON), "explicit true -> ON");

    // The default must be the thing being defaulted TO, or case 1 passes for the
    // wrong reason and would keep passing if someone flipped it back.
    check(LOGBOOK_DEFAULT_ON, "the logbook default must be ON for v8");

    // The resolver is generic, so prove it is the DEFAULT doing the work in case
    // 1 rather than a hardcoded true: the same unset input must follow a false
    // default too. Without this, ResolveToggle could return true and still pass.
    check(!ResolveToggle(nullptr, false), "unset follows a FALSE default too");
    check(!ResolveToggle("", false), "empty follows a FALSE default too");

    // Anything not exactly "true" is false -- matches the String comparison this
    // replaced, so the change cannot alter the meaning of a value already
    // sitting in somebody NVS.
    check(!ResolveToggle("TRUE", LOGBOOK_DEFAULT_ON), "TRUE is not true");
    check(!ResolveToggle("1", LOGBOOK_DEFAULT_ON), "1 is not true");
    check(!ResolveToggle("yes", LOGBOOK_DEFAULT_ON), "yes is not true");

    // ---- rev 3 migration predicate ------------------------------------------
    check(NeedsLogbookReset(0), "rev 0 must have the logbook key cleared");
    check(NeedsLogbookReset(1), "rev 1 must have the logbook key cleared");
    // Rev 2 is the population that matters: shipped units that took #238, whose
    // owners have all saved the form at least once, so every one of them carries
    // an explicit logbook=false that was never a choice.
    check(NeedsLogbookReset(2), "rev 2 must have the logbook key cleared");
    // ONE SHOT. After this a stored false is a real preference, and clearing it
    // again would overwrite a genuine choice on every boot.
    check(!NeedsLogbookReset(3), "rev 3 is current -- must NOT be cleared again");
    check(!NeedsLogbookReset(4), "a FUTURE rev must NOT be cleared");
    check(!NeedsLogbookReset(CONFIG_REV), "CONFIG_REV must satisfy the logbook predicate");

    // ---- rev 5: tz-offset "0" manufactured by the old form ------------------
    //
    // The old config page defaulted this field to the zone derived from the
    // STORED longitude, which on a factory-fresh device is "" -> 0. The
    // customer's first save (the one that sets their location) posted that 0
    // back as if it were a decision. Three of three bench boards carried one.
    {
        const long BEND = -8 * 3600;   // what the longitude fallback gives at -121.29
        const long UTC0 = 0;           // ...and at Greenwich

        check(NeedsTzOffsetAutoMigration(4, "0", BEND),
              "a stored 0 that disagrees with the location is migrated");
        check(NeedsTzOffsetAutoMigration(0, "0", BEND),
              "...including on a device that has never been migrated at all");

        // THE CONTROL, AND THE WHOLE REASON THE PREDICATE TAKES AN OFFSET.
        // A stored 0 is CORRECT for the UK, Portugal, Iceland, West Africa --
        // and there the fallback returns 0 too, so the migration would change
        // nothing while carrying all of the risk. Without this condition it
        // would clear a genuinely-meant value from every UTC user.
        check(!NeedsTzOffsetAutoMigration(4, "0", UTC0),
              "CONTROL: a stored 0 that AGREES with the location is left alone");

        // An explicit non-zero was unambiguously typed -- the old form never
        // manufactured one, because a manufactured value was always 0.
        check(!NeedsTzOffsetAutoMigration(4, "-7", BEND), "an explicit -7 is a choice, untouched");
        check(!NeedsTzOffsetAutoMigration(4, "5.5", BEND), "...as is a half-hour zone");

        // Never saved: nothing to undo, and removing an absent key is not free --
        // it logs a NOT_FOUND on every affected boot.
        check(!NeedsTzOffsetAutoMigration(4, nullptr, BEND), "an absent key is already auto");
        check(!NeedsTzOffsetAutoMigration(4, "", BEND),
              "an empty value is not the string \"0\"");

        // ONE SHOT, same discipline as the logbook migration: from rev 5 the form
        // no longer manufactures zeros, so a stored 0 means what it says.
        check(!NeedsTzOffsetAutoMigration(5, "0", BEND), "rev 5 is current -- must NOT re-fire");
        check(!NeedsTzOffsetAutoMigration(6, "0", BEND), "a FUTURE rev must NOT re-fire");
        check(!NeedsTzOffsetAutoMigration(CONFIG_REV, "0", BEND),
              "CONFIG_REV must satisfy the tz predicate");

        // Positive zones too -- the condition is "differs from zero", not "is
        // negative", and Asia/Australia is the half of the planet a sign
        // assumption would silently skip.
        check(NeedsTzOffsetAutoMigration(4, "0", 10 * 3600),
              "a stored 0 in Sydney (+10) is migrated too");
    }

    // The earlier migrations must not re-fire at the new revision.
    check(!NeedsInfoFieldReset(CONFIG_REV), "rev 5: the info-field reset is done");
    check(!NeedsLocalDetailsMigration(CONFIG_REV), "rev 5: the local-details migration is done");

    // rev 7 (v17): the overhead SCREEN switch takes the v16 RING state only; the push checkbox
    // (lookup-alert) is left exactly as stored. Never saved -> nothing written -> default ON.
    // The table is the frozen one (docs/v17-overhead-card-predictions.md, addendum 2026-10-12):
    // each v16 store is migrated through the same MigrateOverheadSwitch the firmware runs, against
    // a store that records every write, so "lookup-alert left as stored" is checked, not assumed.
    check(NeedsOverheadMigration(6), "rev 6 devices migrate the overhead switch");
    check(!NeedsOverheadMigration(CONFIG_REV), "CONFIG_REV must satisfy the overhead predicate");
    check(OverheadFromLegacy(nullptr) == nullptr && OverheadFromLegacy("") == nullptr,
          "no lookup value (or an empty one) is never-saved: nothing written");
    {
        struct Row { const char* lookup; const char* alert; const char* ovhOn; const char* name; };
        const Row rows[] = {
            { "true",  "false", "true",  "ring on,  push off" },
            { "false", "true",  "false", "ring off, push on  (the OR rule wrote true; the open consequence)" },
            { "true",  "true",  "true",  "ring on,  push on" },
            { "false", "false", "false", "ring off, push off (the new default does not reach it)" },
            { nullptr, nullptr, nullptr, "never saved       (nothing written; default ON applies)" },
        };
        std::printf("  rev-7 migration table:  lookup | lookup-alert -> ovh-on | lookup-alert after | writes\n");
        for (const Row& r : rows) {
            RecordingPrefs p;
            if (r.lookup) p.kv["lookup"] = r.lookup;
            if (r.alert) p.kv["lookup-alert"] = r.alert;
            const char* wrote = MigrateOverheadSwitch(p);
            const bool hasOvh = p.kv.count("ovh-on") != 0;
            const std::string ovh = hasOvh ? p.kv["ovh-on"] : std::string("(absent)");
            const std::string alertAfter = p.kv.count("lookup-alert") ? p.kv["lookup-alert"] : std::string("(absent)");
            std::printf("    %-6s | %-6s -> %-8s | %-8s | %u  %s\n", r.lookup ? r.lookup : "absent", r.alert ? r.alert : "absent",
                        ovh.c_str(), alertAfter.c_str(), (unsigned)p.writes.size(), r.name);
            char what[160];
            std::snprintf(what, sizeof what, "%s: ovh-on %s", r.name, r.ovhOn ? r.ovhOn : "not written");
            check(r.ovhOn ? (hasOvh && ovh == r.ovhOn && wrote && std::string(wrote) == r.ovhOn) : (!hasOvh && !wrote), what);
            std::snprintf(what, sizeof what, "%s: lookup-alert exactly as stored", r.name);
            check(r.alert ? (p.kv.count("lookup-alert") && p.kv["lookup-alert"] == r.alert) : !p.kv.count("lookup-alert"), what);
            std::snprintf(what, sizeof what, "%s: the only key written is ovh-on", r.name);
            check(p.writes.size() == (r.ovhOn ? 1u : 0u) && (p.writes.empty() || p.writes[0] == "ovh-on"), what);
        }
        RecordingPrefs set;                                   // an ovh-on already present is never overwritten
        set.kv["lookup"] = "true"; set.kv["ovh-on"] = "false";
        check(MigrateOverheadSwitch(set) == nullptr && set.kv["ovh-on"] == "false" && set.writes.empty(),
              "ovh-on already present: left as it is, nothing written");
    }

    // rev 6 (v15): coordinates rewritten at 4 dp, once.
    check(NeedsCoordPrecisionMigration(5), "rev 5 devices rewrite their coordinates at 4 dp");
    check(NeedsCoordPrecisionMigration(0), "a pre-migration device does too");
    check(!NeedsCoordPrecisionMigration(CONFIG_REV), "CONFIG_REV must satisfy the coordinate predicate");
    check(sizeof(COORD_KEYS) / sizeof(COORD_KEYS[0]) == 8, "latitude/longitude + three presets x2");

    // The summary comes last: it used to sit above the rev-6 checks and so reported before they ran.
    if (failures == 0) std::printf("test_config_migration: all checks passed\n");
    else               std::printf("test_config_migration: %d FAILURE(S)\n", failures);
    return failures == 0 ? 0 : 1;
}
