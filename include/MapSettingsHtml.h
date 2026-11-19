#pragma once
// The config page's map section (docs/blipscope-map-client-steps.md, "Step D's other half", (b)): the map's box
// ("map-layer"), the town names' box ("map-names") and the "Terrain shading" select ("map-terrain"). Pure C++11, no
// Arduino, as WxLookSelectHtml.h is for the weather's look: the page serves exactly what Html() returns, so
// test/host/test_map_settings_html.cpp grades exactly what is served (DOC2, DNC2, D1's page row).
//
// Both boxes resolve through configmigration::ResolveToggle with the named defaults (DO1, DN1), never a literal of their
// own: an unset key renders TICKED (DO3, DN3), so the first save after the update writes "true", and a stored "false" is
// a deliberate untick. The select resolves through mapterrain::Classify, so an unset or unknown value selects Strong.
// Its labels are the owner's own words, in his order (Q-T4). No '%' anywhere: the page's %PLACEHOLDER% processing would
// eat it (check-config-form.py).
//
// THE GATE lives at the call site, not here: the page renders this section, and its saves run, only under
// FEATURE_CLOUD_FEED && WX_LAYER_SHIPS && !BLIPSCOPE_WEB_RENDERER (BLIPSCOPE_MAP_FETCH's own definition in -D names).
// A build that renders no box declares no box, so the save leaves both keys absent (DO4, DN4).
#include <string>

#include "ConfigMigration.h"
#include "MapTerrain.h"

namespace mapsettingshtml {

constexpr const char* kLabelMap = "Map";
constexpr const char* kLabelNames = "Town names";
constexpr const char* kLabelTerrain = "Terrain shading";

inline std::string Box(const char* name, const char* label, bool on)
{
    return std::string("<label class=\"check\"><input name=\"") + name + "\" type=\"checkbox\" " + (on ? "checked" : "") +
           "><span>" + label + "</span></label>";
}

/** The section for the stored values (nullptr or "" when a key is absent). */
inline std::string Html(const char* storedMap, const char* storedNames, const char* storedTerrain)
{
    const bool map = configmigration::ResolveToggle(storedMap, configmigration::MAP_DEFAULT_ON);
    const bool names = configmigration::ResolveToggle(storedNames, configmigration::MAP_NAMES_DEFAULT_ON);
    const mapterrain::Level lv = mapterrain::Classify(storedTerrain);
    // Q-T4: the owner's labels, in his order
    struct Opt {
        mapterrain::Level level;
        const char* label;
    };
    const Opt opts[] = {{mapterrain::Level::Off, "Off"},
                        {mapterrain::Level::Subtle, "Subtle"},
                        {mapterrain::Level::Strong, "Strong"},
                        {mapterrain::Level::Xstrong, "Extra strong"}};
    std::string h = "<fieldset><legend>Map</legend><div class=\"grid3\">";
    h += Box("map-layer", kLabelMap, map);
    h += Box("map-names", kLabelNames, names);
    h += std::string("<label>") + kLabelTerrain + " <select name=\"map-terrain\">";
    for (const Opt& o : opts)
        h += std::string("<option value=\"") + mapterrain::Name(o.level) + "\"" + (o.level == lv ? " selected" : "") +
             ">" + o.label + "</option>";
    h += "</select></label></div></fieldset>";
    return h;
}

}  // namespace mapsettingshtml
