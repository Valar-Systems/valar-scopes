# Weatherscope: the planned screens

**Status: open.** Docs only, 2026-10-20. No Weatherscope firmware is written until Daniel rules on the points marked **UNDECIDED** below.

Weatherscope is a planned scope of its own: a desk weather radar on the same 1.28-inch round 240 × 240 screen as Blipscope, with a 60-mile range, US only. It is **not** the weather layer under the planes on the Blipscope; that is the Stage 1 spike in [PR #422](https://github.com/Valar-Systems/valar-scopes/pull/422) (`docs/wx-spike-2026-10.md` on `spike/wx-overlay`).

**The screens** are the six mockups in `C:\Github\_weatherscope-mockups` (outside any repo; exported 2026-10-20; the weather in them is simulated). The map is 60 mi around Bend, Oregon. That folder's README describes each image; this doc records what the device needs decided before it can show them.

**For the website.** The site's virtual Weatherscope copies this doc and shows nothing beyond it. Anything marked UNDECIDED stays off the virtual Weatherscope until it is ruled. The static mockup images on the preview page are marketing and are not governed by this doc (ruled 2026-10-20).

What the mockups and their README already fix: the six screens; radar from the NOAA MRMS mosaic; warnings and forecasts from the National Weather Service (NWS); a 60 mi range; US only.

| # | Mockup | Shows |
|---|---|---|
| 1 | on the desk | the device, showing screen 2 |
| 2 | rain on the way | a line of rain to the west, "RAIN IN 18 MIN", "HEAVY · MOVING E 25 MPH" |
| 3 | severe warning | a Severe Thunderstorm Warning polygon over the town, "UNTIL 4:45 PM", "HAIL 1 IN · WIND 60 MPH", "STORM 15 MI SW · MOVING NE 30 MPH" |
| 4 | past-hour loop | one replay frame ("3:12 PM · 30 MIN AGO") and a timeline from −1 h to NOW, then a dashed "+30" |
| 5 | snow on the way | snow, mix and rain in a legend, "SNOW IN 40 MIN", "29°F · LIGHT TO MODERATE" |
| 6 | dry day | "54°", "CLEAR · WIND W 9 MPH", "NO RAIN WITHIN 60 MI", "NEXT RAIN TUE NIGHT" |

## Mockup elements not yet backed by a data source

Each of these appears in a mockup, and neither MRMS nor the NWS supplies it today. Each is **UNDECIDED** until one of its options is ruled.

| Element | Where | Why there is no source yet | Options |
|---|---|---|---|
| Arrival times: "RAIN IN 18 MIN", "SNOW IN 40 MIN" | 1, 2, 5 | MRMS publishes observed frames only. Its one extrapolation product, the AutoNowcaster Final Forecast (a 60-minute extrapolation of storms of 35 dBZ and up), was withdrawn by the NWS in 2023 ([SCN 23-86](https://www.weather.gov/media/notification/pdf_2023_24/scn23-86_autonowcaster_discontinuation.pdf)). The bucket has no `ANC_FinalForecast` folders in 2026, against 293 for `PrecipFlag` up to 2026-10-20. NWS forecasts are hourly, not minute by minute. | (a) our own extrapolation of motion between successive MRMS frames, worked out server-side; (b) a forecast model such as HRRR, which is a third data source, not currently named; (c) no countdown: say only that rain or snow is within N mi |
| The dashed "+30" on the replay timeline | 4 | No future radar frames exist in MRMS or the NWS, for the same reason | (a) drop it; (b) draw it, inactive; (c) fill it from a forecast source, with the same choices as the row above |
| "MIX" in the legend | 5 | MRMS `PrecipFlag` has no wintry-mix class | see [§4](#4-mix-in-mockup-5-undecided) |
| "MT BACHELOR" label | 1–5 | Not a Census place: the 2025 Gazetteer Places file has no record for it | see [§5](#5-town-labels-undecided) |
| The bottom lines of the warning screen, for a Flash Flood Warning | 3 (drawn for a Severe Thunderstorm Warning) | Flash Flood Warnings carry no hail, wind or storm-motion fields. 10 of 10 sampled carried only `flashFloodDetection`, or nothing | see [§3](#3-which-warnings-use-the-warning-screen-undecided) |

## 1. Which screen the device shows: UNDECIDED

The mockups show five screens and no rule for choosing between them.

**Proposed** (planner): an active warning first, then rain or snow approaching or present, then the dry screen.

| Open point | Options |
|---|---|
| 1a. The order | (a) the proposed fixed priority; (b) the same priority, with a timed rotation between screens of equal rank, like the other editions' dwell rotation |
| 1b. What "approaching" means | (a) arrival within N minutes, which needs an arrival-time source (see the table above); (b) precipitation within a distance ring, which works from MRMS frames alone, with no countdown |
| 1c. Precipitation at the location **now** | No mockup shows it; screens 2 and 5 both show it on the way. (a) the rain or snow screen with a "now" headline; (b) a new mockup; (c) the plain radar |
| 1d. When a warning counts as "covering the town" | (a) its polygon contains the device's location; (b) its polygon falls anywhere inside the 60 mi range |

## 2. The past-hour loop: UNDECIDED

Screen 4 shows one frame and a timeline from −1 h to NOW, then a dashed "+30". MRMS composite frames arrive about every 2 minutes (median 120 s over 67 files, wx spike A0 in [PR #422](https://github.com/Valar-Systems/valar-scopes/pull/422)), so an hour holds about 30 frames. On Blipscope, a swipe changes screens and a tap opens a detail card.

| Open point | Options |
|---|---|
| 2a. How the loop opens | (a) a tap (proposed); (b) a swipe to it, like any other screen; (c) it plays by itself, for example when rain is approaching |
| 2b. What the dashed "+30" does | (a) drop it; (b) show it inactive; (c) fill it from a forecast source. There are no future frames in MRMS or the NWS (see the table above). An inactive dashed segment reads like a forecast, so (b) has that cost |
| 2c. How many frames, and how fast | (a) every frame, about 30; (b) every other frame, about 15 |
| 2d. How it ends | (a) after one pass; (b) a tap; (c) a timeout |

## 3. Which warnings use the warning screen: UNDECIDED

**Recommended** (planner): Tornado, Severe Thunderstorm and Flash Flood Warnings. Watches and advisories stay out.

What the NWS alerts feed (`api.weather.gov/alerts`) carries, sampled on 2026-10-20 (up to 10 recent alerts of each type):

| Warning | Polygon | Fields for the bottom lines |
|---|---|---|
| Severe Thunderstorm | yes | new alerts: `maxHailSize`, `maxWindGust`, `eventMotionDescription`. Cancellations and some updates: motion only |
| Tornado | yes | `maxHailSize`, `eventMotionDescription`, `tornadoDetection`. `maxWindGust` was absent in 10 of 10 |
| Flash Flood | yes | `flashFloodDetection` only, or nothing, in 10 of 10 |
| Winter Storm (for comparison) | **no**: it is issued by zone | none |

- **Other warnings issued with polygons:** Snow Squall, Extreme Wind, Special Marine and Dust Storm ([NWS Geolocation](https://www.weather.gov/media/documentation/docs/NWS_Geolocation.pdf)).
- **Storm motion** comes as `<time>...storm...211DEG...37KT...<storm position>`: a direction in degrees (the direction the storm comes from, as in NWS warning text), a speed in knots and the storm's position. Screen 3's "STORM 15 MI SW · MOVING NE 30 MPH" would be worked out from it: distance and bearing from the device, direction of travel, and knots converted to mph.

| Open point | Options |
|---|---|
| 3a. Which warnings | (a) Tornado, Severe Thunderstorm and Flash Flood (recommended); (b) also Snow Squall, which fits the snow screen; (c) also Extreme Wind. Watches and advisories stay out under every option |
| 3b. The bottom lines for a Flash Flood Warning | (a) leave them blank; (b) show the `flashFloodDetection` text; (c) a new mockup for it |
| 3c. When the warning screen ends | (a) at the alert's `expires` time or on a Cancel, whichever comes first; (b) at `expires` only |
| 3d. Two warnings at once | (a) the most severe first (Tornado, then Severe Thunderstorm, then Flash Flood); (b) the newest first |

## 4. "MIX" in mockup 5: UNDECIDED

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

None of them is freezing rain, sleet or a wintry mix. The two "mix" codes are mixes of rain types.

Two related facts:
- MRMS also publishes model temperature grids, `Model_WetBulbTemp_00.50` and `Model_SurfaceTemp_00.50`, both current to 2026-10-20.
- The NWS gridded forecast's weather types include `freezing_rain`, `sleet`, `snow` and `rain` (the `Gridpoint` weather schema in the [api.weather.gov OpenAPI spec](https://api.weather.gov/openapi.json)).

| Options |
|---|
| (a) Snow and rain only: drop MIX from the legend. MRMS alone covers this |
| (b) Our own "mix" band: precipitation where MRMS's model wet-bulb temperature is near 0 °C. That would be our rule, not an MRMS class, and it needs a threshold and a check against real events |
| (c) The NWS gridded forecast's `freezing_rain` and `sleet`. This is a forecast for a grid area at hourly resolution, not an observation |

## 5. Town labels: UNDECIDED

**Proposed** (website): the US Census Gazetteer, with the same source on the device and the site so the two match.

What the 2025 Gazetteer Places file holds ([record layout](https://www.census.gov/programs-surveys/geography/technical-documentation/records-layout/gaz-record-layouts.2025.html), [file](https://www2.census.gov/geo/docs/maps-data/data/gazetteer/2025_Gazetteer/2025_Gaz_place_national.zip)):
- **What it is:** 32,350 records, from the US Census Bureau (a US government work).
- **Columns:** `USPS`, `GEOID`, `GEOIDFQ`, `ANSICODE`, `NAME`, `LSAD`, `FUNCSTAT`, `ALAND`, `AWATER`, `ALAND_SQMI`, `AWATER_SQMI`, `INTPTLAT`, `INTPTLONG`. **There is no population column.**
- **The mockups' six towns** are all present, written "Bend city", "Madras city", "Sisters city", "Redmond city", "Prineville city" and "La Pine city" (`LSAD` 25).
- **Mt Bachelor is not in the file**, because it isn't a Census place.

| Open point | Options |
|---|---|
| 5a. The source | (a) the Gazetteer, on both device and site (proposed) |
| 5b. Which places get a label, with no population to rank by | (a) incorporated cities only (`LSAD` 25), skipping census-designated places; (b) by land area (`ALAND`); (c) add a Census population table; (d) the nearest N to the centre |
| 5c. Mt Bachelor, and other non-places such as peaks and ski areas | (a) leave them out, so the device differs from the mockups there; (b) add USGS GNIS names, which include summits ([GNIS domestic names](https://www.usgs.gov/u.s.-board-on-geographic-names/download-gnis-data); licence to confirm before use); (c) a short hand-made list |
| 5d. Label text | (a) drop the `LSAD` word, so "Bend city" becomes "BEND" |

## 6. Units: UNDECIDED

**Proposed:** °F, mph and mi, the units in all six mockups.

The inputs come in other units, and the device converts them:
- NWS storm motion: knots, and the direction the storm comes from.
- NWS `maxWindGust`: mph ([CAP documentation](https://vlab.noaa.gov/web/nws-common-alerting-protocol/cap-documentation)).
- NWS `maxHailSize`: inches.

| Open point | Options |
|---|---|
| 6a. Units | (a) imperial only (proposed; the scope is US-only); (b) imperial by default, with a metric setting (Blipscope has a km/mi setting) |

## Sources

- NSSL `mrms-support`, [`GRIB2_TABLES/UserTable_MRMS_PrecipFlags.csv`](https://github.com/NOAA-National-Severe-Storms-Laboratory/mrms-support/tree/main/GRIB2_TABLES).
- NWS [SCN 23-86](https://www.weather.gov/media/notification/pdf_2023_24/scn23-86_autonowcaster_discontinuation.pdf): the AutoNowcaster is removed from MRMS.
- The `noaa-mrms-pds` bucket, listed 2026-10-20 ([listing](https://noaa-mrms-pds.s3.amazonaws.com/?list-type=2&prefix=CONUS/&delimiter=/)). Day folders were listed with `start-after=…/20260101`: a plain listing stops at 1,000 names, so it cuts off before the most recent folders. `PrecipFlag` returning 2026 folders is the control that shows the 2026 listing works.
- [api.weather.gov](https://api.weather.gov/) `/alerts?event=…`, sampled 2026-10-20, and its [OpenAPI spec](https://api.weather.gov/openapi.json).
- NWS [CAP documentation](https://vlab.noaa.gov/web/nws-common-alerting-protocol/cap-documentation) and [NWS Geolocation](https://www.weather.gov/media/documentation/docs/NWS_Geolocation.pdf).
- US Census Bureau Gazetteer: [record layouts, 2025](https://www.census.gov/programs-surveys/geography/technical-documentation/records-layout/gaz-record-layouts.2025.html) and the [2025 Places file](https://www2.census.gov/geo/docs/maps-data/data/gazetteer/2025_Gazetteer/2025_Gaz_place_national.zip).
- USGS [GNIS download](https://www.usgs.gov/u.s.-board-on-geographic-names/download-gnis-data).
- MRMS frame cadence: wx spike A0, [PR #422](https://github.com/Valar-Systems/valar-scopes/pull/422).
