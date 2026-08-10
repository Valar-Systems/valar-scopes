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
//       SCREEN_SIZE, BANDED_RENDER, HAS_AUDIO, HAS_IMU, TOUCH_WATCHDOG, SLUG, NAME
//
// Every SKU here is a dual-core S3 with PSRAM. The single-core ESP32-C3 Kit was
// retired 2026-08-09 and its variant deleted. Two capability flags went with it:
//
//   SERIALIZE_TOUCH_BUS -- existed only to stop a touch I2C transfer overlapping a
//     TLS handshake on one core. No single-core SKU, nothing to serialize.
//   ENRICH_ALWAYS -- claimed to control whether enrichment was heap-gated, and by
//     the time it was deleted it was read by NO code at all: the gate it named
//     (ENRICH_TLS_HEAP_FLOOR) had already been replaced by heaphealth::CanHandshake().
//     A flag that reads like a switch and is wired to nothing is worse than absent.
//
// BANDED_RENDER survives with no `true` SKU, deliberately: its consumer, BandCanvas,
// is the render abstraction every edition draws through, and a future PSRAM-less
// board is the hook it exists for. The `if constexpr` sites simply compile out.
//
// Shared firmware NEVER hardcodes hardware: geometry comes from Layout.h (built from
// variant::SCREEN_SIZE) and behaviour from variant::* capability flags. Adding a SKU =
// add a header here + an [env:...] in platformio.ini; touch no shared logic.

#if defined(BLIPSCOPE_VARIANT_S3_21)
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
