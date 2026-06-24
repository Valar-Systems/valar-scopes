# Radar Display

The radar is Blipscope's main screen: a top-down view centred on your location, with every aircraft currently in range plotted around you in real time. Position data is refreshed continuously from the [OpenSky Network](https://opensky-network.org), and between updates each aircraft's position is smoothly interpolated from its last known heading and speed, so contacts glide rather than jump.

Most of the elements below can be toggled individually from the **[[Configuration Reference|Configuration-Reference]]** page.

## Radar sweep

A classic rotating sweep line animates around the dial. It's purely decorative — a nod to the look of a real radar scope — and only ever appears on the radar screen. Turn it off with the **Radar sweep** setting if you prefer a static display.

## Directional aircraft

With **Directional Aircraft** enabled, each contact is drawn as a small triangle pointing in the direction the aircraft is actually travelling (its true track). Disable it to fall back to plain dot markers.

## Flight trails

With **Flight trails** on, Blipscope remembers each aircraft's recent positions and draws a fading tail behind it, so you can see where it's been and the shape of its path — turns, holds, and approaches all become visible at a glance. The trail fades out toward the oldest points and connects cleanly to the live marker.

## Altitude colours

With **Altitude colors** enabled, each aircraft marker is tinted by its barometric altitude, making it easy to tell high-altitude airliners from low traffic at a glance:

| Altitude | Colour |
|---|---|
| Below 1,000 m (~3,300 ft) | 🟢 Green |
| 1,000–3,000 m (~3,300–9,800 ft) | 🟩 Lime |
| 3,000–6,000 m (~9,800–19,700 ft) | 🟡 Yellow |
| 6,000–9,000 m (~19,700–29,500 ft) | 🟦 Cyan |
| Above 9,000 m (~29,500 ft) | ⚪ White |

Red is deliberately never used for altitude — it's reserved for [[emergency squawk alerts|Alerts-and-Watchlist]].

## Highlights

With **Highlights** enabled, Blipscope automatically tags the three most interesting contacts in view with a magenta ring and a label:

- **NEAR** — the closest aircraft to you
- **HIGH** — the highest aircraft in range
- **FAST** — the fastest aircraft in range

These update live as traffic changes, so the standouts are always called out without you having to hunt for them.

## On-screen aircraft info

Independently of the markers, Blipscope can print a block of live text for the aircraft it's tracking (callsign, type, altitude, speed and more). Which fields appear is fully configurable — see **[[Configuration Reference]]** for the complete list of available info fields and their defaults.

## Related

- **[[Aircraft Details]]** — tap a contact for the full detail card
- **[[Screens and Gestures]]** — switch to the list and stats screens
- **[[Alerts and Watchlist]]** — emergency and watchlist highlighting
