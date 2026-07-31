# ADS-B upstream sourcing — requirements + outreach drafts

Working doc for evaluating a paid/licensed ADS-B position source, should the free
community feeds (adsb.lol / adsb.fi) stop covering us at fleet scale. Context: the
egress relay ([README.md](README.md)) already collapses the fleet to ~one upstream
fetch per geographic tile per TTL, so our origin request volume is small and mostly
served from cache — the ceiling we hit is adsb.lol's **per-IP rate limit**, not raw
device count. See [[adsb-lol-anon-bandwidth-throttle]] for the throttle details.

## What we actually need (hard requirements)

1. **Caching + redistribution license.** We cache tiles at the edge and serve them to
   end-user devices. Any source whose ToS forbids caching or redistribution is a
   non-starter — this rules out most per-result commercial APIs (FlightAware AeroAPI,
   FR24). adsb.lol/adsb.fi are ODbL-style (redistribution OK with attribution); that's
   why we're on them.
2. **Geographic point/radius query.** readsb-style `/v2/lat/<lat>/lon/<lon>/dist/<nm>`
   (or equivalent bounding-box) returning live aircraft. This is the whole product.
3. **Per-hex metadata lookup** (`/v2/hex/<icao>`), or we keep enrichment on adsbdb.
4. **Flat-rate or high-volume-friendly pricing**, not pay-per-result — at our volume
   (below) per-result pricing is both costly and license-hostile.
5. **Stable rate posture** we can plan around (documented limits, or a raised limit for
   an identified/paying integrator), from a **small set of fixed egress IPs** (our
   relays), which we can announce for allow-listing.
6. Reasonable uptime; a single-region outage should be survivable (we run failover).

Nice-to-have: route/callsign data; global coverage (community feeds already give this).

## Our volume (what the provider's edge would see)

Not per-device — the relay collapses it. Order of magnitude for the ~55-board pilot:

- **Positions:** ~2–4 requests/second aggregate (one fetch per distinct hot tile per
  ~8 s TTL; fewer if boards cluster) ≈ **5–10 M requests/month**, from **2 fixed IPs**.
- **Enrichment (hex):** bursty but cached 1 h/entry and 30 d in KV → a few thousand
  distinct lookups/month.
- Trend is **down per device** as the fleet grows (more boards share the same tiles).

We can trade freshness for load at will (raise the tile TTL) and add relay IPs to
spread rate — so we can meet almost any per-IP limit; what we can't do is pay
per-result or lose redistribution rights.

## Options, ranked by fit

1. **Direct arrangement with adsb.lol / adsb.fi** — same data, same license, no
   re-architecture. They're building a feeder-key/roadmap. Ask for a paid or sponsored
   higher-limit tier tied to our announced relay IPs. **First contact.**
2. **OpenSky Network (commercial/enterprise)** — EU, community-fed, research-friendly;
   we already support OpenSky as a BYO device source. Get one quote as a backstop.
3. **adsbexchange (RapidAPI commercial)** — verify redistribution rights + cost at
   millions/month before considering.
4. **FlightAware / FR24 / Spire** — best coverage, but per-result + redistribution
   restrictions make them the wrong shape (and likely wrong price) for a
   cache-and-redistribute desk-radar product. Last resort.

Before paying anyone: **shard across relay IPs** (done — positions→relay-a,
enrichment→relay-b) and add $5/mo VPS IPs as needed. Each IP multiplies our per-IP
headroom linearly while keeping the free, redistribution-friendly data.

---

## Draft email — adsb.lol / adsb.fi

> Subject: Licensed/higher-limit access for a commercial hobby product (we feed too)
>
> Hi — I build **Blipscope**, a small desk flight-radar device (ESP32 + round LCD)
> that shows nearby aircraft. It's fed via a Cloudflare Worker + a dedicated-IP caching
> reverse proxy in front of your API, so the whole fleet collapses to roughly one
> request per geographic tile every few seconds — on the order of a few requests per
> second total, from two fixed IPs (I'll share them for allow-listing). I attribute
> your data in-product and in our docs, and we [feed / plan to feed] receivers back.
>
> We're launching a ~55-unit pilot and I want to be a good, *licensed* citizen rather
> than lean on anonymous limits. Do you offer (or would you consider) a paid or
> sponsored **higher-rate / identified-integrator tier**? I'm happy to:
>   - register our egress IPs and a descriptive User-Agent,
> - cap/raise our request rate to whatever you specify,
> - pay a monthly sponsorship or per-tier fee.
>
> What would work on your end? Happy to hop on a call. Thanks for running this.

## Draft email — OpenSky Network (commercial)

> Subject: Commercial API quote — cached redistribution to a small device fleet
>
> Hi OpenSky team — I run **Blipscope**, a desk flight-radar device. Architecture: a
> Cloudflare Worker + dedicated-IP caching proxy queries live positions by
> lat/lon/radius and serves cached tiles to end-user devices, so origin request volume
> is small (a few req/s aggregate, ~5–10 M/month, from 2 fixed IPs) and mostly cache
> hits. We already support OpenSky as a bring-your-own-account source on-device.
>
> For the fleet path I need a plan that **permits caching + redistribution to
> end-user devices** and is priced flat / by rate rather than per result. Could you
> share commercial/enterprise options and pricing that fit a lat/lon/radius live
> query at this volume, and confirm the redistribution terms? Thanks!

---

*Keep the [proxy/README.md](../proxy/README.md) "Upstream licensing posture" table and
this doc in sync when a source's terms change.*
