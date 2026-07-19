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

## Draft: adsb.fi (access + commercial use — we currently get 403)

> **Subject:** API access + commercial use — Blipscope desk radar
>
> Hi adsb.fi team,
>
> I'm Daniel Frenkel, building **Blipscope**, a desk ADS-B radar gadget whose devices
> fetch aircraft via a small Cloudflare Worker proxy we run. I'd like to use adsb.fi
> as a source (primary or failover) for this commercial product, with your permission.
>
> Two questions:
> 1. Requests from our Cloudflare Worker currently get **HTTP 403** — is there an
>    allowlist, required `User-Agent`, header, or key for API access?
> 2. Do you have commercial-use terms / attribution requirements / a fair-use
>    arrangement? Our proxy caches heavily (one area fetch per few seconds, shared
>    across devices), so load is modest and scales with distinct areas, not devices.
>
> Happy to attribute prominently and to support the project (feeding or otherwise).
>
> Thanks,
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
