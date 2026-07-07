#pragma once

#include <stdint.h>
#include "HttpRequestManager.h"
#include "variants/Variant.h"

// One shared touch poll for every edition's HandleTouch, so the C3 guard lives
// in a single place instead of a per-edition copy.
//
// Single-core C3 (variant::SERIALIZE_TOUCH_BUS): a touch I2C transfer that
// overlaps a TLS handshake wedges the CST816 off the bus, so the poll is
// serialized against the network via the HTTP client's request mutex. Skipped
// means a request is mid-flight and the caller must return WITHOUT touching its
// gesture state -- a skipped poll is not a lifted finger, and treating it as
// Idle would synthesize release edges mid-press.
//
// Dual-core S3: touch sits on its own I2C bus and the network runs on another
// core, so there is nothing to serialize -- and the mutex is far from
// uncontended (it is held for the FULL duration of every GET/POST, which on
// fast-polling editions is most of the time), so gating here would silently
// drop every tap that falls inside a fetch window. The poll runs ungated.
// Mirrors the branch in AircraftManager::HandleTouch, where the full story of
// the C3 wedge is documented.
enum class TouchPoll : uint8_t { Skipped, Idle, Touched };

template <typename Tft>
inline TouchPoll ReadTouch(Tft& tft, HttpRequestManager& http, int32_t& tx, int32_t& ty)
{
    if constexpr (variant::SERIALIZE_TOUCH_BUS) {
        if (!http.TryAcquireBus())
            return TouchPoll::Skipped;
        const bool touched = tft.getTouch(&tx, &ty);
        http.ReleaseBus();
        return touched ? TouchPoll::Touched : TouchPoll::Idle;
    } else {
        return tft.getTouch(&tx, &ty) ? TouchPoll::Touched : TouchPoll::Idle;
    }
}
