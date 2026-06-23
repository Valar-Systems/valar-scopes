#pragma once

#include <Arduino.h>
#include "models/TrackedAircraft.h"

// One user-selectable on-screen info field. A single table (see the .cpp) drives
// three places at once, so adding/removing a field is a one-line edit:
//   * the config web form  -> key (form field name) + label
//   * the save handler      -> key
//   * the on-screen renderer-> format()
struct AircraftInfoFieldDef {
    const char* key;     // NVS key + HTML form field name. MUST be <= 15 chars (NVS limit).
    const char* label;   // human-readable label shown on the config page
    bool        defaultOn;   // enabled state before the user has saved anything
    bool        needsLookup; // true if the value comes from the adsbdb enrichment lookup
                             // (type/operator/registration) rather than the OpenSky feed
    String (*format)(const TrackedAircraft& aircraft); // display string; "" hides the line
};

extern const AircraftInfoFieldDef AIRCRAFT_INFO_FIELDS[];
extern const size_t AIRCRAFT_INFO_FIELD_COUNT;
