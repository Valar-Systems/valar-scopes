// Blipscope web renderer: STUBS for the four OtaUpdater.cpp entry points the radar calls. That file is the
// firmware-update path (HTTPUpdate, the OTA partitions), which the demo does not have; the radar only asks
// it for two telemetry strings and stamps a reboot cause. Each stub answers "nothing to report".
#include "OtaUpdater.h"

void StampRebootCause(uint8_t) {}
String TakeBootReasonReport() { return String(); }
void AckBootReasonReport(bool) {}
String TakeOtaMemReport() { return String(); }
