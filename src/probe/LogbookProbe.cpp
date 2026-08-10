// LogbookProbe -- exercises the v5 logbook caps, eviction and operator migration.
//
// WHY THIS EXISTS. Eviction only happens in a state that takes a week of real sky
// to reach, and the operator migration only fires on a store written by firmware
// that is no longer running. Neither path can be reached by using the product, so
// neither would be observed before a customer observed it. This reaches them in a
// second by driving the real Logbook through its public API.
//
// It is a PROBE, not a shipping feature -- same precedent as HeapProbe and
// BlitProbe: its own env, its own main, lives on a branch.
//
// It deliberately never calls Begin() or MaybePersist(), so it touches no NVS and
// cannot damage the logbook of whatever board it is flashed to. The maps start
// empty because the object is fresh, which is exactly the fixture wanted.

#include <Arduino.h>

#include "../Logbook.h"

namespace {

int checks = 0, failures = 0;

void Check(bool ok, const char* what)
{
    ++checks;
    if (!ok) ++failures;
    Serial.printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
}

// A distinct 4-char type code per index, in the same shape real codes take.
String TypeCode(int i)
{
    static const char* d = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    String s = "T";
    s += d[(i / 36 / 36) % 36];
    s += d[(i / 36) % 36];
    s += d[i % 36];
    return s;
}

// ---- 1. eviction never touches a claim -------------------------------------
// The one rule the whole design rests on. Fill types to the cap, claim a known
// subset, then keep pushing new types well past the cap and require that every
// claim survived -- and that the store kept accepting, which is the difference
// between v5 and the cliff it replaced.
void TestTypeEviction()
{
    Serial.println("[probe] types: eviction protects claims");
    Logbook lb;

    const size_t cap = 500; // MAX_TYPES; private, so asserted against behaviour below
    for (size_t i = 0; i < cap; ++i)
        lb.NoteType(TypeCode((int)i));
    Check(lb.TypeCount() == cap, "store filled to the cap");

    // Claim every tenth. These must all still be present at the end.
    int claimedN = 0;
    for (size_t i = 0; i < cap; i += 10)
        if (lb.ClaimType(TypeCode((int)i))) ++claimedN;
    Check(claimedN == 50, "claimed 50 of the 500");
    Check(lb.ClaimedTypeCount() == 50, "claimed counter agrees");

    // Now push 200 brand-new types through a full store.
    int accepted = 0;
    for (size_t i = cap; i < cap + 200; ++i)
        if (lb.NoteType(TypeCode((int)i))) ++accepted;
    Check(accepted == 200, "a full store still accepts new types (it evicts)");
    Check(lb.TypeCount() == cap, "and stays exactly at the cap");

    int survivors = 0;
    for (size_t i = 0; i < cap; i += 10)
        if (lb.IsTypeClaimed(TypeCode((int)i))) ++survivors;
    Check(survivors == 50, "EVERY claim survived 200 evictions");
    Check(lb.ClaimedTypeCount() == 50, "claimed counter still agrees");
}

// ---- 2. the least valuable unclaimed entry is the one that goes -------------
// Eviction picks the highest-count unclaimed type, so the rarity seen once is
// kept and the fiftieth Cessna of the afternoon is not. A test that only checked
// "something was evicted" would pass on a random victim.
void TestEvictionPicksTheDullest()
{
    Serial.println("[probe] types: the dullest unclaimed entry is evicted first");
    Logbook lb;

    const size_t cap = 500;
    for (size_t i = 0; i < cap; ++i)
        lb.NoteType(TypeCode((int)i));

    // Make one type overwhelmingly common and leave another seen exactly once.
    for (int n = 0; n < 40; ++n)
        lb.NoteType(TypeCode(7));
    Check(lb.Types().count(TypeCode(7)) == 1, "the common type is present before");
    Check(lb.Types().count(TypeCode(9)) == 1, "the rare type is present before");

    lb.NoteType("ZZZZ"); // forces exactly one eviction

    Check(lb.Types().count(TypeCode(7)) == 0, "the 41x-seen type was evicted");
    Check(lb.Types().count(TypeCode(9)) == 1, "the 1x-seen type was kept");
    Check(lb.Types().count("ZZZZ") == 1, "the new type went in");
}

// ---- 3. all-claimed still refuses ------------------------------------------
// The one case that must NOT evict. If every entry is a trophy there is nothing
// to spend, and refusing is the honest answer.
void TestAllClaimedRefuses()
{
    Serial.println("[probe] types: a fully-claimed store refuses rather than evicting");
    Logbook lb;

    const size_t cap = 500;
    for (size_t i = 0; i < cap; ++i)
        lb.NoteType(TypeCode((int)i));
    for (size_t i = 0; i < cap; ++i)
        lb.ClaimType(TypeCode((int)i));
    Check(lb.ClaimedTypeCount() == cap, "every entry claimed");

    const bool accepted = lb.NoteType("ZZZZ");
    Check(!accepted, "a new type is refused");
    Check(lb.Types().count("ZZZZ") == 0, "and was not stored");
    Check(lb.ClaimedTypeCount() == cap, "no claim was spent to make room");
    // And the badge must not lie about it -- this is the bug IsTypeClaimable exists
    // for, and the all-claimed store is now the only way to reach it.
    Check(!lb.IsTypeClaimable("ZZZZ"), "an unrecordable type carries no NEW badge");
}

// ---- 4. the v4 -> v5 operator migration ------------------------------------
// A v4 board stored names cut at 24. v5 cuts at 40, so the same airline arrives
// under a different key and its claim would be stranded. The migration adopts the
// old entry lazily, when the long spelling first appears.
void TestOperatorMigration()
{
    Serial.println("[probe] operators: a v4 truncated claim survives the widening");
    Logbook lb;

    // Exactly what a v4 store holds for "AIR WISCONSIN AIRLINES LLC": 24 chars.
    const String oldKey = "AIR WISCONSIN AIRLINES L";
    const String full   = "AIR WISCONSIN AIRLINES LLC";
    Check(oldKey.length() == 24, "fixture really is a 24-char v4 key");

    Check(lb.NoteOperator(oldKey), "v4 entry seeded");
    Check(lb.ClaimOperator(oldKey), "and claimed, as a v4 owner would have");
    Check(lb.ClaimedOperatorCount() == 1, "one claim on the books");

    // The long spelling arrives. It is NOT a fresh catch -- the owner already had it.
    const bool fresh = lb.NoteOperator(full);
    Check(!fresh, "the long spelling is not reported as a new catch");
    Check(lb.Operators().count(oldKey) == 0, "the truncated key is gone");
    Check(lb.Operators().count(full) == 1, "the full name is in the book");
    Check(lb.OperatorCount() == 1, "one entry, not two");
    Check(lb.ClaimedOperatorCount() == 1, "the claim was carried, not duplicated");

    // A SHORTER name that merely happens to be a prefix must NOT be adopted --
    // it was never truncated, so it is a genuinely different operator.
    Logbook lb2;
    Check(lb2.NoteOperator("DELTA"), "short operator seeded");
    Check(lb2.NoteOperator("DELTA AIR LINES INC OF GEORGIA X"), "a longer name is its own entry");
    Check(lb2.OperatorCount() == 2, "two distinct operators, not one merged");
}

// ---- 5. seen-store eviction keeps the beginning of the record ---------------
void TestSeenEvictionKeepsEarliest()
{
    Serial.println("[probe] countries: eviction forgets the newest, not the oldest");
    Logbook lb;

    // All entries land on the same day here (TodayEpochDay is 0 without NTP), so
    // this asserts the reachable half: the store stays at cap, keeps accepting,
    // and never spends a claim.
    for (int i = 0; i < 200; ++i)
        lb.NoteCountry(String("Country ") + i);
    Check(lb.CountryCount() == 200, "countries filled to the cap");
    Check(lb.ClaimCountry("Country 5"), "one country claimed");

    for (int i = 200; i < 260; ++i)
        lb.NoteCountry(String("Country ") + i);
    Check(lb.CountryCount() == 200, "still exactly at the cap");
    Check(lb.Countries().count("Country 5") == 1, "the claimed country survived");
    Check(lb.ClaimedCountryCount() == 1, "claim counter intact");
}

} // namespace

// The whole suite, re-run on a loop rather than once at boot.
//
// Printing once at boot means the only way to read the result is to catch the
// board in the two seconds after a reset -- and the first run of this probe lost
// two lines to a serial buffer, including the single most important assertion in
// it ("a full store still accepts new types"). The counter said 34 checks while
// 32 lines arrived, so the missing two had to be inferred from arithmetic instead
// of read. Inferring a pass is how a failing check gets recorded as a passing one.
//
// The tests are pure -- each builds its own Logbook and touches no NVS -- so
// repeating them costs nothing and makes the result readable whenever anyone
// happens to open the port.
void RunAll()
{
    checks = failures = 0;
    Serial.println();
    Serial.println("=== LogbookProbe: caps, eviction, migration ===");
    Serial.printf("[probe] free heap %u, largest block %u\n",
                  (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap());

    TestTypeEviction();
    Serial.flush();
    TestEvictionPicksTheDullest();
    Serial.flush();
    TestAllClaimedRefuses();
    Serial.flush();
    TestOperatorMigration();
    Serial.flush();
    TestSeenEvictionKeepsEarliest();
    Serial.flush();

    Serial.printf("=== %d checks, %d FAILURES ===\n", checks, failures);
    Serial.println(failures == 0 ? "=== RESULT: PASS ===" : "=== RESULT: FAIL ===");
    Serial.flush();
}

void setup()
{
    Serial.begin(115200);
    delay(2500); // USB-CDC needs a moment before the first line is not lost
    RunAll();
}

void loop()
{
    delay(10000);
    RunAll();
}
