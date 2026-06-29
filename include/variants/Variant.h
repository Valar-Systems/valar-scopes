#pragma once

// Active hardware-variant selector.
//
// Each Blipscope SKU is one ESP32 board with a particular display/touch/pin set and
// a few capability differences. The PlatformIO env for a SKU passes a -DBLIPSCOPE_VARIANT_*
// flag (see platformio.ini); this header includes the matching variant definition.
//
// A variant header provides:
//   - display/touch/backlight CONFIG MACROS consumed by LGFX.h (pins, driver selection
//     via BLIPSCOPE_PANEL_* / BLIPSCOPE_TOUCH_*, SPI/I2C params)
//   - typed values in `namespace variant` consumed by the app logic:
//       SCREEN_SIZE, BANDED_RENDER, ENRICH_ALWAYS, HAS_AUDIO, HAS_IMU, SLUG, NAME
//
// Shared firmware NEVER hardcodes hardware: geometry comes from Layout.h (built from
// variant::SCREEN_SIZE) and behaviour from variant::* capability flags. Adding a SKU =
// add a header here + an [env:...] in platformio.ini; touch no shared logic.

#if defined(BLIPSCOPE_VARIANT_C3_128)
  #include "variants/c3_128.h"
#elif defined(BLIPSCOPE_VARIANT_S3_21)
  #include "variants/s3_21.h"
#elif defined(BLIPSCOPE_VARIANT_S3_128)
  #include "variants/s3_128.h"
#elif defined(BLIPSCOPE_VARIANT_S3_146)
  #include "variants/s3_146.h"
#elif defined(BLIPSCOPE_VARIANT_S3_175_AMOLED)
  #include "variants/s3_175_amoled.h"
#else
  #error "No BLIPSCOPE_VARIANT_* defined. Select a SKU via the PlatformIO env build_flags (see platformio.ini)."
#endif
