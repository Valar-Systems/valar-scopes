// Host test for include/UsageReport.h -- the usage counters' pure half.
//
// =============================================================================
// THE ASSERTION THIS FILE EXISTS FOR
//
// Everything else here is arithmetic. The load-bearing test is the one that says
// the payload CANNOT CARRY AN IDENTITY: no callsign, no tail number, no follow
// target, nothing a person could be looked up by. The device promise is "usage
// counts yes, subjects of usage never", and a promise about a payload should be
// checked against the payload.
//
// It is checked structurally rather than by example. Asserting "the string does
// not contain N4523K" would pass against a builder that happily appends any
// OTHER identity; asserting "the string contains nothing but digits and commas"
// fails for all of them at once. Same shape as the no-digit rule in CLAUDE.md,
// pointed the other way.
//
// =============================================================================
// EVERY CLAIM HAS A CONTROL THAT MUST COME OUT DIFFERENTLY
//
// Notably the cross-wiring control below: a Delta() that copied cardOpens into
// every field would satisfy a naive per-field test, because the field being
// checked would be right. So each counter is moved ALONE and the other five are
// required to stay at zero.
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "../../include/UsageReport.h"

static int failures = 0;
static int checks   = 0;

static void check(bool ok, const char* what)
{
    ++checks;
    if (!ok) { std::printf("  FAIL: %s\n", what); ++failures; }
}

using namespace usage;

// How many comma-separated fields a rendered payload has.
static size_t fieldCount(const char* s)
{
    if (!s || !*s) return 0;
    size_t n = 1;
    for (const char* p = s; *p; ++p) if (*p == ',') ++n;
    return n;
}

int main(int argc, char** argv)
{
    std::printf("== UsageReport ==\n");

    // SC4 (docs/starved-seconds-counter.md): the device's arity must be one the WORKER accepts, read from the
    // Worker's own source -- the other side of the contract -- not restated here. Unreadable = BLIND, exit 2.
    {
        if (argc < 2) { std::printf("FAIL: usage: test_usage_report <proxy/src/metrics.ts> (BLIND)\n"); return 2; }
        FILE* f = std::fopen(argv[1], "rb");
        if (!f) { std::printf("FAIL: cannot read %s (BLIND)\n", argv[1]); return 2; }
        char src[65536]; const size_t n = std::fread(src, 1, sizeof(src) - 1, f); std::fclose(f); src[n] = '\0';
        const char* at = std::strstr(src, "USAGE_FIELD_COUNTS = [");
        if (!at) { std::printf("FAIL: USAGE_FIELD_COUNTS not found in %s (BLIND)\n", argv[1]); return 2; }
        at += std::strlen("USAGE_FIELD_COUNTS = [");
        bool listed = false; int count = 0;
        while (*at && *at != ']') {
            if (*at >= '0' && *at <= '9') { const long v = std::strtol(at, nullptr, 10); ++count; if ((size_t)v == FIELD_COUNT) listed = true; while (*at >= '0' && *at <= '9') ++at; }
            else ++at;
        }
        check(count > 0 && listed, "SC4: the Worker's USAGE_FIELD_COUNTS (read from proxy/src/metrics.ts) includes the device's FIELD_COUNT");
    }

    // =========================================================================
    // §17 / the published promise: the payload cannot carry a subject
    // =========================================================================
    std::printf("  ---- the payload carries counts, and cannot carry identities\n");
    {
        Report r;
        r.delta.cardOpens = 41; r.delta.screenRadar = 7; r.delta.screenList = 3;
        r.delta.screenStats = 2; r.delta.screenFollow = 11; r.delta.logbookClaims = 5;
        r.delta.starvedSeconds = 150;
        r.followEnabled = true;
        r.uptimeHours = 137;

        char buf[MAX_LEN];
        const size_t len = Format(r, buf, sizeof(buf));
        check(len > 0, "a populated report renders");

        bool onlyDigitsAndCommas = true;
        for (const char* p = buf; *p; ++p)
            if (!((*p >= '0' && *p <= '9') || *p == ',')) onlyDigitsAndCommas = false;
        check(onlyDigitsAndCommas,
              "the payload is digits and commas ONLY -- no identity of any kind "
              "can be in it, whatever a future edit tries to append");

        // CONTROL: the assertion above is satisfied by an empty string, and an
        // empty payload would pass every privacy test while collecting nothing.
        check(len >= 15 && fieldCount(buf) == FIELD_COUNT,
              "CONTROL: ... and it is not empty -- 9 fields, as declared");

        // The arity is a contract with the Worker's parser. Pinning the constant
        // is not enough: the constant and the emitted string can disagree.
        check(FIELD_COUNT == 9, "FIELD_COUNT is 9 (the starved-seconds delta appended ninth)");
        check(fieldCount(buf) == FIELD_COUNT,
              "and what is EMITTED has exactly that many fields");

        check(std::strcmp(buf, "41,7,3,2,11,5,1,137,150") == 0,
              "the fields are in the documented order, starvedSeconds appended ninth");
    }

    // followEnabled is a bool on the wire, never a name.
    {
        Report on, off;
        on.followEnabled = true;
        off.followEnabled = false;
        char a[MAX_LEN], b[MAX_LEN];
        Format(on, a, sizeof(a));
        Format(off, b, sizeof(b));
        check(std::strcmp(a, "0,0,0,0,0,0,1,0,0") == 0, "follow enabled renders as 1");
        check(std::strcmp(b, "0,0,0,0,0,0,0,0,0") == 0, "and disabled as 0");
        check(std::strcmp(a, b) != 0, "CONTROL: the flag reaches the payload at all");
    }

    // =========================================================================
    // Deltas: saturating, and not cross-wired
    // =========================================================================
    std::printf("  ---- deltas: saturating, and each counter in its own field\n");
    check(Since(10, 40) == 30, "a normal delta is the difference");
    check(Since(40, 10) == 0,
          "a counter that went BACKWARDS reports zero, not 4.29 billion");
    check(Since(0, 0) == 0, "and an unmoved counter reports nothing");
    // CONTROL: without this, a Since() that always returned 0 would pass both of
    // the assertions above that matter.
    check(Since(0, 1) == 1, "CONTROL: a single event is still counted");

    {
        // Move ONE counter at a time and require the other five to stay put.
        // A Delta() that copied one field everywhere would pass a per-field test.
        struct Case { const char* name; uint32_t Counters::*field; };
        const Case cases[] = {
            { "cardOpens",     &Counters::cardOpens },
            { "screenRadar",   &Counters::screenRadar },
            { "screenList",    &Counters::screenList },
            { "screenStats",   &Counters::screenStats },
            { "screenFollow",  &Counters::screenFollow },
            { "logbookClaims", &Counters::logbookClaims },
            { "starvedSeconds", &Counters::starvedSeconds },
        };
        for (const Case& c : cases) {
            Counters prev, now;
            now.*(c.field) = 9;
            const Counters d = Delta(prev, now);
            size_t moved = 0;
            for (const Case& o : cases) if (d.*(o.field) != 0) ++moved;
            check(d.*(c.field) == 9 && moved == 1,
                  "one counter moves exactly one field");
        }
    }

    // Empty() describes a report with nothing in it -- and is NOT used to
    // suppress one, because silence and zero are different facts.
    {
        Counters z;
        check(Empty(z), "an unmoved snapshot is empty");
        Counters one; one.cardOpens = 1;
        check(!Empty(one), "CONTROL: one event is not");
        Counters st; st.starvedSeconds = 30;
        check(!Empty(st), "starved seconds alone are not empty either");
    }

    // =========================================================================
    // The buffer, at the worst input it can ever see
    // =========================================================================
    std::printf("  ---- the buffer holds the largest payload that can exist\n");
    {
        Report r;
        r.delta.cardOpens = r.delta.screenRadar = r.delta.screenList =
            r.delta.screenStats = r.delta.screenFollow = r.delta.logbookClaims = r.delta.starvedSeconds = 0xffffffffu;
        r.followEnabled = true;
        r.uptimeHours = 0xffffffffu;
        char buf[MAX_LEN];
        const size_t len = Format(r, buf, sizeof(buf));
        check(len > 0, "seven u32 maxima plus the gauge still render");
        check(fieldCount(buf) == FIELD_COUNT, "... with the arity intact");
        check(len < MAX_LEN, "... inside MAX_LEN");

        // A buffer too small must produce NOTHING, not a truncated payload: a
        // half-written field is a number the server would parse as real.
        char small[8];
        check(Format(r, small, sizeof(small)) == 0, "a short buffer renders nothing");
        check(small[0] == '\0', "... and leaves no fragment for a caller to send");
    }

    // =========================================================================
    // Cadence
    // =========================================================================
    std::printf("  ---- cadence: hourly, deferred at boot, correct across the wrap\n");
    check(!Due(1000u, 0u), "nothing is reported in the first seconds of a boot");
    check(!Due(FIRST_REPORT_MS - 1u, 0u), "nor just before the deferral expires");
    check(Due(FIRST_REPORT_MS, 0u), "the first report is due 10 minutes in");
    check(!Due(FIRST_REPORT_MS + 60000u, FIRST_REPORT_MS),
          "a minute after reporting, nothing is due");
    check(Due(FIRST_REPORT_MS + REPORT_INTERVAL_MS, FIRST_REPORT_MS),
          "an hour after reporting, one is");

    // millis() wraps at 49.7 days. Unsigned subtraction is right across it; a
    // signed comparison would stop reporting for 49 days at the wrap, which is
    // the kind of bug a fleet finds and a bench never does.
    {
        const uint32_t justBeforeWrap = 0xffffffffu - 1000u;
        const uint32_t justAfterWrap  = 2000u;   // 3 s later in real time
        check(!Due(justAfterWrap, justBeforeWrap),
              "3 seconds across the wrap is not an hour");
        check(Due(justBeforeWrap + REPORT_INTERVAL_MS, justBeforeWrap),
              "CONTROL: an hour across the wrap IS due");
    }

    std::printf("  ---- uptime is a gauge, and does not pretend past the wrap\n");
    check(UptimeHours(0u) == 0, "a fresh boot is zero hours");
    check(UptimeHours(3600000u) == 1, "an hour is one");
    check(UptimeHours(3599999u) == 0, "and 59:59 is still zero -- it floors");
    check(UptimeHours(0xffffffffu) == 1193, "the wrap point is 1,193 h, as documented");

    std::printf("\n%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
