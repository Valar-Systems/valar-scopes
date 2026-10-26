#pragma once

#include <stddef.h>
#include <stdint.h>
#include <ArduinoJson.h>

#include "../game/Derive.h"

// The Missileer game's served config and its parser, in a file of their own so the host suite can build them
// (test/host/test_game_config.cpp). NO Arduino.h, and nothing here uses String: the host has ArduinoJson
// from .pio/libdeps and no Arduino core. Moved verbatim out of EamModels.h/.cpp, which includes this
// (docs/game-config-parse-test.md).
namespace eam {

// ------------------------------------------------------------------- Missileer game config
// GET {eam-base-url}/api/v1/missileer/config -- the served parameters the drill needs.
//
// PARSED STRICTLY, and `valid` means every field below was present and well-typed. A
// server still serving the pre-ppm shape (`weight` floats) parses as NOT valid, and the
// device then refuses to derive rather than guessing a scale (Fable, 2026-09-23: no
// config in hand -> the message is an ordinary EAM). The derivation spec is valar-eam-feed
// docs/game-derivation.md.
struct GameConfig {
    bool valid = false;
    bool hold = false;               // §1.2 kill switch: no game surfaces at all
    game::DeriveParams params;       // epoch + ORDERED ppm weights + tOffset tiers
    uint32_t ackCutoffS = 0;         // never Offered within this of T
    uint32_t autoDecodeS = 0;        // Offered banner shows the class after this (0 = never)
    uint32_t maxClockSyncAgeS = 0;   // arm only with an SNTP sync younger than this (0 = never)
    uint32_t windowUs = 0;           // §12 execution window, from timing.executionWindowS
    uint32_t bucketUs = 0;           // scoring.bucketS, for the deviation figure (0 = unknown)
};

bool ParseGameConfig(JsonObjectConst root, GameConfig& out);                    // /api/v1/missileer/config; strict

} // namespace eam
