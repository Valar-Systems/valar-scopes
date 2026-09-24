// Host test for the usage report's Follow flag (v15, docs/RELEASE-v15.md item 2).
//
// THE BUG IT GUARDS. usage::Store::SetFollowEnabled() existed and had NO CALLERS,
// so field 7 of every usage report was 0: 0 of 1,493 reports in 30 days said
// Follow was configured, including on units where it was. The flag itself was
// correct by construction -- the defect was a setter nothing called.
//
// So two halves, because neither alone catches that bug:
//
//   1. BEHAVIOUR, through the real store: SetFollowEnabled(true/false) -> Take()
//      -> field 7 of the header reads "1" / "0". (Needs the Preferences shim.)
//   2. WIRING: src/AircraftManager.cpp is not host-buildable, so this reads it and
//      requires the call INSIDE the block that loads the `follow` config -- the
//      block Initialise() runs at boot and on every save. Remove the call and this
//      fails. If the block's anchors cannot be found, the test says it is BLIND
//      (exit 2) rather than passing.
//
// Usage: test_follow_flag <path to src/AircraftManager.cpp>

#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

#include "../../include/UsageStore.h"

static int failures = 0;
static void check(bool ok, const char* what)
{
    std::printf("  %s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++failures;
}

// The Nth (1-based) comma-separated field of a usage header, or "" if absent.
static std::string field(const String& header, int n)
{
    std::string s = header.c_str(), out;
    int f = 1;
    for (char c : s) {
        if (c == ',') { if (f == n) return out; ++f; out.clear(); continue; }
        out += c;
    }
    return f == n ? out : std::string();
}

static int fieldCount(const String& header)
{
    if (header.isEmpty()) return 0;
    int n = 1;
    for (char c : std::string(header.c_str())) if (c == ',') ++n;
    return n;
}

int main(int argc, char** argv)
{
    std::printf("== usage report: the Follow flag ==\n");

    // ---- 1. behaviour, through the real store --------------------------------
    const uint32_t due = usage::FIRST_REPORT_MS + 1;
    {
        usage::Store on;
        on.SetFollowEnabled(true);
        const String h = on.Take(due);
        check(fieldCount(h) == (int)usage::FIELD_COUNT, "CONTROL: a report is produced, with every field");
        check(field(h, 7) == "1", "a configured follow target puts 1 in field 7");
    }
    {
        usage::Store off;
        off.SetFollowEnabled(false);
        const String h = off.Take(due);
        check(fieldCount(h) == (int)usage::FIELD_COUNT, "CONTROL: a report is produced, with every field");
        check(field(h, 7) == "0", "an empty follow target puts 0 in field 7");
    }
    {
        usage::Store never; // nobody calls the setter -- the pre-v15 state
        check(field(never.Take(due), 7) == "0", "CONTROL: a store nobody told reports 0 (what every pre-v15 report was)");
    }

    // ---- 2. wiring: the call exists where followTarget is loaded -------------
    if (argc < 2) { std::printf("  FAIL  no path to AircraftManager.cpp given -- BLIND\n"); return 2; }
    std::ifstream in(argv[1]);
    if (!in) { std::printf("  FAIL  cannot open %s -- BLIND\n", argv[1]); return 2; }
    std::stringstream ss; ss << in.rdbuf();
    const std::string src = ss.str();

    const char* BEGIN = "configServer.GetStoredString(\"follow\")";
    const char* END   = "THE TRAIL BUFFER BELONGS TO THE TRAIL TOGGLE";
    const size_t b = src.find(BEGIN), e = (b == std::string::npos) ? b : src.find(END, b);
    if (b == std::string::npos || e == std::string::npos) {
        std::printf("  FAIL  the follow-config block's anchors were not found -- BLIND, not passing\n");
        return 2;
    }
    const std::string block = src.substr(b, e - b);
    check(block.find("followTarget = want;") != std::string::npos,
          "CONTROL: the block found is the one that assigns followTarget");
    check(block.find("usageStore.SetFollowEnabled(!followTarget.isEmpty());") != std::string::npos,
          "the follow-config block calls usageStore.SetFollowEnabled(!followTarget.isEmpty())");
    check(block.rfind("followTarget = want;") < block.find("usageStore.SetFollowEnabled("),
          "...AFTER followTarget is assigned, so it reflects the new value");

    std::printf(failures ? "FOLLOW FLAG TESTS FAILED\n" : "follow flag: all checks passed\n");
    return failures ? 1 : 0;
}
