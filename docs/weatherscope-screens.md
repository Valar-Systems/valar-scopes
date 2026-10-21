# Weatherscope: the planned screens

**Status: ruled.** Daniel's rulings of 2026-10-20, including the final rulings the same day, are recorded below. No point is left undecided; one check is pending (see [Open items](#open-items)). This is a docs-only change.

Weatherscope is a planned scope of its own: a desk weather radar on the same 1.28-inch round 240 × 240 screen as Blipscope, with a 60-mile range, US only. It is **not** the weather layer under the planes on the Blipscope; that is the Stage 1 spike in [PR #422](https://github.com/Valar-Systems/valar-scopes/pull/422) (`docs/wx-spike-2026-10.md` on `spike/wx-overlay`).

**The screens** are the six mockups in `C:\Github\_weatherscope-mockups` (outside any repo; exported 2026-10-20; the weather in them is simulated). The map is 60 mi around Bend, Oregon. That folder's README describes each image; this doc records the decisions the device needs before it can show them.

**For the website** (ruled 2026-10-20):
- The website's virtual Weatherscope mirrors **shipped Weatherscope firmware only**, not this doc.
- Whether the preview page's mockups may show elements that no data source backs is **moot**: website PR #9 waits for the re-exported mockups (final ruling, 2026-10-20).

What the mockups and their README already fix: the six screens; radar from the NOAA MRMS mosaic; warnings and forecasts from the National Weather Service (NWS); a 60 mi range; US only.

| # | Mockup | Shows |
|---|---|---|
| 1 | on the desk | the device, showing screen 2 |
| 2 | rain on the way | a line of rain to the west, "RAIN IN 18 MIN", "HEAVY · MOVING E 25 MPH" |
| 3 | severe warning | a Severe Thunderstorm Warning polygon over the town, "UNTIL 4:45 PM", "HAIL 1 IN · WIND 60 MPH", "STORM 15 MI SW · MOVING NE 30 MPH" |
| 4 | past-hour loop | one replay frame ("3:12 PM · 30 MIN AGO") and a timeline from −1 h to NOW, then a dashed "+30" |
| 5 | snow on the way | snow, mix and rain in a legend, "SNOW IN 40 MIN", "29°F · LIGHT TO MODERATE" |
| 6 | dry day | "54°", "CLEAR · WIND W 9 MPH", "NO RAIN WITHIN 60 MI", "NEXT RAIN TUE NIGHT" |

## Mockup elements not backed by a data source

Each of these appears in a mockup, and neither MRMS nor the NWS supplies it.

| Element | Where | Why there is no source | Ruling, 2026-10-20 |
|---|---|---|---|
| Arrival times: "RAIN IN 18 MIN", "SNOW IN 40 MIN" | 1, 2, 5 | MRMS publishes observed frames only. Its one extrapolation product, the AutoNowcaster Final Forecast (a 60-minute extrapolation of storms of 35 dBZ and up), was withdrawn by the NWS in 2023 ([SCN 23-86](https://www.weather.gov/media/notification/pdf_2023_24/scn23-86_autonowcaster_discontinuation.pdf)). The bucket has no `ANC_FinalForecast` folders in 2026, against 293 for `PrecipFlag` up to 2026-10-20. NWS forecasts are hourly, not minute by minute. | **No countdown in v1.** "Approaching" means precipitation within 15 mi, from MRMS only ([1b](#1-which-screen-the-device-shows)) |
| The dashed "+30" on the replay timeline | 4 | No future radar frames exist in MRMS or the NWS, for the same reason | **Dropped** ([2b](#2-the-past-hour-loop)) |
| "MIX" in the legend | 5 | MRMS `PrecipFlag` has no wintry-mix class | **Snow and rain only** ([§4](#4-mix-in-mockup-5)) |
| "MT BACHELOR" label | 1–5 | Not a Census place: the 2025 Gazetteer Places file has no record for it | **Left out.** Non-places are not labelled, so MT BACHELOR is dropped ([5c](#5-town-labels); final ruling, changed from a hand-made list) |
| The bottom lines of the warning screen, for a Flash Flood Warning | 3 (drawn for a Severe Thunderstorm Warning) | Flash Flood Warnings carry no hail, wind or storm-motion fields. 10 of 10 sampled carried only `flashFloodDetection`, or nothing | **Show the `flashFloodDetection` text** ([3b](#3-which-warnings-use-the-warning-screen)) |

## Mockups that now need re-exporting

Following the rulings above:

| Mockup | Change |
|---|---|
| 1, on the desk | headline without minutes (it shows screen 2's "RAIN IN 18 MIN"); no MT BACHELOR |
| 2, rain on the way | headline without minutes; no MT BACHELOR |
| 3, severe warning | no MT BACHELOR |
| 4, past-hour loop | no dashed "+30" on the timeline; no MT BACHELOR |
| 5, snow on the way | headline without minutes; no MIX in the legend; the pink mix area shown as snow or rain; no MT BACHELOR |

Unchanged: 6 (dry day), which draws the town dots without labels.

**States with no mockup yet** (new exports, not re-exports): precipitation at the location now (1c), a Flash Flood Warning (3b), and a Snow Squall Warning (3a).

**Also a new export (ruled 2026-10-20):** screen 7, **the legend** ([§7](#7-the-legend-screen-screen-7)). There's no mockup of it yet.

## 1. Which screen the device shows

The mockups show five screens and no rule for choosing between them.

| Point | Options | Ruling, 2026-10-20 |
|---|---|---|
| 1a. The order | (a) a fixed priority: an active warning, then rain or snow approaching or present, then the dry screen; (b) the same priority, with a timed rotation between screens of equal rank | **(a)** |
| 1b. What "approaching" means | (a) arrival within N minutes, which needs an arrival-time source; (b) precipitation within a distance ring, from MRMS frames alone, with no countdown | **(b)**, from MRMS only; no countdown in v1. **Radius: 15 mi** (final ruling) |
| 1c. Precipitation at the location **now** (no mockup shows it) | (a) the rain or snow screen with a "now" headline; (b) a new mockup; (c) the plain radar | **(a)** |
| 1d. When a warning counts as "covering the town" | (a) its polygon contains the device's location; (b) its polygon falls anywhere inside the 60 mi range | **(a)** |

**Changed later on 2026-10-20 (Daniel, on Weatherscope Stage 2's radar product check): where "now" comes from.**
- **"Precipitation here now" (1c) comes from the NWS latest observation at the nearest station, using its present weather. It no longer comes from the radar's centre pixel.**
- **Radar still drives the 15 mi ring (1b) and the drawing.**
- **Why:** in the product check, SeamlessHSR's centre cell read 12.4 dBZ at an airport whose observation reported light rain (`docs/weatherscope-stage2.md`, F3). Most snowing hours at KMSP also read below 15 dBZ at the airport's cell (change 1). A single radar cell under-samples what a station reports.
- **What 1c's ruling (a) still means:** the rain or snow screen with a "now" headline. Only the source of "now" changed.
- **Not decided here:** how observations reach the device. That is part of the shared data path held with the Blipscope weather layer, as with NWS alerts through the proxy.

## 2. The past-hour loop

Screen 4 shows one frame and a timeline from −1 h to NOW. MRMS composite frames arrive about every 2 minutes (median 120 s over 67 files, wx spike A0 in [PR #422](https://github.com/Valar-Systems/valar-scopes/pull/422)), so an hour holds about 30 frames. On Blipscope, a swipe changes screens and a tap opens a detail card.

| Point | Options | Ruling, 2026-10-20 |
|---|---|---|
| 2a. How the loop opens | (a) a tap; (b) a swipe to it, like any other screen; (c) it plays by itself | **(a) a tap** |
| 2b. The dashed "+30" | (a) drop it; (b) show it inactive; (c) fill it from a forecast source | **(a) dropped** |
| 2c. How many frames | (a) every frame, about 30; (b) every other frame, about 15 | **(b) about 15** |
| 2d. How it ends | (a) after one pass; (b) a tap; (c) a timeout | **(a) after one pass** |

## 3. Which warnings use the warning screen

What the NWS alerts feed (`api.weather.gov/alerts`) carries, sampled on 2026-10-20 (up to 10 recent alerts of each type):

| Warning | Polygon | Fields for the bottom lines |
|---|---|---|
| Severe Thunderstorm | yes | new alerts: `maxHailSize`, `maxWindGust`, `eventMotionDescription`. Cancellations and some updates: motion only |
| Tornado | yes | `maxHailSize`, `eventMotionDescription`, `tornadoDetection`. `maxWindGust` was absent in 10 of 10 |
| Flash Flood | yes | `flashFloodDetection` only, or nothing, in 10 of 10 |
| Snow Squall | yes ([NWS Geolocation](https://www.weather.gov/media/documentation/docs/NWS_Geolocation.pdf)) | **not sampled**: there was no recent Snow Squall Warning on 2026-10-20 |
| Winter Storm (for comparison) | **no**: it is issued by zone | none |

**Storm motion** comes as `<time>...storm...211DEG...37KT...<storm position>`: a direction in degrees (the direction the storm comes from, as in NWS warning text), a speed in knots and the storm's position. Screen 3's "STORM 15 MI SW · MOVING NE 30 MPH" is worked out from it: distance and bearing from the device, direction of travel, and knots converted to mph.

| Point | Options | Ruling, 2026-10-20 |
|---|---|---|
| 3a. Which warnings | (a) Tornado, Severe Thunderstorm and Flash Flood; (b) also Snow Squall; (c) also Extreme Wind. Watches and advisories stay out under every option | **(a) plus Snow Squall:** Tornado, Severe Thunderstorm, Flash Flood and Snow Squall Warnings. Watches and advisories stay out |
| 3b. The bottom lines for a Flash Flood Warning | (a) leave them blank; (b) show the `flashFloodDetection` text; (c) a new mockup | **(b)** |
| 3c. When the warning screen ends | (a) at the alert's `expires` time or on a Cancel, whichever comes first; (b) at `expires` only | **(a)** |
| 3d. Two warnings at once | (a) the most severe first: Tornado, then Severe Thunderstorm, then Flash Flood; (b) the newest first | **(a)**, with Snow Squall second (final ruling): **Tornado, Snow Squall, Severe Thunderstorm, Flash Flood** |
| 3e. The bottom lines for a Snow Squall Warning (no live sample seen yet) | (a) storm motion if present, otherwise blank | **(a)** (final ruling). **Confirm against the first live sample** |

## 4. "MIX" in mockup 5

NSSL's official `PrecipFlag` table ([`UserTable_MRMS_PrecipFlags.csv`](https://github.com/NOAA-National-Severe-Storms-Laboratory/mrms-support/tree/main/GRIB2_TABLES)) has these classes:

| Code | Meaning |
|---|---|
| −3 | no coverage |
| 0 | no precipitation |
| 1 | warm stratiform rain |
| 3 | snow |
| 6 | convective rain |
| 7 | rain mixed with hail |
| 10 | cold stratiform rain |
| 91 | tropical/stratiform rain mix |
| 96 | tropical/convective rain mix |

None of them is freezing rain, sleet or a wintry mix. The other options were our own "mix" band from MRMS's model wet-bulb temperature, or the NWS gridded forecast's `freezing_rain` and `sleet` (a forecast, not an observation).

**Ruling, 2026-10-20: (a) snow and rain only.** MIX is dropped from the legend.
- Snow is code 3.
- Every other precipitation code (1, 6, 7, 10, 91, 96) counts as rain. That includes code 7, rain mixed with hail.

## 5. Town labels

What the 2025 Census Gazetteer Places file holds ([record layout](https://www.census.gov/programs-surveys/geography/technical-documentation/records-layout/gaz-record-layouts.2025.html), [file](https://www2.census.gov/geo/docs/maps-data/data/gazetteer/2025_Gazetteer/2025_Gaz_place_national.zip)):
- **What it is:** 32,350 records, from the US Census Bureau (a US government work).
- **No population column**, so places can't be ranked by size.
- **The mockups' six towns** are all present, written "Bend city", "Madras city", "Sisters city", "Redmond city", "Prineville city" and "La Pine city" (`LSAD` 25).
- **Mt Bachelor is not in the file**, because it isn't a Census place.

| Point | Options | Ruling, 2026-10-20 |
|---|---|---|
| 5a. The source | (a) the Gazetteer, on both device and site | **(a)** |
| 5b. Which places get a label | (a) incorporated cities only (`LSAD` 25), skipping census-designated places; (b) by land area; (c) add a Census population table; (d) the nearest N to the centre | **(a)** |
| 5c. Mt Bachelor, and other non-places such as peaks and ski areas | (a) leave them out; (b) add USGS GNIS names; (c) a short hand-made list | **(a)** (final ruling; earlier the same day (c), with no GNIS until its licence was confirmed). Weatherscope ships nationwide, so a hand-made list can't scale. **MT BACHELOR is dropped**, and every mockup showing it is re-exported |
| 5d. Label text | (a) drop the `LSAD` word, so "Bend city" becomes "BEND" | **(a)** |

## 6. Units

The inputs come in other units, and the device converts them:
- NWS storm motion: knots, and the direction the storm comes from.
- NWS `maxWindGust`: mph ([CAP documentation](https://vlab.noaa.gov/web/nws-common-alerting-protocol/cap-documentation)).
- NWS `maxHailSize`: inches.

| Point | Options | Ruling, 2026-10-20 |
|---|---|---|
| 6a. Units | (a) imperial only; (b) imperial by default, with a metric setting | **(a) imperial only:** °F, mph, mi |

## 7. The legend screen (screen 7)

**Ruled by Daniel, 2026-10-20: add a legend screen.** Recorded as received:
- **It is screen 7**, after the others in the swipe order. **A swipe reaches it; a tap returns to the map.**
- **Plain-word labels, in groups:**
  - Light rain (blue, green);
  - Moderate (yellow, orange);
  - Heavy (red);
  - Very heavy / possible hail (magenta, violet, white);
  - snow, Light → Heavy, on the snow ramp;
  - No radar coverage (grey);
  - No data (hatched).

  dBZ in small text is optional.
- **It uses the final palette exactly:** the same table and the same sha256. That is the palette in `docs/weatherscope-stage2.md`, with rain from MetPy's `NWSReflectivity` v1.6.3, pinned at sha256 `f73f0d6264d4…`.
- **Open question for Daniel, later:** show the legend once at first boot? **Noted, not built.**

**Which codes each group covers.** This is read from the colours the ruling names; the wire codes are WXC1's 5 dBZ bands:

| label | colours | rain codes | dBZ |
|---|---|---|---|
| Light rain | blue, green | 1 (blue); 2-4 (greens) | 15-35 |
| Moderate | yellow, orange | 5-6 (yellow, and the 40 dBZ gold `(231, 192, 0)`, read as yellow); 7 (orange) | 35-50 |
| Heavy | red | 8-10 | 50-65 |
| Very heavy / possible hail | magenta, violet, white | 11 (magenta), 12 (violet), 13 (white) | ≥ 65 |
| Snow, Light → Heavy | the snow ramp | snow 1-6 step; 6-13 hold | 15 → ≥ 40 |
| No radar coverage | grey | 14 | — |
| No data | hatched | 15 | — |

**One reading to confirm:** the 40 dBZ gold (code 6) is counted as "yellow". It could equally be read as orange, which would move it to the same group anyway (Moderate). **Dry (black, code 0) has no label**, because the ruling lists none.

**The bench version is built after 2.1 closes**, on the board, alongside the KDFW frame. It comes with a host check that **every swatch on the legend equals the palette table**. Its predictions are frozen before its code, as with every step so far.

## Open items

**No point is undecided** (final rulings, 2026-10-20). One check is pending:

- **3e:** confirm a Snow Squall Warning's bottom-line fields against the first live sample. None was available on 2026-10-20.
- **7, first boot:** should the legend show once at first boot? An open question for Daniel (ruled 2026-10-20 to note it and not build it).

## Sources

- NSSL `mrms-support`, [`GRIB2_TABLES/UserTable_MRMS_PrecipFlags.csv`](https://github.com/NOAA-National-Severe-Storms-Laboratory/mrms-support/tree/main/GRIB2_TABLES).
- NWS [SCN 23-86](https://www.weather.gov/media/notification/pdf_2023_24/scn23-86_autonowcaster_discontinuation.pdf): the AutoNowcaster is removed from MRMS.
- The `noaa-mrms-pds` bucket, listed 2026-10-20 ([listing](https://noaa-mrms-pds.s3.amazonaws.com/?list-type=2&prefix=CONUS/&delimiter=/)). Day folders were listed with `start-after=…/20260101`: a plain listing stops at 1,000 names, so it cuts off before the most recent folders. `PrecipFlag` returning 2026 folders is the control that shows the 2026 listing works.
- [api.weather.gov](https://api.weather.gov/) `/alerts?event=…`, sampled 2026-10-20, and its [OpenAPI spec](https://api.weather.gov/openapi.json).
- NWS [CAP documentation](https://vlab.noaa.gov/web/nws-common-alerting-protocol/cap-documentation) and [NWS Geolocation](https://www.weather.gov/media/documentation/docs/NWS_Geolocation.pdf).
- US Census Bureau Gazetteer: [record layouts, 2025](https://www.census.gov/programs-surveys/geography/technical-documentation/records-layout/gaz-record-layouts.2025.html) and the [2025 Places file](https://www2.census.gov/geo/docs/maps-data/data/gazetteer/2025_Gazetteer/2025_Gaz_place_national.zip).
- USGS [GNIS download](https://www.usgs.gov/u.s.-board-on-geographic-names/download-gnis-data).
- MRMS frame cadence: wx spike A0, [PR #422](https://github.com/Valar-Systems/valar-scopes/pull/422).
