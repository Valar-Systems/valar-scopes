#pragma once
// The mapkey bench image's boot line (docs/blipscope-map-client-steps.md, "Step D's other half", (c): V19S, a real v19
// save read by the board). Pure C++11, no Arduino, so test/host/test_map_key_line.cpp grades exactly the line printed.
//
//   [map-key] config keys=<n> cfg-rev=<absent|REV|other> map-layer=<absent|true|false|other>
//     map-names=<absent|true|false|other> map-terrain=<absent|off|subtle|strong|xstrong|other>
//     -> map=<on|off> names=<on|off> terrain=<off|subtle|strong|xstrong>
//
// EVERY FIELD IS A CLASS, NEVER A VALUE (Daniel's rule: NVS bytes stay on the board; names and counts only). REV is this
// firmware's own CONFIG_REV, a constant, not a stored value. The resolved half comes from the resolvers the device and
// the page use (ResolveToggle with the named defaults, mapterrain::Classify), so the line says what the device does.
// The keys' names live here, once: the bench block in src/main.cpp reads NVS by these constants
// (check_map_draw.py's DW1 names this file as that reader).
#include <stdio.h>
#include <string.h>
#include <string>

#include "ConfigMigration.h"
#include "MapTerrain.h"

namespace mapkeyline {

constexpr const char* kMarker = "[map-key]";
constexpr const char* kKeyMap = "map-layer";
constexpr const char* kKeyNames = "map-names";
constexpr const char* kKeyTerrain = "map-terrain";

/** A stored toggle's class. `s` nullptr: the key is not in NVS. */
inline const char* ToggleClass(const char* s)
{
    if (s == nullptr) return "absent";
    if (strcmp(s, "true") == 0) return "true";
    if (strcmp(s, "false") == 0) return "false";
    return "other";
}

/** A stored terrain value's class: one of the four exactly, else other. */
inline const char* TerrainClass(const char* s)
{
    if (s == nullptr) return "absent";
    const mapterrain::Level all[] = {mapterrain::Level::Off, mapterrain::Level::Subtle, mapterrain::Level::Strong,
                                     mapterrain::Level::Xstrong};
    for (mapterrain::Level l : all)
        if (strcmp(s, mapterrain::Name(l)) == 0) return mapterrain::Name(l);
    return "other";
}

/** The line, for `keys` keys in the namespace, cfg-rev (present or not) and the three stored values (nullptr when a
 *  key is absent). */
inline std::string Line(int keys, bool revPresent, int rev, const char* storedMap, const char* storedNames,
                        const char* storedTerrain)
{
    char revc[16];
    if (!revPresent)
        snprintf(revc, sizeof revc, "absent");
    else if (rev == configmigration::CONFIG_REV)
        snprintf(revc, sizeof revc, "%d", configmigration::CONFIG_REV);
    else
        snprintf(revc, sizeof revc, "other");
    const bool map = configmigration::ResolveToggle(storedMap, configmigration::MAP_DEFAULT_ON);
    const bool names = configmigration::ResolveToggle(storedNames, configmigration::MAP_NAMES_DEFAULT_ON);
    char out[256];
    snprintf(out, sizeof out, "%s config keys=%d cfg-rev=%s %s=%s %s=%s %s=%s -> map=%s names=%s terrain=%s", kMarker,
             keys, revc, kKeyMap, ToggleClass(storedMap), kKeyNames, ToggleClass(storedNames), kKeyTerrain,
             TerrainClass(storedTerrain), map ? "on" : "off", names ? "on" : "off",
             mapterrain::Name(mapterrain::Classify(storedTerrain)));
    return out;
}

}  // namespace mapkeyline
