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

## Standing practice: write down what each result will mean BEFORE you can see which one you got

This is one technique, not several instincts, and it produced most of what went
right in the sessions of 2026-08-31 and 09-01:

- the **four borders** -- the Columbia, the Snake, the Bitterroot, California's
  southern edge -- were named *before* the acceptance test was written, so the
  test could fail. A test written after looking at the output picks whatever the
  output happened to contain;
- `reconcile-fleet.py` **predicted 0/1/3 and then 3/1/0** before it ran;
- the OTA plan's **outcome tables** were written before Run 1 existed;
- `scripts/ota-latest-probe.sh` carries its **verdict table in the script**, so
  the result cannot be argued with after it arrives.

The value is not rigour for its own sake. It is that the moment you can see the
result is the moment you are least able to judge it fairly -- the first
explanation that fits arrives already feeling like the conclusion.

**AND PRE-REGISTER THE OUTSIDE-THE-SET BRANCH, which is the refinement that cost
us one.** The dual-path `download_count` probe was properly pre-registered with
three outcomes: `+7` meant the `latest` alias was uncounted, `>=8` meant it
counted and we had been seeing lag, `0` meant the counter was dead. The result
was **+6**. Because no meaning had been assigned to "none of the above", one was
improvised on the spot -- and the improvised reading argued that tag fetches
count exactly in order to eliminate one hypothesis, in the same paragraph that
argued they do not in order to explain the shortfall. Both cannot hold.

The technique did not fail; **the predicted set was incomplete**. So every
verdict table gets an escape hatch, and "none of the above -> the instrument is
unreliable at this scale, stop and decide" is a legitimate pre-registered
outcome. A table without one quietly guarantees improvisation at exactly the
moment you can least afford it.

### And the same discipline pointed at the INPUT: prove the artifact is holding still

Pre-registration removes the moment where you improvise about the *result*. This
removes the moment where you improvise about *what you were even measuring*. Same
discipline, other end.

**Three failures on 2026-09-01, three different artifacts, one cause:**

- **a tree that had moved.** Three tool calls were spent editing `proxy/src/enrich.ts`
  against a copy that had already been committed and built on. The anchor did not
  match, and the natural reading of that -- "my patch is wrong" -- was the wrong
  one; the file was.
- **a probe opened while its predecessor was still draining.** The `download_count`
  follow-up was designed against a baseline the first probe had not finished
  moving.
- **"an eighth instance" against a table that grew to twelve.** Rows landed in
  parallel from another session, so an ORDINAL written in one context named a
  different row by the time it was read.

**And the cure was already in this repo three times, unnamed:**

- `reconcile-fleet.py`'s caveat computes its window *from the data* -- "measured,
  not assumed";
- the clean Instrument B re-test requires **a baseline read twice at an interval**
  to prove it is static before any fetch is made;
- and the COM4 soak taught it outright: **an anchor against a moving target
  measures the movement.** Three reads of one quantity gave 5132, 5135, 5136, and
  the parser was correct -- the file was still appending.

**The rule, and the proof is always the same shape: read it twice.**

> Prove the artifact is static before anchoring to it.

| what you are anchoring to | the two-read proof |
|---|---|
| a repository | `git log` **before** `git diff` -- and again if minutes have passed |
| a counter, a log, a live capture | baseline, wait, baseline again; only then measure |
| a document other sessions edit | refer to a row by **name**, never by ordinal |

**The cleanest example of the family is not one of the failures, because nobody
made a mistake in it.** A 17:18 status read reported `enrich.ts` dirty and
`routekey.test.ts` untracked; a report written shortly after found `proxy/`
clean. Both observations were correct -- the file was genuinely dirty at 17:18
and genuinely committed minutes later. **Two correct observations that contradict
each other is the signature of this family**, and when you see one, the question
is not which observer was wrong. It is what moved in between.

## Standing practice: when you add a second path, enumerate what the FIRST one establishes

**This started as seven instances in one day, in seven unrelated subsystems: a
guard existed, was correct, and one path did not go through it.** That was not a
coincidence and it was not seven bugs. It is one bug about how second paths get
written — and the table below has kept growing since, which is the argument.

(The count is deliberately not stated as a number anywhere else. It was "seven"
for exactly as long as it took someone to find an eighth.)

**Rows are referenced BY NAME, never by position.** This file is edited by several
sessions at once: an entry written as "an eighth" was accurate when written and false
by the time it was pushed, because rows landed underneath it in the meantime. A
name survives reordering and insertion; an ordinal is a future wrong statement with
nothing to announce the change. Same defect the file is full of -- a reference that
was true once and is not re-checked.

| subsystem | the guard | the path that missed it |
|---|---|---|
| Follow dwell | auto-surface arms a dwell so the device never steals the screen | the SWIPE path armed it too, closing a face the customer had chosen |
| bearing face marker | the rings read the aircraft position from the view | the marker fetched it again from `FollowedAircraft()`, null in the absence states |
| `followAutoReturnTo` | the entry path records the screen to return to | `ClearSessionFollow` hardcoded `Screen::Radar` and never read it |
| session clear | `SetTarget(false)` clears the machine's state | it deliberately KEEPS `last`/`lastFixMs`, so a dismissed flight's position survived into the next subject |
| `ChordWidthPx` | "the rule lives here, once, and every caller goes through it" | the detail card never became a caller |
| `getMaxAllocHeap` | a heap floor gating TLS handshakes | every path went through it; it established nothing, and never fired once in 6,466 samples |
| route cache | `resolveRoute` refuses a route implausible for the aircraft's position | the CACHED branch returns three lines before the test |
| editor anchor | a unique-match guard: refuse unless the anchor appears exactly once | it establishes UNAMBIGUITY, not LOCATION -- a loose anchor matched once, in the wrong function, and edited `DrawStats` instead of `DrawList` |
| admin-0 whole-part rule | "keep a part only if EVERY vertex is inside the box" | it rejected **zero** parts. The 24 N floor was doing the work, and got no scrutiny because attention went to the elaborate mechanism |
| `ProgressAlong`'s clamp | `min(1, x)` keeps progress in range | a ratio above 1.0 is PROOF the aircraft is not between the endpoints; the clamp maps that proof onto 100%, the most reassuring number in the range |
| admin-0 extent bound | a host test asserting no border vertex sits north of 70 N | it CANNOT BE WRITTEN there: the generated `.inc` merges admin-0 and admin-1 and records which layer a line came from nowhere, so the check has no way to scope itself |

**Rows 8 to 10 are not the same failure as 1 to 7, and collapsing them loses the
point.** Most are structural: a guard exists and a path goes round
it. The last three are about guards that *run on every path* and still protect
nothing.

- **The EDITOR ANCHOR row is a guard that answers a narrower question than its name suggests.**
  Unique-match is a real property; it is simply not the property "this edit lands
  where I meant". The check was working perfectly.
- **The ADMIN-0 WHOLE-PART row is a rule that has never rejected anything.** That is not evidence it
  works -- it is evidence it is untested, *and* that something else is silently
  doing the job you attributed to it. **The method that finds these is to relax
  each constraint and see what changes.** Dropping the box floor from 24 N to
  12 N swept in seven parts and 251 vertices and drew lines through Guatemala and
  Haiti; that is what converted "I believe this rule works" into "I know which
  rule is load-bearing". If nothing changes when you relax a constraint, the work
  is happening somewhere you have not looked.
- **`ProgressAlong`'s CLAMP destroys the evidence.** A
  defensive clamp or default that maps an IMPOSSIBLE value onto a PLAUSIBLE one
  does not merely lose a diagnosis, it manufactures reassurance. The clamp
  converts the strongest available evidence of a wrong route into the most
  reassuring possible number. The tell is short enough to grep for: `min(1, x)`,
  `?? 0`, `if (n > max) n = max`. At every one, ask **what did the out-of-range
  value know?**

**THE ADMIN-0 EXTENT BOUND IS THE DIAGNOSTIC ONE, AND THE MOST USEFUL ENTRY IN THIS TABLE.** It
is not a guard with a path around it; it is a guard placed *after the information
it depends on stopped existing*. The same shape appeared twice on 2026-09-01:

| | upstream knows | what discards it | the check that cannot be written |
|---|---|---|---|
| route cache | `/api/0/routeset` takes callsign **+ lat + lng**, so the upstream decides which leg | caching under `rt:${cs}`, with no positional component | `routeContradicted`, downstream, trying to catch the consequence -- and structurally unable to see a REVERSED route, since it has the same two endpoints |
| border data | the generator holds `a0_parts` and `a1_parts` separately | merging both into one `.inc` with no layer marker | a host-test extent bound scoped to admin-0 -- 78.69 N is legitimate admin-1 data and nothing downstream can tell the two apart |

Rows 1 to 10 tell you a hazard exists. This one tells you **where to go**, and
the prescription is a question you can actually ask:

> **When a check refuses to be written, find the last stage that still had the
> information, and put it there.**

The generator got the latitude-band refusal for exactly that reason, and it was
observed firing. The route cache's fix is the same move one layer up: the key,
not a downstream plausibility test.


**Another instance, in an argv path (2026-09-01).** `deploy.sh` ends by calling
`confirm-deploy.sh` -- the step that proves a deploy actually landed -- via
`"$(dirname "$0")/..."`, evaluated *after* the script has `cd`'d into `proxy/`.
Invoked the canonical way (from inside `proxy/`) it resolves. Invoked as
`bash proxy/scripts/deploy.sh` from the repo root it resolves to
`proxy/proxy/scripts/` and dies **exit 127, after a successful upload**. So the
guard existed, ran on one invocation path, and silently did not exist on the
other -- and it was masked because production genuinely *was* healthy that time,
which is the self-camouflaging half below.

Worth noting where this family has now turned up: in code (most of the table), in a
**comment** that stated the hazard beside a path that ignored it, in **test
scaffolding** (an assertion matching a fragment of the shape the author imagined
failure would take), and now in **how a script is invoked**. The shape is not a
property of code — it is a property of second paths, wherever they live.

**The rule.** When you add a second path, enumerate what the first path
*establishes* -- not what function it calls. Sometimes the guard can be shared;
sometimes the property has to be re-derived. Either way the question is what the
first path knows to be true by the time it returns.

That is deliberately harder to apply than "call the same helper", because the ROUTE CACHE row
proves the easy version is not always available: the route check's verdict was
`r.plausible`, computed by the UPSTREAM during a fetch. There is no fetch on the
cached path, so the property had to be re-derived geometrically. A rule phrased
as "reuse the guard" would have had nothing to say there. And `getMaxAllocHeap` is the case that
kills the easy version outright -- every path DID call the guard, and the guard
was measuring something that was never true.

**The tell, when you are looking for instances rather than waiting for them:** a
comment on one path stating a hazard, and another path that does not mention it.
`resolveRoute`'s own comment says *"callsigns get reused across legs"*, two lines
below the `return` that skipped the check. `ChordWidthPx`'s header says *"every
caller goes through it"*. The knowledge was written down both times. Prose does
not run.

**Which is the counterexample worth carrying, because it is the encouraging
half.** On the same day, `scripts/deploy.sh` refused a production deploy over an
untracked `err.log` inside `proxy/` -- a file its own author had just created,
caught by a guard written that morning for exactly that case. The guards that
EXECUTE do protect you, including from the person who wrote them. It is the
guards that are only written down that fail, and every row in the table above is
one of those.

So: a rule added to this file is the weakest form of the fix. Prefer a check that
runs -- a refusal, a test, a `--tol=` the caller must state. Where that is not
possible, the entry at least gives the next person the shape to recognise.

## Standing practice: an instrument that fires correctly into a void

Two failure modes already have entries here: **a guard with a path around it**
(one caller skipped it) and **a rule that never fires** (`getMaxAllocHeap`, which
measured nothing). This is the third, and it is the worst of them, because
nothing anywhere is broken.

The enrolment ledger recorded a runaway loop faithfully — ~600 events a day for
twenty days. The Worker logged every one. The code was right, the data was
right, the number sat in KV the whole time. It surfaced on 2026-09-01 only
because a fleet reconcile run **for an unrelated reason** happened to list the
keys.

There is no failing test to write here and no guard to add. The signal was
perfect. **The only defect was that nothing read it.**

Two things follow, and the second is the one that gets skipped:

- **A new instrument needs a reader, specified at the same time as the writer.**
  "We can query it when we need to" is how this happens — the moment you need to
  query it is the moment you already know something is wrong, and the instrument
  existed to tell you *before* that.
- **The reader must be somewhere a person already goes.** Not a dashboard to
  remember, not a query to run. `scripts/reconcile-fleet.py` prints enrolments
  per day and the fw: table's liveness on every run, because that tool gets run
  whenever anyone asks anything about the fleet.

The tell: an instrument whose value has never appeared in output anyone reads.
Ask of anything you build that records — *what routinely prints this, and if the
answer is "someone would query it", who, and when?*

**Instrument A has exactly this fate available to it.** It will be read closely
during Run 1 and then, unless something reads it routinely, never again — which
is precisely the position the enrolment ledger was in. That is why it is in the
reconcile output rather than only in [docs/ota-control-plan.md](docs/ota-control-plan.md).

**The sharper version, and it is worse than "nobody reads the instrument."** The
plan doc *already said* the two `changes[]` entries were synthetic. That
paragraph was correct, committed, and hours old when **its own author** read the
same KV row from a different direction and reported the `8 -> 9 -> 8` pair as a
firmware downgrade worth investigating. Not a stale doc, not a missing doc, not
someone else's doc. The person who wrote the note did not read the note.

So the lesson is **not** "write it down". It was written down. The lesson is
**put it where the reader is already looking** — and the reader is not in the
document, because whoever trips over a signal arrives from somewhere else
entirely, holding a different question.

That is why the fix is the printed number and not the paragraph: the change
count now appears in `scripts/reconcile-fleet.py`'s output, next to the fw:
rows, where anyone asking anything about the fleet will see it without having
chosen to. The CLAUDE.md row and the doc paragraph are the weak form and are
explicitly labelled as such in both places. **A rule that has to be remembered
before it helps has already failed the case it was written for** — this file's
own opening entry says a check that runs beats a rule that is written, and this
is that principle turned on documentation itself.

## Standing practice: a finding without a named-and-eliminated alternative is not finished

Distinct from every entry above, which are about code structure. This one is
about **reading evidence**, and it is the failure behind the YVR call, behind
reading a photograph sideways, and behind "87 downloads, therefore the timer
works".

The tell is short: **a report that offers exactly one explanation for an
observation.**

- *"`code: 10000`, therefore a missing permission"* -- without naming that a
  token shadowing a working `wrangler login` session produces an identical
  error. It did. Four sessions have gone into that variable and every one began
  by checking whether it was SET; presence was never the question.
- *"87 downloads, therefore discovery and the timer are alive"* -- without
  naming that CI and development traffic produce the same count. And the
  supporting ratio, 87 checks against 3 installs, is equally the shape developer
  traffic makes. **A ratio consistent with two explanations discriminates
  between neither.**

**Do not write this rule as "ask what else produces this".** That is a habit,
and habits fail on output you are currently producing -- it was stated and then
breached in the same message, twice in one day. Write it as something a reader
can check by looking at the text:

> State the alternative explicitly and say why the data excludes it. If it does
> not exclude it, report both.

**The test is whether you can finish the sentence.** *"The alternative is X, and
the data excludes it because --"* either completes or it does not, and the
failure is visible while writing rather than in review. The `+6` paragraph would
have failed its own test at the moment of writing.

Same family as the control that proves a check can fail: a finding with no named
alternative is visibly unfinished, in the way a check with no control is.

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

### Corollary: two builds of the same source are NOT byte-identical, and that is not a wrong build

**A release binary rebuilt from identical source gets a different sha256. Do not read
that as a different build.** Measured 2026-09-01, v9 re-cut from the same firmware
source after a CI-only change: `d583522b...` -> `e789984d...`, identical size, and
**75 differing bytes out of 1,855,616** -- every one accounted for:

| region | bytes |
|---|---|
| `esp_app_desc_t.app_elf_sha256` @0xB0 | 32 |
| trailing image SHA-256 | 32 |
| image checksum byte | 1 |
| WiFiManager `aboutdate` string, `18:46:38` -> `19:18:54` | 5 |
| WiFiManager `Software Info:` string, `18:47:17` -> `19:19:35` | 5 |

**Zero payload bytes differ.** Those timestamps come from WiFiManager's about page
embedding `__DATE__ " " __TIME__`.

Without this written down, the next session to compare shas across a rebuild spends an
hour proving the build is wrong. It is not. **The accounting is what makes the claim
safe** -- "probably just timestamps" is a guess; 75 bytes with all five regions named
and nothing left over is a measurement. If you ever find bytes you cannot place, that
IS a different build and the whole thing stops.

**And the honest half, which is the reusable half.** The determinism check that missed
this grepped `src/` and `include/` for `__DATE__`/`__TIME__`, found nothing, and
concluded the build carried no wall-clock. The wall-clock was in a DEPENDENCY, under
`.pio/libdeps/` -- outside the grep. **The check was scoped to our side of the boundary
and the defect lived on the other side**, which is this file's most repeated shape. A
build's inputs are its source AND its libraries; scope determinism questions to the
link, not to the repo.

So: **compare shas to detect an unexpected republish, never to identify a build.** To
identify a build, use content only that build has -- the pre-registered discriminator
set (strings present in the new image and provably absent from the old), behind an
anchor control. That survives a rebuild; a sha does not.


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

## Standing practice: a gate that refuses to certify is working; check the coupling

**2026-09-01, v9.** `animtest-s3-128` failed the adsbdb launch gate and the whole
fleet's OTA gate went dark: `version` carried `needs: build`, `build` is a MATRIX,
and GitHub hands a dependent job the AGGREGATE of every leg. One slug-less harness
env failed, `version` was skipped, and
`releases/latest/download/version.txt` returned **404** while ten verified binaries
sat on the release. No device could discover v9.

**THE GATE DID ITS JOB. DO NOT "FIX" THE GATE.** It reported
`UNTRUSTWORTHY (control blind)`, not FAIL. The gate plants a reachable adsbdb
string and requires the scanner to FIND it before believing any absence; in the
animation harness -- which contains no enrichment code -- there was nothing to
plant it in, so the control came back blind. It refused to certify rather than
emitting a false PASS on an image it could not actually see into. That is this
file's oldest rule executing correctly, and the next person to meet a red
`animtest` leg will be tempted to relax the gate to clear it. **The defect was
never the gate. It was the COUPLING** -- a row that publishes nothing being able
to block publication.

The fix is [scripts/check-publish-receipts.sh](scripts/check-publish-receipts.sh):
`version` keeps `needs: build` for ORDERING only, `always()` stops the aggregate
deciding anything, and a slug-ful leg emits a receipt after its gate AND its
upload both pass. The discriminator is `slug`, read from the matrix -- an env with
one publishes a binary and must be certified; an env without one publishes
nothing and cannot withhold the fleet's gate. **The direction is asymmetric and
is the entire point:** a slug-FUL leg that fails leaves no receipt, the check
fails, and version.txt does NOT advance. Devices must never be pointed at an
image no launch gate certified.

A receipt rather than "is the asset on the release": the release already holds
assets from earlier runs, so asset-exists is equally true of a stale upload from
a run whose gate failed -- the exact state this must refuse. Same family as the
anchor control: **evidence about THIS run, not about history.**

**THE NEW GATE'S BLOCKING DIRECTION HAS NEVER FIRED, AND THAT IS A ROW IN THE TABLE
ABOVE.** Its permissive direction is proven -- on 2026-09-01 `animtest` failed and
`version` published anyway, which is why v9 shipped at all. Its blocking direction is
proven only in the SCRIPT (selftest plus two deliberate sabotages), never in the
WORKFLOW WIRING. `version` carries `if: always() && github.event_name == 'release'`, so
no push or PR run can exercise it: on those events the job is skipped whatever the
receipts say.

That leaves exactly the shape this file warns about -- **a rule nobody has seen reject
anything is untested, and something else may be doing its job.** The old wiring blocked
on everything, badly; the new wiring blocks on receipts, and a hole in the receipt
logic publishes version.txt for an image the launch gate never certified. Silent, and
it surfaces on the day a shipping SKU actually fails -- the worst possible day to find
out.

**It can be closed safely, and the mechanism is the same `github.event_name ==
'release'` that blocks the easy test.** Cut a throwaway tag (`v9-gatetest`) with one
SHIPPING SKU deliberately broken and publish it as a PRERELEASE. A prerelease is
excluded from `releases/latest`, so no device can see it and
`latest/download/version.txt` keeps resolving to the real release throughout. That buys
a genuine release-event run with a failing slug-ful leg, and the observation is one
bit: **is `version` skipped?** Zero fleet exposure, both directions closed.

NOT during an OTA observation window -- it makes workflow runs and release noise while
a run is the thing being measured. Scheduled for after Run 1 closes
(2026-09-02T20:21:42Z).

**Still open, deliberately.** The gate cannot yet distinguish SKIP ("nothing to
verify here") from UNTRUSTWORTHY ("cannot verify"), so `animtest` stays red
forever. That is the correct fix and it is NOT free: a SKIP that is easy to earn
is a hole in a gate that currently has none. It needs scheduling rather than
filing, because a permanently red leg trains everyone to read past it -- which is
precisely the state the enrolment ledger sat in for twenty days.

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

### Vocabulary: never use a word that spans several of these facts

Not a caution — a rule about which word to type. **"Shipped" is not checkable.**
For a Worker it spans four different facts, and on 2026-08-31 only the first was
true while the last was reported:

| say this | and it means exactly | checked by |
|---|---|---|
| **"PR open."** | a branch exists on the remote with a description | `gh pr view N` |
| **"Merged."** | it is in `main` | `git ls-tree origin/main -- <path>` |
| **"CI green."** | the tests ran and passed | the run |
| **"Deployed, `/healthz` reports `<sha>`."** | **production is running it** | `curl .../healthz` |

Only the last is a statement about the running system, and `workers.yml` has no
deploy step — so for this repo **merged does not imply deployed either**, and the
gap between those two is a three-day, twenty-commit incident already recorded
above.

The cost of the vague word is not vagueness, it is a false negative on a live
bug: *"the parseRevoked fix shipped"* read as *fixed in production*, when
production was still running the broken parser and would have kept running it
until somebody re-checked. **Say which fact. Each of the four is one command.**

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

## Standing practice: when you decline to state a number, assert that it cannot come back

**A decision not to claim something is a decision, and decisions decay.** The
usual way is not an argument — nobody re-litigates it — but a later edit that
looks like an improvement. Someone fills in the blank, because a blank reads as
unfinished.

The instance: Follow's no-coverage copy. The spec's worked example ends *"Next
contact expected around 18:40, near Ireland."* We licence no model of where
receiver coverage resumes and no schedule data, so that time would be invented,
on the one screen whose entire job is to explain an absence honestly. It was cut.

A comment saying "do not add a time here" would have been the normal protection.
It is worth almost nothing: it is advice, sitting next to a string, addressed to
someone who has already decided the string looks incomplete.

**What was written instead is a test that the string contains no digit.**

```cpp
for (const char* p = e; *p; ++p) if (*p >= '0' && *p <= '9') claimsATime = true;
check(!claimsATime, "the ocean copy states no time -- we cannot know one");
check(e[0] != '\0',  "CONTROL: it still says something");
```

That is a different kind of object from a comment. It does not ask anyone to
agree; it fails the build. And it does not need to anticipate the specific
sentence someone might add — any restored precision, in any wording, in any
unit, contains a digit.

**The generalisation, which is the reason this is here.** Wherever a product
declines to state something because the data does not support it, the decline can
usually be made *structural* rather than *remembered*:

| the decline | the assertion that keeps it |
|---|---|
| no arrival time we cannot compute | the string contains no digit |
| no countdown without schedule data | the pre-departure face renders no `%d` |
| no AGL without field elevation | `AglFt()` returns NaN, and a test says so |
| no landing claim without evidence | the rail: `Landed` unreachable from absence |

Note the shape they share: **the honest version has a property the dishonest
version cannot have.** Find that property and assert it. Three of the four rows
above already existed in this codebase before the rule was written down, which is
the sign it was a pattern rather than a preference.

The paired CONTROL is not optional. "Contains no digit" is trivially satisfied by
an empty string, and an empty absence message is a worse failure than a made-up
time — so the assertion that it still says *something* is what stops the guard
from passing in the one state it exists to prevent. Same rule as everywhere else
here: a check that cannot fail is a check nobody can trust.

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

## Standing practice: a rule can be right in its domain and wrong one call site over

**The dangerous rule is not the wrong one. It is the correct one, cited
accurately, at a site it was never about.** Nobody catches that by re-reading the
rule — the rule is fine — and nobody catches it by reading the code, because the
code cites a real principle and looks disciplined doing it.

The instance (2026-08-31). Follow's spec §13.3 says:

> **Follow gets a screen; it never gets THE screen.**

Correct and load-bearing. It governs the **device** raising the Follow face on a
state transition: a followed aircraft going quiet must not steal the display from
a rare contact passing overhead. The auto-surface path arms a dwell and hands the
screen back, exactly as it should.

`SetSessionFollow` — the customer's deliberate **swipe** — armed the same dwell,
citing the same rule, with a comment explaining it. Twenty seconds after asking
to follow an aircraft, the face was gone. That inverted the defining requirement
of the entire feature:

> *"I don't want the card to auto close, I want it to stay open and the aircraft
> followed the entire way on screen"*

And it did more than annoy: **every absence state became unobservable**, because
you cannot watch a flight leave coverage on a screen that closes first. The glass
gate the feature was waiting on could not be run.

**Why it survived review.** The line was commented, the citation was accurate,
and the behaviour it produced was *deliberate*. There is nothing to notice. The
first hypothesis on the bench was that a card timeout had been inherited — a
plausible mechanism that was not what happened.

**What separates the two sites is one word: who asked.** §13.3 protects the
customer from the DEVICE taking the screen. A swipe is the customer TAKING the
screen. Same face, same timer, opposite meaning.

So when a rule is cited at a new call site, ask **whose case it was written for**,
not whether it applies. It will always seem to apply — that is what makes a good
rule general, and it is exactly the property that lets it be misapplied with
confidence.

Cheap tell, worth running: **a rule that fires identically on a device-initiated
and a customer-initiated path is almost certainly wrong on one of them.** The two
have different owners, and a rule about who may take something cannot be
indifferent to who is doing the taking.

## Standing practice: an alarm raised from memory or from prose is not an alarm

**Read the artifact first — the blob, the deployed value, the rendered page —
then raise it.** This is *read the artifact, not the config* applied to incident
reporting, and it is the same rule both times.

2026-08-31 produced three alarms in one session. The tally is the argument:

| alarm | raised from | real? |
|---|---|---|
| "a fleet unit is in a stranger's hands" | an **annotation** beside a real id: *"RMA 2026-08-04, board resold"* | **no** — example text |
| "I committed a file full of real device ids to a public repo" | an **hour-old memory** of what that file contained | **no** — it had been emptied before the commit |
| "production refuses a correctly-derived key" | reading the **bytes** of `cfg:revoked` | **yes** — `parseRevoked` was broken |

The two false ones were both raised from *words*: example text mistaken for a
record, and a recollection of a file mistaken for the file. The true one came
from reading what was actually there. One `git cat-file -p <blob>` would have
killed the second before it was ever stated — and it did kill it, one message
late, which is the right instinct arriving after the cost was paid.

**Why the cost is asymmetric, and why this is not just "be careful".** A false
alarm during an incident does not merely waste time: it *redirects* the
investigation. The RMA annotation sent an hour into "whose device is this and how
did they get it" while a live production auth failure sat unexamined. An alarm is
a steering input, so raising one from unverified material steers on unverified
material.

**This entry fired on its own author the same afternoon it was written.** A
token file was reported deleted on the strength of a Python `os.path.exists()`
that had been handed an MSYS path (`/c/Users/...`) it could not resolve; the file
was still on disk. It was caught before being reported — `ls -la` in bash, which
understands that path, said so — and that is the only one of the day's four
instances caught in time. Worth recording, because it is the difference between a
rule people believe and a rule people have watched fire: the artifact was a
one-word `ls`, and the tool's word was confidently wrong in the reassuring
direction.

**A COUNT AND ITS OWN LIST ARE TWO CLAIMS.** The closing summary of the session
that produced this entry said *"five PRs (#277, #279, #281, #282, #283, #285)"*
and *"three issues (#278, #280, #284, #286)"* — six and four. Writing both is
free; checking they agree is the part that gets skipped, and a total sitting
beside its own enumeration and disagreeing with it is the same contradiction
check being applied to everything else in the document. Whenever you write "N
things" next to the N things, count them.

The same summary said a fix had **shipped** when what had happened was that a PR
was open. See *a green signal is about process; the artifact is elsewhere* —
"shipped" is a word about intent, and MERGED, CI-green and deployed are three
different facts. For a Worker there is a fourth: `workers.yml` has no deploy
step, so even merging does not ship it.

**A SINGLE READING OF A PROPAGATING SYSTEM IS A COIN FLIP, AND IT LANDS ON
WHICHEVER ANSWER YOU WERE BRACED FOR.** The revocation probe of 2026-08-31 had a
stale 401 at the start of its step-2 window, while a ~60 s cache TTL drained. One
poll timed a few seconds earlier reads *"still revoked"* and the conclusion is
that the fix is not live. `polls=3,4,4` is what made it legible as propagation
rather than failure.

That is the exact mirror of the stale-`LIVE` bug found the same morning: there, a
retained value said **fine** when the endpoint was unreachable; here, a retained
value would have said **broken** when the parser was correct. Same mechanism,
opposite directions, both invisible to a single sample. Anything with a cache, a
drain, or an isolate lifetime is sampled until it is stable, never read once.

**A REHEARSAL YOU REASONED ABOUT TESTS YOUR MODEL. A REHEARSAL YOU RAN TESTS THE
CODE.** Only the second can surprise you, and the surprise is the point.

Reasoning about a sabotage produces exactly the failures you already predicted —
which is what makes it feel efficient, and precisely why it is worthless. On
2026-08-31 a deploy-confirmation loop was sabotaged by deleting its
"unreachable" branch, to reproduce a reported bug (a good deploy reported as
unconfirmed). Running it produced the predicted failure **and one nobody had
thought of**:

    unreachable -> cannot observe        got WRONG_SHA     <- predicted
    unreachable OUTRANKS a stale body    got CONFIRMED     <- not predicted

`LIVE` persisted across loop iterations, so an endpoint that answered once and
then went down was reported as a **confirmed deploy**. The reported bug was noisy
in the alarming direction; this one was silent in the reassuring direction, and
it was in the block whose only job is proving what production is running.

Note where it sits: *the readings that need a second source are the ones that
agree with you*. The reassuring-direction failure is structurally invisible to
surprise, so it will never be the one you predict — and it turned up in the very
next check, in a confirmation loop, one message after that rule was written.

**Corollary: a sabotage that does not visibly change the result did not apply.**
The first attempt at that rehearsal silently failed to patch the file, and the
unsabotaged "9 passed" was nearly reported as the rehearsal. Print the failure
count before and after; if they match, the sabotage is the thing that is broken.

The check is one line and it is always available:

| claim | the artifact |
|---|---|
| "this file contains X" | `git cat-file -p <blob>` / `git show <sha>:<path>` |
| "this id is a real device" | the registry: `wrangler kv key list --prefix "enr:dev:"` |
| "this annotation is a record" | nothing — **which is the point.** If provenance cannot be established, it is prose |

Corollary, which is the fix for the third row: **unlabelled real data is a trap
with the sign flipped.** The RMA annotation cost an hour precisely because nobody
had written *"this is fake"* beside it. So example data says it is example data
(`0000000000000000`, `# EXAMPLE -- not a real device`), and real data that must
stay says why it is there. Neither is expensive; the ambiguity between them is.

## Standing practice: on this machine, do file operations in bash, not Python

**Windows Python cannot resolve an MSYS path (`/c/Users/...`, `/tmp/...`), and it
does not error — it reports the file absent.** Every failure is therefore in the
reassuring direction:

| what was done | what Python said | what was true |
|---|---|---|
| delete a credential file | "already absent" | still on disk |
| patch a file to sabotage it | patched | never applied; the selftest passed unchanged |

Twice in one session, both reported as success. Bash understands those paths
because it created them; `git`, `python` and any other native Windows binary do
not, and `/tmp` in particular is two different directories depending on who is
looking.

**So: `ls`, `rm`, `cp`, `test -f` and the verification that follows them happen in
bash.** When Python must touch a file, hand it a `C:/`-style path and check the
result in bash afterwards — the check is what matters, since the failure mode is
silent success.

Same shape as the redirect trap one level down: `cmd > /tmp/x` writes an MSYS
path that a subsequent Windows process cannot open, so the file "does not exist"
to the second command while plainly existing to the first.

## Standing practice: a rule you have just written down does not protect you from it

**Writing the rule is not the control. The control is the thing that fires
without you.**

2026-08-31, twice, hours apart, by the person who had written the rule that day:

- The **enrolment-recency trap** was written into
  [docs/bench-key-rotation.md](docs/bench-key-rotation.md) — *"`lastAt` is the
  last ENROLMENT, not the last time the device was seen; a single enrolment is
  the signature of a working unit."* **Within the hour**, a device with 32,165
  requests in fourteen days was written off as *"a bench board, silent since
  08-17"* — from its last enrolment.
- The **example-text landmine** was written into a commit message — *"example
  text that is indistinguishable from a real record is a landmine"* — and
  twenty minutes later a `git add -A proxy/` swept an operator scratch file into
  a public repo.

Knowing a rule and applying it are different cognitive acts, and the second one
competes with everything else in a debugging session. **A rule is a prompt you
have to remember to read; by the twentieth minute of a good run you are not
reading it.**

So the useful move is not "write it down harder". It is to ask **what narrower,
mechanisable thing was actually behind it** — both instances above sit under one:

> `git add -A` stages what you did not look at.

and then to build the thing that fires by itself:

| the rule | the control that does not need you |
|---|---|
| "don't commit real device ids" | [`.githooks/pre-commit`](.githooks/pre-commit) + [`scripts/check_device_ids.py`](scripts/check_device_ids.py) in CI |
| "don't let example data read as real" | the allowlist file — fake ids are the only ones that pass |
| "don't infer liveness from enrolment" | *(still only a rule — and therefore still likely to fail)* |

**What the last failure of each un-mechanised rule cost** — attach the receipt, or
the rule reads as a style preference:

| rule with no control | last failure | cost |
|---|---|---|
| don't infer liveness from enrolment | 2026-08-31 | a device with 32,165 requests in fourteen days written off as dormant; the blast radius of a production secret rotation stated wrong, twice |

Prose that has to survive on persuasion needs its receipts attached. A reader who
meets *"don't infer liveness from enrolment"* cold will weigh it against whatever
they are trying to get done; a reader who meets it with **an hour and a wrong
blast radius** beside it will not.

That last row is left in deliberately. Not everything mechanises, and the honest
thing is to know which of your rules are load-bearing prose and which are
enforced — because they feel identical from the inside, right up until one of
them doesn't fire.

**Both guards read ONE allowlist**, incidentally, because the first version had
two inline lists and they drifted within four minutes: the hook refused a commit
the CI check considered clean. Two guards on one rule is two rules, and the
second one is always the stale one.

## Standing practice: know which instrument can see the failure you have

**The bench panel and the host suite fail in opposite directions, and the lesson
is not "trust the board".** It is that each one is blind to a whole class of
defect, and picking the wrong instrument means looking straight at a bug and
seeing nothing wrong.

The case that made this a rule (2026-08-30, Follow's globe face). Endpoint labels
were colliding with the fixed text rows, so they got a vertical nudge: shove the
label to whichever side of the offending row is nearer, and repeat. It looked
right, it compiled, and on the panel it looked *fixed* — the label that had been
crowded was no longer crowded.

A host test swept the crowded half of the disc and reported **13 placements
against 40 refusals**. The algorithm was thrashing: where rows sit close together
(6 px and 1 px apart at the bottom of that face) it pushed the label up into the
next row, back down into the first, and out of its iteration bound as "no fit".
A greedy local move cannot see past the row it is standing in.

**So the fix for "the label is missing" would have deleted the label** — in
exactly the cases nobody photographs, and while presenting as a success in the
one case they do. Replacing the walk with an enumeration over the only 2n+1
positions that can be the answer took it to 53 placements, 0 refusals.

The panel could not have found this, and not because nobody looked hard enough:

| | the panel sees | the host suite sees |
|---|---|---|
| **one case, fully rendered** | yes — colour, contrast, crowding, whether it *reads* | no |
| **a population of cases** | one at a time, whichever the feed happens to produce | all of them, including the ones no feed will produce today |
| **absence** | nothing. A label that is not drawn looks like a label that is not there | a count, which is the only form absence has |

A missing label on a panel is invisible **by construction**: there is no gap, no
error, no ellipsis — the pixels that would have been the label are simply the
picture behind it. The refusal rate had no visual signature at all. It had a
number, and only one of the two instruments produces numbers.

The converse holds just as hard, which is why this is not "write more tests":
the host suite would happily pass a label rendered in green on green, at 2 px, or
behind the terminator. **It cannot see a picture.** Both of the defects fixed
that day — *green-on-green over coastline* and *13/40 refusals* — were on the
same feature, in the same hour, and each was invisible to the instrument that
caught the other.

Practical form, and it is a question to ask before reaching for either:

- **Is the property a rendering, or a population?** Colour, contrast, crowding,
  "does it read" — glass. Rates, counts, coverage, "does it ever fail" — host.
- **Can the failure be seen at all in the medium I am about to use?** Absence
  cannot be seen on a panel. A picture cannot be seen in a test.
- **When the property is "this never happens", sweep it.** One observation of a
  thing not happening is not evidence; a sweep with a placed-vs-refused count is.
  Print both numbers — `placed 53, refused 0 over 53 starts` is a result, and
  `PASS` is not.

Same family as [the coverage-gap note in the Follow spec](docs/follow-mode-consolidated.md)
one level down: there, the bench photographs live states because live is what a
bench produces by default, so the absence states go unseen. Here, the bench
cannot photograph a non-event at all. Both are "the instrument decides what you
are able to notice."

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

### A probe that cannot prove it planted is not a probe

**Rehearsing a check red only means something if the sabotage actually
happened.** Three times in one session a probe silently failed to apply, and
each time the resulting green was read as information.

- **The `/tmp` backup that was never written.** A header was copied to `/tmp`
  from bash and restored from Python, which on Windows resolves that path
  somewhere else entirely. Both probes failed to plant, and all three candidate
  layout rows reported `0 failures` — including the two that were broken. The
  reading was "the test passes everywhere", i.e. exactly backwards.
- **The `sed` that matched nothing.** A cross-wiring probe used a pattern with
  one space where the file had four. It reported PASS, which was true and
  meaningless. Redone with an assertion that the edit applied: two real failures.
- **The unfalsifiable prediction.** A message predicted the same slot would
  render `BENCH-UN` and `BENCHUNK`, one paragraph apart, and miscounted the
  source string. A prediction covering both outcomes cannot be wrong, so
  confirming it proves nothing.

The shape is the same as the two entries above it: **the observation cannot
distinguish the passing world from the broken one.** A stale anchor, a moving
target, and a probe that did not plant all produce output that looks like
success.

So, mechanically:

1. **Assert the mutation before measuring.** `assert needle in src` then
   `assert after != before`. Prefer doing it in the language that will read the
   file back, so path and escaping assumptions are shared.
2. **Never construct a probe with `sed` or a shell heredoc when the pattern
   contains backslashes or indentation.** Both have silently eaten probes here.
   Build the edit in Python with `chr(92)` where a literal backslash is needed.
3. **State one expected value, not a range of them.** If you cannot say which
   string should appear, you do not yet know what the check proves.
4. **Read the failure, not just the count.** "0 failures" from a probe that
   never planted is indistinguishable from "0 failures" because the code is
   correct — so print what changed, and check that something did.

### A plausible measurement from a build that never landed

**Same family as the entry above, different mechanism.** There the sabotage did
not apply; here the *fix* did not apply, and both hand back a believable number
about a state that does not exist.

Twice in one night a `pio ... -t upload` was fired while the serial bridge still
held COM4. It fails in about ten seconds with

    A fatal error occurred: Could not open COM4, the port is busy or doesn't exist.

and `bench-capture.ps1` documents exactly this ("killing the process returns
immediately but Windows releases the COM handle a beat later"). The board keeps
running the OLD image, the bridge reattaches, the keys are acknowledged, the
serial log looks completely normal — and the measurement that follows is real
data from the wrong firmware.

**What caught it was that the numbers were too identical.** `arc=40.78ms` against
the previous run's `arc=40.80ms`. Had the change been subtle, two runs agreeing
to a hundredth of a millisecond would have read as *"no regression"* — the most
reassuring possible phrasing of "you measured nothing".

The general trap: **a failed deploy and an ineffective change produce the same
observation.** So does a successful deploy of a change that does nothing. Three
different worlds, one reading.

Mechanically, and this is cheap:

1. **Read the flash's exit status and stop on it.** Not the tail of its output —
   the exit code. A failed upload prints plenty of cheerful text above the error.
2. **Free the port before flashing, and wait for the handle**, which is a poll,
   not a sleep. Killing the holder returns before Windows releases it.
3. **Prove the new image is the one running before you measure it.** The
   `[build] env=` banner names the ENV, not the BUILD, so it cannot distinguish
   two images of the same env — which is precisely this case. Until a build stamp
   exists, the substitute is to change something observable and observe it.
4. **Treat an identical number as suspicious, not as confirmation.** Real
   measurements of a real change move, even slightly. Two runs agreeing exactly
   is evidence about the pipeline, not about the code.

**The third member, and the worst, because it is CONSISTENT.** A generated data
file (`src/anim/Coastlines.inc`) is `#include`d by a `.cpp`, and **SCons does not
track it as a dependency**. Regenerate the data, rebuild, and the build succeeds,
reports nothing, and links the PREVIOUS data.

Three coastline densities were built and measured this way. All three were the
same shipped build. Unlike a failed flash — which produces *one* wrong number and
usually smells wrong — this produces a *complete, internally consistent table* of
wrong numbers, and the conclusion it supports ("density barely affects frame
cost") is exactly the sort of tidy result nobody questions.

What caught it was rule 4 above: two of the image sizes were **byte-identical**
for vertex counts differing by more than a thousand. The fix is one line
(`touch` the including TU) and it is now written at the top of the generator.

So a fourth rule, and the generalisation of the three: **compare the ARTIFACTS,
not the numbers derived from them.** `cmp` the two binaries. If a change that
should alter the image leaves it byte-identical, nothing downstream is worth
reading.

Related, from the same session and worth one line so the symptom is recognised:
**a rehearsal log ran away to 8.1 GB and filled the volume to 13 MB free.** The
first build after that failed with `OSError: [Errno 28] No space left on device`
buried in a SCons traceback — which reads as a build error, not a disk error, and
sent the investigation toward the data that had just changed. Check `df` before
believing a build failure that arrives right after you generated something large.

### Corollary: an anchor is meaningless against a target that is still moving

**A live log is not a file, it is a stream with a filename.** Reading the COM4
soak, the anchor fired three times and reported three different counts for the
same quantity — 5132, 5135, then 5136 frame lines. Every instinct says flaky
regex, and the next hour goes into the parser. The parser was correct. The
capture was still appending, so `grep` and the parse ran against different
bytes; two reads *inside one Python process* disagreed for the same reason.

The tell is the direction: the parse found **more** than `grep` did. A broken
regex drops lines, it does not invent them. An anchor that fails *upward* is
evidence about the file, not the code.

So: **snapshot first, then measure both sides of the same bytes** — and derive
the expected counts from that snapshot rather than hardcoding them, or the
anchor goes stale the moment anything appends. One `cp`, and the check becomes
trustworthy for the same reason it was untrustworthy before.

This is the same family as everything above, one step earlier: those entries are
about a check that cannot detect its own failure, and this is about a check that
reports a failure that is not there. Both end with the artifact being blamed for
the instrument.

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

