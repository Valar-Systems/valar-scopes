# Configuration Reference

Every Blipscope setting is changed from a single web page, served by the device itself — no app required.

## Opening the config page

Once Blipscope is on your WiFi (see **[[Network and Setup]]**), open a browser on any device on the same network and go to the address shown on the device's screen:

```
http://<device-name>.local
```

for example `http://blipscope-a1b2c3.local`. Each board has a unique name, so several Blipscopes can share a network without clashing.

Changes are applied **live** — Blipscope re-reads its settings as soon as you hit **Save**, with no reboot. The page is available any time the device is online.

## Settings

### Location & range

| Setting | Description |
|---|---|
| **Latitude / Longitude** | The centre point of your radar. Used for plotting, distance/bearing, and the solar [[auto-dim|Clock-and-Brightness]]. |
| **Radius** | How far the scan reaches, in **km** or **miles** (pick the unit alongside the value). Capped at ~222 km / 138 mi — a 2° scan box — to stay within OpenSky's rate limits. Switching units converts the value automatically. |

### OpenSky API

| Setting | Description |
|---|---|
| **OpenSky API Client ID** | Your OpenSky client ID (optional). |
| **OpenSky API Client Secret** | Your OpenSky client secret (optional). Stored on the device and shown masked when you reload the page. |

An OpenSky account is free and raises your request limit from 400 to 4,000 per day, making the live view far more responsive. See **[[Network and Setup]]** for details.

### Display toggles

| Toggle | Default | Effect |
|---|---|---|
| **Radar sweep** | On | Animated rotating sweep line on the radar |
| **Directional Aircraft** | On | Draw contacts as heading triangles instead of dots |
| **Flight trails** | On | Fading tails showing each aircraft's recent path |
| **Altitude colors** | On | Tint markers by altitude |
| **Highlights** | On | Auto-tag the NEAR / HIGH / FAST contacts |
| **Auto-dim at night** | On | Dim the screen after dark based on local sunset |

See **[[Radar Display]]** for what each of these looks like.

### Brightness & clock

| Setting | Description |
|---|---|
| **Brightness** | Day-time backlight level (10–255). |
| **Clock UTC offset (hrs)** | Your timezone offset from UTC (−12 to +14, half-hours allowed). |

### Aircraft Info fields

A master **Aircraft Info** toggle turns the on-screen text block on or off. Beneath it, each individual field can be enabled separately. Fields that have no data for the current aircraft are simply skipped.

| Field | Default | Source |
|---|---|---|
| Callsign | On | ADS-B |
| Aircraft type | Off | adsbdb |
| Operator | Off | adsbdb |
| Registration | Off | adsbdb |
| ICAO address | Off | ADS-B |
| Origin country | Off | ADS-B |
| Ground speed | On | ADS-B |
| Vertical rate | Off | ADS-B |
| Barometric alt | On | ADS-B |
| Geometric alt | Off | ADS-B |
| Heading | Off | ADS-B |
| Squawk | Off | ADS-B |
| Category | Off | ADS-B |
| Position source | Off | ADS-B |

Enabling any of the adsbdb-sourced fields (type, operator, registration) triggers the background [[metadata look-ups|Aircraft-Details]].

### Watchlist & alerts

| Setting | Description |
|---|---|
| **Watch** | Aircraft to watch — callsigns, tail numbers, ICAO addresses, or types, separated by commas, semicolons, or newlines. Matched as prefixes. |
| **ntfy.sh topic** | The ntfy topic to send phone notifications to when a watched aircraft flies over. |

Full details on the **[[Alerts and Watchlist]]** page.

## Reset WiFi

The red **Reset WiFi** button forgets the saved WiFi credentials and reboots Blipscope into its setup hotspot, so you can move it to a different network. You'll be asked to confirm first. See **[[Network and Setup]]**.

## Related

- **[[Network and Setup]]** — getting online and OpenSky accounts
- **[[Radar Display]]** · **[[Alerts and Watchlist]]** · **[[Clock and Brightness]]**
