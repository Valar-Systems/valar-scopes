# Aircraft Details

Blipscope has a touchscreen, so any aircraft on the radar is just a tap away from its full story.

## Tapping a contact

Tap an aircraft on the **radar** (or a row on the **list** screen) to open its detail card. Blipscope picks the nearest contact to where you tapped, so you don't have to be pixel-perfect.

The detail card has two pages:

- **Photo page** — leads with a photo of the actual aircraft (or aircraft type) when one is available, plus a compact summary.
- **Data page** — the full readout: callsign, ICAO address, type and full model name, operator, registration, origin country, altitude, ground speed, vertical rate, heading, and squawk.

Tap the card to flip from the photo page to the data page; tap again to close it. (If there's no photo, tapping just closes the card.)

## Metadata enrichment

Raw ADS-B only gives you a callsign and an ICAO address. Blipscope enriches each contact in the background by looking it up on [adsbdb.com](https://www.adsbdb.com), filling in:

- **Aircraft type** (e.g. A320, B738) and **full model name**
- **Operator / airline**
- **Registration** (tail number)
- **Route** — origin and destination, where known
- A **photo**, fetched from airport-data.com

Look-ups are cached per aircraft and throttled, so they're easy on both the device and the data sources. Fields stay blank until the look-up completes, and may stay blank for aircraft the databases don't know.

## Pin to track

Found something you want to keep an eye on? From the detail card, **swipe up to pin** ("track") that aircraft. Blipscope returns to the radar and marks your pinned contact with a white reticle and an always-visible callsign, so you can follow it through the traffic. Swipe up again on its card to unpin, or tap an empty area of the radar to clear the pin.

## Related

- **[[Screens and Gestures]]** — the full gesture map
- **[[Radar Display]]** — what the markers mean
- **[[Alerts and Watchlist]]** — get notified about specific aircraft automatically
