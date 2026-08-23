// Host test for the pure half of the config migration.
//
// Only the DECISION is testable off-device -- Apply() touches NVS. That split is
// deliberate and is the same shape as GameFormat: put the rule in a constexpr
// predicate, test the rule, and keep the I/O thin enough to read.
#include <cstdio>
#include "../../include/ConfigMigration.h"

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

    if (failures == 0) std::printf("test_config_migration: all checks passed\n");
    else               std::printf("test_config_migration: %d FAILURE(S)\n", failures);
    return failures == 0 ? 0 : 1;
}
