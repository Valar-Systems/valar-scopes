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

// The one-shot OTA memory report, rendered as the X-Blip-OTA-Mem header value:
//
//     <fwFrom>,<fwTo>,<preLargest>,<postLargest>,<result>
//
// Returns "" unless an OTA was attempted since the last report. Answers the one
// question the bench gate could not: does an OTA complete at the *fragmented*
// heap a device actually reaches in service? Only the fleet can say, so the
// numbers ride the next cloud check-in the device was making anyway (no extra
// request, no new endpoint -- see README "Telemetry").
//
// CLEARS on read: fire-once and best-effort. A report lost to a failed request
// is not retried -- the next OTA cycle re-covers it, and a dropped sample is
// worth less than a device that keeps retrying telemetry.
//
// LOOP TASK ONLY (it touches NVS). Callers hand the string to the fetch task
// inside the request, exactly like cloudBase/cloudKey.
String TakeOtaMemReport();
