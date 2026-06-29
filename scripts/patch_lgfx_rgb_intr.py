"""Pre-build patch: let LovyanGFX's RGB bus share the LCD_CAM interrupt.

On the S3 RGB SKU (ST7701 480x480), LovyanGFX's Bus_RGB allocates the LCD_CAM VSYNC
interrupt. For ESP-IDF >= 5.4.4 it drops ESP_INTR_FLAG_SHARED (see Bus_RGB.cpp) -- a change
that was needed for the SPI master/slave but is overly broad: applied to LCD_CAM it forces an
*exclusive* level-1-3 interrupt vector, and on the ESP32-S3 most of those are already claimed
as shared by other peripherals. The allocation then fails at boot with:

    E intr_alloc: No free interrupt inputs for LCD_CAM interrupt (flags 0x80E)

and the RGB panel never scans out (the screen stays a flat backlit tint while the firmware
runs on fine). Restoring ESP_INTR_FLAG_SHARED for this interrupt -- exactly what IDF < 5.4.4
used -- lets it attach to an existing shared vector. LCD_CAM accepts a shared interrupt, so
this is safe.

Runs on every build and is idempotent, so it survives a fresh `pio` library install (.pio/ is
gitignored). Harmless on non-S3 envs: Bus_RGB.cpp compiles to nothing off the ESP32-S3.
"""
import os

Import("env")  # noqa: F821  (provided by PlatformIO's SCons environment)

SOURCE = os.path.join(
    env.subst("$PROJECT_LIBDEPS_DIR"), env["PIOENV"],  # noqa: F821
    "LovyanGFX", "src", "lgfx", "v1", "platforms", "esp32s3", "Bus_RGB.cpp",
)

# Anchor on the IDF>=5.4.4 define (INTRDISABLED followed immediately by a newline). The IDF<5.4.4
# branch reads "INTRDISABLED | ESP_INTR_FLAG_SHARED", so this matches only the line we want.
ORIGINAL = "#define LGFX_INTR_FLAGS ESP_INTR_FLAG_INTRDISABLED\n"
REPLACEMENT = "#define LGFX_INTR_FLAGS ESP_INTR_FLAG_INTRDISABLED | ESP_INTR_FLAG_SHARED\n"

try:
    with open(SOURCE, "r", encoding="utf-8") as fh:
        text = fh.read()
except FileNotFoundError:
    print("[patch_lgfx_rgb_intr] %s not found yet; skipping" % SOURCE)
else:
    if ORIGINAL in text:
        with open(SOURCE, "w", encoding="utf-8") as fh:
            fh.write(text.replace(ORIGINAL, REPLACEMENT, 1))
        print("[patch_lgfx_rgb_intr] made the LCD_CAM interrupt shared in Bus_RGB.cpp")
    else:
        # Already patched on a prior build, or the library changed shape.
        print("[patch_lgfx_rgb_intr] anchor not found (already patched or library changed); skipping")
