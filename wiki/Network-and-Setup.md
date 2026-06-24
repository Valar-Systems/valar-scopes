# Network and Setup

Everything about getting Blipscope onto your WiFi, naming, and the OpenSky data account.

## First boot

On first boot Blipscope broadcasts its own WiFi hotspot. Each device's hotspot has a unique name like `Blipscope-A1B2C3` (derived from the board's MAC address) — the exact name is shown right on the screen during setup, so you always know which one to join.

1. On your phone or laptop, connect to the `Blipscope-XXXXXX` hotspot shown on screen.
2. A configuration page opens automatically (if it doesn't, open a browser).
3. Enter your home WiFi credentials and save.
4. Blipscope restarts and joins your network.

> Blipscope's ESP32-C3 is **2.4 GHz only** and cannot see 5 GHz networks. If your router uses one name for both bands, that's fine — it'll use the 2.4 GHz band.

If the hotspot doesn't appear right away, give it a moment; if it's still missing after ~30 seconds, leave and re-enter the WiFi settings on your device to force a refresh.

### Mesh / Nest / eero networks

Mesh systems often reject the first few association attempts while they steer a new client between nodes. Blipscope accounts for this by retrying the connection several times with a timeout, so it keeps trying until one sticks rather than giving up and falling back to setup mode.

## Finding it on your network

Each board registers a unique hostname on your network, so it appears distinctly in your router's device list and is reachable at:

```
http://<device-name>.local
```

for example `http://blipscope-a1b2c3.local` — the same name shown on screen. From there you reach the **[[Configuration Reference|Configuration-Reference]]** page.

## Running several Blipscopes

Because every board derives a unique name from its hardware, you can run multiple Blipscopes on the same network with no extra setup — each gets its own setup hotspot, its own `.local` address, and its own distinct entry in the router's device list.

## Reset WiFi

To move a Blipscope to a different network, use the red **Reset WiFi** button on the config page. It forgets the stored credentials (after a confirmation prompt) and reboots into the setup hotspot, so you can connect it somewhere new.

## OpenSky account (recommended)

Blipscope gets its flight data from the free [OpenSky Network](https://opensky-network.org) API.

- **Without an account** it works fine, but anonymous access is limited to ~400 requests per day.
- **With a free account** that rises to ~4,000 per day, which means much more frequent updates and a noticeably more accurate live view.

To use one: create an account at [opensky-network.org](https://opensky-network.org), find your **client ID** and **client secret** in your account settings, and enter them on the **[[Configuration Reference|Configuration-Reference]]** page. The secret is stored on the device and shown masked afterwards.

## Related

- **[[Configuration Reference]]** — all settings
- **[[Firmware Updates]]** — how the device stays current
