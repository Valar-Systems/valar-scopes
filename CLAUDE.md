# CLAUDE.md

Guidance for working in this repo. Keep it short; the code is well-commented — prefer pointing at it over duplicating it here.

## What this is

Blipscope: ESP32-C3 firmware for a 1.28" round GC9A01 display + CST816T capacitive touch — a desk flight radar fed by OpenSky (cloud) or a local dump1090/readsb `aircraft.json`. PlatformIO + Arduino (pioarduino platform). See [README.md](README.md) for the product/user side.

## Build / flash / monitor

Single env: **`esp32-c3-devkitm-1`** (see [platformio.ini](platformio.ini)).

```sh
pio run                          # build
pio run -t upload                # flash over USB-C (esptool)
pio device monitor -b 115200     # serial at 115200
pio run -t upload -t monitor     # flash then monitor
```

In VS Code, the PlatformIO toolbar buttons do the same. If upload fails to auto-reset: hold **BOOT**, tap **RESET**, release **BOOT**.

- Partitions: `min_spiffs.csv` (firmware is large; OTA needs the room).
- A pre-build script ([scripts/patch_async_buff.py](scripts/patch_async_buff.py)) re-applies a guard to ESPAsyncWebServer in `.pio/` (gitignored) so `-DASYNC_RESPONCE_BUFF_SIZE=1024` survives a fresh lib install. If the config web page silently stops sending after a clean `.pio/`, that patch didn't take.

## The C3's three hard constraints — read before changing memory, networking, or touch

This is a **single-core RISC-V** chip with a tight, fragmenting heap. Most non-obvious code exists to live within that. Don't "simplify" these away without understanding why they're there:

1. **Contiguous heap is scarce (~24–28 KB largest block).** A TLS handshake needs a big contiguous block. That's why the radar renders in two 240×120 bands instead of one 240×240 sprite ([BandCanvas](include/BandCanvas.h), [main.cpp](src/main.cpp)), why feeds are parsed straight off the socket (`GetJson`), and why adsbdb enrichment is **heap-gated** (skipped, not forced, when the largest block is small — `ENRICH_TLS_HEAP_FLOOR`). Do not shrink the shared TLS buffers.
2. **Touch I2C must never overlap a TLS handshake.** On the single core, an overlapping CST816 transfer wedges the controller off the bus until reboot. Touch is serialized against network via the HTTP client's request mutex — `HttpRequestManager::TryAcquireBus/ReleaseBus`, used in `AircraftManager::HandleTouch` (gated on the radar view, ungated on the detail card so close-taps are instant). Background enrichment also pauses briefly after a touch. See PR #8 / commit `56a3df2` for the full diagnosis. There is intentionally **no** RST/bit-bang touch watchdog — it thrashed on auto-sleep and never recovered a real wedge.
3. **A slow TLS handshake can starve the watchdog.** The Task-WDT is raised to 10 s in [setup()](src/main.cpp) because a blocking OpenSky/adsbdb handshake on a background task can keep `async_tcp` from being fed and reboot the board.

## Architecture in three sentences

- [AircraftManager](src/AircraftManager.h) owns essentially all state and runs on the Arduino **loop task**; all `trackedAircraft` mutation happens there.
- Three background FreeRTOS tasks do the blocking network work and never hold pointers into shared state — OpenSky/local **fetch**, adsbdb/photo **enrich**, and **MQTT** publish — handing parsed results back to the loop via queues; they share one HTTP client ([HttpRequestManager](src/HttpRequestManager.h)) because there isn't heap for a second TLS context.
- The UI is three swipe-able screens (Radar / List / Stats) with a detail card overlay; config is an async web page ([ConfigurationWebServer](src/ConfigurationWebServer.cpp)) reachable at `http://<device-name>.local`.

## Conventions

- Branching: feature/fix branches → PR into `main`; `release/vN` branches are cut for OTA releases. Bump `FW_VERSION` ([OtaUpdater.h](src/OtaUpdater.h)) when shipping a release devices should self-update into.
- This is RISC-V single-core: assume **no** true parallelism. Treat anything touching the I2C/SPI buses or the heap as timing-sensitive.
- `credentials.json` (OpenSky client secret) is a user secret — never read, commit, or log it.
