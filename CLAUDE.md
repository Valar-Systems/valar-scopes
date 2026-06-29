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
pio run -e blipscope-eam-c3-128 -t upload     # flash the EAM monitor (same C3 hardware; see FEATURE_EAM)
```

In VS Code, the PlatformIO toolbar buttons do the same. If upload fails to auto-reset: hold **BOOT**, tap **RESET**, release **BOOT**.

- Partitions: `min_spiffs.csv` (firmware is large; OTA needs the room).
- A pre-build script ([scripts/patch_async_buff.py](scripts/patch_async_buff.py)) re-applies a guard to ESPAsyncWebServer in `.pio/` (gitignored) so `-DASYNC_RESPONCE_BUFF_SIZE=1024` survives a fresh lib install. If the config web page silently stops sending after a clean `.pio/`, that patch didn't take.

## Variants / multi-SKU

Blipscope is several boards from one codebase. A `-DBLIPSCOPE_VARIANT_*` flag (set per env) selects a header in [include/variants/](include/variants/) defining pins, the display/touch driver (`BLIPSCOPE_PANEL_*` / `BLIPSCOPE_TOUCH_*`), capability flags (`BANDED_RENDER`, `ENRICH_ALWAYS`, `HAS_AUDIO`, `HAS_IMU`), and `SLUG`/`NAME`. Shared code never hardcodes hardware: geometry comes from [Layout.h](include/Layout.h) (from `variant::SCREEN_SIZE`), behaviour from `variant::*`, and display config from those macros in [LGFX.h](include/LGFX.h) (add a panel via an `#if` block). **Add a SKU = a variant header + an `[env:*]` + a CI matrix row** ([RELEASING.md](RELEASING.md)). Don't reintroduce hardcoded `240`/pins.

Two SKUs exist today: `blipscope-kit-c3-128` (the C3 baseline) and `blipscope-pro-s3-21` (Waveshare ESP32-S3-Touch-LCD-2.1 — first **S3** SKU and first **RGB-bus** panel, an ST7701 480×480). The S3-2.1 also has two board-specific wrinkles the model doesn't share: its panel/touch reset and the ST7701 init chip-select hang off a **TCA9554 I²C IO expander**, and it carries an IMU + buzzer. Both are handled behind the variant: `variant::BoardPreInit()` (a hook called in [setup()](src/main.cpp) before `tft.init()`; a no-op on the C3) drives the expander, and the IMU/buzzer live in [src/board/board_s3_touch21.cpp](src/board/board_s3_touch21.cpp) behind `board::*` (no-ops elsewhere via [Board.h](include/Board.h)). All board I²C uses LovyanGFX's `lgfx::i2c` (same owner as touch) on the loop task — don't reach for Arduino `Wire`.

## FEATURE_EAM — a second product from this codebase

`-DFEATURE_EAM` (set on the `blipscope-eam-*` envs) swaps the radar app for an **HFGCS EAM (Emergency Action Message) monitor** built from the same boards and the same shared infra (display, Wi-Fi, web config, NVS, HTTP/TLS, OTA, ntfy). It compiles **no** radar/aircraft/ADS-B code: the EAM envs' `build_src_filter` drops the radar-only TUs (`AircraftManager`, `MqttPublisher`, `SpecialAircraft`, `AircraftInfoFields`, `Logbook`, `models/`), `[common]` drops `src/eam/` from the radar builds, and [main.cpp](src/main.cpp) picks `EamManager` vs `AircraftManager` at compile time (same `Initialise/Update/Draw` surface). Everything EAM lives in [src/eam/](src/eam/).

- **Data:** the device talks only to one backend ("valar-eam-feed"; base URL is the runtime config `eam-base-url`, defaulting to the `EAM_FEED_BASE` build flag) over its normalized endpoints. [EamFeedClient](src/eam/EamFeedClient.h) runs **one** worker task (reusing the shared TLS client, like the radar's fetch task) with per-endpoint interval/backoff/dedupe/retention; all state stays on the loop task. Shapes + parsers are in [EamModels.h](src/eam/EamModels.h). The one exception to "feed-agnostic" is the optional **command-post watch** ([AbncpProvider.h](src/eam/AbncpProvider.h)): the "OpenSky — your account" source queries OpenSky **directly from the device** with the user's own OAuth creds (reusing [OpenSkyAuthTokenHandler](src/OpenSkyAuthTokenHandler.h)) — never via the backend, **never a baked-in key**, inert until creds are entered.
- **UI:** seven screens (ticker / tempo / codewords / ABNCP / propagation / ICBM / Zulu clock) on a dwell-timed rotation that skips empty feeds; [EamManager](src/eam/EamManager.h) + [EamScreens.cpp](src/eam/EamScreens.cpp), with a real 7-segment clock in [SevenSegment.cpp](src/eam/SevenSegment.cpp). Same C3 touch/TLS serialization (`TryAcquireBus`) and solar auto-dim as the radar.
- **Persistence + alerts:** [EamLogbook](src/eam/EamLogbook.h) (own NVS namespace `eam-log`) tracks seen EAMs/codewords; ntfy alerts reuse the radar's `ntfy-topic` + POST pattern on three toggleable triggers.
- **OTA channel:** `-DFW_OTA_PREFIX="eam-"` makes [OtaUpdater](src/OtaUpdater.cpp) fetch `firmware-eam-<slug>.bin`, so an EAM device never pulls a radar image for the same board. The shared `version.txt` gate is unchanged.
- The config web page is feature-gated in [ConfigurationWebServer.cpp](src/ConfigurationWebServer.cpp) (`#ifdef FEATURE_EAM`): the EAM form + its NVS keys instead of the radar form; the shell (mDNS, `/reset-wifi`, save flag, secret masking) is shared. Adding an EAM SKU = a variant header + an `[env:*]` (with `-DFEATURE_EAM`, the `build_src_filter`, and `FW_OTA_PREFIX`) + a CI row whose slug is `eam-<board>`.

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
