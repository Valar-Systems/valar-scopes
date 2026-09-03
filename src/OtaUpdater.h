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

// ---------------------------------------------------------------------------
// REBOOT-THEN-FETCH. The daily check cannot run at long uptime, and this is why.
//
// Measured on the bench 2026-09-03 (bench-logs/ota-watch-com16-2026-09-02.log):
// the daily timer fired at 48 h uptime and the version check died before it was
// sent --
//
//     [health] ALLOC FAILED: 16717 B (caps 0x804) in heap_caps_calloc
//     [E][ssl_client.cpp:41] (-32512) SSL - Memory allocation failed
//     [ota] version check failed: HTTP -1 connection refused
//
// -- because the largest free block was 1,140 B against a 16,717 B handshake.
// Total free heap was fine (8,788 B free, and megabytes of PSRAM); it is
// CONTIGUITY that decays with uptime, and mbedTLS needs one large block.
//
// So the fix is not to fight the fragmentation but to sidestep it: an always-on
// device has one moment of guaranteed-clean heap per boot, and the boot check
// already runs there. THE SAME CODE SUCCEEDS AT BOOT AND FAILS AT 48 H, which is
// the whole finding -- COM4 updated 8->9 unattended on a fresh heap the same day
// COM16 failed on a fragmented one, at preLargest 31,732 B versus 1,140 B.
//
// This is a real behaviour change for a desk radar: it reboots to update. That is
// seconds of downtime once a day at most (see the cap below), against an update
// path that otherwise NEVER completes on a unit that is never power-cycled.

/**
 * Arm a reboot so the update check runs at boot, on a clean heap.
 *
 * Sets an NVS flag and restarts. Does NOT return on success -- the device is
 * already rebooting. Returns false (and returns normally) when refused.
 *
 * REFUSES MORE OFTEN THAN IT ACCEPTS, deliberately. At most one flag-triggered
 * reboot per 24 h, persisted as wall-clock in NVS so it survives the reboot it
 * is rate-limiting. Without that a boot-time crash between the flag and the
 * check would produce a device that reboots forever, which is a far worse
 * failure than a missed update.
 *
 * `largestBlock` is logged, not tested: the caller has already decided. It is
 * there so a serial capture shows WHY the reboot happened and can be read
 * against the numbers above.
 *
 * LOOP TASK ONLY (NVS).
 */
bool DeferUpdateCheckToReboot(uint32_t largestBlock);

/**
 * Was this boot armed by DeferUpdateCheckToReboot?
 *
 * CLEARS THE FLAG BEFORE RETURNING, and the ordering is the loop guard: the
 * flag is consumed before the check it requested has run, so a crash, a WDT or
 * a panic anywhere inside that check cannot leave the flag standing and re-arm
 * the next boot. A missed update is recoverable; a reboot cycle on a customer's
 * desk is not.
 *
 * The boot check runs every boot regardless -- this only reports whether THIS
 * boot was asked for, so the serial log can say so.
 *
 * LOOP TASK ONLY (NVS).
 */
bool ConsumeDeferredCheckFlag();

// Which OTA slot is this image running from, and is it marked valid yet?
//
// Printed unprompted every boot, for the same reason BuildIdentity prints the env: after
// an update the only way to know it actually took -- rather than the bootloader quietly
// rolling back to the previous slot -- is for the device to say which half of the flash
// it woke up in. `when` labels the call site ("boot", "post-update").
void LogOtaSlot(const char* when);

/**
 * Why this boot happened, as a short stable token: POWERON, SW (a deliberate
 * ESP.restart(), which is what the deferred update check does), PANIC, INT_WDT,
 * TASK_WDT, WDT, BROWNOUT, DEEPSLEEP, EXT, SDIO or UNKNOWN.
 *
 * WHY IT IS TELEMETRY AND NOT JUST A LOG LINE. The update path now turns on
 * reboots, so "did it reboot, and was it ours" became a fleet-wide question --
 * and on 2026-09-03 we could not answer it for COM4, whose pre-check reboot was
 * provable only from a fresh-heap preLargest. A device with no serial cable
 * cannot be asked. So it rides the OTA memory report, beside the heap numbers,
 * where it costs one field on a request already being made.
 */
const char* ResetReasonName();

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
