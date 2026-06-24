# Alerts and Watchlist

Blipscope can call your attention to two kinds of aircraft: ones squawking an emergency, and ones you've specifically asked to watch for.

## Emergency squawk alerts

Aircraft broadcast a four-digit transponder code ("squawk"). Three codes are reserved worldwide for emergencies, and Blipscope flags them automatically — no configuration needed. When a contact in range is squawking one of these, it gets an expanding red "ping" ring and a permanent red label:

| Squawk | Label | Meaning |
|---|---|---|
| **7500** | `HIJACK` | Unlawful interference (hijacking) |
| **7600** | `NORDO` | Radio failure (no radio) |
| **7700** | `EMERG` | General emergency |

Red is reserved exclusively for these alerts — no other marker or altitude colour uses it — so an emergency is unmistakable on the radar.

## Tail-number watchlist

The watchlist lets you tell Blipscope which aircraft you care about. Enter one or more terms (comma-, semicolon-, or newline-separated) in the **Watch** box on the **[[Configuration Reference|Configuration-Reference]]** page. Each term is matched as a prefix against an aircraft's:

- **Callsign** (e.g. `BAW`)
- **Tail number / registration** (e.g. `G-`)
- **ICAO address**
- **Aircraft type** (e.g. `A320`)

So `BAW` matches every British Airways callsign, `G-` matches UK-registered aircraft, and a specific tail number matches just that airframe.

Watchlisted contacts in range get an amber ring and an always-on callsign on the radar, and show in amber on the **[[Screens and Gestures|Screens-and-Gestures]]** list — so they stand out even before you reach for your phone.

> Because the watchlist can match on registration and type, Blipscope automatically enables [[metadata enrichment|Aircraft-Details]] whenever your watchlist isn't empty.

## Phone alerts via ntfy

Pair the watchlist with a free [ntfy.sh](https://ntfy.sh) topic to get a push notification on your phone the moment a watched aircraft flies over — even if you're not looking at the device.

1. Install the **ntfy** app (iOS / Android) or open ntfy.sh in a browser.
2. Subscribe to a topic name of your choice (pick something hard to guess — anyone who knows the topic can read its notifications).
3. Enter that same topic in the **ntfy.sh topic** field on the config page.

When a watchlisted aircraft appears, Blipscope sends a "Blipscope flyover" notification to your topic. Notifications are throttled internally so a single aircraft loitering overhead won't spam you.

> Phone alerts require **both** a non-empty watchlist **and** an ntfy topic. With no topic set, watched aircraft are still highlighted on-screen but no notification is sent.

## Related

- **[[Configuration Reference]]** — where to set the watchlist and topic
- **[[Radar Display]]** — on-screen highlighting
