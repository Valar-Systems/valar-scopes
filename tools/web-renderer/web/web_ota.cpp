// Blipscope web renderer: STUBS for the seven OtaUpdater.cpp entry points the radar calls. That file is the
// firmware-update path (HTTPUpdate, the OTA partitions), which the demo does not have; the radar only asks
// it for two telemetry strings, stamps a reboot cause, and (the touch-wedge cap, A12.119) asks whether an
// image is being written to flash. Each stub answers "nothing to report" -- and the demo writes no flash.
//
// The map (REG-web-terrain, TD3) asks two more, to decide whether this boot spreads its first request over two hours
// (MapPolicy CA9). A page load follows no update and no deferred reboot, so the answers are the truth: none and false.
// A visitor never waits for the spread.
#include "OtaUpdater.h"
#include "RebootCause.h"

void StampRebootCause(uint8_t) {}
String TakeBootReasonReport() { return String(); }
void AckBootReasonReport(bool) {}
String TakeOtaMemReport() { return String(); }
bool ota::FlashWriteActive() { return false; }
uint8_t ConsumeDeferredRebootCause() { return rebootcause::NONE; }
bool BootFollowsUpdate() { return false; }
