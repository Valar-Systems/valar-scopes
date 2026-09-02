#pragma once

#include <Arduino.h>
#include "LGFX.h"           // LGFX + LGFX_Sprite (LGFX_Sprite is a type alias, not forward-declarable)
#include "HttpRequestManager.h"

// Compiled firmware version. Bump this for every release and publish a matching
// version.txt so devices running an older build update themselves.
//
// 7 IS ALSO A PROTOCOL BOUNDARY, not just a release number. The proxy decides
// which photo artifact a device may be served from this exact value
// (FULLBLEED_MIN_FW in proxy/src/photos.ts): >= 7 gets the full-bleed square,
// anything older gets the legacy 150x100 rectangle, because firmware up to 6
// clips rather than scales and would render a square as its own top-left corner.
// Moving this number below 7, or shipping a build that reports 7 without the
// full-bleed card in it, hands those devices a broken photo.
constexpr int FW_VERSION = 9;

// Check GitHub Releases for a newer firmware and self-update if one is published.
// Blocking; on success the device flashes the new image and reboots into it. The
// backbuffer sprite is required to draw the status/progress screen: the SPD2010
// panel drops direct partial writes, so the message is composed full-frame and
// pushed (see BootScreen.h), and RGB panels are flushed after each paint.
//
// Takes the shared HttpRequestManager rather than building its own client: the version
// check runs through it (pinned roots, no extra TLS context), and the download holds its
// request bus so no background poll can be mid-handshake while the update allocates.
void MaybeUpdateFirmware(LGFX& tft, LGFX_Sprite& fb, HttpRequestManager& http);

// Which OTA slot is this image running from, and is it marked valid yet?
//
// Printed unprompted every boot, for the same reason BuildIdentity prints the env: after
// an update the only way to know it actually took -- rather than the bootloader quietly
// rolling back to the previous slot -- is for the device to say which half of the flash
// it woke up in. `when` labels the call site ("boot", "post-update").
void LogOtaSlot(const char* when);

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
