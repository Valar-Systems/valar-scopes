# Blipscope egress relay

A dedicated-IP reverse-proxy cache in front of adsb.lol. It exists for one reason:
**Cloudflare Workers egress from a shared per-colo IP pool, and adsb.lol's
anonymous limiter counts other tenants' traffic against us.** A direct
`api.adsb.lol` curl from any dedicated IP returns live data at the same instant
the Worker path is throttled — the only variable is the IP. The relay gives
adsb.lol one stable, non-shared IP to rate-limit; the Worker consumes tiles from
the relay instead of hitting adsb.lol directly.

The Worker architecture is unchanged (edge cache, SWR, breaker, failover chain);
only the upstream base URL moves (`UPSTREAM_ADSB_LOL_BASE`). The relay adds one
thing the Worker can't do across colos: **request collapsing** (`proxy_cache_lock`),
so the whole fleet's polls for a tile become one upstream fetch per cache window.

## Topology (shipping HA pair)

| relay | host | box | region | base URL var |
|---|---|---|---|---|
| relay-a (primary)   | `relay-a.valarsystems.com` | Hetzner CPX11 | Hillsboro OR | `UPSTREAM_ADSB_LOL_BASE` |
| relay-b (secondary) | `relay-b.valarsystems.com` | Vultr         | Seattle WA   | `UPSTREAM_ADSB_LOL_BASE_B` |

Both hosts are orange-clouded (Cloudflare proxied) with a Cloudflare Origin cert,
so Worker → Cloudflare → relay is TLS end to end and the origin IP stays hidden.
The relays are modelled as **two feeds in the Worker's failover chain**: relay-a
primary → relay-b secondary. relay-b is the *terminal* feed, so the breaker never
skips it (PR #119) — a relay-a outage fails over rather than blanking the fleet.

**Fleet failure story:** relay-a down → chain uses relay-b (invisible). Both down
→ Worker serves SWR cache (up to `SWR_MAX_AGE_S = 600 s`), then the device's stale
ladder escalates honestly. A cold tile during a total double-relay outage still
503s (no data to serve) — the residual gap the HA pair makes rare.

## Files on each box (scp these first)

| path | perms | what |
|---|---|---|
| `/etc/ssl/cloudflare/origin.pem` | 0644 root | Cloudflare Origin **certificate** |
| `/etc/ssl/cloudflare/origin.key` | 0600 root | Origin cert **private key** |
| `/etc/nginx/relay.key`           | 0600 root | the **X-Relay-Key** value (same string as the `RELAY_KEY` Worker secret) |

`setup-relay.sh` reads `relay.key` and generates the root-only nginx map; the key
value is never in the script or in git.

### scp (run locally, per box)

```sh
# relay-a  (Hetzner, Hillsboro)
scp origin.pem origin.key relay.key setup-relay.sh root@<A-IP>:/root/
ssh root@<A-IP> 'install -D -m600 /root/origin.key /etc/ssl/cloudflare/origin.key && \
                 install -D -m644 /root/origin.pem /etc/ssl/cloudflare/origin.pem && \
                 install -D -m600 /root/relay.key  /etc/nginx/relay.key && \
                 sudo CACHE_TTL=6s bash /root/setup-relay.sh'

# relay-b  (Vultr, Seattle) -- identical
scp origin.pem origin.key relay.key setup-relay.sh root@<B-IP>:/root/
ssh root@<B-IP> 'install -D -m600 /root/origin.key /etc/ssl/cloudflare/origin.key && \
                 install -D -m644 /root/origin.pem /etc/ssl/cloudflare/origin.pem && \
                 install -D -m600 /root/relay.key  /etc/nginx/relay.key && \
                 sudo CACHE_TTL=6s bash /root/setup-relay.sh'
```

The same `relay.key` string goes on **both** boxes and into the Worker secret
`RELAY_KEY` (one shared relay key for the fleet).

## Worker secrets / vars

- **Secret:** `RELAY_KEY` — `npx wrangler secret put RELAY_KEY --env staging`
  (then `--env production` at cutover). Value = the contents of `relay.key`.
- **Vars** (already in `wrangler.toml`, not secret): `UPSTREAM_ADSB_LOL_BASE` and
  `UPSTREAM_ADSB_LOL_BASE_B` = the two relay hostnames.

Two adapter instances need **nothing beyond this**: `adsb_lol` (relay-a) and
`adsb_lol_b` (relay-b) are one code shape parameterised by base URL + id; they
share the `RELAY_KEY` header and get independent circuit breakers automatically.

## Verification sequence (staging first, then prod cutover)

1. Boxes up: `curl https://relay-a.valarsystems.com/healthz` → `ok`; a keyless
   `/v2/...` → `403`; with `-H "X-Relay-Key: <key>"` → aircraft JSON + an
   `X-Relay-Cache` header.
2. `npx wrangler secret put RELAY_KEY --env staging`, then
   `npx wrangler deploy --env staging`.
3. Board on **staging** cloud mode; confirm `X-Cache` flips MISS → **HIT** on
   `scopes-staging.valarsystems.com/v1/blips` and the device shows a live,
   non-stale picture.
4. **Cutover:** `npx wrangler secret put RELAY_KEY --env production`, then
   `npx wrangler deploy --env production`.

## Soak (one week) — pass criteria defined up front

Measured from the relay logs + fleet-side `X-Cache`:

| metric | source | PASS |
|---|---|---|
| adsb.lol 429 rate from the relay | `grep -c 'ustatus=429' relay.log` ÷ total | **< 1%** of upstream requests |
| upstream request rate (one IP) | `grep -c 'cache=MISS' relay.log` over time | **< 10 req/s** sustained at pilot volume |
| fleet freshness | Worker `X-Cache` HIT+STALE served vs MISS | **≥ 99%** served warm; STALE runs bounded |
| longest unbroken degraded run | device `[health]` DATA STALE spans | **< 90 s** (never reaches the NoData cap) |
| relay-a kill → failover | stop nginx on relay-a; watch `X-Upstream` | flips to `adsb_lol_b`, **no fleet-visible gap** |

Fail any → tune `CACHE_TTL` up (fewer upstream calls) or coarsen tiles before
adding a third relay IP. The design degrades by a knob, not off a cliff.

## Operator courtesy

Relay IPs are **announced to adsb.lol** as courtesy identification (we feed them).
The production upstream table in [proxy/README.md](../proxy/README.md) records
which IPs are announced — keep it current when relays change.
