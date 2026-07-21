# Standing up Blipscope Cloud production — execution checklist

An ordered, runnable checklist to move the fleet off staging
(`scopes-staging.valarsystems.com`) onto production
(`scopes.valarsystems.com`). Run every command from `proxy/` unless noted.
This supersedes the prose in [README.md](README.md#going-to-production) with an
exact order and the verifications between steps.

**Two gates before you start:**
- **Feed licensing** must be decided first — see
  [RELEASE-GATE-FEEDS.md](RELEASE-GATE-FEEDS.md). Whatever you decide there sets
  the `UPSTREAM_*_ENABLED` values that ship in step 3. **Do not deploy prod with
  unlicensed feeds live unless you have consciously accepted that.**
- Prereqs: Node 20+, a Cloudflare account with **Workers Paid** (rate limiting +
  Analytics Engine), `npx wrangler login`, and the `scopes.valarsystems.com` zone
  on this Cloudflare account.

---

## 0. Pre-flight (no writes)

```sh
cd proxy
npm install
npm test           # vitest + workers pool (miniflare)
npm run typecheck
```

All green before proceeding.

## 1. Production KV namespace — verify, then create only if missing

`wrangler.toml` already lists a production KV id
(`[[env.production.kv_namespaces]] id = "733bf90056104f56be9492e62450a0bf"`).
**Confirm it's real** (the README history references a `REPLACE_ME` placeholder,
so verify rather than assume):

```sh
npx wrangler kv namespace list        # is 733bf9...0bf in the list?
```

- **If present:** keep it, go to step 2.
- **If absent:** create and paste the printed id into `wrangler.toml` under
  `[[env.production.kv_namespaces]]`:

  ```sh
  npx wrangler kv namespace create ENRICH_KV --env production
  ```

## 2. Secrets (production)

```sh
npx wrangler secret put BLIP_KEYS --env production          # device build key(s), comma-separated
npx wrangler secret put DEVICE_KEY_SECRET --env production   # ONLY if using per-device keys (optional)
npx wrangler secret put ADSB_LOL_API_KEY --env production    # ONLY once adsb.lol issues a feeder key (optional)
```

`BLIP_KEYS` is the one that must be set — it's the key the firmware presents as
`X-Blip-Key`. Keep the plaintext; you bake it into the firmware in step 6.

## 3. Set the launch feed posture (from the licensing gate)

Edit `[env.production.vars]` in `wrangler.toml` to reflect the
[RELEASE-GATE-FEEDS.md](RELEASE-GATE-FEEDS.md) decision. Licensed-only launch:

```toml
UPSTREAM_ADSB_FI_ENABLED = "false"
UPSTREAM_AIRPLANES_LIVE_ENABLED = "false"
ROUTE_ADSBDB_ENABLED = "true"        # route fallback; separate item, leave on
```

(Only leave the failover feeds `"true"` if you have accepted the licensing risk.)

## 4. Deploy the Worker

```sh
npm run deploy:production            # = wrangler deploy --env production
```

Add `scopes.valarsystems.com` as a **custom domain** if the route isn't live yet
(the `routes` entry is already in `wrangler.toml`; the zone must be on this
account). Confirm the Worker answers:

```sh
curl -s https://scopes.valarsystems.com/healthz
# {"ok":true,"upstreams":[...]} — and the enabled flags match step 3.
```

## 5. Seed the KV-backed datasets into the fresh production namespace

Order doesn't matter between these; all three must run before devices rely on
overlays/photos/military labels.

```sh
npm run ingest -- --env production            # 212 type photos + manifest + /credits page
npm run ingest:mildb -- --env production       # ~17k military airframes (Mictronics, ODC-By 1.0)
npm run ingest:airports -- --env production    # ~9.4k airport tiles (OurAirports, public domain)
```

Verify each landed:

```sh
KEY=<one of the BLIP_KEYS you set in step 2>
BASE=https://scopes.valarsystems.com
curl -s https://scopes.valarsystems.com/credits | grep -c '<li>'          # photo credit rows > 0
curl -s -H "X-Blip-Key: $KEY" "$BASE/v1/airports?lat=47.39&lon=8.55&r=40"  # non-empty "a":[...]
# military label: an enrich on a known mil hex should fill "op" from the side table
```

## 6. Point the shipping firmware at production

The staging URL is currently hard-coded in **four** envs in
[../platformio.ini](../platformio.ini):
`blipscope-kit-c3-128-cloud`, `blipscope-s3-128`, `blipscope-s3-128-cloud`,
`blipscope-s3-146-cloud`. The default `blipscope-s3-146` is **not yet a cloud
build** — the in-file plan (comment above `[env:blipscope-s3-146-cloud]`) is to
fold `CLOUD_FEED_BASE` into `blipscope-s3-146` at launch and delete the scratch
`-cloud` env.

For whichever env(s) actually ship to customers:

- Set `-DCLOUD_FEED_BASE=\"https://scopes.valarsystems.com\"`.
- Provide the key **without committing it**: `-DCLOUD_FEED_KEY=\"...\"` as a local
  build flag, or (preferred) a per-device key minted with
  `npm run derive-device-key <deviceId>` against `DEVICE_KEY_SECRET`. `credentials.json`
  and any key material stay out of git.

Grep to confirm no shipping env still points at staging:

```sh
grep -n "scopes-staging" platformio.ini      # should be empty for any env you ship
```

## 7. End-to-end smoke test (production)

```sh
BASE=https://scopes.valarsystems.com KEY=<prod key>
curl -s $BASE/healthz
curl -s -H "X-Blip-Key: $KEY" "$BASE/v1/blips?lat=47.39&lon=8.55&r=40"
curl -s -H "X-Blip-Key: $KEY" "$BASE/v1/enrich/4b1817?cs=SWR123&lat=47.4&lon=8.5"
curl -s -H "X-Blip-Key: $KEY" -H "X-Blip-Model: s3-146" "$BASE/v1/config"
```

Expect: `/v1/blips` returns `"v":1` with an `"a":[...]` array; `/v1/enrich`
returns reg/type/operator/route (and `p`/`pk` if the type has a photo);
`/v1/config` returns the `s3-146` cadence tier (2/10/45 s). Then flash one real
bench unit built in step 6 and confirm it draws the radar, a tap fills the detail
card, and — if a covered type is overhead — a "representative photo" renders.

## 8. Post-launch ops

- Flip the weekly refresh workflow to production
  (`.github/workflows/refresh-data.yml` default env) and set the repo secret
  `CLOUDFLARE_API_TOKEN` so it can write KV.
- Watch the first hours: `npm run tail:production`, and the Analytics Engine
  cache-state query in [RELEASE-GATE-FEEDS.md](RELEASE-GATE-FEEDS.md) §1a to see
  the real adsb.lol STALE/503 rate under production traffic.
- Retune the fleet without a deploy via the `cfg:fleet` KV write
  (see [README.md](README.md#fleet-config-operations)).

---

### Known blocker referenced in the repo

The README gates the firmware release on the "overnight slowdown" fix (ROADMAP
"Release readiness"). Confirm that's resolved before cutting the customer
firmware in step 6 — standing up the proxy (steps 1–5) is safe to do ahead of it.
