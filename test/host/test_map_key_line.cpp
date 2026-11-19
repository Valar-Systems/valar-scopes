// Host rows for the mapkey bench image's boot line (include/MapKeyLine.h; docs/blipscope-map-client-steps.md, "Step D's
// other half", (c), V19S): the formatter is pure, so the line the board prints is graded here.
//   MK1  each class from a planted value: absent, true, false and other for the toggles; absent, the four values and
//        other for terrain; absent, CONFIG_REV and other for cfg-rev. "other" for "TRUE", "1" and "strong " (and "").
//   MK2  the resolved half comes from the device's resolvers: unset reads map=on names=on terrain=strong.
//   MK3  V19S's predicted line, exactly: cfg-rev=6 and the three keys absent give map=on names=on terrain=strong.
//   MK4  NO VALUE leaves the board: a stored value never appears in the line (its control: the line still says
//        something, the marker first).
#include <cstdio>
#include <cstring>
#include <string>

#include "../../include/MapKeyLine.h"

static int failures = 0;

static void check(bool ok, const char* what)
{
    std::printf("  %s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++failures;
}

static bool eq(const char* a, const char* b) { return std::strcmp(a, b) == 0; }

int main()
{
    using namespace mapkeyline;
    // MK1: the toggles
    check(eq(ToggleClass(nullptr), "absent") && eq(ToggleClass("true"), "true") && eq(ToggleClass("false"), "false"),
          "MK1 toggles: absent, true, false");
    check(eq(ToggleClass("TRUE"), "other") && eq(ToggleClass("1"), "other") && eq(ToggleClass(""), "other"),
          "MK1 toggles: TRUE, 1 and \"\" are other");
    // MK1: terrain
    check(eq(TerrainClass(nullptr), "absent") && eq(TerrainClass("off"), "off") && eq(TerrainClass("subtle"), "subtle") &&
              eq(TerrainClass("strong"), "strong") && eq(TerrainClass("xstrong"), "xstrong"),
          "MK1 terrain: absent and the four values");
    check(eq(TerrainClass("strong "), "other") && eq(TerrainClass("STRONG"), "other") && eq(TerrainClass("1"), "other"),
          "MK1 terrain: \"strong \", STRONG and 1 are other");
    // MK1: cfg-rev
    char rev[32];
    std::snprintf(rev, sizeof rev, "cfg-rev=%d ", configmigration::CONFIG_REV);
    check(Line(0, false, 0, nullptr, nullptr, nullptr).find("cfg-rev=absent ") != std::string::npos,
          "MK1 cfg-rev: absent");
    check(Line(0, true, configmigration::CONFIG_REV, nullptr, nullptr, nullptr).find(rev) != std::string::npos,
          "MK1 cfg-rev: this firmware's CONFIG_REV");
    check(Line(0, true, configmigration::CONFIG_REV - 1, nullptr, nullptr, nullptr).find("cfg-rev=other ") !=
                  std::string::npos &&
              Line(0, true, configmigration::CONFIG_REV + 1, nullptr, nullptr, nullptr).find("cfg-rev=other ") !=
                  std::string::npos,
          "MK1 cfg-rev: another revision is other");
    // MK2: the resolved half
    check(Line(0, true, 6, "false", "false", "off").find("-> map=off names=off terrain=off") != std::string::npos,
          "MK2 stored false, false, off resolve off, off, off");
    check(Line(0, true, 6, "TRUE", "1", "strong ").find("-> map=off names=off terrain=strong") != std::string::npos,
          "MK2 TRUE and 1 resolve off (not \"true\"); \"strong \" resolves Strong (unknown)");
    // MK3: V19S's predicted line, exactly
    const std::string v19s = Line(23, true, 6, nullptr, nullptr, nullptr);
    check(v19s == "[map-key] config keys=23 cfg-rev=6 map-layer=absent map-names=absent map-terrain=absent -> map=on "
                  "names=on terrain=strong",
          "MK3 V19S: cfg-rev=6, the three keys absent -> map=on names=on terrain=strong");
    // MK4: no value leaves the board
    const char* secret = "xq7-not-a-class";
    const std::string leak = Line(5, true, 6, secret, secret, secret);
    check(leak.find(secret) == std::string::npos, "MK4 a stored value never appears in the line");
    check(leak.rfind("[map-key] config keys=5 ", 0) == 0, "MK4 CONTROL the line still says something, marker first");

    std::printf("test_map_key_line: %s (%d failure(s))\n", failures ? "FAILED" : "all checks passed", failures);
    return failures ? 1 : 0;
}
