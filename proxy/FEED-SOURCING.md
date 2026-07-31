# Cloud feed sourcing — status, plan, and outreach drafts

The cloud feed (zero-setup: device → our Cloudflare Worker → an ADS-B aggregator)
is the product's default. **Local mode** (device → the user's own dump1090/readsb)
is rock-solid but requires a ~$40–100 receiver most buyers won't set up, so the
cloud path has to work on its own.

## The core constraint (verified 2026-07-18)

- **adsb.lol keyless is rate-limited for us.** Our Worker egresses from Cloudflare's
  **shared per-colo IPs**; adsb.lol's anonymous per-IP limit trips on *other* CF
  tenants' traffic, so we get intermittent **HTTP 429** — worst on the high-volume
  `/v2/point` bulk endpoint. It is **not** a ban on us (a direct query from a normal
  IP works fine).
- **adsb.lol's feeder API key is future, not current.** Their docs: *"In the future,
  you will require an API key which you can obtain by feeding adsb.lol."* Keys aren't
  issued yet, so this is **not an actionable fix today**.
- **adsb.lol's feeder API (`re-api`) is IP-locked** to the feeding station's IP, so a
  Cloudflare Worker can't use it even if we feed.
- **airplanes.live works** and isn't limited this way — in testing it served the full
  picture (positions + most types) reliably.

## What shipped (technical)

- **airplanes.live is now PRIMARY for positions** (`/v2/point`), adsb.lol the fallback
  ([chain.ts](src/upstreams/chain.ts) `POINT_ORDER`). Ends the 429 churn and the
  `DATA STALE` the device kept hitting.
- **adsb.lol stays PRIMARY for per-hex metadata** (`HEX_ORDER`) — it carries the ICAO
  type inline and 429s far less on the low-volume hex path; airplanes.live + the
  **adsbdb type backfill** (PR #96) cover any miss.
- **Failover feeds are enabled on production** for the bench soak (owner-approved).

## What's needed (business) — the real launch gate

A feed that is free **and** unlimited **and** reliable **and** commercially licensed
likely doesn't exist. Options, in order of effort:

1. **Commercial permission from airplanes.live / adsb.fi** (cheapest). Community
   aggregators with a contact process; often attribution + an email. Draft below.
2. **A paid commercial ADS-B API** (guaranteed, has an SLA): ADSBexchange (RapidAPI),
   adsb.fi commercial, or FlightAware AeroAPI. Bake a few ¢/device/month into pricing.
3. **adsb.lol feeder key** — revisit when their program launches (offer sent below).

---

## Paid-API pricing (priced 2026-07-18) — and why per-request doesn't fit

**ADSBexchange** ([RapidAPI "Community"](https://rapidapi.com/adsbx/api/adsbexchange-com1/pricing)):
**$10/mo for 10,000 requests** (~$0.001/req), 500 ms updates, query by lat/lon radius /
hex / callsign. Higher volume + commercial = enterprise quote ([enterprise](https://www.adsbexchange.com/products/enterprise-api/)).

**FlightAware AeroAPI v3** ([pricing](https://www.flightaware.com/commercial/aeroapi/v3/pricing.rvt)):
**Personal** = up to $5 free/mo ($20/mo credit if you're an ADS-B feeder); **Standard** =
**$200/mo minimum**, 5 result-sets/sec; **Premium** = 100/sec, volume discount past
$5,000/mo. ~$0.002/query; a "page" = 15 records, billed per page.

**The catch — per-request pricing is the wrong model for a live radar.** Our proxy needs
~1 upstream `/point` fetch **per active geographic tile per refresh window** (shared
across all devices in that tile). Do the math for one *isolated* device (its own tile):

| upstream refresh | fetches/mo per tile | ADSBexchange @ $0.001 | AeroAPI @ ~$0.002 |
|---|---|---|---|
| 30 s | ~86,400 | ~$86/mo | ~$170/mo + $200 min |
| 60 s | ~43,200 | ~$43/mo | ~$86/mo + $200 min |

That's **more than the hardware, per device** — because these APIs are priced for
*occasional flight-status lookups*, not continuous polling. It only gets affordable when
many devices **share a tile** (same metro), or with a very slow refresh. `BLIPS_FRESH_TTL_MS`
is the direct cost knob.

**Conclusion:**
- **Community feeds + commercial permission** (airplanes.live / adsb.fi) remain the
  economically right model — flat/donation, not per-request. **Priority.**
- A **fixed-IP egress proxy** (~$5/mo VPS in front of adsb.lol keyless) is worth a spike:
  a dedicated IP gets adsb.lol's anonymous per-IP limit *to ourselves* instead of sharing
  the Cloudflare pool — possibly fixing the 429 for ~$5/mo flat, no per-request cost.
- **Paid per-request APIs** (ADSBexchange/AeroAPI) only pencil out at heavy device
  clustering; keep as a **spot fallback**, not the backbone.
- A **flat-rate bulk feed** (FlightAware Firehose, ADSBexchange enterprise) is the
  "at real scale" option — predictable but quote-based (likely $100s–1,000s/mo).

## Scaling to 600 units — the three paths, and what blocks each (2026-07-30)

Upstream request rate is `(distinct active areas) ÷ (cache TTL)`. At 600 units across
~40 metros that's **553 distinct areas**, so the free tier's 1 req/s is exceeded by
roughly 40× at realistic peak. Three ways out, in the order we'd prefer them:

| path | needs from adsb.fi | needs from us | status |
| --- | --- | --- | --- |
| **A. Global snapshot** — one query returns everything; rate stops depending on fleet size (**0.033 req/s at a 30 s refresh, forever**) | commercial tier + field parity | the pre-slicer below | **blocked on Q3** |
| **B. N authorised relay IPs** — stay on the per-tile API, run more origins | written permission for N origins | tile coarsening + longer TTL | **blocked on Q4** |
| **C. Free tier only** | nothing | coarsening + TTL, and a hard ceiling around 70–100 units | fallback; does not reach 600 |

**Path B may well win, and it obsoletes the pre-slicer.** Once tiles are coarsened it
needs only **3–6 origins**, not dozens:

| config at 600 units | areas | peak req/s | IPs |
| --- | --- | --- | --- |
| no change | 553 | 40.3 | 41 |
| 4× coarser, 30 s TTL | 253 | 8.4 | 9 |
| 4× coarser, 50 s TTL | 253 | 5.1 | **6** |
| 16× coarser, 50 s TTL | 133 | 2.7 | **3** |

At $6–12/mo per droplet with **zero new code**, that is far cheaper than path A's
engineering. Note this is *not* the IP-sharding we refused: that refusal was about
circumventing a free anonymous limiter. Under a paid agreement stating how many origins
we may run, it is ordinary architecture — and we run exactly the authorised number.

### The pre-slicer (path A only) — scoped, NOT started

**Do not build this until Q3 is answered, and check Q4 first — path B may remove the need
entirely.** Estimate retained so the 600-unit decision carries its true cost:

The right shape is **not** a slicer that pre-generates tiles (a global 0.05° grid is 6.5 M
cells). It is an **in-memory geo-index service** on each relay that holds the latest
snapshot and answers point queries **mimicking adsb.fi's own API surface** — so the Worker
adapter, the relay cache, the TTL/hold-down layers and the device all stay unchanged and it
drops in as a different origin.

| item | estimate |
| --- | --- |
| Core service — fetch loop, parse, grid index, point query, matching response shape | 3–4 d |
| Equivalence soak — its output vs live adsb.fi for the same coordinates | 2 d |
| Ops — systemd, health, metrics, both relays, last-good on fetch failure | 1 d |
| **Total** | **~1.5–2 weeks** |

**Write it in Go, not Node.** A 22 MB JSON parse every 30 s peaks 200–300 MB in Node vs
~50 MB in Go — the difference between a 1 GB and a 2 GB droplet.

Four things that make it more than a weekend:

1. **`dist` must be great-circle, not planar**, or the picture subtly differs from adsb.fi's
   at tile edges and nobody notices until someone compares.
2. **`now` must pass through unchanged.** The freshness-failover in `chain.ts` reads the
   feed's own stamp; if the slicer stamps its own clock, degraded-detection dies silently.
3. **Hex lookups can't come from the snapshot alone** — it holds only airborne aircraft, so
   `/v2/hex` still needs the live endpoint as fallback.
4. **Failure composes correctly** (the good news): snapshot fetch fails → serve last-good
   with an honest `now` → the degraded threshold trips → the chain falls to adsb.lol.

Ongoing COGS: ~100–233 GB/month gzipped per relay at a 30 s refresh, inside a $6–12
droplet's included transfer.

### If the answer is "free tier only" (path C)

50-unit pilot at realistic peak (US time-zone spread, idle/night cadences — **not** an
all-active assumption) is **3.64 req/s**, so *some* change is required before the pilot
ships. Decided 2026-07-30: **TTL 50 s + `TILE_DEG` 0.25 → 0.71 req/s (29 % margin)**.
TTL 50 s alone lands at exactly 1.00 — zero headroom, one busy weekend from breaching —
so it is not shippable on its own even though the table says it "fits".

## Draft: airplanes.live (primary-source permission)

> **Subject:** Commercial API use + attribution — Blipscope desk ADS-B radar
>
> Hi airplanes.live team,
>
> I'm Daniel Frenkel, building **Blipscope** — a small desk "flight radar" gadget (a
> round touch display on an ESP32) that shows aircraft overhead. In cloud mode the
> devices fetch aircraft through a small **Cloudflare Worker proxy we run**, so they
> don't each hit you directly.
>
> I'd like to use your API as a primary source and do it the right way — with your
> permission and proper attribution. How we use it:
>
> - The proxy **caches aggressively**: one `/v2/point` fetch per geographic tile per
>   few seconds, shared across every device in that area — so our request rate scales
>   with distinct **areas**, not device count. It stays low.
> - We already list data sources on the device's public credits page and will add or
>   adjust attribution however you prefer.
> - It's a paid hardware product; if you have a commercial tier, sponsorship, or
>   fair-use arrangement, I'm glad to support the project (financially or by feeding).
>
> Could you let me know: (1) whether commercial use like this is OK, (2) any rate or
> attribution requirements, and (3) whether there's an API key or contact process we
> should be on?
>
> Thanks for running such a great resource.
> Daniel Frenkel — Valar Systems / Blipscope — danielfrenkel@gmail.com

## Draft: adsb.fi reply (redistribution → commercial → rate limit)

**Status 2026-07-29:** testing permission granted; the old HTTP 403 turned out to be
Cloudflare's shared egress, not an adsb.fi block — both relay IPs get 200 unauthenticated.
**The blocker is now purely licensing.** Their published terms are "personal,
non-commercial use only … you may not license, sell, rent, or lease any part of the data
or the service", with no ODbL-style redistribution right.

The three asks are deliberately ordered: **redistribution first**, because our entire
relay/SWR/KV architecture is a redistribution system — if the answer to (1) is no, then
(2) and (3) are moot and we should not spend their goodwill negotiating them. Send as a
reply on the existing testing-permission thread.

> **Subject:** Re: API testing — redistribution, commercial use, and rate limits
>
> Hi adsb.fi team,
>
> Thank you for the go-ahead to test — that was generous, and we've started a 24-hour
> measurement from our two relay IPs (`67.205.155.80`, `104.238.156.243`) at about
> 0.08 req/s each, well inside your 1 req/s limit. Early result: adsb.fi is noticeably
> faster and more consistent than what we run on today.
>
> Before we go any further, I want to be straight with you about what our system
> actually does, because I think it may sit outside your terms as written. I'd rather
> ask than assume.
>
> **Some context.** Blipscope is a small desk ADS-B radar — a round display that shows
> the aircraft overhead. It's a paid hardware product. The devices don't call your API
> directly: they talk to a Cloudflare Worker we run, which fetches an area tile through
> a caching relay on a dedicated IP and fans that one response out to every device
> looking at that area. So one fetch per area per few seconds serves many devices, and
> our load scales with *distinct areas*, not with how many units we sell.
>
> That design is the reason for question 1 — it is, functionally, redistribution.
>
> **1. May we cache your data and redistribute it to our devices?** This is the one
> that matters most, and the one I don't want to get wrong. Your terms say the open
> data is for personal, non-commercial use and may not be licensed, sold, rented or
> leased. Our architecture stores your responses in a cache and serves them onward, so
> I read that as **not permitted** without your explicit say-so. For comparison,
> adsb.lol publishes under ODbL 1.0, which grants that right with attribution and
> share-alike; that's why we built on them. Is there an equivalent grant you'd be
> willing to give in writing — or is redistribution simply off the table for adsb.fi?
> **If it's off the table, please just say so and ignore the rest of this email** —
> nothing else matters if we can't cache and serve your data, and I don't want to waste
> your time on terms we can't use.
>
> **2. Commercial use at launch, not just testing.** Assuming (1) is workable: the
> testing permission covers our bench, but we're preparing to ship to paying customers,
> and I don't want to quietly let a test grant drift into production use. Do you offer
> commercial terms? We're glad to pay. For reference we're sponsoring adsb.lol at
> $50/month, and we'd treat adsb.fi the same way — funding the project, not buying
> preferential treatment.
>
> **3. A rate limit above 1 req/s.** Also assuming (1): the 1 req/s public limit is
> the one hard technical constraint. Our upstream request rate is roughly
> `(distinct active areas) ÷ (cache TTL)`. We cache each area tile for 8 seconds, so
> 1 req/s caps us at about **8 active areas at any moment, per relay IP** — which a
> customer base spread across even a handful of metros passes immediately. We can trade
> freshness for headroom (a 15-second TTL doubles the area count we can serve), but
> beyond that we'd need a higher ceiling. Is there a rate tier — feeder-based, paid, or
> otherwise — that would fit? We'd also happily run receivers and feed adsb.fi.
>
> **Attribution** is easy either way: your terms ask for a citation and a link to your
> home page, and we already carry an equivalent line for adsb.lol on the device's
> configuration page and our public credits page. Yours would sit alongside it.
>
> **One small bug report, as a thank-you for the test access.** Your docs note that
> `/api/v2/lat/lon/dist` is deprecated but still functional. It does still return
> HTTP 200 and valid JSON — but in the older `aircraft.json` schema:
> `{"now": <epoch seconds>, "aircraft": [...]}`, whereas `/api/v3/lat/lon/dist` returns
> `{"ac": [...], "now": <epoch milliseconds>}`. Any client written against the v2/v3
> *object* endpoints reads the `ac` key, so pointing it at the deprecated v2 endpoint
> yields **zero aircraft with no error** — it looks like empty airspace rather than a
> broken call. It cost us a little time; a note in the docs that the deprecated
> endpoint also uses a different response shape (or having it return an error) might
> save someone else the same trip. We're on `/v3` now.
>
> Thanks again for the test access, and for running adsb.fi — it's a genuinely good
> service, and I'd rather have a clear "no" than a quiet misuse of it.
>
> Best,
> Daniel Frenkel — Valar Systems / Blipscope — danielfrenkel@gmail.com

## Draft: adsb.lol (offer to feed + interest in the future feeder-key program)

> **Subject:** Offer to feed + interest in the planned feeder API-key program
>
> Hi adsb.lol team,
>
> I'm Daniel Frenkel, building **Blipscope**, a small desk ADS-B radar gadget. I've
> been using your public API and really value the project and its open (ODbL) stance.
>
> Two things:
>
> 1. Your docs mention that an **API key obtainable by feeding** will be required in
>    the future. When that program lands, we'd like to be part of it — happy to feed
>    adsb.lol and use a proper feeder key instead of the anonymous API. Please keep us
>    posted, or point me to where to sign up.
>
> 2. A practical note that may be useful feedback: our devices reach your API through
>    a **Cloudflare Worker**, whose egress uses Cloudflare's shared per-colo IPs.
>    Those get **429'd** by your anonymous per-IP limit because of *other* Cloudflare
>    tenants' traffic on the same IP — not our volume. A feeder-key path (per-key
>    limits) would solve this cleanly for anyone proxying through a serverless/CDN
>    platform. We'd gladly feed to earn that.
>
> I'm setting up feeding from my own receiver regardless. Thanks for all you do.
>
> Daniel Frenkel — Valar Systems / Blipscope — danielfrenkel@gmail.com

*(Fill in the real sending address / company signature before sending; the Gmail
address above is a placeholder.)*
