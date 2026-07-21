# Launch gate: upstream feed licensing

**Status: BLOCKING for customer launch.** `airplanes.live` and `adsb.fi` are
enabled in **both** the staging and production `[vars]` blocks
([wrangler.toml](wrangler.toml)) for the owner-approved bench soak — but neither
operator has granted commercial-use permission (the outreach drafts are still in
[FEED-SOURCING.md](FEED-SOURCING.md)). Only **adsb.lol (ODbL 1.0)** is a cleared
license today. **Do not ship customer devices with unlicensed position feeds
live.** This doc answers the three questions on the gate and gives the exact
revert.

---

## 1a. What does reverting cost? (the adsb.lol-only 429 rate)

The cost of turning the failover feeds off is entirely "how often does
adsb.lol-only leave the scope stale/empty" — which equals the adsb.lol **429
rate on `/v2/point`**. With failover off, every 429 or outage falls through to
stale-while-revalidate (last picture, original `t`, up to a 10-minute storage
ceiling) → then `503 warming` / `503 upstream_unavailable`; the device shows
**STALE DATA** and eventually loses the picture. That is the exact regression
that made airplanes.live primary in the first place.

**I cannot give you a live number from the repo** — it's a runtime metric, not a
constant. What the repo *does* record:

- [FEED-SOURCING.md](FEED-SOURCING.md): adsb.lol keyless 429s the shared
  Cloudflare per-colo egress IPs "near-constantly" on the high-volume `/v2/point`
  endpoint (tripped by *other* CF tenants' traffic, not our volume); a direct
  query from a normal IP works fine.
- [wrangler.toml](wrangler.toml): failover was enabled 2026-07-10 during a
  multi-hour adsb.lol **hard outage** (502 from Worker *and* residential — their
  backend, not throttling), and again 2026-07-18 because "production had no
  fallback and went empty whenever adsb.lol rate-limited us."

**Measure it before you decide.** Two ways, both against the live Worker:

```sh
# (1) Live tail — count adsb.lol point failures vs successes over a window.
#     Failures are HTTP 429 (throttle) or 5xx (outage); look at evt:upstream lines.
npx wrangler tail --env production --format json \
  | grep -i '"evt":"upstream"' | grep '"id":"adsb_lol"' | grep '"op":"point"'
#     ok:false is a failed point fetch. 429-heavy = throttling; 5xx = outage.
```

```sql
-- (2) Analytics Engine — blips cache-state mix over 24 h is the customer-visible
--     proxy for the 429 rate: STALE / 503 share ~ how often the upstream failed.
--     (Remember: SUM(double4), never COUNT(*) — HIT/STALE are 1:10 sampled.)
SELECT blob2 AS cache_state, SUM(double4) AS requests
FROM blipscope_proxy
WHERE timestamp > NOW() - INTERVAL '24' HOUR AND blob1 = '/v1/blips'
GROUP BY cache_state;
-- A high STALE share (and any 503s in the status doubles) with failover OFF is
-- the reverted-state cost. Run it now WITH failover on to see the counterfactual,
-- then flip a low-traffic window and compare.
```

Decision rule: if adsb.lol-only STALE/503 share is low enough to be acceptable
for launch, revert now (§1c). If it's not, the launch options are §1b.

## 1b. What changes if we feed adsb.lol and get a key?

Two *different* adsb.lol keyed things — don't conflate them:

1. **The public API key (`X-Api-Key`, feeder-earned).** The adapter already sends
   it on both the point and routeset requests when the `ADSB_LOL_API_KEY` secret
   is set ([src/upstreams/adsb_lol.ts](src/upstreams/adsb_lol.ts)). A per-key
   limit replaces the anonymous per-IP limit — which is *exactly* the fix for the
   shared-Cloudflare-egress 429. **But adsb.lol has not launched key issuance
   yet** ("in the future, you will require an API key which you can obtain by
   feeding"), so this is **not actionable today** — it's the right long-term fix,
   pending their program. When it lands: run a feeder, obtain the key, then
   `npx wrangler secret put ADSB_LOL_API_KEY --env production`. No code change.
2. **The `re-api` feeder ingest API.** IP-locked to the feeding station's IP, so
   a Cloudflare Worker **can't use it even if we feed.** Irrelevant to the proxy's
   429.

So: **feeding adsb.lol today does not by itself change the 429** (the anonymous
per-IP limit is unchanged without an issued key), but it (a) is the on-ramp to
the future per-key limit that *does* fix it, and (b) strengthens the ODbL
community/licensing relationship. Feeding is worth starting; it is not a
same-day fix.

Other paths if the key program stays vaporware and the 429 rate is unacceptable
(full analysis in [FEED-SOURCING.md](FEED-SOURCING.md)):
- **Commercial permission from airplanes.live / adsb.fi** — makes the feeds
  we're *already relying on* legitimate; flat/donation, not per-request. Cheapest
  right answer. Outreach drafts are ready to send.
- **Fixed-IP egress proxy** (~$5/mo VPS in front of adsb.lol keyless) — a
  dedicated IP gets adsb.lol's anonymous per-IP limit *to ourselves*, likely
  fixing the 429 without per-request cost. Worth a spike.
- **Paid per-request API** (ADSBexchange / AeroAPI) — only pencils out with heavy
  device-per-tile clustering; keep as spot fallback, not the backbone.

## 1c. The revert (prepped — pull the trigger when you decide)

The enable flags are deploy-time `[vars]`, read as `env.UPSTREAM_*_ENABLED ===
"true"` ([src/upstreams/*.ts](src/upstreams/)). There is **no runtime KV
override** — reverting is a var change + redeploy. It is exactly **two values**
in the production block.

In [wrangler.toml](wrangler.toml), under `[env.production.vars]`, change:

```toml
UPSTREAM_ADSB_FI_ENABLED = "true"          # ->  "false"
UPSTREAM_AIRPLANES_LIVE_ENABLED = "true"   # ->  "false"
```

Then deploy:

```sh
cd proxy
npm run deploy:production        # = wrangler deploy --env production
```

Verify the revert took (both must show `enabled:false`):

```sh
curl -s https://scopes.valarsystems.com/healthz | grep -o '"id":"[a-z_]*","enabled":[a-z]*'
# expect: adsb_fi enabled:false, airplanes_live enabled:false, adsb_lol enabled:true
```

Notes:
- **Do the same in `[env.staging.vars]`** if you want staging to match launch
  posture (recommended so the soak reflects reality once you decide).
- Leave `ROUTE_ADSBDB_ENABLED = "true"` — that's the **route** fallback
  (callsign→origin/destination), a separate, long-standing dependency, not one of
  the two unlicensed *position* feeds this gate is about. adsbdb's own terms are
  not documented in-repo, so treat "adsbdb for routes" as a **separate**
  licensing item to confirm, not part of this flip.
- After reverting, adsb.lol is the sole position source; re-run the §1a queries
  to confirm the STALE/503 rate is what you signed off on.

**One-command alternative (verify once before trusting it):** wrangler can
override vars at deploy without editing the file —
`wrangler deploy --env production --var UPSTREAM_ADSB_FI_ENABLED:false --var
UPSTREAM_AIRPLANES_LIVE_ENABLED:false`. I have not run it in this environment;
confirm the `/healthz` check above shows `enabled:false` before relying on it for
the licensing gate. The file-edit path above is the canonical, unambiguous one.
