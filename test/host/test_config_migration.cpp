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
    check(!NeedsInfoFieldReset(2), "rev 2 is current -- must NOT be reset");
    check(!NeedsInfoFieldReset(3), "a FUTURE rev must NOT be reset");

    // The stamp the device writes must be the one the predicate calls current,
    // or Apply() would migrate forever. Pins the two constants together.
    check(!NeedsInfoFieldReset(CONFIG_REV), "CONFIG_REV must satisfy its own predicate");

    if (failures == 0) std::printf("test_config_migration: all checks passed\n");
    else               std::printf("test_config_migration: %d FAILURE(S)\n", failures);
    return failures == 0 ? 0 : 1;
}
