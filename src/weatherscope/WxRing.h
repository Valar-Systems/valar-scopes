#pragma once
// The live radar's frame ring (docs/weatherscope-stage4.md S4.32, Q115 (a), ruled): kSlots slots of wxcrop::kMaxBytes,
// allocated ONCE, all or none; every other NEW frame is kept, starting with the first, so the kept frames span about the
// past hour of the radar's 2-minute frames; History() is oldest first; under kMinLoop kept frames a tap does nothing.
// Pure C++11 and host-tested (test/host/test_wx_live.cpp, its sabotages test/host/sabotage_wx_live.py). The allocator is
// injected: the device passes PSRAM, the host passes malloc. A frame is offered only when it REPLACED the held crop (a
// 304 is never offered), and one no newer than the newest seen is not new.
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "WxCrop.h"
#include "WxFeeds.h"

namespace wxring {

constexpr size_t kSlots = 15;    // 15 crops of at most 32 KB: 480 KB of PSRAM at most (Q115)
constexpr size_t kMinLoop = 3;   // under 3 kept frames a tap does nothing (Q115)

struct Ring {
    uint8_t* slot[kSlots] = {};
    size_t len[kSlots] = {};
    uint32_t epoch[kSlots] = {};
    size_t count = 0;            // kept frames, at most kSlots
    size_t next = 0;             // the slot the next kept frame overwrites
    uint32_t newestSeen = 0;     // the newest frame epoch offered, kept or not
    uint32_t newSeen = 0;        // new frames offered since boot
    bool ok = false;

    // All or none: if any allocation fails, every slot already allocated is released and the ring stays off.
    template <class Alloc, class Release>
    bool Init(Alloc alloc, Release release)
    {
        for (size_t i = 0; i < kSlots; ++i) {
            slot[i] = (uint8_t*)alloc(wxcrop::kMaxBytes);
            if (!slot[i]) {
                for (size_t j = 0; j < i; ++j) {
                    release(slot[j]);
                    slot[j] = nullptr;
                }
                ok = false;
                return false;
            }
        }
        ok = true;
        return true;
    }

    // Returns true when the frame was kept.
    bool Offer(const uint8_t* data, size_t n, uint32_t frameEpoch)
    {
        if (!ok || data == nullptr || n == 0 || n > wxcrop::kMaxBytes) return false;
        if (newSeen > 0 && frameEpoch <= newestSeen) return false;   // not new: it adds nothing and does not count
        newestSeen = frameEpoch;
        const bool keep = (newSeen % 2) == 0;
        ++newSeen;
        if (!keep) return false;
        memcpy(slot[next], data, n);
        len[next] = n;
        epoch[next] = frameEpoch;
        next = (next + 1) % kSlots;
        if (count < kSlots) ++count;
        return true;
    }

    // The newest min(count, max) kept frames, oldest first.
    size_t History(wx::CropBytes* out, size_t max) const
    {
        const size_t n = count < max ? count : max;
        const size_t first = (next + kSlots - n) % kSlots;
        for (size_t i = 0; i < n; ++i) {
            const size_t k = (first + i) % kSlots;
            out[i].data = slot[k];
            out[i].size = len[k];
        }
        return n;
    }

    bool CanLoop() const { return count >= kMinLoop; }

    // Empties the ring when the query changes (WxAnswer.h SetQuery, S4.37): the slots stay allocated, and the next new
    // frame is kept, as at boot, so one place's frames never loop at another.
    void Clear()
    {
        count = 0;
        next = 0;
        newestSeen = 0;
        newSeen = 0;
    }
};

}  // namespace wxring
