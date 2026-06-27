# CLAUDE.md

Guidance for working in this repo. Keep it short; the code is well-commented — prefer pointing at it over duplicating it here.

## What this is

Blipscope: ESP32-C3 firmware for a 1.28" round GC9A01 display + CST816T capacitive touch — a desk flight radar fed by OpenSky (cloud) or a local dump1090/readsb `aircraft.json`. PlatformIO + Arduino (pioarduino platform). See [README.md](README.md) for the product/user side.

## Build / flash / monitor

Multi-SKU: one PlatformIO env per hardware variant (see [platformio.ini](platformio.ini)). The C3 Kit (`blipscope-kit-c3-128`) is the default; S3 SKUs are added at bring-up.

```sh
pio run                                       # build the default (C3) env
pio run -e blipscope-kit-c3-128 -t upload     # flash over USB-C (esptool)
pio device monitor -b 115200                  # serial at 115200
pio run -e <env> -t upload -t monitor         # build+flash+monitor a specific SKU
```

In VS Code, the PlatformIO toolbar buttons do the same. If upload fails to auto-reset: hold **BOOT**, tap **RESET**, release **BOOT**.

- Partitions: `min_spiffs.csv` (firmware is large; OTA needs the room).
- A pre-build script ([scripts/patch_async_buff.py](scripts/patch_async_buff.py)) re-applies a guard to ESPAsyncWebServer in `.pio/` (gitignored) so `-DASYNC_RESPONCE_BUFF_SIZE=1024` survives a fresh lib install. If the config web page silently stops sending after a clean `.pio/`, that patch didn't take.

## Variants / multi-SKU

Blipscope is several boards from one codebase. A `-DBLIPSCOPE_VARIANT_*` flag (set per env) selects a header in [include/variants/](include/variants/) defining pins, the display/touch driver (`BLIPSCOPE_PANEL_*` / `BLIPSCOPE_TOUCH_*`), capability flags (`BANDED_RENDER`, `ENRICH_ALWAYS`, `HAS_AUDIO`, `HAS_IMU`), and `SLUG`/`NAME`. Shared code never hardcodes hardware: geometry comes from [Layout.h](include/Layout.h) (from `variant::SCREEN_SIZE`), behaviour from `variant::*`, and display config from those macros in [LGFX.h](include/LGFX.h) (add a panel via an `#if` block). **Add a SKU = a variant header + an `[env:*]` + a CI matrix row** ([RELEASING.md](RELEASING.md)). Don't reintroduce hardcoded `240`/pins.

## The C3's three hard constraints — read before changing memory, networking, or touch

These apply to the **C3 Kit** SKU: a **single-core RISC-V** chip with a tight, fragmenting heap — most non-obvious code exists to live within that, so don't "simplify" it away. The S3 SKUs (dual-core + PSRAM) relax all three (full framebuffer, enrichment always-on, touch and network on separate cores) via the variant's capability flags rather than by deleting the guards:

1. **Contiguous heap is scarce (~24–28 KB largest block).** A TLS handshake needs a big contiguous block. That's why the radar renders in two 240×120 bands instead of one 240×240 sprite ([BandCanvas](include/BandCanvas.h), [main.cpp](src/main.cpp)), why feeds are parsed straight off the socket (`GetJson`), and why adsbdb enrichment is **heap-gated** (skipped, not forced, when the largest block is small — `ENRICH_TLS_HEAP_FLOOR`). Do not shrink the shared TLS buffers.
2. **Touch I2C must never overlap a TLS handshake.** On the single core, an overlapping CST816 transfer wedges the controller off the bus until reboot. Touch is serialized against network via the HTTP client's request mutex — `HttpRequestManager::TryAcquireBus/ReleaseBus`, used in `AircraftManager::HandleTouch` (gated on the radar view, ungated on the detail card so close-taps are instant). Background enrichment also pauses briefly after a touch. See PR #8 / commit `56a3df2` for the full diagnosis. There is intentionally **no** RST/bit-bang touch watchdog — it thrashed on auto-sleep and never recovered a real wedge.
3. **A slow TLS handshake can starve the watchdog.** The Task-WDT is raised to 10 s in [setup()](src/main.cpp) because a blocking OpenSky/adsbdb handshake on a background task can keep `async_tcp` from being fed and reboot the board.

## Architecture in three sentences

- [AircraftManager](src/AircraftManager.h) owns essentially all state and runs on the Arduino **loop task**; all `trackedAircraft` mutation happens there.
- Three background FreeRTOS tasks do the blocking network work and never hold pointers into shared state — OpenSky/local **fetch**, adsbdb/photo **enrich**, and **MQTT** publish — handing parsed results back to the loop via queues; they share one HTTP client ([HttpRequestManager](src/HttpRequestManager.h)) because there isn't heap for a second TLS context.
- The UI is three swipe-able screens (Radar / List / Stats) with a detail card overlay; config is an async web page ([ConfigurationWebServer](src/ConfigurationWebServer.cpp)) reachable at `http://<device-name>.local`.

## Conventions

- Branching: feature/fix branches → PR into `main`. OTA is **per-SKU**: bump `FW_VERSION` ([OtaUpdater.h](src/OtaUpdater.h)) once, then a GitHub Release builds and publishes `firmware-<slug>.bin` for every SKU — see [RELEASING.md](RELEASING.md). A device only ever downloads its own slug's binary.
- The C3 SKU is single-core (no true parallelism); treat anything touching the I2C/SPI buses or the heap as timing-sensitive. Put per-variant behaviour behind `variant::` capability flags, not `#ifdef`s scattered through the logic.
- `credentials.json` (OpenSky client secret) is a user secret — never read, commit, or log it.
