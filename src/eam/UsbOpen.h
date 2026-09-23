#pragma once
// "Open this message on the computer": the device half of FEATURE_USB_OPEN.
//
// Missileer on the 1.28" Kit S3 enumerates as a COMPOSITE USB device -- the CDC
// serial port it always had (flashing + monitor) and a HID keyboard -- which
// needs ARDUINO_USB_MODE=0 (TinyUSB). The S3's USB-Serial/JTAG peripheral that
// MODE=1 selects cannot present HID. See the [env:missileer-s3-128] comment.
//
// A long press on the touchscreen (EamManager::HandleTouch) calls Request(). What
// gets typed is decided by UsbOpenPlan.h, which the host tests grade; this file
// only replays it, a few characters per loop pass, so the display and the feed
// keep running while it types.
#include <Arduino.h>
#include "UsbOpenPlan.h"

namespace usbopen {

#if defined(FEATURE_USB_OPEN)
void Begin();                 // setup(): start the HID keyboard
// Start typing `url` (from PlanUrl) for `os`. False when busy or `url` is empty.
bool Request(Os os, const String& url);
void Pump();                  // loop task: advance the typing state machine
bool Busy();
#else
inline void Begin() {}
inline bool Request(Os, const String&) { return false; }
inline void Pump() {}
inline bool Busy() { return false; }
#endif

} // namespace usbopen
