<h1 align=center>
  🔭 Valar Scopes
</h1>
<h6 align=center>
  a tiny desk display that turns a stream of live data into something you can glance at
</h6>
<p align=center>
  <a href="#the-editions">EDITIONS</a> - <a href="#more-editions-on-the-way">ROADMAP</a> - <a href="#get-a-kit">GET A KIT</a> - <a href="#assembly">ASSEMBLY</a> - <a href="#firmware">FIRMWARE</a> - <a href="#setup--usage">SETUP</a> - <a href="#thanks">THANKS</a>
</p>

**Valar Scopes** is a line of small open-source gadgets for your desk: an ESP32-S3 driving a round touchscreen. The line comes in several **Editions** — and the trick is that they're all the *same hardware*. Each Edition is a separate firmware that turns the screen into a window onto a different stream of live data, and pings your phone when something notable happens. They all share the same Wi-Fi setup, web config page, persistent storage, over-the-air updates, and [ntfy](https://ntfy.sh) alerts — so once you know one, you know them all. You just flash the Edition you want.

## The Editions

| | Edition | What it is |
|---|---|---|
| 📡 | **[Blipscope](#-blipscope-aviation)** *(the original — Aviation)* | A live **flight radar**. Aircraft plotted around your location from public ADS-B data, with tap-to-inspect detail cards, a spotting logbook, and a "look up!" overhead alert. |
| 📟 | **[Missileer](#-missileer-eam-monitor)** *(HFGCS / EAM)* | An **HFGCS Emergency Action Message monitor**. A command-console ticker of nuclear-command radio traffic, an activity gauge, Skyking codewords, an airborne-command-post watch, HF propagation, and ICBM-test windows. |
| 🛰️ | **[Orbitscope](#-orbitscope-space)** *(Space)* | Live **space data**. The **ISS** ground track, a **T-minus countdown** to the next rocket launch, and a **geomagnetic aurora gauge**, straight from free public APIs. |
| 🌐 | **[Quakescope](#-quakescope-seismic)** *(Seismic)* | A live **earthquake radar**. Quakes plotted by bearing and distance from the keyless USGS feed, with magnitude rings, tap-to-inspect cards, and alerts for big, nearby, or tsunami-flagged events. |
| 🐦 | **[Quillscope](#-quillscope-birding)** *(Birding)* | A **notable-sightings radar**. Birds reported near you from eBird, on a tap-to-inspect radar plus rotating screens (notable ticker, day-list count, nearest hotspot, target species) — and a ping when a rarity shows up. |
| 🎣 | **[Reelscope](#-reelscope-fishing)** *(Fishing)* | A **fishing console** for fresh water and salt. An on-device **solunar "best bite times"** band, plus live river gauges (USGS), tides and waves (NOAA), water temperature, and barometric trend — all keyless. |
| 🤖 | **[Claudescope](#-claudescope-claude-usage)** *(Claude usage)* | A live gauge for your **Claude usage limits**. Your **session** and **weekly** caps as ring gauges with reset countdowns, and a phone ping when a limit runs low — read from your own Claude login through a small helper on your network. |
| 🚗 | **[Speedscope](#-speedscope-speed-radar)** *(Speed radar — the newest edition)* | A **desk speed-radar** for your street. Pairs over Wi-Fi with a **[MiniSpeedCam](https://github.com/Valar-Systems/MiniSpeedCam)** on your network to show the last vehicle's speed, a live proximity gauge, recent passes, and today's count/top/average — with a ping when someone speeds. Keyless. |

All eight run on the same Valar Scopes board; the Edition is chosen by the firmware you flash (and a kit can be re-flashed to a different Edition any time). [More editions are on the way](#more-editions-on-the-way).

---

## 📡 Blipscope (Aviation)

The original Valar Scope: a small flight radar that sits on your desk and shows live aircraft around your location in real time — pulled from public ADS-B data — so you can glance over and see what's in the sky above you, where it came from, and where it's headed.

- **Live radar view** — aircraft plotted around your location, with fading trails, type-aware heading markers (helicopters, gliders, and heavies each draw differently), and altitude-based colour coding.
- **Tap to inspect** — touch an aircraft to open a detail card with callsign, type, operator, registration, route, altitude, speed, and a photo. Pin one to keep tracking it.
- **List & stats screens** — swipe between the radar, a list of everything in range, and at-a-glance statistics.
- **Spotting logbook** — an optional persistent "lifelist" that tallies every unique aircraft type, airline, and country you've ever seen overhead, shown on the Stats screen, with a gold "NEW" flag on first sightings.
- **Tail-number watchlist** — get a phone notification (via [ntfy](https://ntfy.sh)) whenever a specific aircraft flies over.
- **Emergency squawk alerts** — highlights aircraft broadcasting 7500/7600/7700.
- **"Look up!" overhead alert** — flashes a cyan ring when an aircraft passes within a set distance of your location, so you can glance up and spot it for real, with an optional phone alert.
- **Special-aircraft detection** — flags military (orange "MIL", from the ICAO address), helicopters (violet "HELI", from the ADS-B category), and distinctive callsigns such as rescue/police/test flights (blue "SPC") — all worked out offline with no account or lookup needed, plus an optional phone alert when a military aircraft flies over.
- **NTP clock & auto-dim** — keeps accurate time and dims itself at night based on your local sunrise/sunset.
- **Home Assistant / MQTT** — optionally publishes a live summary (aircraft count, nearest flight, and overhead/military flags) to your MQTT broker, with auto-discovery so Home Assistant creates the sensors for you.
- **Over-the-air updates** — pulls new firmware automatically from GitHub Releases, so it stays current without plugging in.
- **Configurable range & display** — set your centre point and scan radius in km or miles, and toggle the on-screen elements you want.

The [Setup & Usage](#setup--usage) section below — including the OpenSky and "run your own receiver" guides — covers Blipscope in full.

## 📟 Missileer (EAM Monitor)

Flash the Missileer firmware and the same device becomes a desk readout for the U.S. Air Force [High Frequency Global Communications System (HFGCS)](https://en.wikipedia.org/wiki/High_Frequency_Global_Communications_System) and the [Emergency Action Messages](https://en.wikipedia.org/wiki/Emergency_Action_Message) and **Skyking** broadcasts that move across it. (STRATCOM — [U.S. Strategic Command](https://en.wikipedia.org/wiki/United_States_Strategic_Command) — is the authority these messages are issued under.) Instead of plotting aircraft, the round screen becomes a command-console:

- a scrolling **ticker** of the latest EAM, broken into its phonetic groups;
- an activity **tempo** gauge — how busy the net is today versus normal;
- recent **Skyking codewords**, with the ones new to your device flagged;
- an **airborne-command-post watch** (is an E-4B "Nightwatch" / E-6B Mercury up right now?);
- **HF propagation** — the best frequency to listen on, with solar flux and K-index;
- the next **ICBM-test ("Glory Trip") window**, with a live countdown;
- and an idle **Zulu (UTC) clock** drawn as real seven-segment digits.

It reuses the same Wi-Fi setup, web config, ntfy alerts, and over-the-air updates as the radar, and runs on its own firmware update channel so the two never cross.

### 📖 [Full guide → Missileer (EAM Monitor) on the Wiki](https://github.com/Valar-Systems/valar-scopes/wiki/Missileer)

## 🛰️ Orbitscope (Space)

Flash the Orbitscope firmware and the device becomes a small mission console for the sky above you — talking **directly to free, public space APIs** with no backend and no API key baked in:

- an **ISS tracker** — a north-polar globe with the station plotted live, showing whether it's sunlit or in Earth's shadow, plus its altitude and speed;
- a **launch T-minus** screen — the next rocket launch with a big live countdown, provider, vehicle, mission, and pad;
- a **geomagnetic Kp gauge** — a 270° aurora dial (QUIET → G1–G5) with a recent-trend sparkline, so a glance tells you if tonight is worth looking up.

It pings your phone when a launch is imminent (T-10 / T-1) or the aurora is stirring (high Kp). Beyond the three above it has grown a whole console of screens — a Deep Space Network board, deep-space-probe distances, solar-flare activity, ISS visible-pass predictions, a star map, asteroid flybys, Moon phase, and eclipse countdowns. Next on the roadmap: a **Skywatch** sky-dome plotting *every* satellite passing overhead — bright passes and Starlink trains, not just the ISS — computed on-device from public orbital data. Same shared Wi-Fi setup, web config, alerts, and OTA as the other editions.

### 📖 [Full guide → Orbitscope (Space) on the Wiki](https://github.com/Valar-Systems/valar-scopes/wiki/Orbitscope)

## 🌐 Quakescope (Seismic)

Flash the Quakescope firmware and the same device becomes a live **earthquake radar** — built on the same polar view as Blipscope (the Aviation edition), but plotting quakes instead of aircraft, straight from the **keyless [USGS](https://earthquake.usgs.gov) feed** (no account, no API key):

- **Live quake radar** — recent earthquakes plotted by bearing and distance around your location, sized and coloured by magnitude, with static range rings instead of a sweep.
- **Tap to inspect** — touch a quake to open a detail card with magnitude, depth, place name, and how long ago it struck.
- **List & stats screens** — swipe between the radar, a list of recent quakes, and at-a-glance statistics (largest today, counts by magnitude).
- **Two queries at once** — a worldwide "recent significant" view and a radius-bounded "near me" view, so distant big ones and small local ones both show up.
- **Phone alerts** — get an [ntfy](https://ntfy.sh) notification for a big quake anywhere, a quake near you, or any event carrying a **tsunami** advisory. Seeded at boot so the backlog never pings you.

It reuses the same Wi-Fi setup, web config, alerts, and over-the-air updates as the radar, on its own firmware update channel.

## 🐦 Quillscope (Birding)

Flash the Quillscope firmware and the device becomes a desk window onto the birds being reported near you — live from the **[Cornell Lab eBird API](https://ebird.org)**. It's a **hybrid** of the two interface styles: a tap-to-inspect radar *and* a set of rotating data screens.

- **Sightings radar** — recent reports plotted around your location, with **notable** birds ringed in gold; tap any blip for a detail card (species, count, location, how long ago).
- **Rotating screens** — a **notable ticker** of recent rarities, a **day-list** species count for your area, your **nearest hotspot**, and a **target species** watchlist — auto-rotating, skipping any feed with no data, and swipeable by hand.
- **Phone alerts** — an [ntfy](https://ntfy.sh) ping the moment a **notable** bird is reported nearby, or when one of your **target species** turns up. Seeded at boot so only fresh sightings notify you.
- **Bring your own key** — eBird's API is free; you enter your own token on the config page (it's never baked into the firmware, and it's masked once saved). Nothing is polled until a token and a location are set.

Same shared Wi-Fi setup, web config, alerts, and OTA as the other editions, on its own update channel.

## 🎣 Reelscope (Fishing)

Flash the Reelscope firmware and the device becomes a fishing-conditions console for your water — covering **both freshwater and saltwater**, all from **free, keyless** public feeds. Pick your water type and stations in the web config; every dial can be toggled on or off.

- **Freshwater (USGS)** — river gauge height, streamflow (CFS) with a rising/steady/falling trend, water temperature, and turbidity where the site reports it — from the keyless [USGS Water Services](https://waterservices.usgs.gov) feed, keyed by your gauge's site number.
- **Saltwater (NOAA)** — tide state with a countdown to the next high/low, water temperature, and offshore wave height / swell period — from [NOAA CO-OPS](https://tidesandcurrents.noaa.gov) tide stations and [NDBC](https://www.ndbc.noaa.gov) buoys.
- **Solunar bite windows** — the day's **major/minor feeding periods** on a 24-hour band, computed **on-device** from the sun and moon for your location (no network) — plus barometric pressure + trend, moon phase, and basic weather.
- **Hybrid interface** — the enabled dials auto-rotate (skipping any with no data) and are swipeable; **tap any dial to inspect it** in a detail card.
- **Phone alerts** — an [ntfy](https://ntfy.sh) ping when a **bite window opens**, when your **river crosses a flow threshold**, or when the **water temperature enters your active-feeding band**. Edge-seeded at boot so the backlog never pings you.

No API keys, no account, no baked-in backend. Same shared Wi-Fi setup, web config, alerts, and OTA as the other editions, on its own update channel.

## 🤖 Claudescope (Claude usage)

Flash the Claudescope firmware and the device becomes a desk gauge for your live **Claude usage limits** — so you can glance over and see how much of your **session** and **weekly** allowance is left before you hit a wall.

- **Two ring gauges** — your current **session** and **weekly** usage as circular meters, each with a **countdown to when it resets** (and the reset time in your local zone).
- **At-a-glance rotation** — the session and weekly screens auto-rotate and are swipeable; **tap either** to open a detail card, with an idle clock for when you're not looking.
- **Phone alerts** — an [ntfy](https://ntfy.sh) ping when your **session** or **weekly** usage crosses a threshold you set (default 80%), so you get a heads-up before you run out. Edge-seeded at boot, so it only warns on fresh crossings.
- **Your login stays yours** — the device never talks to Claude directly and holds no credentials. A small **helper ("sidecar")** on your own network keeps your Claude login and hands the device only a pre-digested usage summary; nothing is baked in, and nothing is polled until you point it at your sidecar.

Same shared Wi-Fi setup, web config, alerts, and OTA as the other editions, on its own update channel.

### 📖 [Full guide → Claudescope (incl. sidecar setup) on the Wiki](https://github.com/Valar-Systems/valar-scopes/wiki/Claudescope)

## 🚗 Speedscope (Speed radar)

Flash the Speedscope firmware and the device becomes a **desk speed-radar** for your street. It pairs over your Wi-Fi with a **[MiniSpeedCam](https://github.com/Valar-Systems/MiniSpeedCam)** — a small radar speed camera — on the same network and turns its readings into glanceable dials.

- **Last pass** — the most recent vehicle's speed as a big number, flagged over or under the posted limit you set.
- **Live proximity** — a real-time arc gauge of the camera's radar signal, so you can watch a vehicle approach.
- **Recent passes & today's stats** — a list of the latest passes, plus today's count, top speed, average, and share over the limit — all tallied on-device.
- **Camera health** — the MiniSpeedCam's signal, IP, uptime, memory, last upload, and firmware, so you know it's alive and feeding.
- **Phone alerts** — an [ntfy](https://ntfy.sh) ping when someone speeds past a threshold you set, when a new fastest-of-the-day is recorded, or when the camera drops offline. Edge-seeded at boot so the backlog never pings you.
- **Keyless & local** — it talks only to your MiniSpeedCam over the LAN (found by its name, or a fixed IP); no account, no API key, no cloud. Nothing is polled until the camera is reachable.

Speedscope needs a **[MiniSpeedCam](https://github.com/Valar-Systems/MiniSpeedCam)** running the companion firmware (its `/api/events` endpoint). Same shared Wi-Fi setup, web config, alerts, and OTA as the other editions, on its own update channel.

### 📖 [Full guide → Speedscope on the Wiki](https://github.com/Valar-Systems/valar-scopes/wiki/Speedscope)

## More editions on the way

Every Edition is the same recipe: pick a **free public data feed**, draw a few glanceable screens, and wire up phone alerts — the Wi-Fi setup, web config, OTA, and ntfy come for free from the shared platform. That makes new Editions cheap to add, and there's a long list of streams that would look great on a round desk display. Some we're considering:

**Things you plot around you** *(reusing Blipscope's polar radar view):*

- 🔥 **Wildfire Edition** — active fire detections radiating around you from NASA's FIRMS satellites, with an "it's getting closer" proximity alert — for fire-season desks.
- 🚢 **Maritime Edition** — ship traffic (AIS) around a harbour or coastline, the radar's natural sibling for the coast.

**Things you read as a dial or ticker** *(reusing the Orbitscope/Missileer rotating screens):*

- ⛅ **Weather Edition** — local conditions, a "next rain" countdown dial, and a 36-hour forecast ribbon around the bezel, from the keyless Open-Meteo feed.
- 🌫️ **Air Quality Edition** — a glanceable AQI / UV / pollen dial, pinging you when the air outside turns unhealthy.
- ₿ **Mempool Edition** — live Bitcoin fees, block height, and network hashrate on a dial, from the keyless mempool.space API.
- 🚆 **Transit Edition** — a "leave now" countdown to the next bus or train from your stop.
- 📻 **Ham Radio Edition** — HF band conditions, solar flux, and live DX spots for radio amateurs, reusing Missileer's propagation feed.

Have an Edition you'd love to see — or a free data feed that belongs on a round screen? [Open an issue](https://github.com/Valar-Systems/valar-scopes/issues) and tell us.

---

## Get a kit

The easiest way to build a Valar Scope is to grab a kit. It includes the display module, the redesigned enclosure parts, and everything else you need in one box — no hunting around marketplaces for the right components, and the hardware is guaranteed to match the firmware and the enclosure. The hardware is the same across editions; you choose which one to run by the firmware you flash, and you can re-flash to a different edition any time.

### 👉 [Order a Valar Scopes kit from Valar Systems](https://valarsystems.com/products/blipscope)

Buying a kit is also the best way to support continued development of the project. Thank you 🙏

## Assembly

The Valar Scopes enclosure has been completely redesigned, so the build steps live in the project wiki rather than here in the README:

### 📖 [Assembly guide → Valar Scopes Wiki](https://github.com/Valar-Systems/valar-scopes/wiki)

The wiki walks through the full build with photos. We recommend skimming the [Setup & Usage](#setup--usage) section below before you start, so you can test the hardware before everything is closed up.

## Firmware

Kits ship with firmware already flashed, and the device keeps itself up to date [over the air](#-blipscope-aviation). For most people there's nothing to install.

If you want to build from source or hack on it yourself, the firmware is here in this repo:

1. Install [VS Code](https://code.visualstudio.com/) with the [PlatformIO IDE extension](https://marketplace.visualstudio.com/items?itemName=platformio.platformio-ide).
2. Restart VS Code and open this repository folder — PlatformIO pulls in the dependencies automatically.
3. Plug the board in via USB-C and hit the upload button (→) in the bottom status bar.

If the board doesn't reboot into the new firmware automatically, hold the **BOOT** button, press **RESET** once, then release **BOOT**. If an upload fails, double-check the board selected in the status bar, try a different USB port, and make sure your cable supports data (some USB-C cables are charge-only). More on PlatformIO [here](https://docs.platformio.org/en/latest/).

### Build variants

Each Edition is a separate compile-time build from this one repo, one PlatformIO env each (see [platformio.ini](platformio.ini)). Pick the env for the edition and board you want:

```sh
pio run -e blipscope-s3-146    -t upload   # 📡 Blipscope   — Aviation, S3 1.46" AMOLED (default)
pio run -e blipscope-pro-s3-21 -t upload   # 📡 Blipscope   — Aviation, S3 2.1" RGB panel
pio run -e missileer-s3-146    -t upload   # 📟 Missileer   — EAM/HFGCS monitor, S3 1.46" AMOLED
pio run -e orbitscope-s3-146   -t upload   # 🛰️ Orbitscope  — Space, S3 1.46" AMOLED
pio run -e quakescope-s3-146   -t upload   # 🌐 Quakescope  — USGS quake radar, S3 1.46" AMOLED
pio run -e quillscope-s3-146   -t upload   # 🐦 Quillscope  — eBird sightings, S3 1.46" AMOLED
pio run -e reelscope-s3-146    -t upload   # 🎣 Reelscope   — Fishing (fresh + salt conditions), S3 1.46" AMOLED
pio run -e claudescope-s3-146  -t upload   # 🤖 Claudescope — Claude usage gauge, S3 1.46" AMOLED
pio run -e speedscope-s3-146   -t upload   # 🚗 Speedscope  — MiniSpeedCam speed radar, S3 1.46" AMOLED
```

Each env is named for its product; the `FEATURE_*` build flags (and the OTA channels they drive) keep their original names:

- **Blipscope** = *(default, no feature flag)* = `blipscope-s3-146` / `blipscope-pro-s3-21`
- **Missileer** = `FEATURE_EAM` = `missileer-s3-146`
- **Orbitscope** = `FEATURE_SPACE` = `orbitscope-s3-146`
- **Quakescope** = `FEATURE_SEISMIC` = `quakescope-s3-146`
- **Quillscope** = `FEATURE_BIRDING` = `quillscope-s3-146`
- **Reelscope** = `FEATURE_FISHING` = `reelscope-s3-146`
- **Claudescope** = `FEATURE_CLAUDESCOPE` = `claudescope-s3-146`
- **Speedscope** = `FEATURE_SPEED` = `speedscope-s3-146`

Each non-default edition reuses the same boards, Wi-Fi setup, web config, and OTA, but compiles a different app and ships on its own OTA channel (`firmware-<edition>-<slug>.bin`), so a device only ever flashes the edition it was built for. Developer notes — including how to add a new edition or SKU — are in [CLAUDE.md](CLAUDE.md) and [RELEASING.md](RELEASING.md).

## Setup & Usage

The first-boot Wi-Fi setup and the web config page work the same on every edition. The OpenSky and "run your own receiver" sections are specific to **Blipscope** (the Aviation edition); the other editions have their own settings (Quakescope and Quillscope, like the radar, take a **Location** for their range, Quillscope takes your free eBird API token, and Reelscope takes your water type plus its USGS gauge / NOAA station).

### First boot

On first boot, your Valar Scope broadcasts its own WiFi hotspot. Each device has a unique name like `Blipscope-A1B2C3` — the exact name is shown on the screen during setup. Connect to that hotspot from your phone or laptop and a configuration page appears automatically (open a browser if it doesn't). Enter your WiFi credentials and hit save; the device restarts and joins your network.

If the hotspot doesn't appear straight away, give it a moment. If it still hasn't shown up after 30 seconds, leave the WiFi settings on your device and go back in to force a refresh.

### Configuration

Once it's on your network, the config page is reachable from any device on the same network at the address shown on screen — `http://<device-name>.local` (for example `http://blipscope-a1b2c3.local`).

On **Blipscope** (the Aviation edition) you can set:

- **Location** (latitude and longitude) — the centre point of your radar.
- **Radar radius** — how far the scan extends, in km or miles (capped at ~222 km / 138 mi to stay within data rate limits).
- **Data source** — pull flight data from the OpenSky Network (the cloud default) or from your own ADS-B receiver on the local network (see below).
- **Display options** — toggle the on-screen elements and aircraft info fields.
- **Watchlist & alerts** — tail numbers to watch and the ntfy topic to notify, plus toggles to highlight military aircraft, helicopters, and special flights, and to alert when a military aircraft flies over.
- **OpenSky credentials** — your client ID and secret (optional, but recommended).
- **Home Assistant / MQTT** — broker address and port, optional username/password, base topic, and an auto-discovery toggle. When enabled, Blipscope publishes a retained `<base>/summary` (and, with discovery on, the Home Assistant config so an "Aircraft in range" count, a "Nearest aircraft" sensor, and "Aircraft overhead" / "Military aircraft in range" binary sensors appear automatically).

The config page is available any time the device is on WiFi, so you can tweak settings whenever you like.

### A note on OpenSky

Blipscope uses [OpenSky Network's](https://opensky-network.org) free API for flight data. It works without an account, but making one (it's free) raises your daily request limit from 400 to 4000 — which lets Blipscope poll roughly every 22 seconds instead of every ~3.5 minutes, so the live view is far more accurate.

OpenSky moved to OAuth2 credentials in 2026, so you need a **client ID** and **client secret** (not your account username/password). To get them:

1. Create a free account or log in at [opensky-network.org](https://opensky-network.org).
2. Open your **Account** page (top-right username menu → Account).
3. Create a new **API client**.
4. Note the two values:
   - **Client ID** — shown on the account page.
   - **Client Secret** — *not* shown on the page. A `credentials.json` file is downloaded to your computer containing both the client ID and secret. Open it in any text editor to copy them out.
5. Enter both into the **OpenSkyAPI Client ID** and **Client Secret** fields on Blipscope's config page.

The secret only ever exists in that downloaded file — if you lose it, use **Reset Credential** on your OpenSky account page to generate a new one (this invalidates the old secret). Keep `credentials.json` private; treat it like a password.

### Using your own ADS-B receiver

If you run your own ADS-B receiver — a Raspberry Pi with [dump1090-fa](https://github.com/flightaware/dump1090), [readsb](https://github.com/wiedehopf/readsb), [PiAware](https://www.flightaware.com/adsb/piaware/), [tar1090](https://github.com/wiedehopf/tar1090), or an ADS-B Exchange feeder image — Blipscope can read directly from it instead of OpenSky. Local data has **no rate limits** and refreshes about once a second, so the radar is smoother and more accurate, and works even if OpenSky is down.

#### Don't have a receiver yet?

A receiver is three things: an **SDR USB dongle**, a **1090 MHz antenna**, and **decoder software** on a Raspberry Pi (any Pi 2 or newer — even a Pi Zero 2 W — is plenty; the decoder is light).

- **Recommended starter:** a [Nooelec NESDR SMArt v5](https://www.nooelec.com/store/nesdr-smart-sdr.html) dongle plus a [Nooelec 1090 MHz ADS-B antenna](https://www.nooelec.com/store/sdr/sdr-addons/1090mhz-ads-b-antenna-5dbi-sma.html). Both ends are SMA, so they screw straight together with no adapter, and the pair runs well under the price of an all-in-one kit. In an RF-noisy spot you can add a [1090 MHz band-pass filter](https://www.nooelec.com/store/sdr/sdr-addons/) later, but try without one first.
- **If you can find one — better filtering:** a [FlightAware Pro Stick Plus](https://www.flightaware.com/adsb/prostick/) has a 1090 MHz filter + amp built in, so it shrugs off nearby noise. Pair it with the same SMA antenna above (no adapter needed). It's frequently out of stock, so don't wait on it if the Nooelec combo is available.

> Avoid the **RTL-SDR Blog V4** kit — its tuner chip was discontinued, so remaining stock sells at inflated prices. The **RTL-SDR Blog V3** (still in production) is a fine dongle if you already have one, but you'd still need to add a 1090 MHz antenna.

> The biggest factor for range is **antenna height and sky view**, not the dongle — a cheap antenna in an attic or upstairs window beats an expensive one on a desk.

For software, the quickest path is to flash the **[PiAware](https://www.flightaware.com/adsb/piaware/) SD-card image**: it bundles `dump1090-fa`, which serves exactly the feed Blipscope reads, and it earns you a free FlightAware account for feeding. Once it's running, check the receiver's map in a browser at `http://<pi-ip>:8080/` to confirm it's seeing aircraft.

#### Pointing Blipscope at it

On the config page, set **Data source** to *My own ADS-B receiver* and enter your receiver's address in **Receiver URL**. You can type just the device's IP (for example `192.168.1.50`) and Blipscope will assume the conventional `/data/aircraft.json` path, or paste the full URL if your setup serves it elsewhere (e.g. `http://192.168.1.50/tar1090/data/aircraft.json`). The receiver must be reachable on the same network as Blipscope.

PiAware / dump1090-fa serve the data on **port 8080**, so enter `http://<pi-ip>:8080` for those — Blipscope appends `/data/aircraft.json` to give `http://<pi-ip>:8080/data/aircraft.json`. If in doubt, open the receiver's map in a browser and use whatever base address it serves from, with `/data/aircraft.json` on the end.

> Altitudes and speeds in the receiver's feed are reported in feet and knots; Blipscope converts them so the display matches the OpenSky readings (metres and m/s). Aircraft outside your configured radius are filtered out the same way OpenSky bounds its results.

That's it — once configured, you'll have a live view of everything flying over your location. Enjoy ✈️

## Thanks

Valar Scopes is built on the wonderful **[Micro Radar](https://github.com/AnthonySturdy/micro-radar)** project by **[Anthony Sturdy](https://github.com/AnthonySturdy)**. Anthony designed and open-sourced the original device — the concept, the enclosure, and the firmware this project grew from. None of this would exist without his work, and we're hugely grateful he shared it with the world. Go give the [original repo](https://github.com/AnthonySturdy/micro-radar) a star. 🌟

The original Micro Radar was itself inspired by [therealhacksaw](https://www.instagram.com/therealhacksaw/)'s desk radar.

Valar Scopes is maintained by [Valar Systems](https://valarsystems.com).

## License

Valar Scopes is released under the **Open Community License (OCL v1)** — see [LICENSE](LICENSE) for the full text. In short: as a non-commercial user you're free to use, copy, modify, and hack it however you like, and to share derivatives under the same share-alike terms. Commercial replication requires a separate business or repair license. The aim is to keep the project open and repairable while preventing straight commercial cloning. Learn more about the OCL [here](https://github.com/OpenCommunityLicence/OpenCommunityLicence).
