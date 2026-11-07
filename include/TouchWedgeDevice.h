#pragma once
// The touch-wedge cap's device side: the one Io both editions hand the driver (TouchWedgeCap.h).
// Device only -- the host test supplies its own Io. The NVS bodies are the radar's former
// BootTouchWedge/StoreTouchRun, moved here unchanged.
#include <Arduino.h>
#include <Preferences.h>
#include "OtaUpdater.h"   // StampRebootCause, REBOOT_CAUSE_TOUCH_WEDGE, ota::FlashWriteActive
#include "SaveLock.h"
#include "TouchWedgeCap.h"

namespace touchwedge {

class DeviceIo : public Io {
public:
    uint8_t ReadRun() override
    {
        Preferences p;
        uint8_t stored = 0;
        if (p.begin(NVS_NS, true)) {
            stored = p.getUChar(NVS_KEY, 0);
            p.end();
        }
        return stored;
    }
    bool WriteRun(uint8_t run) override
    {
        Preferences p;
        if (!p.begin(NVS_NS, false)) return false;
        p.putUChar(NVS_KEY, run);
        p.end();
        return true;
    }
    void StampCause() override { StampRebootCause(REBOOT_CAUSE_TOUCH_WEDGE); }
    void Restart() override
    {
        Serial.flush();
        delay(100);
        ESP.restart();
    }
    bool FlashWriteActive() override { return ota::FlashWriteActive(); }
    bool TryHoldSave() override { return savelock::TryHoldForRestart(); }
    void Print(const char* text) override { Serial.print(text); }
};

inline DeviceIo& Device()
{
    static DeviceIo io;
    return io;
}

} // namespace touchwedge
