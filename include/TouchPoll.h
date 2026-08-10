#pragma once

#include <stdint.h>
#include "HttpRequestManager.h"
#include "variants/Variant.h"

// One shared touch poll for every edition's HandleTouch.
//
// Every SKU is a dual-core S3: touch sits on its own I2C bus and the network runs
// on another core, so there is nothing to serialize and the poll runs ungated.
//
// It was not always so. The retired single-core C3 gated this poll on the HTTP
// client's request mutex, because a touch I2C transfer overlapping a TLS
// handshake wedged the CST816 off the bus until reboot (PR #8 / commit 56a3df2).
// That guard is gone with the board, and it should NOT be reintroduced on an S3
// "for safety": the mutex is held for the FULL duration of every GET/POST, which
// on a fast-polling edition is most of the time, so gating here silently drops
// every tap that lands inside a fetch window.
//
// `Skipped` is retained in the enum because callers branch on it and it is the
// correct answer for any future bus-contended board: a skipped poll is not a
// lifted finger, and treating it as Idle synthesizes release edges mid-press.
enum class TouchPoll : uint8_t { Skipped, Idle, Touched };

template <typename Tft>
inline TouchPoll ReadTouch(Tft& tft, HttpRequestManager& http, int32_t& tx, int32_t& ty)
{
    (void)http; // no bus serialization on any current SKU -- see above
    return tft.getTouch(&tx, &ty) ? TouchPoll::Touched : TouchPoll::Idle;
}
