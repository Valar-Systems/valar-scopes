#pragma once

#include <Arduino.h>
#include "LGFX.h"           // LGFX + LGFX_Sprite (LGFX_Sprite is a type alias, not forward-declarable)

// Compiled firmware version. Bump this for every release and publish a matching
// version.txt so devices running an older build update themselves.
constexpr int FW_VERSION = 4;

// Check GitHub Releases for a newer firmware and self-update if one is published.
// Blocking; on success the device flashes the new image and reboots into it. The
// backbuffer sprite is required to draw the status/progress screen: the SPD2010
// panel drops direct partial writes, so the message is composed full-frame and
// pushed (see BootScreen.h), and RGB panels are flushed after each paint.
void MaybeUpdateFirmware(LGFX& tft, LGFX_Sprite& fb);
