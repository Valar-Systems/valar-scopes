# Clock and Brightness

Blipscope keeps accurate time and manages its own screen brightness, including dimming itself at night.

## NTP clock

On every boot Blipscope synchronises its clock from internet time servers (NTP, via `pool.ntp.org`), so the time is always accurate without a battery-backed clock or any manual setting.

The clock runs internally in UTC. To show your local time, set the **Clock UTC offset (hrs)** on the **[[Configuration Reference|Configuration-Reference]]** page. Half-hour zones are supported (e.g. `5.5` for India, `-3.5` for Newfoundland). Blipscope pre-fills a sensible default offset estimated from the longitude you configure, so it's usually close out of the box — adjust it for your exact zone and for daylight-saving time.

## Auto-dim at night

With **Auto-dim at night** enabled, Blipscope automatically dims the display after dark and brightens it again at dawn. Rather than using a fixed clock time, it calculates the real position of the sun for your configured location and the current UTC time using the NOAA solar equations — so it tracks your actual local sunrise and sunset throughout the year. No light sensor required.

## Brightness

The **Brightness** slider sets the display's day-time backlight level (from dim to full). The backlight is driven by PWM, so it's smoothly dimmable. When auto-dim is active, this is the level Blipscope returns to during daylight.

## Related

- **[[Configuration Reference]]** — clock offset, auto-dim, and brightness controls
- **[[Network and Setup]]** — set your location, which auto-dim uses to find the sun
