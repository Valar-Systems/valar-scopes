#pragma once

#include <cstdint>

#include "variants/Variant.h"

class LGFX; // defined in LGFX.h; only referenced by-reference below

// Optional on-board peripherals (IMU, buzzer) that only some SKUs carry, behind the
// variant capability flags. SKUs without them get inline no-ops here, so call sites stay
// identical on every board -- guard them with `if constexpr (variant::HAS_IMU/HAS_AUDIO)`
// and the dead branch (plus these no-ops) drops out at compile time.
//
// The real implementations live in src/board/board_s3_touch21.cpp (compiled only for that
// variant). variant::BoardPreInit() -- the expander/reset/CS bring-up that must run before
// tft.init() -- is declared per-variant in include/variants/*.h, not here.
namespace board {

    struct Imu {
        float ax, ay, az; // acceleration in g
    };

#if defined(BLIPSCOPE_VARIANT_S3_21) || defined(BLIPSCOPE_VARIANT_S3_146)
    void BuzzerChirp(uint16_t ms); // start a short beep (non-blocking)
    void BuzzerUpdate();           // pump each loop: ends a beep when its time is up
    bool ImuRead(Imu& out);        // read the accelerometer; false if the read failed
    // Write the RGB panel's PSRAM framebuffer back from cache so the LCD_CAM DMA scans the
    // freshly-drawn pixels. LovyanGFX's RGB bus omits this and it's required on ESP-IDF 5.5;
    // call after every frame is pushed to the panel.
    void DisplayFlush(LGFX& tft);
#else
    inline void BuzzerChirp(uint16_t) {}
    inline void BuzzerUpdate() {}
    inline bool ImuRead(Imu&) { return false; }
    inline void DisplayFlush(LGFX&) {}
#endif

}
