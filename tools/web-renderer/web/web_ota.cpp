// Blipscope web renderer: STUBS for the five OtaUpdater.cpp entry points the radar calls. That file is the
// firmware-update path (HTTPUpdate, the OTA partitions), which the demo does not have; the radar only asks
// it for two telemetry strings, stamps a reboot cause, and (the touch-wedge cap, A12.119) asks whether an
// image is being written to flash. Each stub answers "nothing to report" -- and the demo writes no flash.
#include "OtaUpdater.h"

void StampRebootCause(uint8_t) {}
String TakeBootReasonReport() { return String(); }
void AckBootReasonReport(bool) {}
String TakeOtaMemReport() { return String(); }
bool ota::FlashWriteActive() { return false; }
