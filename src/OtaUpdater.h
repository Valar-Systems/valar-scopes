#pragma once

#include <Arduino.h>

class LGFX; // forward declaration; defined in LGFX.h

// Compiled firmware version. Bump this for every release and publish a matching
// version.txt so devices running an older build update themselves.
constexpr int FW_VERSION = 3;

// Check GitHub Releases for a newer firmware and self-update if one is published.
// Blocking; on success the device flashes the new image and reboots into it.
void MaybeUpdateFirmware(LGFX& tft);
