# CLAUDE.md

Guidance for working in this repo. Keep it short; the code is well-commented — prefer pointing at it over duplicating it here.

## What this is

Blipscope: ESP32-S3 firmware for round touch LCDs — a desk flight radar fed by Blipscope Cloud (the [proxy/](proxy/) Worker), OpenSky (BYO account), or a local dump1090/readsb `aircraft.json`. PlatformIO + Arduino (pioarduino platform). Every SKU is a dual-core ESP32-S3 with PSRAM: the original single-core ESP32-C3 Kit (1.28" GC9A01 + CST816T) was retired 2026-06-29, briefly revived bench-only for the cloud-feed program, and **deleted from the tree 2026-08-09** — variant header, envs, wedge-bisection harness and its single-core capability flags with it. See [README.md](README.md) for the product/user side.

## Build / flash / monitor

Multi-SKU: one PlatformIO env per hardware variant (see [platformio.ini](platformio.ini)). The S3 1.28" Kit S3 (`blipscope-s3-128`) is the default SKU — "the default", unqualified, means this board.

```sh
pio run                                       # build the default (S3 1.46") env
pio run -e blipscope-s3-146 -t upload         # flash over USB-C (esptool)
pio device monitor -b 115200                  # serial at 115200
pio run -e <env> -t upload -t monitor         # build+flash+monitor a specific SKU
pio run -e missileer-s3-146 -t upload         # flash Missileer, the EAM monitor (same S3 board; see FEATURE_EAM)
```

In VS Code, the PlatformIO toolbar buttons do the same. If upload fails to auto-reset: hold **BOOT**, tap **RESET**, release **BOOT**.

- Partitions: `min_spiffs.csv` (firmware is large; OTA needs the room).
- A pre-build script ([scripts/patch_async_buff.py](scripts/patch_async_buff.py)) re-applies a guard to ESPAsyncWebServer in `.pio/` (gitignored) so `-DASYNC_RESPONCE_BUFF_SIZE=1024` survives a fresh lib install. If the config web page silently stops sending after a clean `.pio/`, that patch didn't take.

## Variants / multi-SKU

Blipscope is several boards from one codebase. A `-DBLIPSCOPE_VARIANT_*` flag (set per env) selects a header in [include/variants/](include/variants/) defining pins, the display/touch driver (`BLIPSCOPE_PANEL_*` / `BLIPSCOPE_TOUCH_*`), capability flags (`BANDED_RENDER`, `ENRICH_ALWAYS`, `HAS_AUDIO`, `HAS_IMU`), and `SLUG`/`NAME`. Shared code never hardcodes hardware: geometry comes from [Layout.h](include/Layout.h) (from `variant::SCREEN_SIZE`), behaviour from `variant::*`, and display config from those macros in [LGFX.h](include/LGFX.h) (add a panel via an `#if` block). **Add a SKU = a variant header + an `[env:*]` + a CI matrix row** ([RELEASING.md](RELEASING.md)). Don't reintroduce hardcoded `240`/pins. A SKU still in bring-up gets a **slug-less** matrix row — built and run through the launch gate, never published; the `slug` is what names `firmware-<slug>.bin` and points an OTA channel at it. Leaving such an env out of CI entirely is how the 1.75" AMOLED quietly stopped compiling.

The shipping radar SKUs are S3: `blipscope-s3-128` (**Kit S3** — jxl/EC-Buying 1.28" GC9A01 + CST816D; graduated to a CI row 2026-07-15 on the wedge gate passing — **the default**), `blipscope-s3-146` (Waveshare ESP32-S3-Touch-LCD-1.46B — SPD2010 412×412 QSPI), and `blipscope-pro-s3-21` (Waveshare ESP32-S3-Touch-LCD-2.1 — first **RGB-bus** panel, an ST7701 480×480). The Kit S3 is the one SKU whose *hardware* needs a per-batch acceptance gate before it ships — the touch IC's factory config and the u.FL-vs-chip antenna build are both silent, per-batch substitution risks; see [INCOMING-INSPECTION.md](INCOMING-INSPECTION.md). The S3-2.1 also has two board-specific wrinkles the others don't share: its panel/touch reset and the ST7701 init chip-select hang off a **TCA9554 I²C IO expander**, and it carries an IMU + buzzer. Both are handled behind the variant: `variant::BoardPreInit()` (a hook called in [setup()](src/main.cpp) before `tft.init()`; an inline no-op in every other variant header) drives the expander, and the IMU/buzzer live in [src/board/board_s3_touch21.cpp](src/board/board_s3_touch21.cpp) behind `board::*` (no-ops elsewhere via [Board.h](include/Board.h)). All board I²C uses LovyanGFX's `lgfx::i2c` (same owner as touch) on the loop task — don't reach for Arduino `Wire`.

## FEATURE_EAM — a second product (Missileer)

`-DFEATURE_EAM` (set on the `missileer-*` envs) swaps the radar app for **Missileer** — an HFGCS EAM (Emergency Action Message) monitor — built from the same boards and the same shared infra (display, Wi-Fi, web config, NVS, HTTP/TLS, OTA, ntfy). It compiles **no** radar/aircraft/ADS-B code: the EAM envs' `build_src_filter` drops the radar-only TUs (`AircraftManager`, `MqttPublisher`, `SpecialAircraft`, `AircraftInfoFields`, `Logbook`, `models/`), `[common]` drops `src/eam/` from the radar builds, and [main.cpp](src/main.cpp) picks `EamManager` vs `AircraftManager` at compile time (same `Initialise/Update/Draw` surface). Everything EAM lives in [src/eam/](src/eam/).

- **Data:** the device talks only to one backend ("valar-eam-feed"; base URL is the runtime config `eam-base-url`, defaulting to the `EAM_FEED_BASE` build flag) over its normalized endpoints. [EamFeedClient](src/eam/EamFeedClient.h) runs **one** worker task (reusing the shared TLS client, like the radar's fetch task) with per-endpoint interval/backoff/dedupe/retention; all state stays on the loop task. Shapes + parsers are in [EamModels.h](src/eam/EamModels.h). The one exception to "feed-agnostic" is the optional **command-post watch** ([AbncpProvider.h](src/eam/AbncpProvider.h)): the "OpenSky — your account" source queries OpenSky **directly from the device** with the user's own OAuth creds (reusing [OpenSkyAuthTokenHandler](src/OpenSkyAuthTokenHandler.h)) — never via the backend, **never a baked-in key**, inert until creds are entered.
- **UI:** seven screens (ticker / tempo / codewords / ABNCP / propagation / ICBM / Zulu clock) on a dwell-timed rotation that skips empty feeds; [EamManager](src/eam/EamManager.h) + [EamScreens.cpp](src/eam/EamScreens.cpp), with a real 7-segment clock in [SevenSegment.cpp](src/eam/SevenSegment.cpp). Shares the radar's touch poll ([TouchPoll.h](include/TouchPoll.h)) and solar auto-dim.
- **Persistence + alerts:** [EamLogbook](src/eam/EamLogbook.h) (own NVS namespace `eam-log`) tracks seen EAMs/codewords; ntfy alerts reuse the radar's `ntfy-topic` + POST pattern on three toggleable triggers.
- **OTA channel:** `-DFW_OTA_PREFIX="eam-"` makes [OtaUpdater](src/OtaUpdater.cpp) fetch `firmware-eam-<slug>.bin`, so an EAM device never pulls a radar image for the same board. The shared `version.txt` gate is unchanged.
- The config web page is feature-gated in [ConfigurationWebServer.cpp](src/ConfigurationWebServer.cpp) (`#ifdef FEATURE_EAM`): the EAM form + its NVS keys instead of the radar form; the shell (mDNS, `/reset-wifi`, save flag, secret masking) is shared. Adding an EAM SKU = a variant header + an `[env:*]` (with `-DFEATURE_EAM`, the `build_src_filter`, and `FW_OTA_PREFIX`) + a CI row whose slug is `eam-<board>`.

## FEATURE_SPACE — a third product (Orbitscope)

`-DFEATURE_SPACE` (set on the `orbitscope-*` envs) swaps the radar app for **Orbitscope** (formerly "Spacescope" — some in-code comments/UI strings still use the old name) — a desk window onto live space data (ISS, rocket launches, space weather, deep-space probes) — built from the same boards and the same shared infra as the radar and EAM. It compiles **no** radar/aircraft or EAM code: the SPACE env's `build_src_filter` drops the radar-only TUs **and** `src/eam/`, `[common]` drops `src/space/` from the radar builds (and the EAM env drops it too), and [main.cpp](src/main.cpp) picks `SpaceManager` vs `EamManager` vs `AircraftManager` at compile time (same `Initialise/Update/Draw` surface). Everything Orbitscope lives in [src/space/](src/space/). The config page gains a `#elif defined(FEATURE_SPACE)` branch alongside the EAM one in [ConfigurationWebServer.cpp](src/ConfigurationWebServer.cpp). OTA channel: `-DFW_OTA_PREFIX="space-"` (its own `firmware-space-<slug>.bin`). It pulls directly from free public space APIs and **bakes in no backend**; the optional `valar-space-feed` backend is the runtime `space-base-url` config (default empty, from the `SPACE_FEED_BASE` flag). **Status: Stage-1 skeleton** — the product gate, config form, OTA channel, and the rotation/touch/brightness shell with a splash + UTC clock are in; the feed client + data screens land in later stages (each = a feed + a `DrawX()` + a `HasData()` case).

## FEATURE_SEISMIC — a fourth product (Quakescope)

`-DFEATURE_SEISMIC` (set on the `quakescope-*` envs) swaps the radar app for **Quakescope**, the Seismic edition — a desk earthquake radar fed by the keyless **USGS** feed — built from the same boards and shared infra as the radar/EAM/Space apps. Like them it compiles **no** radar/aircraft, EAM, or Space code: the SEISMIC env's `build_src_filter` drops the radar-only TUs **and** `src/eam/` **and** `src/space/`, `[common]` already drops `src/seismic/`'s siblings, and [main.cpp](src/main.cpp) picks `SeismicManager` vs `SpaceManager` vs `EamManager` vs `AircraftManager` at compile time (same `Initialise/Update/Draw` surface). Everything lives in [src/seismic/](src/seismic/). The config page gains a `#elif defined(FEATURE_SEISMIC)` branch alongside the EAM/Space ones in [ConfigurationWebServer.cpp](src/ConfigurationWebServer.cpp). OTA channel: `-DFW_OTA_PREFIX="seismic-"` (its own `firmware-seismic-<slug>.bin`).

Unlike Space/EAM (rotating screens), the Seismic UI mirrors the **Aviation radar**: three swipe-able screens (Radar / List / Stats) with a tap-to-inspect detail-card overlay ([SeismicManager](src/seismic/SeismicManager.h) + [SeismicScreens.cpp](src/seismic/SeismicScreens.cpp)). Its quake radar is **static range rings**, not the aircraft PPI sweep, so [main.cpp](src/main.cpp) gates the sweep block out of the FEATURE_SEISMIC build (alongside EAM/Space). The data layer mirrors `SpaceFeedClient`: [SeismicFeedClient](src/seismic/SeismicFeedClient.h) runs one worker over the **USGS FDSN event query API** (a worldwide "recent" query + a radius-bounded "near me" query, both bounded by `limit` so the JSON stays small), with shapes + parsers in [SeismicModels.h](src/seismic/SeismicModels.h). It bakes in **no backend** (the optional `se-base-url` config is empty by default). ntfy alerts reuse the shared `ntfy-topic` on three toggles (big quake worldwide / quake near you / tsunami-flagged), edge-seeded at boot so the backlog never fires.

## FEATURE_BIRDING — a fifth product (Quillscope)

`-DFEATURE_BIRDING` (set on the `quillscope-*` envs) swaps the radar app for **Quillscope**, the Birding edition — notable bird sightings near you, live from the **Cornell eBird API** — built from the same boards and shared infra. Its `build_src_filter` drops the radar-only TUs **and** `src/eam/` **and** `src/space/` **and** `src/seismic/`, and [main.cpp](src/main.cpp) picks `BirdingManager` at compile time. Everything lives in [src/birding/](src/birding/). The config page gains a `#elif defined(FEATURE_BIRDING)` branch in [ConfigurationWebServer.cpp](src/ConfigurationWebServer.cpp). OTA channel: `-DFW_OTA_PREFIX="birding-"`.

- **Data / key:** the source is the eBird API 2.0; it needs the user's **own free token** (config `ebird-key`, sent as the `X-eBirdApiToken` header) — never a baked-in key, the same BYO pattern as the radar's OpenSky login and masked on the config page the same way (`*`-mask on GET, skip-if-masked on save). [BirdingFeedClient](src/birding/BirdingFeedClient.h) runs one worker over three endpoints (recent *notable* nearby, all recent nearby, nearby hotspots), all bounded by `dist`/`maxResults`; shapes + parsers in [BirdingModels.h](src/birding/BirdingModels.h). Nothing is polled until a key **and** a location are set.
- **UI:** a **hybrid** of the two existing shells — a dwell-timed auto-rotation that skips empty feeds AND swipe-to-navigate (from Space/EAM), **plus** a tap-to-inspect detail-card overlay on the radar / notable screens (from the Seismic/Aviation radar). Screens: sightings Radar, Notable, Big-Day species count, nearest Hotspot, Targets, Splash, Clock ([BirdingManager](src/birding/BirdingManager.h) + [BirdingScreens.cpp](src/birding/BirdingScreens.cpp)). The sightings radar uses static range rings, so [main.cpp](src/main.cpp) gates the aircraft PPI sweep out of the build (alongside EAM/Space/Seismic). ntfy alerts on two toggles (notable sighting / target species), seeded at boot via a seen-species set so the backlog never fires.

## FEATURE_FISHING — a sixth product (Reelscope, Fishing edition)

`-DFEATURE_FISHING` (set on the `reelscope-*` envs) swaps the radar app for the **Fishing edition** (product name **Reelscope**) — a desk fishing-conditions console covering **both freshwater and saltwater**, whose hero is an **on-device SOLUNAR "best bite times" forecast**. Its `build_src_filter` drops the radar-only TUs **and** `src/eam/`/`src/space/`/`src/astro/`/`src/seismic/`/`src/birding/`, and [main.cpp](src/main.cpp) picks `FishingManager` at compile time. Everything lives in [src/fishing/](src/fishing/). The config page gains a `#elif defined(FEATURE_FISHING)` branch in [ConfigurationWebServer.cpp](src/ConfigurationWebServer.cpp). OTA channel: `-DFW_OTA_PREFIX="fishing-"`.

- **Data:** [FishingFeedClient](src/fishing/FishingFeedClient.h) mirrors `SeismicFeedClient`/`BirdingFeedClient` — one background worker over **five water_type-gated endpoints**: Flow (FRESH, USGS Water Services `/iv/`), Tides (SALT, NOAA CO-OPS `hilo` predictions), WaterTemp (SALT, CO-OPS `water_temperature`), Buoy (SALT, NDBC `realtime2`), and Weather (SHARED, Open-Meteo — air temp/wind/precip + a computed barometric trend). One fetch in flight at a time, each endpoint on its own interval + backoff; all state stays on the loop task. Shapes + parsers in [FishingModels.h](src/fishing/FishingModels.h). A fresh-only or salt-only device (config `fi-water`) never calls the other family's APIs. It bakes in **no key and no backend** — every source is keyless; the optional `fi-base-url` aggregator config is empty by default. The solunar/sun/moon math runs **on-device** with no network, from the edition's **own** `src/fishing/Solunar.cpp` (Schlyter low-precision) — **not** the shared `src/astro/`, which is now Space-only.
- **UI:** the **hybrid** shell (like Birding) — dwell-timed auto-rotation that skips empty/disabled dials, swipe-to-navigate, and a tap-to-inspect detail-card overlay; the hero is the Solunar bite dial. Screens (the `Screen` enum, also the `fi-v-*` toggles): Tide, Flow (river), Temp (water temp), Solunar, Weather (barometer/wind/air/precip/swell), Moon, CatchLog, Clock, + internal Splash ([FishingManager](src/fishing/FishingManager.h) + [FishingScreens.cpp](src/fishing/FishingScreens.cpp)). `fi-units` toggles imperial/metric. No PPI sweep, so [main.cpp](src/main.cpp) gates the sweep out of the FEATURE_FISHING build.
- **Catch log:** [FishingLogbook](src/fishing/FishingLogbook.h) (own NVS namespace `fi-log`, mirroring the other editions' logbooks) tracks lifetime catch tallies, the share landed during an active solunar window ("bite-window hit rate"), best single day, and a fishing-day streak; tap the CatchLog screen to record a catch.
- **Alerts:** ntfy on five toggles — bite window opening (`fi-a-solunar`), river flow crossing a CFS threshold (`fi-a-flow` + `fi-flow-cfs`), water temp entering a °F band (`fi-a-temp` + `fi-temp-lo/hi`), tide approaching (`fi-a-tide`), and barometric drop (`fi-a-baro`) — plus an optional speaker chime (`fi-chime`); all edge-seeded at boot so the backlog never fires.
- **Retired sibling:** an earlier **Angler edition** (`FEATURE_ANGLER`, `src/angler/`) was a competing solunar-only implementation, retired in favour of this one; the `src/astro/` ephemeris it once shared with Spacescope stays in the tree, now Space-only.

## FEATURE_CLAUDESCOPE — a seventh product (Claudescope)

`-DFEATURE_CLAUDESCOPE` (set on the `claudescope-*` envs) swaps the radar app for **Claudescope** — a desk gauge for your live **Claude usage limits** (session + weekly). Its `build_src_filter` drops the radar-only TUs **and** `src/eam/`/`src/space/`/`src/astro/`/`src/seismic/`/`src/birding/`/`src/fishing/`, and [main.cpp](src/main.cpp) picks `ClaudescopeManager` at compile time. Everything lives in [src/claudescope/](src/claudescope/). The config page gains a `#elif defined(FEATURE_CLAUDESCOPE)` branch in [ConfigurationWebServer.cpp](src/ConfigurationWebServer.cpp). OTA channel: `-DFW_OTA_PREFIX="claudescope-"`.

- **Data / sidecar (BYO, no baked-in key):** the device **never talks to Claude directly** and holds no credential. A small **`claudescope-sidecar`** on the user's LAN holds the Claude OAuth token and republishes the (undocumented) usage-window state as normalized JSON; the device only ever sees pre-chewed JSON. [ClaudescopeFeedClient](src/claudescope/ClaudescopeFeedClient.h) mirrors `SpaceFeedClient` — one background worker over a **single endpoint** (the sidecar's `/usage.json`), ~30 s poll with exponential backoff and last-good retention; all scheduling + the result store live on the loop task. It **bakes in no backend** — the sidecar address is the runtime `cl-base-url` config (empty by default), and with none set `BuildRequest` re-arms so an unconfigured device never opens a socket. Shapes + parsers in [ClaudescopeModels.h](src/claudescope/ClaudescopeModels.h).
- **UI:** a dwell-timed auto-rotation that skips empty screens + swipe-to-navigate (like Space), plus a tap-to-inspect detail-card overlay (like Fishing). Screens (the `Screen` enum): Session and Weekly — each a **ring gauge** (`DrawRingGauge`) of percent-used with a **reset countdown** — plus Clock, + internal Splash ([ClaudescopeManager](src/claudescope/ClaudescopeManager.h) + [ClaudescopeScreens.cpp](src/claudescope/ClaudescopeScreens.cpp)). No PPI sweep, so [main.cpp](src/main.cpp) gates the sweep out of the FEATURE_CLAUDESCOPE build.
- **Alerts:** ntfy on two toggles — the session limit crossing a % threshold (`cl-alert-session` + `cl-session-pct`, default 80) and the weekly limit crossing one (`cl-alert-week` + `cl-week-pct`, default 80) — edge-seeded at boot so the backlog never fires. Other config: `cl-base-url`, `cl-tz-offset`.

## FEATURE_SPEED — an eighth product (Speedscope)

`-DFEATURE_SPEED` (set on the `speedscope-*` envs) swaps the radar app for **Speedscope** — a desk speed-radar console that **ties into a [MiniSpeedCam](https://github.com/Valar-Systems/MiniSpeedCam)** (minispeedcam.com) over the LAN — built from the same boards and shared infra as the other editions. Like them it compiles **no** radar/aircraft or sibling-edition code: the SPEED env's `build_src_filter` drops the radar-only TUs **and** every sibling edition dir, `[common]` drops `src/speed/`, and [main.cpp](src/main.cpp) picks `SpeedManager` at compile time (same `Initialise/Update/Draw` surface). Everything lives in [src/speed/](src/speed/). The config page gains a `#elif defined(FEATURE_SPEED)` branch in [ConfigurationWebServer.cpp](src/ConfigurationWebServer.cpp) (append `&& !defined(FEATURE_SPEED)` to the six radar-default guard lists too). OTA channel: `-DFW_OTA_PREFIX="speed-"`.

- **Data / the tie-in:** the source is a MiniSpeedCam on the same network — **keyless** (no BYO secret, no config masking). The camera's own local API deliberately exposes no vehicle speeds, so this required a small companion endpoint on the **MiniSpeedCam r1.1 firmware**: a `GET /api/events` ring of recent passes (`{speed, ageSec, mag, dir}`) added in that repo's `events.h` alongside the existing `GET /api/state` (health + settings + live radar proximity `signal`). [SpeedFeedClient](src/speed/SpeedFeedClient.h) runs one worker polling both endpoints over plain HTTP on the LAN; shapes + parsers in [SpeedModels.h](src/speed/SpeedModels.h). The camera host (`sc-host`, default `MiniSpeedCam`) is resolved **mDNS name → IP on-device** ([SpeedManager](src/speed/SpeedManager.h)`::MaybeResolveOrigin`, since Arduino's resolver won't query mDNS itself); a bare IP or the optional `sc-base-url` proxy skip mDNS. The device has no RTC, so events carry a device-relative `ageSec` that Speedscope stamps with an absolute epoch from its own NTP clock. Speeds stay in the **camera's** unit (the `kph`/`isKph` flags). Nothing is polled until a host resolves.
- **UI:** a **hybrid** like Birding/Fishing — dwell-timed auto-rotation that skips empty screens AND swipe-to-navigate, plus tap-to-inspect detail cards. Screens: Last-pass (big speed, over/under the `sc-limit`), Live (radar-proximity arc gauge vs the camera's signal thresholds), Recent (last passes), Today (count/top/avg/%over, computed on-device), Camera (RSSI/IP/uptime/heap/last-upload/claim/fw health), Clock, Splash ([SpeedManager](src/speed/SpeedManager.h) + [SpeedScreens.cpp](src/speed/SpeedScreens.cpp)). There is **no** geographic radar/PPI sweep (the camera is a fixed point), so [main.cpp](src/main.cpp) gates the sweep out of the build (alongside the other non-aviation editions). ntfy alerts on three toggles (speeder over `sc-alert-speed` / new fastest-of-the-day / camera offline), edge-seeded at boot so the backlog never fires.

## Memory, networking and touch — what is actually true now

**This section used to describe three hard constraints inherited from the single-core ESP32-C3 Kit. That board was deleted from the tree on 2026-08-09, and two of the three were already false before it went.** They are written out below because the wrong version of this section is how one of them survived in shipping code for months: `ExitDetail()` was still freeing the photo sprite "so a TLS handshake has room" on a board where that sprite lives in PSRAM and cannot touch the internal heap at all.

Every SKU is now a **dual-core S3 with PSRAM**. Read the state below as current, and treat a C3-shaped justification found anywhere in the tree as a bug report rather than a rule.

1. **Contiguous heap: still real, but no number can gate it.** A TLS handshake needs one large contiguous internal block, and the shared TLS buffers must not be shrunk — that part holds. What does *not* hold is gating on a heap figure. `ENRICH_TLS_HEAP_FLOOR` compared `ESP.getMaxAllocHeap()` against 16,000 and was deleted 2026-08-09: issue #163 and [HeapProbe](src/probe/HeapProbe.cpp) proved on hardware that `getMaxAllocHeap()` is a max *across regions* and latches onto reserves nothing allocates from — over 54 h it took five distinct values in 6,466 samples and never fired once, including at the two moments an allocation genuinely failed. **The gate is [`heaphealth::CanHandshake()`](src/HeapHealth.h)**: it trial-allocates the real size and believes the answer. Enrichment is not heap-gated by a constant on any board.

   Banded rendering ([BandCanvas](include/BandCanvas.h)) still exists and every edition draws through it, but `variant::BANDED_RENDER` is `false` on every SKU — it is a one-band pass-through, kept as the hook for a future PSRAM-less board. Parsing feeds straight off the socket (`GetJson`) is still worth doing on its own merits.

   **Sprites go in PSRAM and cost nothing internally.** Measured 2026-08-09 on the s3-128: a 240×240 backbuffer plus a 150×100 and a 240×240 photo sprite moved `psram_free` by 73,532 B and left the internal heap untouched. Do not free a PSRAM sprite to "make room" for a handshake.

2. **Touch I2C vs TLS: gone with the board, and must not come back.** On the C3's single core an overlapping CST816 transfer wedged the controller off the bus until reboot (PR #8 / commit `56a3df2`), so touch was serialized against the network via `HttpRequestManager::TryAcquireBus/ReleaseBus`. `variant::SERIALIZE_TOUCH_BUS` and both call sites were removed 2026-08-09. **Do not reinstate the gate on an S3 as a precaution:** the mutex is held for the full duration of every GET/POST, which under always-on enrichment is most of the time, so gating the poll silently drops every tap landing inside a fetch window.

   [TouchWatchdog](src/TouchWatchdog.h) stays, and stays for a *different* reason than the one it was written for. It is active only where `variant::TOUCH_WATCHDOG` is set — now just the Kit S3 (`s3-128`). It was armed there to A/B the CST816 wedge against the C3; that comparison ended with the C3, but the supervisor earns its place in production by keeping `MaintainNoSleep` re-arming the touch IC's `DisAutoSleep` (0xFE) after a silent chip-internal reset, which this batch's CST816 does (see [INCOMING-INSPECTION.md](INCOMING-INSPECTION.md)). It probes only at moments the chip is **provably awake** and recovers with one driver re-init + a 450 ms boot wait + a backoff ladder, counting wedges/recoveries into the `[health]` line.

3. **The 10 s Task-WDT: kept deliberately, on a dead rationale.** [setup()](src/main.cpp) raises the Task-WDT to 10 s because a blocking handshake on a background task could keep `async_tcp` from being fed and reboot the board — a *single-core* argument. On a dual-core S3 with networking on the other core it no longer applies. It is left at 10 s anyway: that is a conservative default, and tightening a reboot threshold wants a reason of its own rather than the absence of the old one. Recorded here so the next person doesn't mistake it for a live constraint.

## Architecture in three sentences

- [AircraftManager](src/AircraftManager.h) owns essentially all state and runs on the Arduino **loop task**; all `trackedAircraft` mutation happens there.
- Three background FreeRTOS tasks do the blocking network work and never hold pointers into shared state — OpenSky/local **fetch**, adsbdb/photo **enrich**, and **MQTT** publish — handing parsed results back to the loop via queues; they share one HTTP client ([HttpRequestManager](src/HttpRequestManager.h)) because there isn't heap for a second TLS context.
- The UI is three swipe-able screens (Radar / List / Stats) with a detail card overlay; config is an async web page ([ConfigurationWebServer](src/ConfigurationWebServer.cpp)) reachable at `http://<device-name>.local`.

## Conventions

- Branching: feature/fix branches → PR into `main`. OTA is **per-SKU**: bump `FW_VERSION` ([OtaUpdater.h](src/OtaUpdater.h)) once, then a GitHub Release builds and publishes `firmware-<slug>.bin` for every SKU — see [RELEASING.md](RELEASING.md). A device only ever downloads its own slug's binary.
- Put per-variant behaviour behind `variant::` capability flags, not `#ifdef`s scattered through the logic — and delete a flag once no SKU sets it and no code reads it. `ENRICH_ALWAYS` survived as a flag that read like a switch and was wired to nothing.
- `credentials.json` (OpenSky client secret) is a user secret — never read, commit, or log it.

## Standing practice: read the artifact, not the config

**For anything that gates what ships, verify the built thing — not the source that was supposed to
produce it.** Build systems have asymmetries that are invisible in the file and obvious in the
output, and every instance here has been silent rather than loud. One example reads as a fluke, so
the list is the point:

- **`-U`/`-D` in [platformio.ini](platformio.ini):** PlatformIO puts `-U` in `CCFLAGS` and `-D` in
  `CPPDEFINES`, so an undefine beats a *later* redefine whatever order the file shows. A bench env
  built with **no backend URL at all**, and compiled clean. Invisible in the ini; one `grep` on the ELF.
- **The shipping env vs the CI matrix:** the cloud feed lived in an env CI never built, so every
  released radar binary had it compiled out. The ini looked entirely deliberate.
- **A wrong-SKU flash** presents exactly like a dead panel — healthy boot, `tft.init=1`, black
  screen. Nothing in the source says which board an image was for.
- **`embed-pages.mjs --check` / `embed-fonts.mjs --check`** exist because editing `pages/*.html`
  and forgetting to regenerate leaves every test passing against the old markup.

So: `grep` the ELF for the string that proves a flag took, diff the generated module against its
source, read the `[build] env=` banner off the board rather than trusting which command you ran.
When a check *can* assert on the built output instead of the input, it should — the input is a
statement of intent, and intent is the thing that was already wrong.

Corollary, same root cause: **when a check protects a property, confirm the check's own
environment has that property** — see [RELEASING.md](RELEASING.md).

## Standing practice: a default only reaches keys that were never saved

**Changing a `defaultOn` in firmware reaches factory-fresh devices and nobody
else.** The config page posts the WHOLE form, and an unchecked box is simply
absent from the body — so `SaveToggle()` writes an explicit `"false"` for it. From
that moment the key has a value, and no future default can reach it:

```cpp
infoFieldEnabled[i] = stored.isEmpty() ? defaultOn : (stored == "true");
```

The first time a customer saves *anything*, the defaults in force that day are
frozen into their NVS as explicit values. And they must save something — setting
a location is a whole-form save, and a device without one shows an empty radar.

So #238 (aircraft type + operator ship on) could not reach a single configured
device. It looked like a one-line fix and was inert everywhere it mattered.

**The rule: any change to a `defaultOn` needs a migration alongside it**, or it
ships to nobody who already owns the product. See
[include/ConfigMigration.h](include/ConfigMigration.h) — it DELETES the key
rather than forcing a value, because deleting says *"you never made a choice
about this"*, which is the truth when the old default was off.

Two corollaries worth carrying:

- **A stored value and a firmware default are different kinds of thing.** Reading
  a toggle tells you what the device will do; it does not tell you whether anyone
  ever decided it.
- **The same shape appears wherever a UI writes a full snapshot.** Any
  "save everything" form freezes every default it renders, including ones the
  customer never looked at.

## Standing practice: a green signal is about process; the artifact is elsewhere

**CI green does not mean deployed. MERGED does not mean in main. `/healthz` and the
tree are the artifacts.** Three separate incidents in one week, all the same shape:

- **`MERGED` ≠ in main.** PR #233 was opened against a branch that was then
  squash-merged, so it merged into a dead end. GitHub was not wrong — the merge
  happened. 26 files and ~15,200 lines were absent from main for hours while the
  badge read MERGED. `git ls-tree origin/main src/FactoryReset.cpp` was empty, and
  that was the only thing that said so.
- **CI green ≠ deployed.** `workers.yml` typechecks and tests the Workers; it has no
  deploy step. A green run on main was read as "the change is live", and production
  sat **20 commits behind** for three days while the code it needed was in main.
  `curl /healthz` reports the commit actually running, and it disagreed.
- **A merged PR's data can outrun its code.** 9,837 `pa:` rows and an `ovr:` override
  were written to production KV while the Worker that reads them was undeployed. The
  rows were live and unread, and the licence obligation they carried went unmet.

The tell is always the same: a signal that describes *process* being read as a fact
about *the running system*. The counter is one command, and it is never the badge:

| question | the artifact |
|---|---|
| is it in main? | `git ls-tree origin/main -- <path>` |
| is it deployed? | `curl .../healthz` → the stamped commit |
| is it on the board? | the `[build] env=` banner on serial |
| did the flag take? | `grep` the ELF |

Same family as everything below, one level up: the input is a statement of intent,
and a green check is a statement that the intent was processed.

## Standing practice: take a check's input from the *other* side of the contract

**A test that requests the path the test chose will pass against a feature that is
dead.** Device enrollment shipped with the firmware popup opening
`scopes.valarsystems.com/enroll` while the Worker routed only
`/blipscope/enroll`. It was a 404 — the whole feature — sitting behind sixteen
passing tests, because every one of them asked for the URL *the tests* had
picked. Neither side was wrong internally. The contract between them was simply
never exercised, and nothing about a green suite says which of those two things
it proved.

The fix is not "add a test for the other path" — that is the same assumption
typed a second time, and it goes stale the same way. It is to **derive the
check's input from the other side**:
[smoke-prod.sh](proxy/scripts/smoke-prod.sh) greps the enrol URLs out of
[ConfigurationWebServer.cpp](src/ConfigurationWebServer.cpp) and fetches each one
against the live Worker, so what gets requested is the firmware's own string.
Change either side and the check follows.

When the other side is in this repo, **read it**. When it is not, transcribe it
and say so out loud — [test/missileer-routes.test.ts](proxy/test/missileer-routes.test.ts)
pins another repo's route table and its header states exactly which failures that
can and cannot catch. A transcription is the weaker form and should never be
mistaken for the strong one.

Same family as the entry above: the input is a statement of intent, and here
*both* sides stated it, separately.

## Standing practice: watch for the fix whose failure mimics the bug

Some mistakes announce themselves. The ones that keep costing us time here are
the ones where **being wrong looks exactly like the problem you were fixing** —
so the evidence that the fix is broken reads as evidence that it hasn't finished
working yet.

Three instances, and the shape is the point:

- **A wrong exclusion range.** The non-ICAO table
  ([icaoalloc.ts](proxy/src/icaoalloc.ts) / [SpecialAircraft.cpp](src/SpecialAircraft.cpp))
  exists to stop enriching addresses that can never resolve. A range wrongly
  *included* blanks a **real** aircraft — which is precisely the symptom of the
  enrichment bug the table was added to cure. The first draft listed five
  registry-empty regions and blanked `f40001`; it read as a data gap, not a code
  defect.
- **A test that requests the path the test chose.** Sixteen passing tests around
  an enrolment endpoint that 404'd, because a green suite looks the same whether
  it proved the contract or only its own assumption.
- **A rehearsal that couldn't fail.** A check whose environment lacked the
  property it was checking passes for the same reason a correct system does.

What they share: **the failing and the passing state produce the same
observation.** No amount of staring at that observation separates them.

The move is always the same — find a control whose result differs between the
two worlds, and run it *before* believing the result:

- an exclusion list gets a **positive** case that must still resolve (real
  aircraft, named in the test, from the blocks nearest the exclusion)
- a contract check derives its input from the **other side**
- a rehearsal is made to fail on purpose once, and observed failing

Corollary for exclusion lists specifically, since they recur: the two error
directions are not symmetric. Missing an entry costs one pointless request —
the status quo. A wrong entry silently removes something real. So an exclusion
earns its place by **positive evidence that it was observed**, never by absence
from a snapshot. When in doubt, leave it out.

## Standing practice: never measure the sky through the anonymous endpoint

The relays exist because the upstreams throttle us by IP
([blipscope-egress-relay](docs/), and the adsb.lol per-IP notes). A workstation
`curl` to `api.adsb.lol` does not go through them — so it is throttled, and
**the throttled response is not an error.** It is a clean `200` with
`"msg": "No error"` and an empty `ac:[]`.

That reads exactly like "nothing is flying there", which is how it was used to
conclude a hex was not airborne. The control that caught it: our own `/v1/blips`
had returned five aircraft over Heathrow seconds earlier, and asking adsb.lol
anonymously for three of those same hexes returned `ac:[]` for all three. The
measurement was of the throttle, not the sky.

So: **query traffic through our own Worker**, which uses the relays and the key.
Direct anonymous upstream calls are for asking *"does this endpoint have a record
for X"* where a negative is stated rather than implied — adsbdb's
`{"response":"unknown aircraft"}` is a real answer and safe to trust; an empty
array is not. Same family as the two entries above and the one below: an empty
result that cannot distinguish "no data" from "not allowed to see it" is a check
that cannot detect its own failure.

## Standing practice: a cross-language port of Worker math must pin its rounding

**When Worker logic is reimplemented in another language, the arithmetic that
looks most obviously identical is the part that silently diverges.** Not the
algorithm — the primitives underneath it.

The instance: Skyscope transcribes the wire schema's request quantization from
[proxy/src/schema.ts](proxy/src/schema.ts) into Python. `Math.round` and
Python's `round()` read as the same function and are not. JavaScript rounds
halves **away from zero for positives** (`Math.round(1028.5) === 1029`); Python
rounds halves **to even** (`round(1028.5) == 1028`). So a Skyscope station and a
Blipscope device at identical coordinates landed on *different cache tiles*
whenever the tile index fell exactly on `.5`.

The consequence there was mild — a cache miss, not wrong data — and that is
precisely why it is worth writing down. It was found by a unit test disagreeing
with a hand-computed expectation, and nothing about the port would ever have
surfaced it: both implementations are correct in their own language, both pass
their own tests, and the divergence only exists at the boundary.

**The class, which is what to watch for.** Any of these differ across
JS/Python/C++ and all of them appear in this Worker's math:

- **half-way rounding** — `Math.round` (half up) vs Python `round` (half to
  even) vs C++ `std::round` (half away from zero);
- **integer division and modulo of negatives** — `-7 / 2 | 0` truncates toward
  zero in JS, `-7 // 2` floors in Python; `%` follows the sign of the *dividend*
  in JS and C++ and of the *divisor* in Python, which matters for every
  `((x % 360) + 360) % 360` normalisation;
- **float-to-string** — JS `toFixed` vs Python `%.2f` disagree on ties, and the
  tile key is a *string*;
- **integer width** — JS bitwise ops coerce to int32; Python integers do not
  overflow.

**So the rule: a port of Worker math pins its rounding semantics explicitly and
proves it with a cross-implementation test.** In practice that means a named
helper rather than the language's built-in (Skyscope has `schema.js_round`,
which is `floor(x + 0.5)` and is documented as being JS's semantics, not
Python's), plus a test asserting the *disagreement* — `js_round(1028.5) == 1029`
**and** `round(1028.5) == 1028` — so the reason the helper exists cannot be
optimised away by someone who reasonably assumes the built-in would do.

Same family as the transcription entry above: the strong form of the check
derives its input from the other side ([tools/check_schema_sync.py in
Skyscope](https://github.com/Valar-Systems/skyscope) parses this repo's
`schema.ts` directly). But a schema diff compares *constants*, and this class of
defect lives in the *operations* — so the two checks are complementary and
neither substitutes for the other.

## Standing practice: a KV bulk put drops keys silently, and `Success!` means nothing

**Established twice now, on two different key families, so it is a property of the
tool and not a bad day.** `wrangler kv bulk put` reports `Success!` per chunk and
exits zero while having written fewer keys than it was given.

| | what was loaded | what vanished |
|---|---|---|
| 2026-08-25 | `rt:` — 619,103 route keys | `rt:IGO7J`, mid-chunk, in a chunk unrelated to the 524 that were retried |
| 2026-08-28 | `ap:` — 39,105 airport keys | `ap:VTF`, on the first production load |

Both were confirmed absent rather than assumed: a live `GET` returned 404 and a
direct KV read found nothing, which together separate "the key is missing" from
"the list index has not caught up yet" — and the second is a real state that
looks identical for a minute or two after a load.

**What this invalidates.** Every quantity an ingest can report about its own
write:

- `written=39105` counts chunks the CLI **accepted**. The increment happens on a
  zero exit status, so a dropped key increments it.
- A sample passes. One key in 39,105 has a 1-in-3,258 chance of appearing in a
  12-key sample; the routes case was 1 in 51,592.
- A global percentage reads as a rounding error. 39,104/39,105 is "100%".

**So an ingest is not done when the writer says so. It is done when something
that is not the writer has enumerated the whole namespace and diffed it, per
shard.** That is what `verify-routes.ts` and `verify-airports.ts` are, why they
report per shard rather than per corpus, and why they refuse to judge a listing
too small to be trustworthy (exit 2, distinct from exit 1's real gap).

**And the repair has to be able to override the diff.** This is the part that is
easy to leave out. A shard-hash diff correctly skips a shard whose source has not
changed — which is exactly the state a dropped key leaves behind, so without a
`--force-shard` escape the hole is **permanent**: every later run writes nothing
and the key never returns. A mirror without that flag does not converge on
correct, it accumulates silent gaps forever.

The four steps exist for this and are not ceremony: **write → verify →
force-shard repair → verify**, and only then seal the meta key. Both incidents
above were caught at step 2 and fixed at step 3.

## Standing practice: a presence check prints a boolean, never a value

**The question is almost always "is this secret set?" — which is one bit. Print
the bit.** Printing the secret to answer it puts the secret in scrollback, in the
session transcript, in CI logs, and in whatever the screen was being shared to.

The instance (2026-08-28): checking whether `CLOUDFLARE_API_TOKEN` was available
before an ingest run, via

```sh
reg query "HKCU\Environment" | grep -i token      # DON'T
```

`reg query` prints **names and values**, and so four live secrets —
`CLOUDFLARE_API_TOKEN`, `BLIP_KEY`, `MSC_BOOTSTRAP_TOKEN`, `BLIP_DEVICE` — went
into the transcript in plaintext and had to be rotated. Narrowing to one name
does not help: `reg query "HKCU\Environment" /v CLOUDFLARE_API_TOKEN` prints that
value too. The tool's job is to show you values; the mistake was asking it.

**The safe forms all end in a boolean, and none of them can be made to leak by a
tool doing its job:**

```sh
[ -n "$CLOUDFLARE_API_TOKEN" ] && echo present || echo absent
reg query "HKCU\Environment" /v NAME >/dev/null 2>&1 && echo present || echo absent
powershell -NoProfile -Command "if ([Environment]::GetEnvironmentVariable('NAME','User')) { 'present' } else { 'absent' }"
```

If a length is genuinely needed to tell a truncated paste from a good one, print
the **length**, never a prefix. A prefix of a token is still a prefix of a token,
and the entropy you left out is not the part that identifies it.

**Why this is its own entry and not a footnote to the filtering rule.** The
filtering rule says a filter written against expected output is blindest when the
command fails. This is the mirror image: the filter was written against the
output's *shape* (`grep` for a name) while the command emitted a different shape
(name **and** value), and the cost landed instantly rather than being hidden. The
rule that covers both: **decide what you need out of a command before running it,
and shape the command to emit exactly that** — do not emit everything and sort it
out afterwards, because "afterwards" is too late for a secret and too generous
for a failure.

Related, and the reason this is not merely tidiness: this project has already
leaked one credential through logs — see the Wi-Fi password incident, fixed
forward in PR #183. That one was firmware serial output; this one was a shell.
Same class, different surface.

## Standing practice: never filter the output of a command you are testing for failure

Twice in one week a `grep`/`tail` on a command's output hid the failure it was
run to detect. The clearest instance: `wrangler secret put` was piped through a
filter, the Cloudflare `Authentication error [code: 10000]` was dropped, and what
survived was the whoami banner — which reads exactly like success. The secret was
never set, and it was about to be reported as done.

The mechanism is general. A filter is written against the output you *expect*,
so it is at its least reliable in precisely the case you are running the command
to detect. Pipe it whole, or grep for the failure token **as well as** the
success one — never only the latter. It is this repo's recurring rule — a check
must be able to detect its own failure — applied to the shell: output you have
pre-trimmed to the passing shape cannot show you anything else.

**The shape of it, which is the part worth remembering:** a filter is written
against the output you EXPECT, so it is blindest exactly when the command fails.
The rule alone did not hold — it was breached twice more in the same session it
was written. So the procedure, not the principle:

1. **Run a diagnostic command bare the first time.** No `grep`, no `head`, no
   `2>/dev/null`.
2. **Send stderr to its own file** (`2>err.log`) rather than merging or
   discarding it. Cloudflare's `wrangler` puts a 401 on stderr and a cheerful
   "would you like to report this?" on stdout, so a stdout-only read of a failed
   command looks like a parse problem rather than an auth problem.
3. **Only add a filter once you have seen the raw output** and know both what
   success and failure look like.

The cost is a few lines of scrollback. The cost of the alternative was reporting
a secret as set when it was not.

**The same rule governs an assertion, not just a pipe.** A test that checks for a
*substring* has pre-trimmed the output exactly like a `grep` — and it is blindest
in the case it exists for. Instance (2026-08-26): the launch gate's matrix parser
got a selftest whose cases were `case "$got" in *" charlie-retired "*)`, i.e.
"this token must be absent". Rehearsed against a parser broken on purpose, it
printed eight PASSes and `SELFTEST PASSED`. `sed`'s `s///` rewrites only the
matched span and prints the whole line, so the commented-out row came back as
`#charlie-retired` — the forbidden token, present, wearing a hash. The assertion
was written against the shape the author imagined the failure would take.

The fix is the same one as for filters: **stop matching fragments, compare the
whole thing.** The selftest now asserts the parser's entire output equals an
entire expected list, so anything extra fails regardless of what it looks like.
`grep -c pattern` → compare the full list; `assert x in out` → `assert out == y`.

### Worked example: the rule, written down, then broken twice in one hour

The principle above was already in this file when
[verify-release.sh](scripts/verify-release.sh)'s square-photo gate was written —
a check whose entire job is to stop a release from removing photographs from
every device in the fleet. It shipped with **two** failures of exactly this rule.
Both are recorded because knowing the rule demonstrably does not prevent it.

**1. It read stdout instead of the exit status, so it passed on absence.**
The probe asked KV for a pointer key and treated *non-empty stdout* as "the key
exists". On a **missing** key `wrangler kv key get` exits 1 and prints a cheerful
`Would you like to report this error to Cloudflare?` **on stdout**. So a
deliberately made-up aircraft type came back `PASS`. The gate against blanking
every card in the fleet would have passed against a completely empty library.

> A stdout-only read does not merely fail to detect the problem — **it reports
> the problem as success**, because the failure output is chatty and lands on the
> stream being read as the answer.

The fix is both halves, and neither alone is enough: **check the exit status**,
and **validate the shape of what came back** — using the consumer's own rule
(here `isValidPhotoKey`/`BLOB_KEY_RE`, so a value the probe rejects is a value
the device would reject).

**2. It ran from the wrong directory, so it reported "absent" for everything.**
`npx wrangler` was invoked from the repo root, where there is no `wrangler.toml`.
Every lookup returned nothing, which the probe read as "no squares published" —
a fleet-wide emergency, reported with total confidence, caused by a `cd`.

**What saved it was the anchor control, and only that.** Before believing any
result, the probe requires a key it *knows* exists to resolve. On its first real
run that control fired and said *"cannot distinguish a missing library from a
probe that cannot read KV"* — which is the true statement — instead of the false
and much more exciting one.

**The two fixes compound, and that is the point of pairing them.** They answer
different questions and each is nearly useless alone:

| | answers | without it |
|---|---|---|
| anchor control | *is this result trustworthy at all?* | a broken probe reports a fleet-wide emergency with total confidence |
| stderr to a file | *why is it broken?* | you know not to trust it, and nothing else — so you bisect |

Measured on the third catch (a transient Cloudflare **401** during a KV probe,
API rate-limiting in the wake of ~1,900 ingest writes): the anchor said *don't
believe this run*, and the captured stderr said **`401: Unauthorized`**. Cause
established in one second, retry succeeded, finding re-established properly.
The two previous catches had the anchor but not the capture, and each cost a
round of guessing at a silent empty string — the same symptom, three different
causes (wrong directory, stdout-vs-exit-status, expired auth), and only the
third one was legible on sight.

So: **a guard that can tell you a result is untrustworthy should also be able to
tell you why.** Refusing to answer is the correct behaviour and is still a dead
end if the reason was thrown away.

**So the standing requirement, not a suggestion:** any probe that reports absence
must first prove it can observe presence. A negative result from an unvalidated
probe is not evidence, and "everything is missing" is the single most likely
shape of a broken probe. Both failures above produce it, and neither is visible
in the code by reading it — only by running the control.

Corollary for a gate with more than two outcomes: keep **"the thing is missing"**
and **"the thing was never there"** distinct. The square probe reports a type
with a rectangle but no square as `FAIL` (a release would propagate the gap) and
a type absent from the library entirely as `WARN` (nothing regresses). Collapsing
them into one "not found" is what makes a gate either cry wolf or stay silent.

Finally: once the system is healthy the failing branch may become **unreachable**
— after a full publish, no real type sits in the "rectangle but no square" state.
A gate that cannot be made to fail is a gate nobody can check. Leave a seam that
forces it (`PROBE_PANEL=999`), and use it.

## Usage telemetry: counts yes, subjects never (revised 2026-08-27)

**This section used to read "Non-goal: behavioural telemetry (settled 2026-08-02)"
and said never.** It was reversed deliberately, by Daniel, and the reversal is
recorded rather than edited away so the next person does not read the new code as
a violation of an old rule and revert it.

**What is collected now.** Anonymous counts of feature use, at most hourly, on a
request the device already makes: detail-card opens, screen switches per screen,
logbook claims, whether Follow is configured (a boolean), and uptime hours. Eight
integers. See [include/UsageReport.h](include/UsageReport.h).

**What is not, and this is the part that has not moved.** Which aircraft was
opened. The callsign, the tail number, the follow target. Location. Configuration.
A timestamp per event. Anything from the §17 list.

**The line, in one sentence: count THAT a feature was used, never WHAT it was used
on.** The old rule drew its line at collecting nothing; this one draws it between
the action and its subject. That is a narrower line and it is easier to cross by
accident, which is why it is enforced by construction and not by this paragraph:

- `usage::Format()` takes a struct of **integers**. There is no parameter that
  could carry a name, so appending one requires changing a signature — a visible
  act, not an accident.
- `test/host/test_usage_report.cpp` asserts the payload is **digits and commas
  only**, with a control that it is not merely empty. That fails for every
  identity at once rather than for a named example.
- `recordUsage()` in the Worker re-asserts the same shape from the other side of
  the wire and drops anything else.

**And the disclosure is part of the feature, not a follow-up.** Adding a counter
without updating [README.md](README.md)'s Privacy & telemetry section and
[proxy/pages/support.html](proxy/pages/support.html) makes a published statement
false — both said "permanently" before this. If a future change widens what is
collected, those two files change in the same commit or the change is not done.

