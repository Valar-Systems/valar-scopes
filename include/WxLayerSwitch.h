#pragma once
// The radar's weather switch, "wx-layer": ON when unset, the owner's saved choice after that (docs/wx-device.md, "The
// layer on by default"; Daniel, 2026-11-06). Pure C++11, no Arduino: AircraftManager reads it through here, and
// test/host/test_wx_layer_switch.cpp grades it. The config page's default must say the same ("true"), and
// test/host/check_wx_wiring.py reads both.
//
// Why no ConfigMigration entry (CLAUDE.md, "a default only reaches keys that were never saved"): v18 has no such
// setting, so an update to v19 finds the key unset and starts with the layer on. From then on the page's SaveToggle
// writes "true" or "false", so a saved "false" is an owner's choice and stands. Anything else is off, the sweep's rule.
//
// THE WEBSITE'S RENDERER MUST NOT INHERIT THIS. It has no pixels, so its config map sets "wx-layer" to "false" in
// writing, and tools/web-renderer/check_wx_off.mjs refuses a map without that entry: absent no longer means off.
#include <string.h>

namespace wxlayerswitch {

inline bool On(const char* stored) { return stored[0] == '\0' || strcmp(stored, "true") == 0; }

}  // namespace wxlayerswitch
