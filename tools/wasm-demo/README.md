# Blipscope web radar (a WebAssembly spike)

The radar firmware's own draw code, compiled with Emscripten and shown in a browser. Every draw call is
recorded as a vector command and replayed on a `<canvas>` at the screen's real resolution (CSS px ×
devicePixelRatio). The panel's 240 or 480 pixels are never rasterised and scaled up. A spike, to see what
the idea looks like before any full effort.

## Open it

```sh
tools/wasm-demo/build.sh s3-128          # the Kit S3, 1.28 in, 240 px
tools/wasm-demo/build.sh s3-21           # the Pro S3, 2.1 in, 480 px (same page, a second button)
cd tools/wasm-demo/dist && python -m http.server 8087
# then http://localhost:8087/
```

**Requirements:**
- Emscripten on `PATH`, or `EMSDK=~/emsdk`. Built with 6.0.13, in WSL Ubuntu 24.04.
- The firmware's libraries, which come from `pio pkg install -e blipscope-s3-128`, or `LIBDEPS=` pointing at any
  `.pio/libdeps/<env>`.

`node tools/wasm-demo/smoke.mjs tools/wasm-demo/dist/s3-128 30` runs a SKU headless for 30 s. It prints what
the last frame drew, op by op, and exits 0 only when the radar drew contacts with no pixel or raster
draws.

## What is real and what is not

**Compiled unmodified, from `src/`:**
- `AircraftManager` (the radar, list, stats, card, sweep, labels and gestures);
- `CloudFeed`, `AircraftInfoFields`, `SpecialAircraft`, `IcaoCountry`, `Logbook`, `SevenSegment`, `ConfigMigration`,
  `HeapHealth`, `MqttPublisher`, `OpenSkyAuthTokenHandler`, `TouchWatchdog`, `TlsAllocator`;
- `models/Aircraft`, `anim/Coastlines` and `anim/StateBorders`;
- the real `include/LGFX.h`, `BandCanvas.h` and `DrawHelpers.h`, including `DrawRadarSweep`.

**The frame** (`web/web_main.cpp`) is `main.cpp`'s `loop()` draw cycle, line for line.

**The fetch and enrich tasks** are the firmware's own `RunFetchTask` and `RunEnrichTask`. They run on cooperative
Emscripten fibers (`web/web_rtos.cpp`): a blocking queue receive yields to the frame loop. Their HTTP calls are
answered from a synthetic scene (`web/web_scene.cpp`), and `CloudFeed::ParseBlips` and `ParseEnrich` parse the
answers.

**Replaced** (in `shim/` and `web/`, so the build compiles and has no network):

| replaced | by |
|---|---|
| LovyanGFX | a recorder (`shim/LovyanGFX.hpp`) that keeps LovyanGFX's colour-by-type rules, its 565 quantisation and Font0's metrics, so layout matches the panel |
| the Arduino core, FreeRTOS, ESP-IDF calls, Wi-Fi, NVS, mbedTLS | stand-ins. NVS is an in-memory map. Wi-Fi is "connected" with an RFC 5737 address |
| `HttpRequestManager` | a body that answers `/blips` and `/enrich` from the scene; everything else 404s |
| `ConfigurationWebServer` | a fixed settings map: Bend, 40 km, the cloud source; every other key empty, so the firmware's defaults apply |
| four `OtaUpdater.cpp` functions | stubs, no OTA |
| the 2.1 in board's buzzer and IMU | stubs |
| the touch chip on I2C | always healthy |

**Not in the demo:** photos (the card says "No photo available"), Wi-Fi setup, the config page, OTA and MQTT.

**The scene:**
- 21 made-up aircraft fly straight tracks across a 42 km disc around Bend, and loop.
- Hex codes are in the US civil block, so each is enriched like a real contact. That is nearest-first, one lookup
  every 5 s, as on the device.
- Callsigns, types and routes are illustrative, not recorded.

## Outside the device build

PlatformIO compiles `src/` and `lib/`. Nothing here is on any env's path: `platformio.ini` names `tools/` only in
a comment.
