# Flight route sources: what each gives, its licence, its cost (a read, 2026-11-13)

**For:** the owner, who is deciding (a) and (b) of the 2026-11-13 route commission, and the reviewer.
**Status:** a read only. Every page below was read on **2026-11-13 between 06:05Z and 06:14Z**. Only public pages
and documentation were read: no sign-up, no key and no trial call. Where a term is unclear, this says so rather than
guessing. People are named by role, never by name.

**Why now.** Cards take origin/destination from our `rt:` mirror first and then from upstream. adsb.lol's routeset has
returned no data since July, and adsbdb was switched off on 2026-08-26 over licensing. In the hour 00:00-01:00Z on
2026-11-13, at least **53.8%** of `/enrich` requests ended with no route: 36.8% missed the mirror, and 17.0% were
withheld as implausible for the aircraft's position.

## 0. The answer, short

1. **adsbdb does not own its route data.** Its own README credits the route data to two individual compilers and says it
   "may not be copied, published, or incorporated into other databases without the explicit permission of" one of them
   (the rights holder) (section 2.2). So a grant from adsbdb's operator alone probably does not cover the routes; the rights holder's
   grant is the one that counts, for caching especially. **Ask both.** hexdb.io, the obvious free alternative, credits
   one of the same two compilers in its footer (section 2.4), so it is not a way around the question.
2. **adsb.lol's routes are the same data as our mirror.** adsb.lol says "the VRS standing data is formatted and used for
   showing routes" (section 2.3). A revived routeset would add position-aware leg choice, and **no coverage**.
3. **The mirror itself has not been refreshed on record since 2026-08-26, and nothing would show it if it were stale.**
   Its upstream publishes changes daily. No workflow runs the route ingest, and `meta:routes` is read by nothing in
   `proxy/src` (section 2.1). This option costs no licence and no money, and it should be measured before anything is bought.
4. **Of the paid APIs, only AeroDataBox's published terms fit a per-callsign lookup shown on our devices on their face.**
   Paid plans allow commercial use and need no attribution. Caching is allowed for up to 7 days or the response's
   `max-age`. Showing data to End Users inside an Application is carved out of the sublicensing ban. **Cost: 2 API units
   per callsign lookup** (section 2.5). Its reading still wants their written confirmation.
5. **FlightAware and Flightradar24 both forbid using their data to backfill or supplement another real-time feed.**
   That is exactly our use: a route beside adsb.lol/adsb.fi positions. Neither is a candidate without written
   permission (sections 2.6 and 2.7).
6. **FAA SWIM is free and allows business apps, but it is US-only.** It needs an account, a long-running message consumer
   rather than a Worker request, and LADD filtering (section 2.9). **OpenSky carries no routes in its REST API**, and needs a
   written licence for commercial or operational use (section 2.10).

## 1. Summary table

| source | gives (callsign → route?) | licence, in one line | cost | fits us as published? |
|---|---|---|---|---|
| **our `rt:` mirror** (`vradarserver/standing-data`) | yes: callsign → origin/destination, a static table | CC0 1.0 | $0 | yes. Provenance upstream of the SDM site is **not established** (section 2.1) |
| **adsbdb** | yes: callsign → route, live API | the route data is a third party's; no copying, publishing or incorporating without its rights holder's permission | none published | **no**, not without the rights holder's grant (section 2.2) |
| **adsb.lol routeset** | yes, position-aware; the **same data** as the mirror | ODbL for adsb.lol; the route data is VRS standing data | sponsored ($50/month) | adds no coverage; endpoint dead since 2026-07-08 on the repo's record (section 2.3) |
| **hexdb.io** | yes: callsign → route | **no licence or terms on the page**; its footer credits one of adsbdb's two route compilers | free, no key | **no**: the same rights question as adsbdb, and no grant (section 2.4) |
| **AeroDataBox** | yes: flight status by ATC callsign or ICAO24, live or scheduled | paid plans: commercial use, caching ≤7 days, End Use inside an Application allowed | 2 units per lookup; from $7.50/month (5,000 units) | **yes on their face**; confirm in writing (section 2.5) |
| **FlightAware AeroAPI** | yes: `/flights/{ident}` per flight | Standard licence: B2C use allowed; **no backfill of another real-time feed; no commercial aircraft situational displays** | $0.005 per result set; $100/month minimum | **no** without written permission (section 2.6) |
| **Flightradar24 API** | origin/destination per flight | commercial use allowed; **no supplementing/backfilling other real-time data; no competing products** | not read (portal needs JS; blog 403) | **no** without written permission (section 2.7) |
| **aviationstack** | real-time flights by flight number; airline routes (schedules) | API data terms **not found**: the site's "terms" link resolves to the parent company's general website terms | $49.99/month for 10,000 requests and up | unclear (section 2.8) |
| **Aviation Edge** | airline routes, flight tracker | use "within your own applications or systems", but no "distribute the data" and no mirroring | $299/month for 30,000 calls and up | unclear: is a route on a customer's device "distributing"? (section 2.8) |
| **AirLabs** | routes and schedules API | terms (2021) are silent on data use, caching and redistribution | prices not rendered on the page | unclear on everything (section 2.8) |
| **FAA SWIM (SCDS)** | US flight plans, keyed by aircraft id (callsign) | free; business apps allowed; **LADD filtering required** | $0, plus a consumer to run | possible for US flights only; sign-up and agreement not read (section 2.9) |
| **OpenSky** | **no routes** via REST; estimated airports for past flights | written licence for any commercial or operational use | quote only | **no** (section 2.10) |

## 2. Each source

### 2.1 Our `rt:` mirror: `vradarserver/standing-data` (619,103 rows, CC0)

- **The repo records:**
  - source: `vradarserver/standing-data`, the main-branch tarball ([proxy/scripts/ingest-routes.ts:101](../proxy/scripts/ingest-routes.ts));
  - licence: CC0 1.0, "verified by reading LICENSE" ([ingest-routes.ts:25-28](../proxy/scripts/ingest-routes.ts);
    [docs/follow-mode-consolidated.md:1785](follow-mode-consolidated.md)).
  - It records nothing further upstream.
- **The public pages say:**
  - the standing-data README: "CSV files built from the aviation data submitted by Virtual Radar Server users to the SDM
    site";
  - GitHub's description of the repository: "Dumps of aviation data from Virtual Radar Server's SDM site" (repository
    created 2022-08-26);
  - the LICENSE: the plain CC0 1.0 Universal legal code;
  - the SDM site's home page carries only a copyright line naming its operator, with no provenance or import statement.
- **Not established:** whether any SDM route rows were seeded from, or copied out of, the compilation adsbdb's README
  credits (section 2.2). Neither source names it, and none says the SDM data is independent of it.
  - Two readings fit the text, and nothing read separates them: user-submitted from scratch, or user-submitted
    including copies. This doc chooses neither.
  - Only the SDM site's operator can answer. That is an owner action; no one has been contacted.
- **Freshness, which bears on the 36.8% miss rate:**
  - The upstream commits daily: "Standing data changes" at about 04:49Z each day, the latest on 2026-11-12, read
    through GitHub's public API.
  - No workflow runs `ingest:routes`. It is only an npm script ([proxy/package.json:19](../proxy/package.json)), run by hand.
  - The last load the docs record is **2026-08-26**, in staging and production
    ([docs/follow-mode-consolidated.md:84-87](follow-mode-consolidated.md)). No later refresh is recorded.
  - `ingest-routes.ts:43-47` says the meta key exists so that "/healthz correctly reports stale". But no file in
    `proxy/src` reads `meta:routes`, so the mirror's age is printed nowhere routine.
  - Dating it takes one read of `meta:routes`, which is not a device key. That read was not run.
- **How it is called:** a by-hand ingest from the tarball into KV, diffed per shard, then `verify-routes.ts`.
- **Terms we cannot meet:** none.

### 2.2 adsbdb

- **Gives:**
  - callsign → route, with midpoints for a few routes;
  - `GET https://api.adsbdb.com/v[major]/callsign/[CALLSIGN]`, and `.../aircraft/[MODE_S]?callsign=` for both at once;
  - an unknown callsign returns 404 `{"response":"unknown callsign"}`.
- **Licence, in adsbdb's own words** ([github.com/mrjackwills/adsbdb README](https://github.com/mrjackwills/adsbdb), main
  branch, read 2026-11-13). The names are replaced by roles here:
  > "The flight route data is the work of [two named individuals, the compilers], and may not be copied, published, or
  > incorporated into other databases without the explicit permission of [one of them, the rights holder]."
  - adsbdb's home page carries no terms; its embedded metadata says only `"isAccessibleForFree": true`.
- **Cost:** none published. The owner's sponsorship offer is in progress.
- **Terms we cannot meet:** caching. Our 24 h `rt:` cache is "incorporated into other databases", the same reading
  `ingest-routes.ts:20-23` already gives it. Showing a route on a sold device may also be "published".
  - **So the grant that would let us use it is the rights holder's, not adsbdb's operator's.**
  - The owner's ask (a) is best sent to both: adsbdb's operator for API use and rate, and the rights holder for the data.

### 2.3 adsb.lol routeset

- **Gives:** callsign + position → route. The position lets the upstream pick the leg.
- **Its data, in adsb.lol's words** ([adsb.lol/docs/acknowledgements/open-source](https://www.adsb.lol/docs/acknowledgements/open-source/),
  read 2026-11-13). Under the heading "Virtual Radar Server Standing Data": "the VRS standing data is formatted and
  used for showing routes."
  - **The same source as our mirror.** Reviving it adds leg choice by position, which bears on the 17.0% withheld as
    implausible. It adds **no coverage** for the 36.8% that miss.
- **Status:** 201 with an empty body since 2026-07-08, on the repo's record (`ingest-routes.ts:13-18`). Not re-checked,
  because a trial call was out of scope.
- **Cost:** covered by the existing $50/month sponsorship.
- **Terms we cannot meet:** none known.

### 2.4 hexdb.io

- **Gives:** `GET https://hexdb.io/api/v2/routes/{callsign}`, with ICAO and IATA codes and multi-leg stops, and the
  legacy `/api/v1/route/icao/{callsign}`. Free, no key; the page says "3.1M+ Requests served daily".
- **Licence:** **none stated.** The page ([hexdb.io](https://hexdb.io/), read 2026-11-13) contains no licence, terms,
  "commercial" or copyright text.
- **Provenance:** the footer reads "Data courtesy of" five sources: PlaneBase/PlanePlotter, two named individuals,
  ip2location and Airport-Data. It does not say which source supplied which data.
  - **One of the two individuals is one of the two route compilers named in adsbdb's README** (section 2.2).
  - Its own example route row is dated `2014-04-20`.
- **Cost:** free.
- **Terms we cannot meet:** the same as adsbdb's. Its routes plausibly come from the same compilation; the footer does
  not say so outright, but it credits one of the compilers and no other route source. hexdb grants nothing, so caching
  it would need the same rights holder's permission.

### 2.5 AeroDataBox: the one paid API whose published terms fit on their face

- **Gives:** `GET /flights/{searchBy}/{searchParam}`, "Flight Status (single day) / TIER 2". `searchBy` takes `Number`,
  `Reg`, **`CallSign`** ("Search by ATC call-sign") or **`Icao24`**. From its OpenAPI spec v1.15.4.0
  ([doc.aerodatabox.com](https://doc.aerodatabox.com/)):
  - it returns live status "if the flight is within the coverage", "or flight schedule data otherwise";
  - "Some flights may have partial data. The data may be absent for either arrival or departure airport".
- **Licence** ([aerodatabox.com/terms](https://aerodatabox.com/terms/), "Last updated: October 11, 2026"):
  - **Commercial use (5.3):** allowed "when the Contents were retrieved through a channel that required payment to
    AeroDataBox".
  - **Attribution (5.4):** "required only when the Contents were retrieved through a channel that did not require
    payment".
  - **Caching (5.5):** allowed for "the immediate operational needs of the Application, feature, project, or service
    that is already operational". Copies may be kept no longer than "the greater of: seven (7) consecutive calendar
    days; or if the HTTP response ... includes a “Cache-Control” header specifying a “max-age” directive, the number of
    consecutive seconds specified therein; or any other caching or retention period ... when such period explicitly
    specified by your Plan Terms".
  - **Showing it to our customers:**
    - an "Application" is "a software product, service, or feature that you create and operate, which has material
      value independent of the Services ... within which Contents or Derived Works are made available to End Users".
    - The exclusion: "An offering does not qualify as an Application to the extent it makes Contents available as a
      data feed, dataset, or in any manner that provides, or could be used as, a substitute for the Services."
  - **Our reading, for them to confirm in writing:** a route shown on a sold Blipscope is End Use inside an
    Application. The proxy's `/v1/enrich` answers only our own devices, which is not a data feed.
- **Cost** ([aerodatabox.com/pricing](https://aerodatabox.com/pricing/), USD, pre-tax): **a callsign lookup is Tier 2 =
  2 units.**

  | channel | plan | $/month | units/month | callsign lookups/month |
  |---|---|---|---|---|
  | API.Market | Pro | 7.50 | 5,000 | 2,500 |
  | API.Market | Ultra | 37.50 | 50,000 | 25,000 |
  | RapidAPI | Pro | 8 | 5,000 | 2,500 |
  | RapidAPI | Ultra | 40 | 50,000 | 25,000 |
  | direct | Starter | 19 | 40,000 | 20,000 |
  | direct | Growth ("Extended caching") | 99 | 400,000 | 200,000 |
  | direct | Scale ("Maximum caching") | 499 | 4,000,000 | 2,000,000 |

  - The direct plans sit under a heading the page also labels "New" and "Coming soon". Whether they can be bought today
    is **unclear**.
  - What "Extended" and "Maximum" caching allow is **not stated** on the page read.
- **Terms we cannot meet:** none found. To confirm with them: the Application reading above, and the retention that
  "Extended caching" grants.

### 2.6 FlightAware AeroAPI

- **Gives:** `GET /flights/{ident}`, "Get information for a flight", per flight leg, with origin and destination.
- **Licence:** the AeroAPI Standard License, the November 2022 version by its footer, the PDF FlightAware links
  ([AeroAPI_Standard_License.pdf](https://www.flightaware.com/commercial/aeroapi/AeroAPI_Standard_License.pdf), read
  2026-11-13). Licensee may:
  - "Incorporate AeroAPI Data as embedded data or create Derivative Works in a business-to-individual consumer
    application";
  - "Provide AeroAPI Data to third-parties without additional fee or charge to such third-parties ... only when AeroAPI
    Data is provided in other than raw format".
- **Licensee may not:**
  - **8.** "Use AeroAPI Data for commercial aircraft situational displays."
  - **9.** "Store AeroAPI data in raw format ... for a period longer than thirty (30) days".
  - **10.** "Use the AeroAPI Data in conjunction with or as a backfill to data sourced from any other real-time or
    near-realtime flight data provider without the prior written permission of FlightAware."
- **Cost** ([flightaware.com/commercial/aeroapi](https://flightaware.com/commercial/aeroapi/)):
  - `/flights/{ident}` is **$0.005 per result set** (15 records);
  - the Standard tier (B2C) has a **$100/month minimum**, so up to about 20,000 lookups a month cost the minimum;
  - Personal is "personal or academic purposes only".
- **Terms we cannot meet:**
  - **10 as written.** Our routes sit beside adsb.lol/adsb.fi positions, so this needs FlightAware's written permission.
  - **8 is unclear:** whether a desk radar for consumers is a "commercial aircraft situational display" is theirs to say.
  - Also unclear: whether a device that is sold counts as "additional fee or charge".

### 2.7 Flightradar24 API

- **Gives:** origin and destination per flight. The endpoint list and credit costs could not be read: the portal renders
  in JavaScript, and its blog post returned 403.
- **Licence:** the FR24 Terms of Service, "Last updated: June 23rd 2026", section 6
  ([flightradar24.com/terms-and-conditions](https://www.flightradar24.com/terms-and-conditions)).
  - **Allowed (6.3.1):** "Commercial use and the creation of derivative works from data sourced via the FR24 API is
    permitted at each subscription level".
  - **Prohibited:**
    - "Using the FR24 API to supplement or backfill any other near-time or real-time flight data sourced from an
      alternative data provider for any commercial or non-commercial use."
    - "Developing competing services or products based on the information or insights gathered from our data."
    - Storage beyond the per-endpoint limits documented in the API portal.
- **Cost:** plans are Explorer, Essential and Advanced. Prices were **not read**.
- **Terms we cannot meet:**
  - **the backfill clause, as written;**
  - possibly "competing services or products", since Blipscope is a flight radar.
  - Both need FR24's written word.

### 2.8 aviationstack, Aviation Edge, AirLabs: commercial, but their terms do not answer our question

**aviationstack** ([pricing](https://aviationstack.com/pricing))
- Free: 100 requests, "Non-Commercial Use".
- Basic: $49.99/month, 10,000 requests (+$0.019996 each).
- Professional: $149.99/month, 50,000.
- Business: $499.99/month, 250,000.
- "Airline Routes" is a schedule feature; real-time flights are looked up by flight number, which is not always the
  callsign.
- **The site's "terms" link resolves to the parent company's general website terms**
  (`ideracorp.com/Legal/Terms-of-Use`). Those cover the website and software, not API data. **The API data terms were
  not found:** caching and display rights are unknown.

**Aviation Edge** ([premium API](https://aviation-edge.com/premium-api/))
- Plans, after a discounted first month:
  - Developer $299/month, 30,000 calls;
  - Business $599/month, 100,000;
  - Business Gold $1,499/month, 500,000.
  - All include "Airline Routes API" and "Flight Tracker API".
- Terms ([api-terms-of-service](https://aviation-edge.com/api-terms-of-service/), "Effective Date: 30 June 2025"):
  - 4.2: "You may use the data for internal commercial purposes or within your own applications or systems. However,
    you may not: Resell, sublicense, lease, lend, or distribute the data or access to the Services";
  - 8.2: "You may not mirror our data to third-party platforms or store it for the purpose of recreating our datasets."
- **Unclear:** whether a route shown on a customer's device is "distribute the data".

**AirLabs** ([airlabs.co](https://airlabs.co/))
- The pricing section renders as template placeholders, so no prices were read.
- The terms ([terms-of-service](https://airlabs.co/terms-of-service), "Updated 07/20/2021") contain no clause on data
  use, caching, redistribution or attribution. They say only that the Service and its content "are and will remain the
  exclusive property of airlabs.co (Data Products Ltd) and its licensors".
- **Unclear on everything that matters here.**

### 2.9 FAA SWIM, through the Cloud Distribution Service (SCDS)

- **Gives:** the FAA's flight data, distributed "via Solace Java Message Service (JMS) messaging" (SCDS General
  Guideline v1.1, 2024-09-11,
  [faa.gov](https://www.faa.gov/sites/faa.gov/files/air_traffic/technology/swim/governance/SCDS-Guideline-Document_v1.1_09.11.2024)).
  - Flight plans carry departure and arrival per flight.
  - **US only.** The exact product (TFMS or SFDPS) and its field list were not read.
- **Licence, in the guideline's words:**
  - "SCDS Consumers MAY use SWIM data for non-NAS-impacting purposes."
  - "SCDS data consumers who use SWIM data and provide non-NAS impacting external business service operations will be
    responsible for notifying their users of planned or unplanned outages, and of the acceptable use of data obtained via
    SCDS".
  - **LADD** ([faa.gov/pilots/ladd](https://www.faa.gov/pilots/ladd)): "Vendors who subscribe to FAA SWIM Data feeds are
    bound by a Data Access User Agreement to filter any LADD participant from public display of aircraft flight data."
- **Cost:** free. The guideline says SCDS is provided "free of charge", with rules for anyone over 200 GB/day or
  2 TB/month.
- **How it would be called:** not from a Worker. It needs a long-running JMS subscriber, for example on the station
  host, writing keys like `rt:`. That is new infrastructure, not a fetch.
- **Terms we cannot meet:** none known, but **the Data Access User Agreement itself was not read**, because it sits
  behind the sign-up. LADD filtering is an obligation we would have to build and keep current. How often the LADD list
  changes was not read.

### 2.10 OpenSky Network

- **Gives no routes:**
  - "Routes, ADS-C and MLAT data are currently not available through the REST API"
    ([REST docs](https://openskynetwork.github.io/opensky-api/rest.html)).
  - `/flights/aircraft` gives estimated airports for past flights only: "Flights are updated by a batch process at
    night, i.e., only flights from the previous day or earlier are available".
- **Licence** ([terms of use](https://opensky-network.org/about/terms-of-use)):
  - "Any use by a for-profit or commercial entity requires written permission and a license granted by the OpenSky
    Network."
  - "Operational use of the REST API in any live product, service, or automated system also requires a written license,
    regardless of the entity’s non-profit status."
- **Cost:** quote only.
- **Terms we cannot meet:** both, without a written licence. It is not a route source in any case.

## 3. Read and set aside, or not read

- **airframes.io** (ACARS-derived): "Bulk redistribution, resale, or commercial use beyond your access tier requires
  prior arrangement" and "The Airframes API and its licensing terms are still being finalized"
  ([licensing](https://docs.airframes.io/api/licensing/)). It is not a route table, so it was set aside.
- **OAG, Cirium:** enterprise schedule data, priced by quote. Their pages did not render to a plain read (a bot wall),
  and no price was read.
- **Not read in this pass:** Eurocontrol NM B2B, Spire, Aireon, ADS-B Exchange and OpenFlights. Ask if any should be.

## 4. The number that sizes any paid option, not measured here

Every paid row is priced per lookup, so cost is linear in **distinct callsign misses per month**. With a cache, a
callsign is bought once per retention window, not once per request.
- That count was not measured. It is one read-only Workers Logs query that prints counts only, like the logs-bytes
  read.
- Without it, a guide:
  - 20,000 lookups a month is the AeroDataBox direct Starter at $19, or FlightAware's $100 minimum;
  - 200,000 a month is AeroDataBox Growth at $99, or about $1,000 at FlightAware's per-set price.
- Any paid plan is a separate subscription, outside the Cloudflare overage allowance, so it is the owner's call.

## 5. Side findings (for the reviewer's queue)

- **P2. The `rt:` mirror has no scheduled refresh, and its staleness signal has no reader (section 2.1).** This
  probably contributes to the 36.8% that miss. Refresh it and measure before buying anything.
- **P3. OpenSky's terms require a written licence for "integration into a live product".** The radar's OpenSky source
  and Missileer's command-post watch call OpenSky from the device, with the customer's own account. Whether a
  customer's own key in our firmware is "operational use" by us or by them is unclear. For the owner's awareness;
  nothing here is a safety matter.
